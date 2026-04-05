// json-config.cc — Minimal flat-JSON config parser
#include "common/json-config.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

// Skip whitespace and single-line // comments
void SkipWs(const std::string &s, size_t *pos) {
  while (*pos < s.size()) {
    char c = s[*pos];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      ++(*pos);
    } else if (c == '/' && *pos + 1 < s.size() && s[*pos + 1] == '/') {
      // skip to end of line
      *pos = s.find('\n', *pos);
      if (*pos == std::string::npos) *pos = s.size();
    } else {
      break;
    }
  }
}

// Read a JSON quoted string starting at s[*pos] == '"'.
// Advances past the closing quote.
bool ReadString(const std::string &s, size_t *pos, std::string *out) {
  if (*pos >= s.size() || s[*pos] != '"') return false;
  ++(*pos);
  out->clear();
  while (*pos < s.size()) {
    char c = s[*pos];
    if (c == '"') {
      ++(*pos);
      return true;
    }
    if (c == '\\' && *pos + 1 < s.size()) {
      ++(*pos);
      char n = s[*pos];
      switch (n) {
        case '"': out->push_back('"'); break;
        case '\\': out->push_back('\\'); break;
        case '/': out->push_back('/'); break;
        case 'n': out->push_back('\n'); break;
        case 'r': out->push_back('\r'); break;
        case 't': out->push_back('\t'); break;
        default: out->push_back(n); break;
      }
    } else {
      out->push_back(c);
    }
    ++(*pos);
  }
  return false;  // unterminated
}

// Read a JSON value (number, boolean, or null) as a raw string.
bool ReadLiteral(const std::string &s, size_t *pos, std::string *out) {
  size_t start = *pos;
  while (*pos < s.size()) {
    char c = s[*pos];
    if (c == ',' || c == '}' || c == ' ' || c == '\t' ||
        c == '\r' || c == '\n') {
      break;
    }
    ++(*pos);
  }
  if (*pos == start) return false;
  *out = s.substr(start, *pos - start);
  return true;
}

}  // namespace

bool LoadJsonConfig(const std::string &path,
                    std::vector<std::string> *args_out) {
  args_out->clear();

  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    fprintf(stderr, "[Config] Cannot open: %s\n", path.c_str());
    return false;
  }

  std::ostringstream ss;
  ss << ifs.rdbuf();
  std::string json = ss.str();

  size_t pos = 0;
  SkipWs(json, &pos);
  if (pos >= json.size() || json[pos] != '{') {
    fprintf(stderr, "[Config] Expected '{' at start of %s\n", path.c_str());
    return false;
  }
  ++pos;

  while (true) {
    SkipWs(json, &pos);
    if (pos >= json.size()) {
      fprintf(stderr, "[Config] Unexpected end of file in %s\n", path.c_str());
      return false;
    }
    if (json[pos] == '}') break;

    // Read key
    std::string key;
    if (!ReadString(json, &pos, &key)) {
      fprintf(stderr, "[Config] Expected key string at position %zu\n", pos);
      return false;
    }

    SkipWs(json, &pos);
    if (pos >= json.size() || json[pos] != ':') {
      fprintf(stderr, "[Config] Expected ':' after key \"%s\"\n", key.c_str());
      return false;
    }
    ++pos;
    SkipWs(json, &pos);

    // Read value
    std::string value;
    if (pos < json.size() && json[pos] == '"') {
      if (!ReadString(json, &pos, &value)) {
        fprintf(stderr, "[Config] Unterminated string for key \"%s\"\n",
                key.c_str());
        return false;
      }
    } else {
      if (!ReadLiteral(json, &pos, &value)) {
        fprintf(stderr, "[Config] Bad value for key \"%s\"\n", key.c_str());
        return false;
      }
    }

    // Convert to --key=value (skip null values)
    if (value != "null") {
      args_out->push_back("--" + key + "=" + value);
    }

    SkipWs(json, &pos);
    if (pos < json.size() && json[pos] == ',') ++pos;
  }

  fprintf(stderr, "[Config] Loaded %zu settings from %s\n",
          args_out->size(), path.c_str());
  return true;
}

MergedArgs MergeConfigAndArgs(int argc, char *argv[]) {
  MergedArgs m;

  // argv[0] is the program name — always keep it
  if (argc > 0) {
    m.storage.push_back(argv[0]);
  }

  // Scan for --config=<file>
  std::string config_path;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--config=", 0) == 0) {
      config_path = arg.substr(9);
    }
  }

  // Load JSON config first (lower priority)
  if (!config_path.empty()) {
    std::vector<std::string> json_args;
    if (LoadJsonConfig(config_path, &json_args)) {
      for (auto &a : json_args) {
        m.storage.push_back(std::move(a));
      }
    }
  }

  // Append CLI args after (higher priority — last wins in ParseOptions)
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--config=", 0) == 0) continue;  // already consumed
    m.storage.push_back(std::move(arg));
  }

  // Build argv pointers
  m.argv.reserve(m.storage.size());
  for (auto &s : m.storage) {
    m.argv.push_back(s.c_str());
  }

  return m;
}
