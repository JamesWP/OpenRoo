#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <string.h>
#include "cfaktsound.h"
#include "log.h"

/* ── Forward declarations ───────────────────────────────────────────────── */

static void CFaktSound_ReleaseComRefs_impl(CFaktSound *self);
static int  CFaktSound_Initialize_impl(CFaktSound *self, HWND window,
                                       UINT bufferflags, short channels,
                                       int samplespersec, USHORT bitspersample,
                                       void *logger);

/* ── Method implementations ─────────────────────────────────────────────── */

static void CFaktSound_BlankFields_impl(CFaktSound *self)
{
    memset(self, 0, sizeof(*self));
    self->vtable = const_cast<void*>(CFAKTSOUND_VTABLE);
}

static CFaktSound *CFaktSound_ScalarDeletingDtor_impl(CFaktSound *self, DWORD free_memory)
{
    /* CFaktSound objects are always embedded — free_memory is always 0 in practice. */
    CFaktSound_ReleaseComRefs_impl(self);
    self->vtable = const_cast<void*>(CFAKTSOUND_VTABLE);
    return self;
}

static void CFaktSound_ClearState_impl(CFaktSound *self)
{
    self->vtable = const_cast<void*>(CFAKTSOUND_VTABLE);
    CFaktSound_ReleaseComRefs_impl(self);
}

static void CFaktSound_ReleaseComRefs_impl(CFaktSound *self)
{
    if (self->directsound3dlistener) {
        self->directsound3dlistener->Release();
        self->directsound3dlistener = NULL;
    }
    if (self->soundbuffer) {
        self->soundbuffer->Release();
        self->soundbuffer = NULL;
    }
    if (self->directsound) {
        self->directsound->Release();
        self->directsound = NULL;
    }
    /* We never set logger_initialized=1, so we never own the logger.
     * Just clear both fields unconditionally. */
    self->logger_initialized = 0;
    self->logger = NULL;
}

static int CFaktSound_Initialize_impl(CFaktSound *self, HWND window,
                                      UINT bufferflags, short channels,
                                      int samplespersec, USHORT bitspersample,
                                      void *logger)
{
    log_write("CFaktSound::Initialize(window=%p flags=0x%lx ch=%d rate=%d bits=%d)\n",
              (void*)window, (unsigned long)bufferflags,
              (int)channels, samplespersec, (int)bitspersample);

    CFaktSound_ReleaseComRefs_impl(self);

    /* Store caller-provided logger (not owned; logger_initialized stays 0). */
    self->logger = logger;

    /* Create the DirectSound device. */
    HMODULE hDSound = GetModuleHandleA("dsound.dll");
    log_write("CFaktSound::Initialize: dsound.dll loaded at %p\n", (void*)hDSound);

    HRESULT hr = DirectSoundCreate(NULL, &self->directsound, NULL);
    if (FAILED(hr)) {
        log_write("CFaktSound::Initialize: DirectSoundCreate failed hr=0x%lx\n",
                  (unsigned long)hr);
        goto fail;
    }

    hr = self->directsound->SetCooperativeLevel(window, DSSCL_PRIORITY);
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

        hr = self->directsound->CreateSoundBuffer(&desc, &self->soundbuffer, NULL);
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
        hr = self->soundbuffer->SetFormat(&wfx);
        if (FAILED(hr))
            log_write("CFaktSound::Initialize: SetFormat failed hr=0x%lx (continuing)\n",
                      (unsigned long)hr);
    }

    {
        self->caps_check.dwSize = sizeof(DSCAPS);
        hr = self->directsound->GetCaps(&self->caps_check);
        if (FAILED(hr))
            log_write("CFaktSound::Initialize: GetCaps failed hr=0x%lx (continuing)\n",
                      (unsigned long)hr);
    }

    self->soundbuffer->Play(0, 0, DSBPLAY_LOOPING);
    log_write("CFaktSound::Initialize: OK\n");
    return 1;

fail:
    CFaktSound_ReleaseComRefs_impl(self);
    return 0;
}

static int CFaktSound_InitializeWith3DAudio_impl(CFaktSound *self, HWND window,
                                                 UINT bufferflags, short channels,
                                                 int samplespersec, USHORT bitspersample,
                                                 void *logger)
{
    log_write("CFaktSound::InitializeWith3DAudio\n");

    int ok = CFaktSound_Initialize_impl(self, window,
                                        bufferflags | DSBCAPS_CTRL3D,
                                        channels, samplespersec, bitspersample, logger);
    if (!ok) return 0;

    GUID iid = IID_IDirectSound3DListener;
    HRESULT hr = self->soundbuffer->QueryInterface(iid,
                     (void**)&self->directsound3dlistener);
    if (FAILED(hr)) {
        log_write("CFaktSound::InitializeWith3DAudio: QueryInterface 3DListener failed hr=0x%lx\n",
                  (unsigned long)hr);
        CFaktSound_ReleaseComRefs_impl(self);
        return 0;
    }

    log_write("CFaktSound::InitializeWith3DAudio: OK\n");
    return 1;
}

