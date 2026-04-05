// asr-module.h — Streaming ASR (online recognizer) module
#ifndef VOICE_ASSISTANT_ASR_MODULE_H_
#define VOICE_ASSISTANT_ASR_MODULE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "sherpa-onnx/csrc/online-recognizer.h"
#include "sherpa-onnx/csrc/parse-options.h"
#include "common/thread-safe-queue.h"

class AsrModule {
 public:
  ~AsrModule();

  // Callback when partial or final text is available.
  //   segment_id, text, is_endpoint
  std::function<void(int32_t, const std::string &, bool)> on_text;

  // Register CLI flags (asr- prefixed to avoid clash with KWS).
  void Register(sherpa_onnx::ParseOptions *po);

  // Validate config, create recognizer + stream. Returns false on error.
  bool Init();

  // Start the worker thread. Call after Init() and setting callbacks.
  void Start();

  // Stop the worker thread.
  void Stop();

  // Queue audio for processing (non-blocking).
  void AcceptWaveform(int32_t sample_rate, const float *samples, int32_t n);

  // Queue end-of-utterance command (non-blocking).
  void EndOfUtterance();

  // Queue reset command (non-blocking).
  void Reset();

 private:
  void WorkerLoop();
  void DoAcceptWaveform(int32_t sample_rate, const float *samples, int32_t n);
  void DoEndOfUtterance();
  void DoReset();
  void Decode();

  enum class CmdType { kAudio, kEndOfUtterance, kReset };
  struct Command {
    CmdType type;
    int32_t sample_rate = 0;
    std::vector<float> samples;
  };

  // CLI-registered fields (asr- prefix)
  std::string asr_encoder_;
  std::string asr_decoder_;
  std::string asr_joiner_;
  std::string asr_tokens_;
  std::string asr_provider_ = "cpu";
  std::string asr_decoding_method_ = "greedy_search";
  int32_t asr_num_threads_ = 2;
  bool asr_enable_endpoint_ = true;

  sherpa_onnx::OnlineRecognizerConfig config_;
  std::unique_ptr<sherpa_onnx::OnlineRecognizer> recognizer_;
  std::unique_ptr<sherpa_onnx::OnlineStream> stream_;
  int32_t segment_id_ = 0;

  ThreadSafeQueue<Command> queue_;
  std::thread thread_;
};

#endif  // VOICE_ASSISTANT_ASR_MODULE_H_
