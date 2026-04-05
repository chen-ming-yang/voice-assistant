// mic-capture.h
//
// Platform-agnostic microphone capture interface.
// Each platform provides its own implementation (e.g. mic-capture-win.cc).

#ifndef MIC_CAPTURE_H_
#define MIC_CAPTURE_H_

#include <cstdint>
#include <vector>

struct MicCapture;

// Open the default microphone. Returns nullptr on failure.
MicCapture *MicOpen();

// Get the sample rate (Hz) of the opened microphone.
float MicGetSampleRate(MicCapture *mic);

// Read available audio samples (mono, float in [-1, 1]).
// Non-blocking: writes 0 samples if nothing is available yet.
void MicRead(MicCapture *mic, std::vector<float> *samples);

// Stop capture and release all resources.
void MicClose(MicCapture *mic);

#endif  // MIC_CAPTURE_H_
