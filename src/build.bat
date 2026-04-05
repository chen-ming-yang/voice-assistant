@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 >NUL 2>&1

set ROOT=c:\Users\89488\sherpa-onnx
set DEPS=%ROOT%\build\_deps
set SRC=%ROOT%\voice_assistant

cl /EHsc /std:c++17 /O2 /utf-8 /MD ^
  /I %ROOT% ^
  /I %SRC% ^
  /I %DEPS%\onnxruntime-src\include ^
  /I %DEPS%\kaldi_decoder-src ^
  /I %DEPS%\kaldi_native_fbank-src ^
  /I %DEPS%\kaldifst-src ^
  /I %DEPS%\openfst-src\src\include ^
  /I %DEPS%\simple-sentencepiece-src ^
  /I %DEPS%\json-src\include ^
  /I %DEPS%\eigen-src ^
  /Fe:%ROOT%\build\bin\Release\voice-assistant.exe ^
  %SRC%\keyword-spotter-mic.cc ^
  %SRC%\mic-capture-win.cc ^
  /link ^
  /LIBPATH:%ROOT%\build\lib\Release ^
  /LIBPATH:%DEPS%\onnxruntime-src\lib ^
  sherpa-onnx-core.lib onnxruntime.lib kaldi-native-fbank-core.lib ^
  kaldi-decoder-core.lib ssentencepiece_core.lib sherpa-onnx-fst.lib ^
  sherpa-onnx-fstfar.lib sherpa-onnx-kaldifst-core.lib kissfft-float.lib ^
  ucd.lib ole32.lib uuid.lib
