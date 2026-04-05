// tts-test.cc — Standalone TTS test program.
// Usage (zipvoice):
//   tts-test.exe --tts-encoder=... --tts-decoder=... --tts-vocoder=...
//                --tts-tokens=... [--tts-lexicon=...] [--tts-data-dir=...]
//                --tts-prompt-audio=... --tts-prompt-text="..."
//                [--tts-num-steps=4] [--tts-speed=1.0] [--tts-volume=1.0]
//                [--text="你好世界"] [--output-wav=output.wav]
// Usage (matcha):
//   tts-test.exe --tts-acoustic-model=... --tts-vocoder=... --tts-tokens=...
//                [--tts-lexicon=...] [--tts-dict-dir=...] [--tts-rule-fsts=...]
//                [--tts-speed=1.0] [--tts-volume=1.0] [--tts-sid=0]
//                [--text="你好世界"] [--output-wav=output.wav]
//
// Synthesizes the given text, saves to WAV and plays via ffplay.
// If --text is not specified, enters interactive mode reading from stdin.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "sherpa-onnx/csrc/offline-tts.h"
#include "sherpa-onnx/csrc/parse-options.h"
#include "sherpa-onnx/csrc/wave-reader.h"
#include "sherpa-onnx/csrc/wave-writer.h"

static void PlayWavWithFFplay(const std::string &wav_path) {
  std::string cmd = "ffplay -nodisp -autoexit \"" + wav_path + "\"";
  fprintf(stderr, "[tts-test] Playing with ffplay: %s\n", wav_path.c_str());
  int ret = system(cmd.c_str());
  if (ret != 0) {
    fprintf(stderr, "[tts-test] ffplay returned %d\n", ret);
  }
}

