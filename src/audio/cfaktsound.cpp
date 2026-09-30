#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <string.h>
#include "cfaktsound.h"
#include "log.h"


CFaktSound::CFaktSound()
    : logger_initialized_(0), logger_(NULL), directsound_(NULL),
      soundbuffer_(NULL), directsound3dlistener_(NULL)
{
    memset(&caps_check_, 0, sizeof(caps_check_));
}

CFaktSound::~CFaktSound()
{
    releaseComRefs();
}

void CFaktSound::releaseComRefs()
{
    if (directsound3dlistener_) {
        directsound3dlistener_->Release();
        directsound3dlistener_ = NULL;
    }
    if (soundbuffer_) {
        soundbuffer_->Release();
        soundbuffer_ = NULL;
    }
    if (directsound_) {
        directsound_->Release();
        directsound_ = NULL;
    }
    // The logger is never owned, so both fields are simply cleared.
    logger_initialized_ = 0;
    logger_ = NULL;
}

int CFaktSound::initialize(HWND window,
                                      UINT bufferflags, short channels,
                                      int samplespersec, USHORT bitspersample,
                                      void *logger)
{
    log_write("CFaktSound::Initialize(window=%p flags=0x%lx ch=%d rate=%d bits=%d)\n",
              (void*)window, (unsigned long)bufferflags,
              (int)channels, samplespersec, (int)bitspersample);

    releaseComRefs();

    logger_ = logger;

    HMODULE hDSound = GetModuleHandleA("dsound.dll");
    log_write("CFaktSound::Initialize: dsound.dll loaded at %p\n", (void*)hDSound);

    HRESULT hr = DirectSoundCreate(NULL, &directsound_, NULL);
    if (FAILED(hr)) {
        log_write("CFaktSound::Initialize: DirectSoundCreate failed hr=0x%lx\n",
                  (unsigned long)hr);
        goto fail;
    }

    hr = directsound_->SetCooperativeLevel(window, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        log_write("CFaktSound::Initialize: SetCooperativeLevel failed hr=0x%lx\n",
                  (unsigned long)hr);
        goto fail;
    }

    {
        DSBUFFERDESC desc = {};
        desc.dwSize        = sizeof(DSBUFFERDESC);
        desc.dwFlags       = bufferflags | DSBCAPS_PRIMARYBUFFER;
        desc.dwBufferBytes = 0;
        desc.lpwfxFormat   = NULL;

        hr = directsound_->CreateSoundBuffer(&desc, &soundbuffer_, NULL);
        if (FAILED(hr)) {
            log_write("CFaktSound::Initialize: CreateSoundBuffer (primary) failed hr=0x%lx\n",
                      (unsigned long)hr);
            goto fail;
        }
    }

    {
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = channels;
        wfx.nSamplesPerSec  = (DWORD)samplespersec;
        wfx.wBitsPerSample  = bitspersample;
        wfx.nBlockAlign     = (WORD)((bitspersample >> 3) * channels);
        wfx.nAvgBytesPerSec = wfx.nBlockAlign * (DWORD)samplespersec;
        wfx.cbSize          = 0;
        hr = soundbuffer_->SetFormat(&wfx);
        if (FAILED(hr))
            log_write("CFaktSound::Initialize: SetFormat failed hr=0x%lx (continuing)\n",
                      (unsigned long)hr);
    }

    {
        caps_check_.dwSize = sizeof(DSCAPS);
        hr = directsound_->GetCaps(&caps_check_);
        if (FAILED(hr))
            log_write("CFaktSound::Initialize: GetCaps failed hr=0x%lx (continuing)\n",
                      (unsigned long)hr);
    }

    // The primary buffer plays for as long as the device lives.
    soundbuffer_->Play(0, 0, DSBPLAY_LOOPING);
    log_write("CFaktSound::Initialize: OK\n");
    return 1;

fail:
    releaseComRefs();
    return 0;
}

int CFaktSound::initializeWith3DAudio(HWND window,
                                                 UINT bufferflags, short channels,
                                                 int samplespersec, USHORT bitspersample,
                                                 void *logger)
{
    log_write("CFaktSound::InitializeWith3DAudio\n");

    int ok = initialize(window,
                                        bufferflags | DSBCAPS_CTRL3D,
                                        channels, samplespersec, bitspersample, logger);
    if (!ok) return 0;

    GUID iid = IID_IDirectSound3DListener;
    HRESULT hr = soundbuffer_->QueryInterface(iid,
                     (void**)&directsound3dlistener_);
    if (FAILED(hr)) {
        log_write("CFaktSound::InitializeWith3DAudio: QueryInterface 3DListener failed hr=0x%lx\n",
                  (unsigned long)hr);
        releaseComRefs();
        return 0;
    }

    log_write("CFaktSound::InitializeWith3DAudio: OK\n");
    return 1;
}

int CFaktSound::create3DListener(int enable)
{
    if (enable) {
        if (directsound3dlistener_) return 1;
        if (!soundbuffer_) return 0;
        GUID iid = IID_IDirectSound3DListener;
        HRESULT hr = soundbuffer_->QueryInterface(iid,
                         (void**)&directsound3dlistener_);
        if (FAILED(hr)) {
            log_write("CFaktSound::Create3DListener: QueryInterface failed hr=0x%lx\n",
                      (unsigned long)hr);
            releaseComRefs();
            return 0;
        }
    } else {
        if (directsound3dlistener_) {
            directsound3dlistener_->Release();
            directsound3dlistener_ = NULL;
        }
    }
    return 1;
}

void CFaktSound::commitSettings()
{
    if (directsound3dlistener_)
        directsound3dlistener_->CommitDeferredSettings();
}

void CFaktSound::setPosition(vec3d *pos, DWORD dwApply)
{
    if (directsound3dlistener_)
        directsound3dlistener_->SetPosition(
            pos->x, pos->y, pos->z, dwApply);
}

void CFaktSound::setOrientation(vec3d *front, vec3d *top, DWORD dwApply)
{
    if (directsound3dlistener_)
        directsound3dlistener_->SetOrientation(
            front->x, front->y, front->z,
            top->x,   top->y,   top->z,
            dwApply);
}

void CFaktSound::apply3DRolloffParams(float rolloff_factor, DWORD dwApply)
{
    if (directsound3dlistener_)
        directsound3dlistener_->SetRolloffFactor(rolloff_factor, dwApply);
}
