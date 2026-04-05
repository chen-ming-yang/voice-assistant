// kws-module.cc — Keyword Spotter module implementation
#include "kws/kws-module.h"

#include <stdio.h>

KwsModule::~KwsModule() { Stop(); }

void KwsModule::Register(sherpa_onnx::ParseOptions *po) {
  config_.Register(po);
}

bool KwsModule::Init() {
  if (!config_.Validate()) {
    fprintf(stderr, "Errors in KWS config!\n");
    return false;
  }
  fprintf(stderr, "KWS config: %s\n", config_.ToString().c_str());

  spotter_ = std::make_unique<sherpa_onnx::KeywordSpotter>(config_);
  stream_ = spotter_->CreateStream();
  return true;
}

void KwsModule::Start() {
  thread_ = std::thread(&KwsModule::WorkerLoop, this);
}

void KwsModule::Stop() {
  queue_.Shutdown();
  if (thread_.joinable()) thread_.join();
}

void KwsModule::AcceptWaveform(int32_t sample_rate, const float *samples,
                               int32_t n) {
  queue_.Push({sample_rate, {samples, samples + n}});
}

void KwsModule::WorkerLoop() {
  AudioChunk chunk;
  while (queue_.Pop(chunk)) {
    stream_->AcceptWaveform(chunk.sample_rate, chunk.samples.data(),
                            static_cast<int32_t>(chunk.samples.size()));
    Decode();
  }
}

void KwsModule::Decode() {
  while (spotter_->IsReady(stream_.get())) {
    spotter_->DecodeStream(stream_.get());
    const auto r = spotter_->GetResult(stream_.get());
    if (!r.keyword.empty()) {
      ++keyword_index_;
      if (on_keyword) {
        on_keyword(keyword_index_, r.AsJsonString());
      }
      spotter_->Reset(stream_.get());
    }
  }
}
