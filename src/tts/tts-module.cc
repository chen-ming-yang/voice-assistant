// tts-module.cc — Text-to-Speech module implementation
#include "tts/tts-module.h"

#include <algorithm>
#include <stdio.h>

#include "sherpa-onnx/csrc/wave-reader.h"
#include "sherpa-onnx/csrc/wave-writer.h"

static void PlayWavWithFFplay(const std::string &wav_path) {
  std::string cmd = "ffplay -nodisp -autoexit \"" + wav_path + "\" 2>nul";
  fprintf(stderr, "[TTS] Playing with ffplay: %s\n", wav_path.c_str());
  int ret = system(cmd.c_str());
  if (ret != 0) {
    fprintf(stderr, "[TTS] ffplay returned %d\n", ret);
  }
}

TtsModule::~TtsModule() {
  Stop();
}

void TtsModule::Register(sherpa_onnx::ParseOptions *po) {
  // Zipvoice model flags
  po->Register("tts-encoder", &tts_encoder_,
               "Path to TTS encoder .onnx (zipvoice). "
               "Provide this for zipvoice, or --tts-acoustic-model for matcha.");
  po->Register("tts-decoder", &tts_decoder_,
               "Path to TTS decoder .onnx (zipvoice).");

  // Matcha model flags
  po->Register("tts-acoustic-model", &tts_acoustic_model_,
               "Path to TTS acoustic model .onnx (matcha). "
               "Provide this for matcha, or --tts-encoder for zipvoice.");
  po->Register("tts-dict-dir", &tts_dict_dir_,
               "Path to TTS dict directory (matcha, for Chinese jieba).");
  po->Register("tts-rule-fsts", &tts_rule_fsts_,
               "Comma-separated rule FST files for text normalization (matcha).");

  // Shared flags
  po->Register("tts-vocoder", &tts_vocoder_,
               "Path to TTS vocoder .onnx.");
  po->Register("tts-tokens", &tts_tokens_, "Path to TTS tokens.txt.");
  po->Register("tts-lexicon", &tts_lexicon_, "Path to TTS lexicon.txt.");
  po->Register("tts-data-dir", &tts_data_dir_,
               "Path to TTS espeak-ng-data directory.");
  po->Register("tts-prompt-audio", &tts_prompt_audio_,
               "Path to prompt/reference audio WAV for zero-shot TTS (zipvoice).");
  po->Register("tts-prompt-text", &tts_prompt_text_,
               "Transcription of the prompt audio (zipvoice).");
  po->Register("tts-num-steps", &tts_num_steps_,
               "Number of flow-matching steps, default 4 (zipvoice).");
  po->Register("tts-provider", &tts_provider_, "TTS provider (cpu/cuda).");
  po->Register("tts-num-threads", &tts_num_threads_, "TTS num threads.");
  po->Register("tts-speed", &tts_speed_, "TTS speech speed (1.0 = normal).");
  po->Register("tts-volume", &tts_volume_,
               "TTS output volume gain (1.0=default, 2.0=louder). "
               "Valid range: 0.1 to 3.0.");
  po->Register("tts-sid", &tts_sid_, "TTS speaker ID (multi-speaker models).");
}

