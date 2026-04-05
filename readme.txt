It is a voice assistant base on sherpa onnx.
Only support windows now because I only have a windows computer now.
Try to add more features.
Maybe need a good performance computer because contains local nlu.


V1.0
[Listening] ──keyword──→ [Active] ──say "退出"──→ [Listening]
    │                       │
    KWS only          VAD + ASR (continuous)

ASR -> local NLU -> return responce -> tts

support base function contains vad, kws, asr, nlu and tts
all modules use sherpa onnx except nlu .




