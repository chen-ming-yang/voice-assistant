// nlu-module.cc — NLU module implementation (HTTP + local llama-cli)
#include "nlu/nlu-module.h"

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>

#include <cctype>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

// --------------- local subprocess handle ---------------
#ifdef _WIN32
struct NluLocalProcess {
  HANDLE stdin_write = INVALID_HANDLE_VALUE;
  HANDLE stdout_read = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION pi = {};

  void Close() {
    if (stdin_write != INVALID_HANDLE_VALUE) {
      CloseHandle(stdin_write);
      stdin_write = INVALID_HANDLE_VALUE;
    }
    if (stdout_read != INVALID_HANDLE_VALUE) {
      CloseHandle(stdout_read);
      stdout_read = INVALID_HANDLE_VALUE;
    }
    if (pi.hProcess) {
      TerminateProcess(pi.hProcess, 0);
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hProcess);
      pi.hProcess = nullptr;
    }
    if (pi.hThread) {
      CloseHandle(pi.hThread);
      pi.hThread = nullptr;
    }
  }
};
#else
struct NluLocalProcess {};
#endif

namespace {

std::wstring ToWide(const std::string &s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (n <= 0) return L"";
  std::wstring out;
  out.resize(n - 1);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
  return out;
}

std::string JsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (unsigned char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(static_cast<char>(c));
        break;
    }
  }
  return out;
}

std::string JsonUnescape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\' || i + 1 >= s.size()) {
      out.push_back(s[i]);
      continue;
    }

    char n = s[++i];
    switch (n) {
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case '\\':
        out.push_back('\\');
        break;
      case '"':
        out.push_back('"');
        break;
      default:
        out.push_back(n);
        break;
    }
  }
  return out;
}

bool ParseHttpUrl(const std::string &url, bool *https, std::string *host,
                  int32_t *port, std::string *path) {
  constexpr const char *kHttp = "http://";
  constexpr const char *kHttps = "https://";

  size_t pos = 0;
  if (url.rfind(kHttp, 0) == 0) {
    *https = false;
    *port = 80;
    pos = 7;
  } else if (url.rfind(kHttps, 0) == 0) {
    *https = true;
    *port = 443;
    pos = 8;
  } else {
    return false;
  }

  size_t slash = url.find('/', pos);
  std::string host_port =
      slash == std::string::npos ? url.substr(pos) : url.substr(pos, slash - pos);
  *path = slash == std::string::npos ? "/" : url.substr(slash);
  if (path->empty()) *path = "/";

  size_t colon = host_port.rfind(':');
  if (colon != std::string::npos) {
    *host = host_port.substr(0, colon);
    std::string port_str = host_port.substr(colon + 1);
    if (port_str.empty()) return false;
    for (char c : port_str) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    *port = std::stoi(port_str);
  } else {
    *host = host_port;
  }

  if (host->empty()) return false;

  // If only root is given, target llama.cpp default endpoint.
  if (*path == "/") {
    *path = "/completion";
  }
  return true;
}

// Strip ANSI escape sequences (\x1b[...X) from a string.
std::string StripAnsi(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      // Skip until the terminating letter [A-Za-z]
      i += 2;
      while (i < s.size() &&
             !std::isalpha(static_cast<unsigned char>(s[i]))) {
        ++i;
      }
      // skip the terminating letter itself
      continue;
    }
    out.push_back(s[i]);
  }
  return out;
}

// Remove llama-cli perf stats lines like "Prompt 154.1 t/s Generation 37.4 t/s"
std::string StripPerfStats(const std::string &s) {
  std::string out;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t eol = s.find('\n', pos);
    if (eol == std::string::npos) eol = s.size();
    std::string line = s.substr(pos, eol - pos);
    // Skip lines containing "t/s" (token-per-second stats)
    if (line.find("t/s") == std::string::npos) {
      if (!out.empty()) out.push_back('\n');
      out.append(line);
    }
    pos = eol + 1;
  }
  return out;
}

std::string ExtractLlamaContent(const std::string &json) {
  const std::string key = "\"content\"";
  size_t k = json.find(key);
  if (k == std::string::npos) return "";

  size_t colon = json.find(':', k + key.size());
  if (colon == std::string::npos) return "";
  size_t q1 = json.find('"', colon + 1);
  if (q1 == std::string::npos) return "";

  std::string raw;
  raw.reserve(json.size() - q1);
  for (size_t i = q1 + 1; i < json.size(); ++i) {
    char c = json[i];
    if (c == '"' && json[i - 1] != '\\') {
      return JsonUnescape(raw);
    }
    raw.push_back(c);
  }

  return "";
}

