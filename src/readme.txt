== Build sherpa-onnx (from repo root, in PowerShell) ==

cd build
Remove-Item -Recurse -Force CMakeCache.txt, CMakeFiles -ErrorAction SilentlyContinue; & "C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A x64 -DSHERPA_ONNX_ENABLE_TTS=ON -DSHERPA_ONNX_USE_STATIC_CRT=OFF -DCMAKE_BUILD_TYPE=Release -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=./install -DBUILD_ESPEAK_NG_EXE=OFF ..
& "C:\Program Files\CMake\bin\cmake.exe" --build . --config Release --parallel
& "C:\Program Files\CMake\bin\cmake.exe" -DCMAKE_INSTALL_PREFIX="C:/Users/89488/sherpa-onnx/build/install" .; & "C:\Program Files\CMake\bin\cmake.exe" --build . --config Release --target install --parallel
cd ..

== Run voice-assistant (from voice-assistant/ directory, in PowerShell) ==

== using config.json (recommended) ==

.\build\src\Release\voice-assistant.exe --config=config.json

  Config file: voice-assistant/config.json
  Edit config.json to switch between TTS models, NLU backend, etc.
  CLI args override config.json values, e.g.:
    .\build\src\Release\voice-assistant.exe --config=config.json --tts-volume=3.0

== with zipvoice TTS (full command line, no config file) ==

..\build\src\Release\voice-assistant.exe `
  --silero-vad-model=silero_vad.onnx `
  --encoder=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/encoder-epoch-13-avg-2-chunk-16-left-64.onnx `
  --decoder=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/decoder-epoch-13-avg-2-chunk-16-left-64.onnx `
  --joiner=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/joiner-epoch-13-avg-2-chunk-16-left-64.onnx `
  --tokens=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt `
  --keywords-file=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords.txt `
  --asr-encoder=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/encoder.int8.onnx `
  --asr-decoder=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/decoder.onnx `
  --asr-joiner=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/joiner.int8.onnx `
  --asr-tokens=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/tokens.txt `
  --tts-encoder=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/encoder.int8.onnx `
  --tts-decoder=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/decoder.int8.onnx `
  --tts-vocoder=vocos_24khz.onnx `
  --tts-tokens=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/tokens.txt `
  --tts-lexicon=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/lexicon.txt `
  --tts-data-dir=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/espeak-ng-data `
  --tts-prompt-audio=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/test_wavs/leijun-1.wav `
  --tts-prompt-text="那还是36年前, 1987年. 我呢考上了武汉大学的计算机系." `
  --nlu-server-url=http://192.168.31.27:8080/ `
  --nlu-timeout-ms=8000 `
  --nlu-n-predict=128 `
  --nlu-cache-prompt=false `
  --nlu-prompt-prefix="Answer briefly in one sentence: " `
  --tts-volume=2.0

== with matcha TTS ==

..\build\src\Release\voice-assistant.exe `
  --silero-vad-model=silero_vad.onnx `
  --encoder=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/encoder-epoch-13-avg-2-chunk-16-left-64.onnx `
  --decoder=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/decoder-epoch-13-avg-2-chunk-16-left-64.onnx `
  --joiner=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/joiner-epoch-13-avg-2-chunk-16-left-64.onnx `
  --tokens=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt `
  --keywords-file=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords.txt `
  --asr-encoder=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/encoder.int8.onnx `
  --asr-decoder=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/decoder.onnx `
  --asr-joiner=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/joiner.int8.onnx `
  --asr-tokens=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/tokens.txt `
  --tts-acoustic-model=matcha-icefall-zh-baker/model-steps-3.onnx `
  --tts-vocoder=vocos-22khz-univ.onnx `
  --tts-lexicon=matcha-icefall-zh-baker/lexicon.txt `
  --tts-tokens=matcha-icefall-zh-baker/tokens.txt `
  --tts-dict-dir=matcha-icefall-zh-baker/dict `
  --tts-rule-fsts=matcha-icefall-zh-baker/phone.fst,matcha-icefall-zh-baker/date.fst,matcha-icefall-zh-baker/number.fst `
  --nlu-server-url=http://192.168.31.27:8080/ `
  --nlu-timeout-ms=8000 `
  --nlu-n-predict=128 `
  --nlu-cache-prompt=false `
  --nlu-prompt-prefix="Answer briefly in one sentence: " `
  --tts-volume=2.0

== with matcha TTS + local NLU (llama-cli) ==

