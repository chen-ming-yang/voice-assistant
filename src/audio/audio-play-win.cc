// audio-play-win.cc
//
// Windows WASAPI implementation of the AudioPlayer interface.

#define WIN32_LEAN_AND_MEAN
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "audio/audio-play.h"

struct AudioPlayer {
  IAudioClient *audio_client = nullptr;
  IAudioRenderClient *render_client = nullptr;
  WAVEFORMATEX *fmt = nullptr;
  HANDLE event = nullptr;
  UINT32 buffer_frames = 0;
  int32_t sample_rate = 0;
  bool com_initialized = false;
};

AudioPlayer *AudioPlayerOpen(int32_t sample_rate) {
  auto *p = new AudioPlayer;
  p->sample_rate = sample_rate;

  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  p->com_initialized = true;

  IMMDeviceEnumerator *enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                              CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void **>(&enumerator)))) {
    fprintf(stderr, "AudioPlayer: CoCreateInstance failed\n");
    delete p;
    return nullptr;
  }

  IMMDevice *device = nullptr;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                  &device))) {
    fprintf(stderr, "AudioPlayer: GetDefaultAudioEndpoint failed\n");
    enumerator->Release();
    delete p;
    return nullptr;
  }
  enumerator->Release();

  if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void **>(&p->audio_client)))) {
    fprintf(stderr, "AudioPlayer: Activate failed\n");
    device->Release();
    delete p;
    return nullptr;
  }
  device->Release();

  // Use a simple mono float format at the requested sample rate
  WAVEFORMATEX wfx = {};
  wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  wfx.nChannels = 1;
  wfx.nSamplesPerSec = sample_rate;
  wfx.wBitsPerSample = 32;
  wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
  wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

  // Check if format is supported; if not, use closest match
  WAVEFORMATEX *closest = nullptr;
  HRESULT hr = p->audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                   &wfx, &closest);
  const WAVEFORMATEX *use_fmt = &wfx;
  if (hr == S_FALSE && closest) {
    use_fmt = closest;
  } else if (FAILED(hr)) {
    // Fall back to mix format
    p->audio_client->GetMixFormat(&p->fmt);
    use_fmt = p->fmt;
  }

  // 100ms buffer
  REFERENCE_TIME dur = 1000000;  // 100ms in 100ns units
  hr = p->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK, dur, 0,
                                   use_fmt, nullptr);
  if (FAILED(hr)) {
    fprintf(stderr, "AudioPlayer: Initialize failed (0x%lx)\n", hr);
    if (closest) CoTaskMemFree(closest);
    p->audio_client->Release();
    delete p;
    return nullptr;
  }

  // Save the actual format
  if (!p->fmt) {
    p->fmt = static_cast<WAVEFORMATEX *>(
        CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    *p->fmt = *use_fmt;
  }
  if (closest) CoTaskMemFree(closest);

  p->audio_client->GetBufferSize(&p->buffer_frames);

  p->event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  p->audio_client->SetEventHandle(p->event);

  if (FAILED(p->audio_client->GetService(
          __uuidof(IAudioRenderClient),
          reinterpret_cast<void **>(&p->render_client)))) {
    fprintf(stderr, "AudioPlayer: GetService(RenderClient) failed\n");
    p->audio_client->Release();
    CloseHandle(p->event);
    delete p;
    return nullptr;
  }

  fprintf(stderr, "AudioPlayer: opened (rate=%d, channels=%d, bits=%d)\n",
          p->fmt->nSamplesPerSec, p->fmt->nChannels, p->fmt->wBitsPerSample);
  return p;
}

