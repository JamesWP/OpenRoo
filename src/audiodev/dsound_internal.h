/* The DirectSound half of the layer, private to src/audiodev. */
#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <string>
#include <vector>
#include "audiodev.h"

namespace audiodev {

struct DeviceState {
    IDirectSound           *directsound;
    IDirectSoundBuffer     *primary;   // kept playing for the device's life
    IDirectSound3DListener *listener;  // NULL when 3D sound is off
};

struct BufferState {
    std::string           filename;    // owned copy of the path
    IDirectSoundBuffer   *soundbuffer;
    IDirectSound3DBuffer *threeD;      // NULL for a 2D buffer
};

extern LogFn g_log;
#define AD_LOG(...) do { if (::audiodev::g_log) ::audiodev::g_log(__VA_ARGS__); } while (0)

/* A parsed .wav file. */
struct Wav {
    WAVEFORMATEX      format = {};
    std::vector<BYTE> pcm;
};
bool loadWav(const char *path, Wav *out);

/* Creates a buffer for wav, with flags, and fills it.  NULL on failure. */
IDirectSoundBuffer *createWavBuffer(IDirectSound *ds, DWORD flags,
                                    const Wav &wav);

/* Copies wav's sample data into an existing buffer. */
bool fillWavBuffer(IDirectSoundBuffer *buf, const Wav &wav);


}  // namespace audiodev
