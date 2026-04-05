// mic-capture-win.cc
//
// Windows WASAPI implementation of the MicCapture interface.

#define WIN32_LEAN_AND_MEAN
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "audio/mic-capture.h"

struct MicCapture {
  IAudioClient *audio_client = nullptr;
  IAudioCaptureClient *capture_client = nullptr;
  WAVEFORMATEX *fmt = nullptr;
  float sample_rate = 0;
  bool com_initialized = false;
};

// Convert a block of samples to mono float [-1, 1].
static void ConvertToFloat(const BYTE *src, UINT32 num_frames,
                           const WAVEFORMATEX *fmt,
                           std::vector<float> *out) {
  if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
      (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
       reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(fmt)->SubFormat ==
           KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
    const float *f = reinterpret_cast<const float *>(src);
    for (UINT32 i = 0; i < num_frames; ++i) {
      float s = 0;
      for (WORD ch = 0; ch < fmt->nChannels; ++ch) {
        s += f[i * fmt->nChannels + ch];
      }
      out->push_back(s / fmt->nChannels);
    }
  } else {
    // Assume 16-bit PCM
    const int16_t *p = reinterpret_cast<const int16_t *>(src);
    for (UINT32 i = 0; i < num_frames; ++i) {
      float s = 0;
      for (WORD ch = 0; ch < fmt->nChannels; ++ch) {
        s += p[i * fmt->nChannels + ch] / 32768.0f;
      }
      out->push_back(s / fmt->nChannels);
    }
  }
}

MicCapture *MicOpen() {
  MicCapture *mic = new MicCapture;

  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  mic->com_initialized = true;

  IMMDeviceEnumerator *enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                               CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                               reinterpret_cast<void **>(&enumerator)))) {
    fprintf(stderr, "Failed to create IMMDeviceEnumerator\n");
    delete mic;
    return nullptr;
  }

  IMMDevice *device = nullptr;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole,
                                                  &device))) {
    fprintf(stderr, "Failed to get default microphone\n");
    enumerator->Release();
    delete mic;
    return nullptr;
  }
  enumerator->Release();

  if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void **>(&mic->audio_client)))) {
    fprintf(stderr, "Failed to activate IAudioClient\n");
    device->Release();
    delete mic;
    return nullptr;
  }
  device->Release();

  mic->audio_client->GetMixFormat(&mic->fmt);
  mic->sample_rate = static_cast<float>(mic->fmt->nSamplesPerSec);

  fprintf(stderr, "Mic: %u Hz, %u ch, %u bits\n",
          mic->fmt->nSamplesPerSec, mic->fmt->nChannels,
          mic->fmt->wBitsPerSample);

  REFERENCE_TIME buf_duration = 200000;  // 100ns units -> 20ms
  if (FAILED(mic->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                            buf_duration, 0, mic->fmt,
                                            nullptr))) {
    fprintf(stderr, "Failed to initialize IAudioClient\n");
    CoTaskMemFree(mic->fmt);
    mic->audio_client->Release();
    delete mic;
    return nullptr;
  }

  if (FAILED(mic->audio_client->GetService(
          __uuidof(IAudioCaptureClient),
          reinterpret_cast<void **>(&mic->capture_client)))) {
    fprintf(stderr, "Failed to get IAudioCaptureClient\n");
    CoTaskMemFree(mic->fmt);
    mic->audio_client->Release();
    delete mic;
    return nullptr;
  }

  mic->audio_client->Start();
  return mic;
}

float MicGetSampleRate(MicCapture *mic) {
  return mic->sample_rate;
}

void MicRead(MicCapture *mic, std::vector<float> *samples) {
  samples->clear();

  UINT32 packet_length = 0;
  mic->capture_client->GetNextPacketSize(&packet_length);

  while (packet_length > 0) {
    BYTE *data = nullptr;
    UINT32 num_frames = 0;
    DWORD flags = 0;
    mic->capture_client->GetBuffer(&data, &num_frames, &flags, nullptr,
                                   nullptr);

    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
      ConvertToFloat(data, num_frames, mic->fmt, samples);
    }

    mic->capture_client->ReleaseBuffer(num_frames);
    mic->capture_client->GetNextPacketSize(&packet_length);
  }
}

void MicClose(MicCapture *mic) {
  if (!mic) return;

  if (mic->audio_client) mic->audio_client->Stop();
  if (mic->fmt) CoTaskMemFree(mic->fmt);
  if (mic->capture_client) mic->capture_client->Release();
  if (mic->audio_client) mic->audio_client->Release();

  if (mic->com_initialized) CoUninitialize();

  delete mic;
}