bool TtsModule::Init() {
  bool use_zipvoice = !tts_encoder_.empty();
  bool use_matcha = !tts_acoustic_model_.empty();

  if (!use_zipvoice && !use_matcha) {
    fprintf(stderr, "TTS module disabled (no --tts-encoder or --tts-acoustic-model).\n");
    return true;
  }
  if (use_zipvoice && use_matcha) {
    fprintf(stderr, "Error: specify --tts-encoder (zipvoice) OR --tts-acoustic-model (matcha), not both.\n");
    return false;
  }

  if (use_zipvoice) {
    config_.model.zipvoice.encoder = tts_encoder_;
    config_.model.zipvoice.decoder = tts_decoder_;
    config_.model.zipvoice.vocoder = tts_vocoder_;
    config_.model.zipvoice.tokens = tts_tokens_;
    config_.model.zipvoice.lexicon = tts_lexicon_;
    config_.model.zipvoice.data_dir = tts_data_dir_;
  } else {
    config_.model.matcha.acoustic_model = tts_acoustic_model_;
    config_.model.matcha.vocoder = tts_vocoder_;
    config_.model.matcha.lexicon = tts_lexicon_;
    config_.model.matcha.tokens = tts_tokens_;
    config_.model.matcha.data_dir = tts_data_dir_;
    config_.model.matcha.dict_dir = tts_dict_dir_;
    config_.rule_fsts = tts_rule_fsts_;
  }
  config_.model.num_threads = tts_num_threads_;
  config_.model.provider = tts_provider_;
  config_.model.debug = 0;

  if (!tts_prompt_audio_.empty()) {
    bool is_ok = false;
    prompt_samples_ =
        sherpa_onnx::ReadWave(tts_prompt_audio_, &prompt_sample_rate_, &is_ok);
    if (!is_ok || prompt_samples_.empty()) {
      fprintf(stderr, "TTS: failed to read prompt audio: %s\n",
              tts_prompt_audio_.c_str());
      return false;
    }
    fprintf(stderr, "TTS: loaded prompt audio: %s (%d samples, %d Hz)\n",
            tts_prompt_audio_.c_str(),
            static_cast<int32_t>(prompt_samples_.size()), prompt_sample_rate_);
  }

  if (!config_.Validate()) {
    fprintf(stderr, "Errors in TTS config!\n");
    return false;
  }
  fprintf(stderr, "TTS config: %s\n", config_.ToString().c_str());

  tts_ = std::make_unique<sherpa_onnx::OfflineTts>(config_);

  int32_t sr = tts_->SampleRate();
  fprintf(stderr, "TTS sample rate: %d\n", sr);

  return true;
}

void TtsModule::Start() {
  if (!tts_) return;
  thread_ = std::thread(&TtsModule::WorkerLoop, this);
}

void TtsModule::Stop() {
  queue_.Shutdown();
  if (thread_.joinable()) thread_.join();
}

void TtsModule::Speak(const std::string &text) {
  if (!tts_ || text.empty()) return;
  queue_.Push(text);
}

void TtsModule::WorkerLoop() {
  std::string text;
  while (queue_.Pop(text)) {
    DoSpeak(text);
  }
}

void TtsModule::DoSpeak(const std::string &text) {
  fprintf(stderr, "[TTS] Synthesizing: %s\n", text.c_str());

  sherpa_onnx::GeneratedAudio audio;
  if (!prompt_samples_.empty()) {
    audio = tts_->Generate(text, tts_prompt_text_, prompt_samples_,
                           prompt_sample_rate_, tts_speed_, tts_num_steps_);
  } else {
    audio = tts_->Generate(text, tts_sid_, tts_speed_);
  }

  if (audio.samples.empty()) {
    fprintf(stderr, "[TTS] No audio generated\n");
    return;
  }

  fprintf(stderr, "[TTS] Playing %d samples at %d Hz\n",
          static_cast<int32_t>(audio.samples.size()), audio.sample_rate);

  // Append 200ms silence to avoid tail cutoff
  audio.samples.resize(audio.samples.size() + audio.sample_rate / 5, 0.0f);

  float gain = std::clamp(tts_volume_, 0.1f, 3.0f);
  if (gain != 1.0f) {
    for (float &s : audio.samples) {
      s = std::clamp(s * gain, -1.0f, 1.0f);
    }
    fprintf(stderr, "[TTS] Applied volume gain: %.2f\n", gain);
  }

  // Write to temp WAV and play with ffplay
  std::string wav_path = "tts-output.wav";
  bool ok = sherpa_onnx::WriteWave(
      wav_path, audio.sample_rate, audio.samples.data(),
      static_cast<int32_t>(audio.samples.size()));
  if (!ok) {
    fprintf(stderr, "[TTS] Failed to write WAV: %s\n", wav_path.c_str());
    return;
  }
  PlayWavWithFFplay(wav_path);

  fprintf(stderr, "[TTS] Done\n");
  if (on_done) on_done(text);
}
