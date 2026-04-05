// nlu-module.h — NLU module via HTTP (llama.cpp server) or local llama-cli
#ifndef VOICE_ASSISTANT_NLU_MODULE_H_
#define VOICE_ASSISTANT_NLU_MODULE_H_

#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "sherpa-onnx/csrc/parse-options.h"
#include "common/thread-safe-queue.h"

struct NluLocalProcess;  // defined in .cc

class NluModule {
 public:
  ~NluModule();

  // Callback when NLU returns a response.
  std::function<void(const std::string &response)> on_result;

  // Register CLI flags.
  void Register(sherpa_onnx::ParseOptions *po);

  // Validate config. Returns false on error.
  // If neither --nlu-server-url nor --nlu-llama-cli is given, NLU is disabled.
  bool Init();

  // Start the worker thread. Call after Init() and setting callbacks.
  void Start();

  // Stop the worker thread.
  void Stop();

  // Queue text for NLU processing (non-blocking).
  // No-op if NLU is disabled.
  void Process(const std::string &text);

  // Stop the worker thread and release resources.
  void Close();

  bool enabled() const { return !nlu_url_.empty() || local_proc_ != nullptr; }

 private:
  void WorkerLoop();
  void DoProcess(const std::string &text);
  void DoProcessHttp(const std::string &text);
  void DoProcessLocal(const std::string &text);
  bool LaunchLocalProcess();
  std::string ReadUntilPrompt(int32_t timeout_ms);

  // --- HTTP mode fields ---
  std::string nlu_url_;
  bool nlu_https_ = false;
  std::string nlu_host_;
  int32_t nlu_port_ = 80;
  std::string nlu_path_ = "/completion";

  // --- Local mode fields ---
  std::string nlu_llama_cli_;   // path to llama-cli executable
  std::string nlu_model_;       // path to GGUF model file
  std::string nlu_llama_args_;  // extra args for llama-cli
  NluLocalProcess *local_proc_ = nullptr;

  // --- Shared fields ---
  int32_t nlu_timeout_ms_ = 8000;
  int32_t nlu_n_predict_ = 256;
  bool nlu_cache_prompt_ = false;
  std::string nlu_prompt_prefix_ = "Answer briefly in one sentence: ";

  ThreadSafeQueue<std::string> queue_;
  std::thread thread_;
};

#endif  // VOICE_ASSISTANT_NLU_MODULE_H_
