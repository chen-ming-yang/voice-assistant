// vad-module.h — Voice Activity Detection module
#ifndef VOICE_ASSISTANT_VAD_MODULE_H_
#define VOICE_ASSISTANT_VAD_MODULE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "sherpa-onnx/csrc/parse-options.h"
#include "sherpa-onnx/csrc/voice-activity-detector.h"
#include "common/thread-safe-queue.h"

// Callback invoked for each speech event.
//   on_speech_start:      speech onset detected; args = lookback audio
//   on_speech_chunk:      a window_size chunk while speech is active (streaming)
//   on_speech_segment:    complete segment (samples at VAD sample rate)
//   on_trailing_chunk:    a window_size chunk during trailing silence
//   on_trailing_end:      trailing silence period finished
struct VadCallbacks {
  std::function<void(const float *, int32_t)> on_speech_start;
  std::function<void(const float *, int32_t)> on_speech_chunk;
  std::function<void(const float *, int32_t)> on_speech_segment;
  std::function<void(const float *, int32_t)> on_trailing_chunk;
  std::function<void()> on_trailing_end;
};

class VadModule {
 public:
  ~VadModule();

  // Register CLI flags on ParseOptions.
  void Register(sherpa_onnx::ParseOptions *po);

  // Validate config. Returns false on error.
  bool Init();

  // Start the worker thread. Call after Init() and setting callbacks.
  void Start();

  // Stop the worker thread.
  void Stop();

  // Queue audio for processing (non-blocking).
  void AcceptWaveform(const float *samples, int32_t n);

  int32_t sample_rate() const { return config_.sample_rate; }
  int32_t window_size() const { return config_.silero_vad.window_size; }

  const sherpa_onnx::VadModelConfig &config() const { return config_; }

 private:
  void WorkerLoop();
  void ProcessChunk(const float *samples, int32_t n);

  sherpa_onnx::VadModelConfig config_;
  std::unique_ptr<sherpa_onnx::VoiceActivityDetector> vad_;
  float trailing_silence_seconds_ = 0.6f;
  float lookback_seconds_ = 0.8f;
  int32_t trailing_remaining_ = 0;
  bool speech_on_ = false;
  std::vector<float> lookback_buf_;

  ThreadSafeQueue<std::vector<float>> queue_;
  std::thread thread_;

 public:
  VadCallbacks cb;
};

#endif  // VOICE_ASSISTANT_VAD_MODULE_H_