bool IsStopTypeLimit(const std::string &json) {
  size_t p = json.find("\"stop_type\"");
  if (p == std::string::npos) return false;
  size_t q = json.find("\"limit\"", p);
  return q != std::string::npos && (q - p) < 128;
}

#ifdef _WIN32
bool HttpPostJson(const std::string &host, int32_t port, bool https,
                  const std::string &path, int32_t timeout_ms,
                  const std::string &body, int32_t *status,
                  std::string *response) {
  *status = -1;
  response->clear();

  std::wstring whost = ToWide(host);
  std::wstring wpath = ToWide(path);
  if (whost.empty() || wpath.empty()) return false;

  HINTERNET h_session = WinHttpOpen(L"voice-assistant-nlu/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
  if (!h_session) return false;

  WinHttpSetTimeouts(h_session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  HINTERNET h_connect = WinHttpConnect(
      h_session, whost.c_str(), static_cast<INTERNET_PORT>(port), 0);
  if (!h_connect) {
    WinHttpCloseHandle(h_session);
    return false;
  }

  DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET h_request = WinHttpOpenRequest(
      h_connect, L"POST", wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!h_request) {
    WinHttpCloseHandle(h_connect);
    WinHttpCloseHandle(h_session);
    return false;
  }

  const wchar_t *headers = L"Content-Type: application/json; charset=utf-8\r\n";
  BOOL ok = WinHttpSendRequest(
      h_request, headers, -1L, reinterpret_cast<LPVOID>(const_cast<char *>(body.data())),
      static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
  if (!ok) {
    WinHttpCloseHandle(h_request);
    WinHttpCloseHandle(h_connect);
    WinHttpCloseHandle(h_session);
    return false;
  }

  ok = WinHttpReceiveResponse(h_request, nullptr);
  if (!ok) {
    WinHttpCloseHandle(h_request);
    WinHttpCloseHandle(h_connect);
    WinHttpCloseHandle(h_session);
    return false;
  }

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  WinHttpQueryHeaders(h_request,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                      WINHTTP_NO_HEADER_INDEX);
  *status = static_cast<int32_t>(status_code);

  while (true) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(h_request, &available)) break;
    if (available == 0) break;

    std::string chunk;
    chunk.resize(available);
    DWORD downloaded = 0;
    if (!WinHttpReadData(h_request, &chunk[0], available, &downloaded)) break;
    chunk.resize(downloaded);
    response->append(chunk);
  }

  WinHttpCloseHandle(h_request);
  WinHttpCloseHandle(h_connect);
  WinHttpCloseHandle(h_session);
  return true;
}
#endif

}  // namespace

void NluModule::Register(sherpa_onnx::ParseOptions *po) {
  po->Register("nlu-server-url", &nlu_url_,
               "HTTP URL for llama.cpp server "
               "(e.g. http://192.168.31.27:8080/ or /completion). "
               "Empty to disable.");
  po->Register("nlu-llama-cli", &nlu_llama_cli_,
               "Path to llama-cli executable for local NLU. "
               "Mutually exclusive with --nlu-server-url.");
  po->Register("nlu-model", &nlu_model_,
               "Path to GGUF model file for local NLU (used with --nlu-llama-cli).");
  po->Register("nlu-llama-args", &nlu_llama_args_,
               "Extra arguments for llama-cli "
               "(e.g. \"--jinja --reasoning off --temp 1.0\").");
  po->Register("nlu-timeout-ms", &nlu_timeout_ms_,
               "Timeout for NLU requests in milliseconds.");
  po->Register("nlu-n-predict", &nlu_n_predict_,
               "Max number of generated tokens from llama.cpp.");
  po->Register("nlu-cache-prompt", &nlu_cache_prompt_,
               "Whether llama.cpp caches prompt between requests.");
  po->Register("nlu-prompt-prefix", &nlu_prompt_prefix_,
               "Prefix added before ASR text to steer short TTS-friendly responses.");
}

bool NluModule::Init() {
  bool have_url = !nlu_url_.empty();
  bool have_cli = !nlu_llama_cli_.empty();

  if (have_url && have_cli) {
    fprintf(stderr,
            "NLU: cannot use both --nlu-server-url and --nlu-llama-cli\n");
    return false;
  }

  if (!have_url && !have_cli) {
    fprintf(stderr, "NLU: disabled (no --nlu-server-url or --nlu-llama-cli)\n");
    return true;
  }

  // --- Local llama-cli mode ---
  if (have_cli) {
    if (nlu_model_.empty()) {
      fprintf(stderr, "NLU: --nlu-model is required with --nlu-llama-cli\n");
      return false;
    }
    return LaunchLocalProcess();
  }

  // --- HTTP mode ---
  if (!ParseHttpUrl(nlu_url_, &nlu_https_, &nlu_host_, &nlu_port_, &nlu_path_)) {
    fprintf(stderr, "NLU: invalid URL: %s\n", nlu_url_.c_str());
    return false;
  }

  fprintf(stderr,
      "NLU HTTP endpoint: %s (host=%s port=%d path=%s timeout=%dms n_predict=%d cache_prompt=%d)\n",
          nlu_url_.c_str(), nlu_host_.c_str(), nlu_port_, nlu_path_.c_str(),
      nlu_timeout_ms_, nlu_n_predict_, nlu_cache_prompt_ ? 1 : 0);
  return true;
}

