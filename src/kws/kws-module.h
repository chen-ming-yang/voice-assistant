// kws-module.h — Keyword Spotter module
#ifndef VOICE_ASSISTANT_KWS_MODULE_H_
#define VOICE_ASSISTANT_KWS_MODULE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "sherpa-onnx/csrc/keyword-spotter.h"
#include "sherpa-onnx/csrc/parse-options.h"
#include "common/thread-safe-queue.h"

class KwsModule {
 public:
  ~KwsModule();

  // Callback when a keyword is detected. Args: index, json result string.
  std::function<void(int32_t, const std::string &)> on_keyword;

  // Register CLI flags on ParseOptions.
  void Register(sherpa_onnx::ParseOptions *po);

  // Validate config and create the spotter + stream. Returns false on error.
  bool Init();

  // Start the worker thread. Call after Init() and setting callbacks.
  void Start();

  // Stop the worker thread.
  void Stop();

  // Queue audio for processing (non-blocking).
  void AcceptWaveform(int32_t sample_rate, const float *samples, int32_t n);

 private:
  void WorkerLoop();
  void Decode();

  struct AudioChunk {
    int32_t sample_rate;
    std::vector<float> samples;
  };

  sherpa_onnx::KeywordSpotterConfig config_;
  std::unique_ptr<sherpa_onnx::KeywordSpotter> spotter_;
  std::unique_ptr<sherpa_onnx::OnlineStream> stream_;
  int32_t keyword_index_ = 0;

  ThreadSafeQueue<AudioChunk> queue_;
  std::thread thread_;
};

#endif  // VOICE_ASSISTANT_KWS_MODULE_H_
