#define DIRECTSOUND_VERSION 0x0800
#include "dsound_internal.h"

namespace audiodev {

Device::Device() : state_(new DeviceState())
{
}

Device::~Device()
{
    destroy();
    delete state_;
}

bool Device::isUp() const
{
    return state_->directsound != NULL;
}

void Device::destroy()
{
    if (state_->listener) {
        state_->listener->Release();
        state_->listener = NULL;
    }
    if (state_->primary) {
        state_->primary->Release();
        state_->primary = NULL;
    }
    if (state_->directsound) {
        state_->directsound->Release();
        state_->directsound = NULL;
    }
}

/* The primary buffer's 3D interface is the listener. */
static bool query_listener(DeviceState *s)
{
    GUID iid = IID_IDirectSound3DListener;
    HRESULT hr = s->primary->QueryInterface(iid, (void**)&s->listener);
    if (FAILED(hr)) {
        AD_LOG("audiodev: QueryInterface 3DListener failed hr=0x%lx\n",
               (unsigned long)hr);
        return false;
    }
    return true;
}

bool Device::create(const DeviceConfig &config)
{
    AD_LOG("audiodev: create(window=%p 3d=%d ch=%d rate=%d bits=%d)\n",
           config.window, (int)config.enable3D, config.channels,
           config.sampleRate, config.bitsPerSample);

    destroy();

    HRESULT hr = DirectSoundCreate(NULL, &state_->directsound, NULL);
    if (FAILED(hr)) {
        AD_LOG("audiodev: DirectSoundCreate failed hr=0x%lx\n",
               (unsigned long)hr);
        goto fail;
    }

    hr = state_->directsound->SetCooperativeLevel((HWND)config.window,
                                                  DSSCL_PRIORITY);
    if (FAILED(hr)) {
        AD_LOG("audiodev: SetCooperativeLevel failed hr=0x%lx\n",
               (unsigned long)hr);
        goto fail;
    }

    {
        DSBUFFERDESC desc = {};
        desc.dwSize  = sizeof(DSBUFFERDESC);
        desc.dwFlags = DSBCAPS_PRIMARYBUFFER
                     | (config.enable3D ? DSBCAPS_CTRL3D : 0);
        hr = state_->directsound->CreateSoundBuffer(&desc, &state_->primary,
                                                    NULL);
        if (FAILED(hr)) {
            AD_LOG("audiodev: CreateSoundBuffer (primary) failed hr=0x%lx\n",
                   (unsigned long)hr);
            goto fail;
        }
    }

    {
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = (WORD)config.channels;
        wfx.nSamplesPerSec  = (DWORD)config.sampleRate;
        wfx.wBitsPerSample  = (WORD)config.bitsPerSample;
        wfx.nBlockAlign     = (WORD)((config.bitsPerSample >> 3) * config.channels);
        wfx.nAvgBytesPerSec = wfx.nBlockAlign * (DWORD)config.sampleRate;
        hr = state_->primary->SetFormat(&wfx);
        if (FAILED(hr))
            AD_LOG("audiodev: SetFormat failed hr=0x%lx (continuing)\n",
                   (unsigned long)hr);
    }

    // The primary buffer plays for as long as the device lives.
    state_->primary->Play(0, 0, DSBPLAY_LOOPING);

    if (config.enable3D && !query_listener(state_))
        goto fail;
    AD_LOG("audiodev: create OK\n");
    return true;

fail:
    destroy();
    return false;
}

bool Device::set3DEnabled(bool enable)
{
    if (enable) {
        if (state_->listener) return true;
        if (!state_->primary) return false;
        if (!query_listener(state_)) {
            destroy();
            return false;
        }
    } else if (state_->listener) {
        state_->listener->Release();
        state_->listener = NULL;
    }
    return true;
}

static DWORD apply(bool immediate)
{
    return immediate ? DS3D_IMMEDIATE : DS3D_DEFERRED;
}

void Device::commit()
{
    if (state_->listener)
        state_->listener->CommitDeferredSettings();
}

void Device::setListenerPosition(const float pos[3], bool immediate)
{
    if (state_->listener)
        state_->listener->SetPosition(pos[0], pos[1], pos[2], apply(immediate));
}

void Device::setListenerOrientation(const float front[3], const float top[3],
                                    bool immediate)
{
    if (state_->listener)
        state_->listener->SetOrientation(front[0], front[1], front[2],
                                         top[0], top[1], top[2],
                                         apply(immediate));
}

void Device::setListenerRolloff(float rolloff, bool immediate)
{
    if (state_->listener)
        state_->listener->SetRolloffFactor(rolloff, apply(immediate));
}

}  // namespace audiodev