void AudioPlayerPlay(AudioPlayer *player, const float *samples, int32_t n) {
  if (!player || n <= 0) return;

  // Resample if device rate differs from the requested rate.
  const float *src = samples;
  int32_t src_n = n;
  std::vector<float> resampled;

  int32_t device_rate =
      static_cast<int32_t>(player->fmt->nSamplesPerSec);
  if (device_rate != player->sample_rate && player->sample_rate > 0) {
    double ratio =
        static_cast<double>(device_rate) / player->sample_rate;
    int32_t out_n = static_cast<int32_t>(n * ratio + 0.5);
    resampled.resize(out_n);
    for (int32_t i = 0; i < out_n; ++i) {
      double pos = i / ratio;
      int32_t idx = static_cast<int32_t>(pos);
      double frac = pos - idx;
      if (idx + 1 < n) {
        resampled[i] = static_cast<float>(
            samples[idx] * (1.0 - frac) + samples[idx + 1] * frac);
      } else {
        resampled[i] = samples[n - 1];
      }
    }
    src = resampled.data();
    src_n = out_n;
    fprintf(stderr, "[AudioPlayer] Resampled %d -> %d samples (%d -> %d Hz)\n",
            n, out_n, player->sample_rate, device_rate);
  }

  IAudioClient *ac = player->audio_client;
  IAudioRenderClient *rc = player->render_client;
  UINT32 buf_frames = player->buffer_frames;
  UINT32 channels = player->fmt->nChannels;
  bool is_float = (player->fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                  (player->fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                   reinterpret_cast<WAVEFORMATEXTENSIBLE *>(player->fmt)
                           ->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

  HRESULT hr_start = ac->Start();
  if (FAILED(hr_start)) {
    fprintf(stderr, "[AudioPlayer] Start failed (0x%lx)\n", hr_start);
    return;
  }

  int32_t offset = 0;
  int32_t frames_written = 0;
  while (offset < src_n) {
    UINT32 padding = 0;
    ac->GetCurrentPadding(&padding);
    UINT32 avail = buf_frames - padding;
    if (avail == 0) {
      WaitForSingleObject(player->event, 100);
      continue;
    }

    UINT32 to_write = static_cast<UINT32>(src_n - offset);
    if (to_write > avail) to_write = avail;

    BYTE *buf = nullptr;
    HRESULT hr_buf = rc->GetBuffer(to_write, &buf);
    if (FAILED(hr_buf)) {
      fprintf(stderr, "[AudioPlayer] GetBuffer failed (0x%lx)\n", hr_buf);
      break;
    }

    if (is_float && channels == 1) {
      // Direct copy
      memcpy(buf, src + offset, to_write * sizeof(float));
    } else if (is_float) {
      // Duplicate mono to all channels
      float *dst = reinterpret_cast<float *>(buf);
      for (UINT32 i = 0; i < to_write; ++i) {
        for (UINT32 ch = 0; ch < channels; ++ch) {
          dst[i * channels + ch] = src[offset + i];
        }
      }
    } else {
      // 16-bit PCM output
      int16_t *dst = reinterpret_cast<int16_t *>(buf);
      for (UINT32 i = 0; i < to_write; ++i) {
        float s = src[offset + i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t val = static_cast<int16_t>(s * 32767.0f);
        for (UINT32 ch = 0; ch < channels; ++ch) {
          dst[i * channels + ch] = val;
        }
      }
    }

    rc->ReleaseBuffer(to_write, 0);
    offset += to_write;
    frames_written += to_write;
  }

  fprintf(stderr, "[AudioPlayer] Wrote %d frames total\n", frames_written);

  // Drain remaining audio
  UINT32 padding = 1;
  while (padding > 0) {
    WaitForSingleObject(player->event, 200);
    ac->GetCurrentPadding(&padding);
  }
  // Extra wait to let hardware finish rendering the last buffer
  WaitForSingleObject(player->event, 200);

  ac->Stop();
  ac->Reset();
}

void AudioPlayerClose(AudioPlayer *player) {
  if (!player) return;
  if (player->render_client) player->render_client->Release();
  if (player->audio_client) player->audio_client->Release();
  if (player->event) CloseHandle(player->event);
  if (player->fmt) CoTaskMemFree(player->fmt);
  delete player;
}
