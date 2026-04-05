// vad-module.cc — Voice Activity Detection module implementation
#include "vad/vad-module.h"

#include <stdio.h>

VadModule::~VadModule() { Stop(); }

void VadModule::Register(sherpa_onnx::ParseOptions *po) {
  config_.Register(po);
  po->Register("vad-trailing-silence", &trailing_silence_seconds_,
               "Seconds of audio to continue sending after speech ends.");
  po->Register("vad-lookback", &lookback_seconds_,
               "Seconds of pre-speech audio to send when speech starts.");
}

bool VadModule::Init() {
  if (!config_.Validate()) {
    fprintf(stderr, "Errors in VAD config!\n");
    return false;
  }
  fprintf(stderr, "VAD config: %s\n", config_.ToString().c_str());
  fprintf(stderr, "Trailing silence: %.2fs\n", trailing_silence_seconds_);
  fprintf(stderr, "Lookback: %.2fs\n", lookback_seconds_);

  vad_ = std::make_unique<sherpa_onnx::VoiceActivityDetector>(config_, 30);
  return true;
}

void VadModule::Start() {
  thread_ = std::thread(&VadModule::WorkerLoop, this);
}

void VadModule::Stop() {
  queue_.Shutdown();
  if (thread_.joinable()) thread_.join();
}

void VadModule::AcceptWaveform(const float *samples, int32_t n) {
  queue_.Push({samples, samples + n});
}

void VadModule::WorkerLoop() {
  std::vector<float> chunk;
  while (queue_.Pop(chunk)) {
    ProcessChunk(chunk.data(), static_cast<int32_t>(chunk.size()));
  }
}

void VadModule::ProcessChunk(const float *samples, int32_t n) {
  int32_t ws = window_size();
  int32_t trailing_total =
      static_cast<int32_t>(trailing_silence_seconds_ * config_.sample_rate);
  int32_t lookback_capacity =
      static_cast<int32_t>(lookback_seconds_ * config_.sample_rate);

  int32_t offset = 0;
  while (offset + ws <= n) {
    vad_->AcceptWaveform(samples + offset, ws);

    // State transitions
    if (vad_->IsSpeechDetected() && !speech_on_) {
      speech_on_ = true;
      trailing_remaining_ = 0;
      // Send lookback audio (pre-speech onset) then notify
      if (cb.on_speech_start) {
        cb.on_speech_start(lookback_buf_.data(),
                           static_cast<int32_t>(lookback_buf_.size()));
      }
      lookback_buf_.clear();
    }
    if (!vad_->IsSpeechDetected() && speech_on_) {
      speech_on_ = false;
      trailing_remaining_ = trailing_total;
    }

    // While speech is active, stream each chunk
    if (speech_on_) {
      if (cb.on_speech_chunk) cb.on_speech_chunk(samples + offset, ws);
    } else if (trailing_remaining_ <= 0) {
      // Not in speech and not trailing — accumulate lookback
      lookback_buf_.insert(lookback_buf_.end(), samples + offset,
                           samples + offset + ws);
      if (static_cast<int32_t>(lookback_buf_.size()) > lookback_capacity) {
        lookback_buf_.erase(
            lookback_buf_.begin(),
            lookback_buf_.begin() +
                (lookback_buf_.size() - lookback_capacity));
      }
    }

    // Trailing silence: keep feeding post-speech audio
    if (!speech_on_ && trailing_remaining_ > 0) {
      if (cb.on_trailing_chunk) cb.on_trailing_chunk(samples + offset, ws);
      trailing_remaining_ -= ws;
      if (trailing_remaining_ <= 0) {
        if (cb.on_trailing_end) cb.on_trailing_end();
      }
    }

    offset += ws;
  }

  // Emit completed speech segments
  while (!vad_->Empty()) {
    const auto &seg = vad_->Front();
    if (cb.on_speech_segment) {
      cb.on_speech_segment(seg.samples.data(),
                           static_cast<int32_t>(seg.samples.size()));
    }
    vad_->Pop();
  }
}
