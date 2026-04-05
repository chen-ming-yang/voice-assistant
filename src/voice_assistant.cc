// voice_assistant.cc — Main entry point
//
// Pipeline:
//   Mic → Resample(16kHz) → KWS (Listening state only)
//   Keyword detected → enter Active state (VAD + ASR)
//   VAD speech segments → ASR → text output
//   Stay Active until ASR recognizes "退出" → back to Listening

#include <stdio.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "asr/asr-module.h"
#include "kws/kws-module.h"
#include "audio/mic-capture.h"
#include "nlu/nlu-module.h"
#include "sherpa-onnx/csrc/parse-options.h"
#include "sherpa-onnx/csrc/resample.h"
#include "tts/tts-module.h"
#include "vad/vad-module.h"
#include "common/json-config.h"

static std::atomic<bool> g_stop{false};

static void SignalHandler(int /*sig*/) {
  g_stop = true;
  fprintf(stderr, "\nCaught Ctrl+C. Exiting...\n");
}

// Assistant states
enum class State {
  kListening,  // KWS only — waiting for wake word
  kActive,     // VAD + ASR — processing user speech
};

static std::string SanitizeTextForTts(const std::string &in,
                                      size_t max_bytes = 300) {
  std::string out;
  out.reserve(in.size());

  bool prev_space = false;
  for (unsigned char c : in) {
    // Keep non-ASCII bytes (e.g., Chinese UTF-8 bytes) untouched.
    if (c >= 128) {
      out.push_back(static_cast<char>(c));
      prev_space = false;
      continue;
    }

    // Keep readable ASCII letters/digits and sentence punctuation.
    bool keep = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') || c == '.' || c == ',' || c == '?' ||
                c == '!' || c == ' ';

    if (keep) {
      char ch = static_cast<char>(c);
      if (ch == ' ') {
        if (prev_space) continue;
        prev_space = true;
      } else {
        prev_space = false;
      }
      out.push_back(ch);
    } else {
      if (!prev_space) {
        out.push_back(' ');
        prev_space = true;
      }
    }

    if (out.size() >= max_bytes) break;
  }

  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

int main(int32_t argc, char *argv[]) {
  signal(SIGINT, SignalHandler);

  const char *kUsageMessage = R"usage(
Voice assistant: Mic -> KWS -> VAD -> ASR.

KWS runs in Listening state. When a keyword is detected, enters Active state.
In Active state, VAD+ASR process speech continuously.
Say "退出" to return to Listening (KWS) state.

Usage:
  voice-assistant \
    --silero-vad-model=/path/to/silero_vad.onnx \
    --tokens=/path/to/tokens.txt \
    --encoder=/path/to/encoder.onnx \
    --decoder=/path/to/decoder.onnx \
    --joiner=/path/to/joiner.onnx \
    --keywords-file=keywords.txt \
    --asr-encoder=/path/to/asr-encoder.onnx \
    --asr-decoder=/path/to/asr-decoder.onnx \
    --asr-joiner=/path/to/asr-joiner.onnx \
    --asr-tokens=/path/to/asr-tokens.txt
)usage";

  sherpa_onnx::ParseOptions po(kUsageMessage);

  KwsModule kws;
  VadModule vad;
  AsrModule asr;
  NluModule nlu;
  TtsModule tts;
  std::string exit_keyword = "退出";

  kws.Register(&po);
  vad.Register(&po);
  asr.Register(&po);
  nlu.Register(&po);
  tts.Register(&po);
  po.Register("exit-keyword", &exit_keyword,
              "ASR text that triggers return to Listening state.");

  // Merge --config=<file.json> with CLI args (CLI overrides JSON)
  MergedArgs merged = MergeConfigAndArgs(argc, argv);
  po.Read(merged.argc(), merged.argv.data());

  if (po.NumArgs() != 0) {
    po.PrintUsage();
    return EXIT_FAILURE;
  }

  if (!kws.Init() || !vad.Init() || !asr.Init() || !nlu.Init())
    return EXIT_FAILURE;

  // TTS is optional; Init() returns true even if not configured
  if (!tts.Init()) return EXIT_FAILURE;

  // --- State machine ---
  std::atomic<State> state{State::kListening};
  std::atomic<bool> tts_speaking{false};
  int32_t vad_sr = vad.sample_rate();

  // KWS: keyword triggers transition to Active (only checked in Listening)
  kws.on_keyword = [&](int32_t idx, const std::string &json) {
    fprintf(stderr, "\n[Keyword #%d detected] %s\n", idx, json.c_str());
    fflush(stderr);
    state.store(State::kActive);
    asr.Reset();
    fprintf(stderr, "[State] Active — say \"%s\" to return to listening.\n",
            exit_keyword.c_str());
  };

  // VAD callbacks — active in Active state
  vad.cb.on_speech_start = [&](const float *samples, int32_t n) {
    fprintf(stderr, "[VAD] Speech detected (lookback: %d samples)\n", n);
    if (n > 0) {
      asr.AcceptWaveform(vad_sr, samples, n);
    }
  };

  // Stream each chunk to ASR in real-time while speech is active
  vad.cb.on_speech_chunk = [&](const float *samples, int32_t n) {
    asr.AcceptWaveform(vad_sr, samples, n);
  };

  vad.cb.on_trailing_chunk = [&](const float *samples, int32_t n) {
    asr.AcceptWaveform(vad_sr, samples, n);
  };

  vad.cb.on_trailing_end = [&]() {
    fprintf(stderr, "[VAD] Trailing silence ended\n");
    // Finalize current utterance, but stay in Active state
    asr.EndOfUtterance();
  };

  // NLU: handle server response — speak it via TTS
  nlu.on_result = [&](const std::string &response) {
    fprintf(stderr, "[NLU Result] %s\n", response.c_str());
    if (tts.enabled()) {
      std::string tts_text = SanitizeTextForTts(response);
      fprintf(stderr, "[TTS Input] %s\n", tts_text.c_str());
      if (!tts_text.empty()) {
        tts_speaking.store(true);
        tts.Speak(tts_text);
      }
    }
  };

  // TTS: playback finished — resume mic processing
  tts.on_done = [&](const std::string &) {
    fprintf(stderr, "[TTS] Playback finished, resuming mic\n");
    tts_speaking.store(false);
  };

  // ASR: text output — check for exit keyword, send final text to NLU
  asr.on_text = [&](int32_t seg, const std::string &text, bool is_final) {
    if (is_final) {
      fprintf(stderr, "[ASR Final #%d] %s\n", seg, text.c_str());
      // Check if the recognized text contains the exit keyword
      if (text.find(exit_keyword) != std::string::npos) {
        state.store(State::kListening);
        fprintf(stderr, "[State] Listening — waiting for keyword...\n\n");
      } else if (nlu.enabled()) {
        // Only send to NLU if it's not the exit command
        nlu.Process(text);
      }
    } else {
      fprintf(stderr, "\r[ASR] %s", text.c_str());
    }
    fflush(stderr);
  };

  // --- Open microphone ---
  MicCapture *mic = MicOpen();
  if (!mic) {
    fprintf(stderr, "Failed to open microphone!\n");
    return EXIT_FAILURE;
  }

  // Start worker threads for each module
  kws.Start();
  vad.Start();
  asr.Start();
  nlu.Start();
  tts.Start();

  float mic_rate = MicGetSampleRate(mic);

  // Resampler: mic sample rate → 16 kHz
  std::unique_ptr<sherpa_onnx::LinearResample> resampler;
  if (static_cast<int32_t>(mic_rate) != vad_sr) {
    float cutoff = std::min(mic_rate, static_cast<float>(vad_sr)) / 2.0f;
    resampler = std::make_unique<sherpa_onnx::LinearResample>(
        static_cast<int32_t>(mic_rate), vad_sr, cutoff, 6);
    fprintf(stderr, "Resampling: %.0f Hz -> %d Hz\n", mic_rate, vad_sr);
  }

  int32_t ws = vad.window_size();
  std::vector<float> mic_samples;
  std::vector<float> resampled_buf;

  fprintf(stderr, "[State] Listening — waiting for keyword...\n\n");

  while (!g_stop) {
    MicRead(mic, &mic_samples);

    if (!mic_samples.empty()) {
      // Resample to 16 kHz
      std::vector<float> samples_16k;
      if (resampler) {
        resampler->Resample(mic_samples.data(), mic_samples.size(), false,
                            &samples_16k);
      } else {
        samples_16k.assign(mic_samples.begin(), mic_samples.end());
      }

      State cur = state.load();

      // Skip mic input while TTS is playing to prevent feedback loop
      if (tts_speaking.load()) continue;

      // Listening: feed KWS only
      if (cur == State::kListening) {
        kws.AcceptWaveform(vad_sr, samples_16k.data(), samples_16k.size());
      }

      // Active: feed VAD (which feeds ASR via callbacks)
      if (cur == State::kActive) {
        resampled_buf.insert(resampled_buf.end(), samples_16k.begin(),
                             samples_16k.end());
        while (static_cast<int32_t>(resampled_buf.size()) >= ws) {
          vad.AcceptWaveform(resampled_buf.data(), ws);
          resampled_buf.erase(resampled_buf.begin(),
                              resampled_buf.begin() + ws);
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // Stop worker threads and close microphone
  kws.Stop();
  vad.Stop();
  asr.Stop();
  nlu.Stop();
  tts.Stop();
  MicClose(mic);
  return 0;
}
