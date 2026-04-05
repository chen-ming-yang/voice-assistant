// json-config.h — Load a flat JSON config file into argc/argv style args
#ifndef VOICE_ASSISTANT_JSON_CONFIG_H_
#define VOICE_ASSISTANT_JSON_CONFIG_H_

#include <string>
#include <vector>

// Parse a flat JSON object file (keys are flag names, values are strings,
// numbers, or booleans) and build a vector of "--key=value" strings.
// Returns false if the file cannot be opened or is malformed.
bool LoadJsonConfig(const std::string &path,
                    std::vector<std::string> *args_out);

// Merge JSON config args with real command-line args.
// Scans argv for --config=<file>, loads it, then appends remaining CLI args
// (which override JSON values since ParseOptions uses last-wins).
// Returns the merged argv as a vector of C strings suitable for po.Read().
// The caller must keep `storage` alive as long as the returned pointers are
// used.
struct MergedArgs {
  std::vector<std::string> storage;  // owns the strings
  std::vector<const char *> argv;    // points into storage
  int argc() const { return static_cast<int>(argv.size()); }
};

MergedArgs MergeConfigAndArgs(int argc, char *argv[]);

#endif