void NluModule::Process(const std::string &text) {
  if (!enabled() || text.empty()) return;
  fprintf(stderr, "[NLU] Queuing text: %s\n", text.c_str());
  queue_.Push(text);
}

void NluModule::Start() {
  if (!enabled()) return;
  fprintf(stderr, "[NLU] Starting worker thread\n");
  thread_ = std::thread(&NluModule::WorkerLoop, this);
}

void NluModule::Stop() {
  fprintf(stderr, "[NLU] Stopping...\n");
  queue_.Shutdown();
  if (thread_.joinable()) thread_.join();
  fprintf(stderr, "[NLU] Stopped\n");
}

void NluModule::WorkerLoop() {
  fprintf(stderr, "[NLU] Worker thread started\n");
  std::string text;
  while (queue_.Pop(text)) {
    fprintf(stderr, "[NLU] Popped from queue: %s\n", text.c_str());
    DoProcess(text);
  }
  fprintf(stderr, "[NLU] Worker thread exiting\n");
}

void NluModule::DoProcess(const std::string &text) {
  if (local_proc_) {
    DoProcessLocal(text);
    return;
  }
  DoProcessHttp(text);
}

void NluModule::DoProcessLocal(const std::string &text) {
#ifdef _WIN32
  fprintf(stderr, "[NLU] Local processing: %s\n", text.c_str());

  std::string prompt = nlu_prompt_prefix_ + text + "\n";

  DWORD written;
  BOOL ok = WriteFile(local_proc_->stdin_write, prompt.c_str(),
                      static_cast<DWORD>(prompt.size()), &written, nullptr);
  if (!ok || written != static_cast<DWORD>(prompt.size())) {
    fprintf(stderr, "[NLU] Failed to write to llama-cli stdin (error %lu)\n",
            GetLastError());
    return;
  }

  std::string response = ReadUntilPrompt(nlu_timeout_ms_);

  // Strip ANSI escape codes and perf stats from llama-cli output
  response = StripAnsi(response);
  response = StripPerfStats(response);

  // Trim whitespace
  while (!response.empty() &&
         std::isspace(static_cast<unsigned char>(response.front()))) {
    response.erase(response.begin());
  }
  while (!response.empty() &&
         std::isspace(static_cast<unsigned char>(response.back()))) {
    response.pop_back();
  }

  if (response.empty()) {
    fprintf(stderr, "[NLU] Local: empty response\n");
    return;
  }

  fprintf(stderr, "[NLU] Local response: %s\n", response.c_str());
  if (on_result) on_result(response);
#else
  (void)text;
  fprintf(stderr, "[NLU] Local llama-cli currently implemented for Windows only\n");
#endif
}

void NluModule::DoProcessHttp(const std::string &text) {
  fprintf(stderr, "[NLU] HTTP request text: %s\n", text.c_str());

  std::string prompt = nlu_prompt_prefix_ + text;

    std::string body =
      "{\"prompt\":\"" + JsonEscape(prompt) + "\",\"n_predict\":" +
      std::to_string(nlu_n_predict_) +
      ",\"cache_prompt\":" + (nlu_cache_prompt_ ? "true" : "false") + "}";

#ifdef _WIN32
  int32_t status = -1;
  std::string raw_response;
  bool ok = HttpPostJson(nlu_host_, nlu_port_, nlu_https_, nlu_path_,
                         nlu_timeout_ms_, body, &status, &raw_response);
  if (!ok) {
    fprintf(stderr, "[NLU] HTTP request failed\n");
    return;
  }
  fprintf(stderr, "[NLU] HTTP status=%d, bytes=%d\n", status,
          static_cast<int32_t>(raw_response.size()));
  if (status < 200 || status >= 300) {
    fprintf(stderr, "[NLU] HTTP non-success response: %s\n",
            raw_response.c_str());
    return;
  }

  std::string response = ExtractLlamaContent(raw_response);
  if (response.empty()) response = raw_response;

  if (IsStopTypeLimit(raw_response)) {
    fprintf(stderr,
            "[NLU] Response hit token limit. Increase --nlu-n-predict (current=%d).\n",
            nlu_n_predict_);
  }

  fprintf(stderr, "[NLU] Parsed response: %s\n", response.c_str());
  if (on_result) on_result(response);
#else
  (void)body;
  fprintf(stderr, "[NLU] HTTP client currently implemented for Windows only\n");
#endif
}