static int CFaktSound_Create3DListener_impl(CFaktSound *self, int enable)
{
    if (enable) {
        if (self->directsound3dlistener) return 1; /* already enabled */
        if (!self->soundbuffer) return 0;
        GUID iid = IID_IDirectSound3DListener;
        HRESULT hr = self->soundbuffer->QueryInterface(iid,
                         (void**)&self->directsound3dlistener);
        if (FAILED(hr)) {
            log_write("CFaktSound::Create3DListener: QueryInterface failed hr=0x%lx\n",
                      (unsigned long)hr);
            CFaktSound_ReleaseComRefs_impl(self);
            return 0;
        }
    } else {
        if (self->directsound3dlistener) {
            self->directsound3dlistener->Release();
            self->directsound3dlistener = NULL;
        }
    }
    return 1;
}

static void CFaktSound_CommitSettings_impl(CFaktSound *self)
{
    //log_write("CFaktSound::CommitSettings(this=%p listener=%p)\n",
    //          self, self->directsound3dlistener);
    if (self->directsound3dlistener)
        self->directsound3dlistener->CommitDeferredSettings();
}

static void CFaktSound_SetPosition_impl(CFaktSound *self, vec3d *pos, DWORD dwApply)
{
    //log_write("CFaktSound::SetPosition(this=%p pos=(%.3f,%.3f,%.3f) apply=%lu)\n",
    //          self, pos->x, pos->y, pos->z, (unsigned long)dwApply);
    if (self->directsound3dlistener)
        self->directsound3dlistener->SetPosition(
            pos->x, pos->y, pos->z, dwApply);
}

static void CFaktSound_SetOrientation_impl(CFaktSound *self,
                                           vec3d *front, vec3d *top, DWORD dwApply)
{
    //log_write("CFaktSound::SetOrientation(this=%p front=(%.3f,%.3f,%.3f) top=(%.3f,%.3f,%.3f) apply=%lu)\n",
    //          self, front->x, front->y, front->z, top->x, top->y, top->z,
    //          (unsigned long)dwApply);
    if (self->directsound3dlistener)
        self->directsound3dlistener->SetOrientation(
            front->x, front->y, front->z,
            top->x,   top->y,   top->z,
            dwApply);
}

static void CFaktSound_Apply3DRolloffParams_impl(CFaktSound *self,
                                                 float rolloff_factor, DWORD dwApply)
{
    //log_write("CFaktSound::Apply3DRolloffParams(this=%p rolloff=%.3f apply=%lu)\n",
    //          self, rolloff_factor, (unsigned long)dwApply);
    if (self->directsound3dlistener)
        self->directsound3dlistener->SetRolloffFactor(rolloff_factor, dwApply);
}

/* ── Exports — extern "C" thiscall wrappers ─────────────────────────────── */
extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
CFaktSound_BlankFields(CFaktSound *self)
    { CFaktSound_BlankFields_impl(self); }

__declspec(dllexport) CFaktSound * __attribute__((thiscall))
CFaktSound_ScalarDeletingDtor(CFaktSound *self, DWORD free_memory)
    { return CFaktSound_ScalarDeletingDtor_impl(self, free_memory); }

__declspec(dllexport) void __attribute__((thiscall))
CFaktSound_ClearState(CFaktSound *self)
    { CFaktSound_ClearState_impl(self); }

__declspec(dllexport) void __attribute__((thiscall))
CFaktSound_ReleaseComRefs(CFaktSound *self)
    { CFaktSound_ReleaseComRefs_impl(self); }

__declspec(dllexport) int __attribute__((thiscall))
CFaktSound_Initialize(CFaktSound *self, HWND window,
                      UINT bufferflags, short channels,
                      int samplespersec, USHORT bitspersample,
                      void *logger)
    { return CFaktSound_Initialize_impl(self, window, bufferflags,
                                        channels, samplespersec, bitspersample, logger); }

__declspec(dllexport) int __attribute__((thiscall))
CFaktSound_InitializeWith3DAudio(CFaktSound *self, HWND window,
                                 UINT bufferflags, short channels,
                                 int samplespersec, USHORT bitspersample,
                                 void *logger)
    { return CFaktSound_InitializeWith3DAudio_impl(self, window, bufferflags,
                                                   channels, samplespersec, bitspersample, logger); }

__declspec(dllexport) int __attribute__((thiscall))
CFaktSound_Create3DListener(CFaktSound *self, int enable)
    { return CFaktSound_Create3DListener_impl(self, enable); }

__declspec(dllexport) void __attribute__((thiscall))
CFaktSound_CommitSettings(CFaktSound *self)
    { CFaktSound_CommitSettings_impl(self); }

__declspec(dllexport) void __attribute__((thiscall))
CFaktSound_SetPosition(CFaktSound *self, vec3d *pos, DWORD dwApply)
    { CFaktSound_SetPosition_impl(self, pos, dwApply); }

__declspec(dllexport) void __attribute__((thiscall))
CFaktSound_SetOrientation(CFaktSound *self, vec3d *front, vec3d *top, DWORD dwApply)
    { CFaktSound_SetOrientation_impl(self, front, top, dwApply); }

__declspec(dllexport) void __attribute__((thiscall))
CFaktSound_Apply3DRolloffParams(CFaktSound *self, float rolloff_factor, DWORD dwApply)
    { CFaktSound_Apply3DRolloffParams_impl(self, rolloff_factor, dwApply); }

} // extern "C"
