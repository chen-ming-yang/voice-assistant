// tts-module.h — Text-to-Speech module (offline, matcha or zipvoice)
#ifndef VOICE_ASSISTANT_TTS_MODULE_H_
#define VOICE_ASSISTANT_TTS_MODULE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "sherpa-onnx/csrc/offline-tts.h"
#include "sherpa-onnx/csrc/parse-options.h"
#include "common/thread-safe-queue.h"

class TtsModule {
 public:
  ~TtsModule();

  // Callback after speech is fully played. Args: the text that was spoken.
  std::function<void(const std::string &)> on_done;

  // Register CLI flags (tts- prefixed).
  void Register(sherpa_onnx::ParseOptions *po);

  // Validate config, load model, open audio device. Returns false on error.
  bool Init();

  // Start the worker thread.
  void Start();

  // Stop the worker thread.
  void Stop();

  // Queue text for TTS synthesis + playback (non-blocking).
  void Speak(const std::string &text);

  bool enabled() const { return tts_ != nullptr; }

 private:
  void WorkerLoop();
  void DoSpeak(const std::string &text);

  // CLI fields — zipvoice
  std::string tts_encoder_;
  std::string tts_decoder_;

  // CLI fields — matcha
  std::string tts_acoustic_model_;
  std::string tts_dict_dir_;
  std::string tts_rule_fsts_;

  // CLI fields — shared
  std::string tts_vocoder_;
  std::string tts_tokens_;
  std::string tts_lexicon_;
  std::string tts_data_dir_;
  std::string tts_prompt_audio_;
  std::string tts_prompt_text_;
  int32_t tts_num_steps_ = 4;
  std::string tts_provider_ = "cpu";
  int32_t tts_num_threads_ = 2;
  float tts_speed_ = 1.0f;
  float tts_volume_ = 1.0f;
  int32_t tts_sid_ = 0;

  // Loaded prompt audio
  std::vector<float> prompt_samples_;
  int32_t prompt_sample_rate_ = 0;

  sherpa_onnx::OfflineTtsConfig config_;
  std::unique_ptr<sherpa_onnx::OfflineTts> tts_;

  ThreadSafeQueue<std::string> queue_;
  std::thread thread_;
};

#endif  // VOICE_ASSISTANT_TTS_MODULE_H_