void NluModule::Close() {
  Stop();
#ifdef _WIN32
  if (local_proc_) {
    local_proc_->Close();
    delete local_proc_;
    local_proc_ = nullptr;
  }
#endif
}

NluModule::~NluModule() { Close(); }

// ------- Local llama-cli subprocess -------

bool NluModule::LaunchLocalProcess() {
#ifdef _WIN32
  // Build command line:
  //   "<llama-cli>" -m "<model>" -cnv --simple-io -n <n_predict> <extra>
  std::string cmd = "\"" + nlu_llama_cli_ + "\" -m \"" + nlu_model_ +
                    "\" -cnv --simple-io -n " +
                    std::to_string(nlu_n_predict_);
  if (!nlu_llama_args_.empty()) {
    cmd += " " + nlu_llama_args_;
  }

  fprintf(stderr, "[NLU] Launching local: %s\n", cmd.c_str());

  // Create pipes for child stdin / stdout
  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE child_stdin_read = INVALID_HANDLE_VALUE;
  HANDLE child_stdin_write = INVALID_HANDLE_VALUE;
  HANDLE child_stdout_read = INVALID_HANDLE_VALUE;
  HANDLE child_stdout_write = INVALID_HANDLE_VALUE;

  if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) {
    fprintf(stderr, "[NLU] CreatePipe(stdin) failed (%lu)\n", GetLastError());
    return false;
  }
  SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);

  if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) {
    fprintf(stderr, "[NLU] CreatePipe(stdout) failed (%lu)\n", GetLastError());
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdin_write);
    return false;
  }
  SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  si.hStdInput = child_stdin_read;
  si.hStdOutput = child_stdout_write;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);  // share our stderr
  si.dwFlags = STARTF_USESTDHANDLES;

  local_proc_ = new NluLocalProcess;

  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');

  BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr,
                           TRUE,       // inherit handles
                           0,          // creation flags (keep console)
                           nullptr, nullptr, &si, &local_proc_->pi);

  // Close the child-side pipe ends (parent does not use them)
  CloseHandle(child_stdin_read);
  CloseHandle(child_stdout_write);

  if (!ok) {
    fprintf(stderr, "[NLU] Failed to launch llama-cli (error %lu)\n",
            GetLastError());
    CloseHandle(child_stdin_write);
    CloseHandle(child_stdout_read);
    delete local_proc_;
    local_proc_ = nullptr;
    return false;
  }

  local_proc_->stdin_write = child_stdin_write;
  local_proc_->stdout_read = child_stdout_read;

  fprintf(stderr,
          "[NLU] Local process PID %lu — waiting for model to load...\n",
          local_proc_->pi.dwProcessId);

  // Wait for the initial "> " prompt (model loading can take a while)
  std::string initial = ReadUntilPrompt(120000);
  if (!initial.empty()) {
    fprintf(stderr, "[NLU] Local initial output: %s\n", initial.c_str());
  }
  fprintf(stderr, "[NLU] Local model loaded and ready\n");
  return true;
#else
  fprintf(stderr, "[NLU] Local llama-cli currently implemented for Windows only\n");
  return false;
#endif
}

std::string NluModule::ReadUntilPrompt(int32_t timeout_ms) {
#ifdef _WIN32
  std::string buf;
  if (!local_proc_ ||
      local_proc_->stdout_read == INVALID_HANDLE_VALUE) {
    return buf;
  }

  HANDLE pipe = local_proc_->stdout_read;
  ULONGLONG deadline = GetTickCount64() + timeout_ms;

  while (GetTickCount64() < deadline) {
    DWORD avail = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) break;

    if (avail > 0) {
      char c;
      DWORD n;
      if (!ReadFile(pipe, &c, 1, &n, nullptr) || n == 0) break;
      buf.push_back(c);
      continue;
    }

    // No data available — check whether the buffer ends with the "> " prompt.
    if (buf.size() >= 2 && buf[buf.size() - 2] == '>' &&
        buf[buf.size() - 1] == ' ') {
      // Confirm: wait 150 ms and verify no more data is arriving.
      Sleep(150);
      PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr);
      if (avail == 0) {
        // Strip trailing "> " and any preceding newline characters.
        buf.resize(buf.size() - 2);
        while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r')) {
          buf.pop_back();
        }
        break;
      }
      continue;  // more data arrived, keep reading
    }

    Sleep(10);
  }

  return buf;
#else
  return "";
#endif
}
