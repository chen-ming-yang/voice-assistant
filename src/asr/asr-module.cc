// asr-module.cc — Streaming ASR module implementation
#include "asr/asr-module.h"

#include <stdio.h>

AsrModule::~AsrModule() { Stop(); }

void AsrModule::Register(sherpa_onnx::ParseOptions *po) {
  po->Register("asr-encoder", &asr_encoder_, "Path to ASR encoder.onnx");
  po->Register("asr-decoder", &asr_decoder_, "Path to ASR decoder.onnx");
  po->Register("asr-joiner", &asr_joiner_, "Path to ASR joiner.onnx");
  po->Register("asr-tokens", &asr_tokens_, "Path to ASR tokens.txt");
  po->Register("asr-provider", &asr_provider_, "ASR provider (cpu/cuda).");
  po->Register("asr-decoding-method", &asr_decoding_method_,
               "ASR decoding method: greedy_search or modified_beam_search.");
  po->Register("asr-num-threads", &asr_num_threads_, "ASR num threads.");
  po->Register("asr-enable-endpoint", &asr_enable_endpoint_,
               "Enable endpoint detection for ASR.");
}

bool AsrModule::Init() {
  if (asr_encoder_.empty()) {
    fprintf(stderr, "ASR module disabled (no --asr-encoder provided).\n");
    return true;  // not an error — ASR is optional
  }

  config_.model_config.transducer.encoder = asr_encoder_;
  config_.model_config.transducer.decoder = asr_decoder_;
  config_.model_config.transducer.joiner = asr_joiner_;
  config_.model_config.tokens = asr_tokens_;
  config_.model_config.provider_config.provider = asr_provider_;
  config_.model_config.num_threads = asr_num_threads_;
  config_.model_config.debug = 0;
  config_.decoding_method = asr_decoding_method_;
  config_.enable_endpoint = asr_enable_endpoint_;

  if (!config_.Validate()) {
    fprintf(stderr, "Errors in ASR config!\n");
    return false;
  }
  fprintf(stderr, "ASR config: %s\n", config_.ToString().c_str());

  recognizer_ =
      std::make_unique<sherpa_onnx::OnlineRecognizer>(config_);
  stream_ = recognizer_->CreateStream();
  return true;
}

void AsrModule::Start() {
  if (!recognizer_) return;  // disabled
  thread_ = std::thread(&AsrModule::WorkerLoop, this);
}

void AsrModule::Stop() {
  queue_.Shutdown();
  if (thread_.joinable()) thread_.join();
}

void AsrModule::AcceptWaveform(int32_t sample_rate, const float *samples,
                               int32_t n) {
  if (!recognizer_) return;
  Command cmd;
  cmd.type = CmdType::kAudio;
  cmd.sample_rate = sample_rate;
  cmd.samples.assign(samples, samples + n);
  queue_.Push(std::move(cmd));
}

void AsrModule::EndOfUtterance() {
  if (!recognizer_) return;
  queue_.Push({CmdType::kEndOfUtterance});
}

void AsrModule::Reset() {
  if (!recognizer_) return;
  queue_.Push({CmdType::kReset});
}

void AsrModule::WorkerLoop() {
  Command cmd;
  while (queue_.Pop(cmd)) {
    switch (cmd.type) {
      case CmdType::kAudio:
        DoAcceptWaveform(cmd.sample_rate, cmd.samples.data(),
                         static_cast<int32_t>(cmd.samples.size()));
        break;
      case CmdType::kEndOfUtterance:
        DoEndOfUtterance();
        break;
      case CmdType::kReset:
        DoReset();
        break;
    }
  }
}

void AsrModule::DoAcceptWaveform(int32_t sample_rate, const float *samples,
                                 int32_t n) {
  stream_->AcceptWaveform(sample_rate, samples, n);
  Decode();
}

void AsrModule::DoEndOfUtterance() {
  // Feed tail padding (0.3s of silence) so the model can finish
  float tail[4800] = {0};
  stream_->AcceptWaveform(16000, tail, 4800);
  stream_->InputFinished();

  // Final decode
  while (recognizer_->IsReady(stream_.get())) {
    recognizer_->DecodeStream(stream_.get());
  }

  auto r = recognizer_->GetResult(stream_.get());
  if (!r.text.empty() && on_text) {
    on_text(segment_id_, r.text, true);
  }

  // Reset for next utterance
  ++segment_id_;
  stream_ = recognizer_->CreateStream();
}

void AsrModule::DoReset() {
  stream_ = recognizer_->CreateStream();
}

void AsrModule::Decode() {
  while (recognizer_->IsReady(stream_.get())) {
    recognizer_->DecodeStream(stream_.get());
  }

  auto r = recognizer_->GetResult(stream_.get());

  if (config_.enable_endpoint && recognizer_->IsEndpoint(stream_.get())) {
    if (!r.text.empty() && on_text) {
      on_text(segment_id_, r.text, true);
    }
    ++segment_id_;
    recognizer_->Reset(stream_.get());
  } else if (!r.text.empty() && on_text) {
    on_text(segment_id_, r.text, false);
  }
}
