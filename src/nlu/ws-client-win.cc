// ws-client-win.cc — WinHTTP WebSocket client implementation
#include "nlu/ws-client.h"

#include <stdio.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>

struct WsClient {
  HINTERNET session = nullptr;
  HINTERNET connection = nullptr;
  HINTERNET websocket = nullptr;
};

// Parse "ws://host:port/path" or "wss://host:port/path"
static bool ParseWsUrl(const std::string &url, std::wstring &host,
                       INTERNET_PORT &port, std::wstring &path,
                       bool &use_ssl) {
  std::string s = url;
  use_ssl = false;

  if (s.size() >= 6 && s.substr(0, 6) == "wss://") {
    use_ssl = true;
    s = s.substr(6);
  } else if (s.size() >= 5 && s.substr(0, 5) == "ws://") {
    s = s.substr(5);
  } else {
    fprintf(stderr, "WsClient: invalid WebSocket URL scheme: %s\n",
            url.c_str());
    return false;
  }

  auto slash_pos = s.find('/');
  std::string host_port;
  std::string path_str = "/";
  if (slash_pos != std::string::npos) {
    host_port = s.substr(0, slash_pos);
    path_str = s.substr(slash_pos);
  } else {
    host_port = s;
  }

  auto colon_pos = host_port.find(':');
  std::string host_str;
  if (colon_pos != std::string::npos) {
    host_str = host_port.substr(0, colon_pos);
    port = static_cast<INTERNET_PORT>(
        std::stoi(host_port.substr(colon_pos + 1)));
  } else {
    host_str = host_port;
    port = use_ssl ? 443 : 80;
  }

  host.assign(host_str.begin(), host_str.end());
  path.assign(path_str.begin(), path_str.end());
  return true;
}

WsClient *WsConnect(const std::string &url) {
  std::wstring host, path;
  INTERNET_PORT port = 0;
  bool use_ssl = false;

  if (!ParseWsUrl(url, host, port, path, use_ssl)) return nullptr;

  HINTERNET hSession =
      WinHttpOpen(L"VoiceAssistant/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    fprintf(stderr, "WsClient: WinHttpOpen failed (%lu)\n", GetLastError());
    return nullptr;
  }

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
  if (!hConnect) {
    fprintf(stderr, "WsClient: WinHttpConnect failed (%lu)\n", GetLastError());
    WinHttpCloseHandle(hSession);
    return nullptr;
  }

  DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                          WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    fprintf(stderr, "WsClient: WinHttpOpenRequest failed (%lu)\n",
            GetLastError());
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return nullptr;
  }

  if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL,
                        0)) {
    fprintf(stderr, "WsClient: WebSocket upgrade option failed (%lu)\n",
            GetLastError());
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return nullptr;
  }

  if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0,
                          0, 0)) {
    fprintf(stderr, "WsClient: WinHttpSendRequest failed (%lu)\n",
            GetLastError());
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return nullptr;
  }

  if (!WinHttpReceiveResponse(hRequest, NULL)) {
    fprintf(stderr, "WsClient: WinHttpReceiveResponse failed (%lu)\n",
            GetLastError());
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return nullptr;
  }

  HINTERNET hWebSocket = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
  if (!hWebSocket) {
    fprintf(stderr, "WsClient: WebSocket upgrade failed (%lu)\n",
            GetLastError());
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return nullptr;
  }

  WinHttpCloseHandle(hRequest);

  auto *ws = new WsClient;
  ws->session = hSession;
  ws->connection = hConnect;
  ws->websocket = hWebSocket;

  fprintf(stderr, "WsClient: connected to %s\n", url.c_str());
  return ws;
}

bool WsSend(WsClient *ws, const std::string &text) {
  if (!ws) return false;
  DWORD err = WinHttpWebSocketSend(
      ws->websocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
      const_cast<void *>(static_cast<const void *>(text.data())),
      static_cast<DWORD>(text.size()));
  if (err != NO_ERROR) {
    fprintf(stderr, "WsClient: send failed (%lu)\n", err);
    return false;
  }
  return true;
}

bool WsRecv(WsClient *ws, std::string *out) {
  if (!ws) return false;
  out->clear();
  char buf[4096];
  DWORD bytes_read = 0;
  WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type;

  do {
    DWORD err = WinHttpWebSocketReceive(ws->websocket, buf, sizeof(buf),
                                        &bytes_read, &buf_type);
    if (err != NO_ERROR) {
      fprintf(stderr, "WsClient: receive failed (%lu)\n", err);
      return false;
    }
    out->append(buf, bytes_read);
  } while (buf_type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);

  if (buf_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
    fprintf(stderr, "WsClient: server closed connection\n");
    return false;
  }
  return true;
}

void WsClose(WsClient *ws) {
  if (!ws) return;
  if (ws->websocket) {
    WinHttpWebSocketClose(ws->websocket,
                          WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    WinHttpCloseHandle(ws->websocket);
  }
  if (ws->connection) WinHttpCloseHandle(ws->connection);
  if (ws->session) WinHttpCloseHandle(ws->session);
  delete ws;
}