int main(int argc, char *argv[]) {
  std::string text;
  std::string output_wav = "tts-test-output.wav";

  // TTS config fields — zipvoice
  std::string tts_encoder;
  std::string tts_decoder;

  // TTS config fields — matcha
  std::string tts_acoustic_model;
  std::string tts_dict_dir;
  std::string tts_rule_fsts;

  // TTS config fields — shared
  std::string tts_vocoder;
  std::string tts_tokens;
  std::string tts_lexicon;
  std::string tts_data_dir;
  std::string tts_prompt_audio;
  std::string tts_prompt_text;
  int32_t tts_num_steps = 4;
  std::string tts_provider = "cpu";
  int32_t tts_num_threads = 2;
  float tts_speed = 1.0f;
  float tts_volume = 1.0f;
  int32_t tts_sid = 0;

  sherpa_onnx::ParseOptions po("TTS test");
  po.Register("text", &text,
              "Text to synthesize. If empty, reads lines from stdin.");
  po.Register("output-wav", &output_wav,
              "Path to save output WAV file (default: tts-test-output.wav).");
  po.Register("tts-encoder", &tts_encoder,
              "Path to TTS encoder .onnx (zipvoice).");
  po.Register("tts-decoder", &tts_decoder,
              "Path to TTS decoder .onnx (zipvoice).");
  po.Register("tts-acoustic-model", &tts_acoustic_model,
              "Path to TTS acoustic model .onnx (matcha).");
  po.Register("tts-dict-dir", &tts_dict_dir,
              "Path to TTS dict directory (matcha).");
  po.Register("tts-rule-fsts", &tts_rule_fsts,
              "Comma-separated rule FSTs (matcha).");
  po.Register("tts-vocoder", &tts_vocoder, "Path to TTS vocoder .onnx.");
  po.Register("tts-tokens", &tts_tokens, "Path to TTS tokens.txt.");
  po.Register("tts-lexicon", &tts_lexicon, "Path to TTS lexicon.txt.");
  po.Register("tts-data-dir", &tts_data_dir, "Path to espeak-ng-data dir.");
  po.Register("tts-prompt-audio", &tts_prompt_audio,
              "Path to prompt/reference WAV for zero-shot TTS.");
  po.Register("tts-prompt-text", &tts_prompt_text,
              "Transcription of the prompt audio.");
  po.Register("tts-num-steps", &tts_num_steps,
              "Number of flow-matching steps (default 4).");
  po.Register("tts-provider", &tts_provider, "TTS provider (cpu/cuda).");
  po.Register("tts-num-threads", &tts_num_threads, "TTS num threads.");
  po.Register("tts-speed", &tts_speed, "TTS speech speed.");
  po.Register("tts-volume", &tts_volume, "TTS volume gain.");
  po.Register("tts-sid", &tts_sid, "TTS speaker ID (matcha).");
  po.Read(argc, argv);

  bool use_zipvoice = !tts_encoder.empty();
  bool use_matcha = !tts_acoustic_model.empty();
  if (!use_zipvoice && !use_matcha) {
    fprintf(stderr, "Please provide --tts-encoder (zipvoice) or --tts-acoustic-model (matcha).\n");
    return 1;
  }
  if (use_zipvoice && use_matcha) {
    fprintf(stderr, "Error: specify --tts-encoder (zipvoice) OR --tts-acoustic-model (matcha), not both.\n");
    return 1;
  }

  // Build config
  sherpa_onnx::OfflineTtsConfig config;
  if (use_zipvoice) {
    config.model.zipvoice.encoder = tts_encoder;
    config.model.zipvoice.decoder = tts_decoder;
    config.model.zipvoice.vocoder = tts_vocoder;
    config.model.zipvoice.tokens = tts_tokens;
    config.model.zipvoice.lexicon = tts_lexicon;
    config.model.zipvoice.data_dir = tts_data_dir;
  } else {
    config.model.matcha.acoustic_model = tts_acoustic_model;
    config.model.matcha.vocoder = tts_vocoder;
    config.model.matcha.lexicon = tts_lexicon;
    config.model.matcha.tokens = tts_tokens;
    config.model.matcha.data_dir = tts_data_dir;
    config.model.matcha.dict_dir = tts_dict_dir;
    config.rule_fsts = tts_rule_fsts;
  }
  config.model.num_threads = tts_num_threads;
  config.model.provider = tts_provider;
  config.model.debug = 1;

  if (!config.Validate()) {
    fprintf(stderr, "TTS config validation failed!\n");
    return 1;
  }

  sherpa_onnx::OfflineTts tts(config);
  int32_t sr = tts.SampleRate();
  fprintf(stderr, "[tts-test] TTS sample rate: %d\n", sr);

  // Load prompt audio if provided
  std::vector<float> prompt_samples;
  int32_t prompt_sr = 0;
  if (!tts_prompt_audio.empty()) {
    bool is_ok = false;
    prompt_samples = sherpa_onnx::ReadWave(tts_prompt_audio, &prompt_sr, &is_ok);
    if (!is_ok || prompt_samples.empty()) {
      fprintf(stderr, "Failed to read prompt audio: %s\n",
              tts_prompt_audio.c_str());
      return 1;
    }
    fprintf(stderr, "[tts-test] Prompt audio: %s (%d samples, %d Hz)\n",
            tts_prompt_audio.c_str(),
            static_cast<int32_t>(prompt_samples.size()), prompt_sr);
  }

  auto synthesize_and_play = [&](const std::string &input_text) {
    fprintf(stderr, "[tts-test] Synthesizing: %s\n", input_text.c_str());

    sherpa_onnx::GeneratedAudio audio;
    if (use_zipvoice && !prompt_samples.empty()) {
      audio = tts.Generate(input_text, tts_prompt_text, prompt_samples,
                           prompt_sr, tts_speed, tts_num_steps);
    } else {
      audio = tts.Generate(input_text, static_cast<int64_t>(tts_sid), tts_speed);
    }

    if (audio.samples.empty()) {
      fprintf(stderr, "[tts-test] No audio generated!\n");
      return;
    }

    // Apply volume
    float gain = std::clamp(tts_volume, 0.1f, 3.0f);
    if (gain != 1.0f) {
      for (float &s : audio.samples) {
        s = std::clamp(s * gain, -1.0f, 1.0f);
      }
    }

    fprintf(stderr, "[tts-test] Generated %d samples at %d Hz (%.2f seconds)\n",
            static_cast<int32_t>(audio.samples.size()), audio.sample_rate,
            static_cast<float>(audio.samples.size()) / audio.sample_rate);

    // Append 200ms silence to avoid tail cutoff
    audio.samples.resize(audio.samples.size() + audio.sample_rate / 5, 0.0f);

    // Save WAV
    bool ok = sherpa_onnx::WriteWave(
        output_wav, audio.sample_rate, audio.samples.data(),
        static_cast<int32_t>(audio.samples.size()));
    if (ok) {
      fprintf(stderr, "[tts-test] Saved WAV to: %s\n", output_wav.c_str());
    } else {
      fprintf(stderr, "[tts-test] Failed to save WAV!\n");
    }

    // Play audio via ffplay
    PlayWavWithFFplay(output_wav);
  };

  if (!text.empty()) {
    synthesize_and_play(text);
  } else {
    fprintf(stderr,
            "Interactive mode. Type a sentence and press Enter.\n"
            "Type 'quit' or Ctrl+Z to exit.\n\n");
    std::string line;
    while (true) {
      fprintf(stderr, "> ");
      if (!std::getline(std::cin, line)) break;
      if (line == "quit" || line == "exit") break;
      if (line.empty()) continue;
      synthesize_and_play(line);
    }
  }

  fprintf(stderr, "Done.\n");
  return 0;
}
