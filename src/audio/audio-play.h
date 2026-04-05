// audio-play.h
//
// Platform-agnostic audio playback interface.
// Each platform provides its own implementation (e.g. audio-play-win.cc).

#ifndef AUDIO_PLAY_H_
#define AUDIO_PLAY_H_

#include <cstdint>

struct AudioPlayer;

// Open the default audio output device at the given sample rate.
// Returns nullptr on failure.
AudioPlayer *AudioPlayerOpen(int32_t sample_rate);

// Play mono float audio samples (blocking until playback completes).
// samples: PCM float in [-1, 1], n: number of samples.
void AudioPlayerPlay(AudioPlayer *player, const float *samples, int32_t n);

// Close the audio player and release all resources.
void AudioPlayerClose(AudioPlayer *player);

#endif  // AUDIO_PLAY_H_