..\build\src\Release\voice-assistant.exe `
  --silero-vad-model=silero_vad.onnx `
  --encoder=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/encoder-epoch-13-avg-2-chunk-16-left-64.onnx `
  --decoder=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/decoder-epoch-13-avg-2-chunk-16-left-64.onnx `
  --joiner=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/joiner-epoch-13-avg-2-chunk-16-left-64.onnx `
  --tokens=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/tokens.txt `
  --keywords-file=sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/test_wavs/keywords.txt `
  --asr-encoder=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/encoder.int8.onnx `
  --asr-decoder=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/decoder.onnx `
  --asr-joiner=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/joiner.int8.onnx `
  --asr-tokens=sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30/tokens.txt `
  --tts-acoustic-model=matcha-icefall-zh-baker/model-steps-3.onnx `
  --tts-vocoder=vocos-22khz-univ.onnx `
  --tts-lexicon=matcha-icefall-zh-baker/lexicon.txt `
  --tts-tokens=matcha-icefall-zh-baker/tokens.txt `
  --tts-dict-dir=matcha-icefall-zh-baker/dict `
  --tts-rule-fsts=matcha-icefall-zh-baker/phone.fst,matcha-icefall-zh-baker/date.fst,matcha-icefall-zh-baker/number.fst `
  --nlu-llama-cli=C:\Users\89488\haha\llama.cpp\build\bin\llama-cli.exe `
  --nlu-model=C:\Users\89488\haha\llama.cpp\models\Qwen3.5-2B-Q4_K_M.gguf `
  --nlu-llama-args="--jinja --reasoning off --temp 1.0 --top-p 1.0 --top-k 20 --repeat-penalty 1.0 --presence-penalty 2.0" `
  --nlu-timeout-ms=30000 `
  --nlu-n-predict=128 `
  --nlu-prompt-prefix="Answer briefly in one sentence: " `
  --tts-volume=2.0

powershell -ExecutionPolicy Bypass -File voice_assistant\test-llama-api.ps1 `
  -BaseUrl http://192.168.31.27:8080/ `
  -Text "你好，请做个自我介绍"

== Quick build voice-assistant only (from repo root, cmd) ==

voice_assistant\build.bat

& "C:\Program Files\CMake\bin\cmake.exe" --build build --config Release --target voice-assistant



[Listening] ──keyword──→ [Active] ──say "退出"──→ [Listening]
    │                       │
    KWS only          VAD + ASR (continuous)


Mic → KWS (keyword) → VAD+ASR (speech) → ASR final text → NLU (HTTP or local llama) → TTS (speak response)


& "C:\Program Files\CMake\bin\cmake.exe" -G "Visual Studio 18 2026" -A x64 "-DCMAKE_INSTALL_PREFIX=C:/Users/89488/voice-assistant/build/install" -DSHERPA_ONNX_ENABLE_TTS=ON -DSHERPA_ONNX_USE_STATIC_CRT=OFF -DCMAKE_BUILD_TYPE=Release -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF -DBUILD_SHARED_LIBS=ON -DBUILD_ESPEAK_NG_EXE=OFF ..
& "C:\Program Files\CMake\bin\cmake.exe" --build . --config Release --target voice-assistant --parallel



download asr model

Invoke-WebRequest -Uri "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30.tar.bz2" -OutFile "sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30.tar.bz2"
tar xvf sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30.tar.bz2
rm sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30.tar.bz2


download kws model

Invoke-WebRequest -Uri "https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2" -OutFile "sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2" -UseBasicParsing
tar xf sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2


download tts model (zipvoice)

wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/sherpa-onnx-zipvoice-distill-int8-zh-en-emilia.tar.bz2 -OutFile sherpa-onnx-zipvoice-distill-int8-zh-en-emilia.tar.bz2

download tts model (matcha)

wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/matcha-icefall-zh-baker.tar.bz2 -OutFile matcha-icefall-zh-baker.tar.bz2
tar xf matcha-icefall-zh-baker.tar.bz2

download vocoder (zipvoice)

wget https://github.com/k2-fsa/sherpa-onnx/releases/download/vocoder-models/vocos_24khz.onnx -OutFile vocos_24khz.onnx

download vocoder (matcha)

wget https://github.com/k2-fsa/sherpa-onnx/releases/download/vocoder-models/vocos-22khz-univ.onnx -OutFile vocos-22khz-univ.onnx


matcha tts model demo

..\build\src\Release\tts-test.exe `
  --tts-acoustic-model=matcha-icefall-zh-baker/model-steps-3.onnx `
  --tts-vocoder=vocos-22khz-univ.onnx `
  --tts-lexicon=matcha-icefall-zh-baker/lexicon.txt `
  --tts-tokens=matcha-icefall-zh-baker/tokens.txt `
  --tts-dict-dir=matcha-icefall-zh-baker/dict `
  --tts-rule-fsts=matcha-icefall-zh-baker/phone.fst,matcha-icefall-zh-baker/date.fst,matcha-icefall-zh-baker/number.fst `
  --text="你好世界"


zipvoice tts model, used for voice conversion

..\build\src\Release\tts-test.exe `
  --tts-encoder=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/encoder.int8.onnx `
  --tts-decoder=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/decoder.int8.onnx `
  --tts-vocoder=vocos_24khz.onnx `
  --tts-tokens=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/tokens.txt `
  --tts-lexicon=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/lexicon.txt `
  --tts-data-dir=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/espeak-ng-data `
  --tts-prompt-audio=sherpa-onnx-zipvoice-distill-int8-zh-en-emilia/test_wavs/leijun-1.wav `
  "--tts-prompt-text=那还是36年前, 1987年. 我呢考上了武汉大学的计算机系." `
  --text="你好世界"
