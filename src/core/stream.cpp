#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <string.h>
#include <stdio.h>
#include "stream.h"
#include "static.h"  // the shared KAROO_SOUND_FX / _DIAG helpers
#include <stdlib.h>
#include "log.h"

#define FOURCC(a,b,c,d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | \
     ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

static const DWORD ID_RIFF = FOURCC('R','I','F','F');
static const DWORD ID_WAVE = FOURCC('W','A','V','E');
static const DWORD ID_FMT  = FOURCC('f','m','t',' ');
static const DWORD ID_DATA = FOURCC('d','a','t','a');

static bool wav_read_dword(HANDLE f, DWORD *out) {
    DWORD n;
    return ReadFile(f, out, 4, &n, NULL) && n == 4;
}

/* Parses a WAV file into a heap-allocated WAVEFORMATEX and a heap-allocated
 * PCM block, both freed by the caller with HeapFree.  Returns true on success.
 */
static bool parse_wav(const char *path,
                      WAVEFORMATEX **fmt_out, BYTE **pcm_out, DWORD *pcm_size_out)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("CStream: parse_wav: can't open '%s' err=%lu\n",
                  path, GetLastError());
        return false;
    }

    WAVEFORMATEX *fmt  = NULL;
    BYTE         *pcm  = NULL;
    DWORD         pcm_sz = 0;

    DWORD id, size, wave_id;
    if (!wav_read_dword(f, &id)   || id   != ID_RIFF) goto fail;
    if (!wav_read_dword(f, &size))                      goto fail;
    if (!wav_read_dword(f, &wave_id) || wave_id != ID_WAVE) goto fail;

    while (!fmt || !pcm) {
        DWORD chunk_id, chunk_size;
        DWORD n;
        if (!wav_read_dword(f, &chunk_id) || !wav_read_dword(f, &chunk_size))
            break;

        if (chunk_id == ID_FMT) {
            DWORD to_read = chunk_size < sizeof(WAVEFORMATEX)
                          ? chunk_size : sizeof(WAVEFORMATEX);
            fmt = (WAVEFORMATEX*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           sizeof(WAVEFORMATEX));
            if (!fmt) goto fail;
            if (!ReadFile(f, fmt, to_read, &n, NULL) || n != to_read) goto fail;
            DWORD skip = chunk_size - to_read;
            if (skip) SetFilePointer(f, (LONG)(skip + (skip & 1)), NULL, FILE_CURRENT);
        } else if (chunk_id == ID_DATA) {
            pcm_sz = chunk_size;
            pcm    = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pcm_sz);
            if (!pcm) goto fail;
            if (!ReadFile(f, pcm, pcm_sz, &n, NULL) || n != pcm_sz) goto fail;
        } else {
            DWORD skip = chunk_size + (chunk_size & 1);
            SetFilePointer(f, (LONG)skip, NULL, FILE_CURRENT);
        }
    }

    CloseHandle(f);
    if (!fmt || !pcm) {
        HeapFree(GetProcessHeap(), 0, fmt);
        HeapFree(GetProcessHeap(), 0, pcm);
        return false;
    }
    *fmt_out      = fmt;
    *pcm_out      = pcm;
    *pcm_size_out = pcm_sz;
    return true;

fail:
    CloseHandle(f);
    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    return false;
}

/* Polls the buffer every 50 ms and sets dwThread_done once it stops playing or
 * stop_event is signalled. */
static DWORD WINAPI WatcherProc(LPVOID param)
{
    CStreamSoundbuffer *s = static_cast<CStreamSoundbuffer*>(param);
    for (;;) {
        DWORD ret = WaitForSingleObject(s->stop_event, 50);
        if (ret == WAIT_OBJECT_0) break;
        if (!s->pSoundbuffer) { s->dwThread_done = 1; break; }
        DWORD status = 0;
        HRESULT hr = s->pSoundbuffer->GetStatus(&status);
        if (FAILED(hr) || !(status & DSBSTATUS_PLAYING)) {
            s->dwThread_done = 1;
            break;
        }
    }
    return 0;
}

/* There is no array form of the destructor: nothing allocates these in blocks,
 * so bit 1 of the flags is not tested. */

void *CStream_ScalarDeletingDtor(CStreamSoundbuffer *self, unsigned int flags);

static void *const g_CStreamVtable[1] = { (void *)&CStream_ScalarDeletingDtor };

void *CStream_Vtable(void)
{
    return (void *)g_CStreamVtable;
}

void *CStream_ScalarDeletingDtor(CStreamSoundbuffer *self, unsigned int flags)
{
    static unsigned long seen;
    CStatic_SoundFirstCall("CStreamSoundbuffer::ScalarDeletingDtor", &seen);

    CStream_DeinitInstance(self);
    if (flags & 1)
        free(self);
    return self;
}


static void CStream_Stop_impl(CStreamSoundbuffer *self);
static void CStream_ReleaseResources_impl(CStreamSoundbuffer *self);

static CStreamSoundbuffer* CStream_Initialize_impl(CStreamSoundbuffer *self)
{
    log_write("CStream::Initialize(this=%p)\n", self);
    memset(self, 0, sizeof(*self));
    self->vtable       = CStream_Vtable();
    self->dwThread_done = 1;
    InitializeCriticalSection(&self->cs);
    return self;
}

static int CStream_Prepare_impl(CStreamSoundbuffer *self, WaveInfo *wi)
{
    log_write("CStream::Prepare(this=%p, file='%s')\n",
              self, wi ? (wi->pFilename ? wi->pFilename : "<null>") : "<null wi>");

    CStream_ReleaseResources_impl(self);

    if (!wi || !wi->pFilename || !wi->pDirectsound) {
        log_write("CStream::Prepare: bad WaveInfo\n");
        return 0;
    }

    self->pDirectsound = wi->pDirectsound;

    DWORD len = (DWORD)strlen(wi->pFilename) + 1;
    self->filename = (char*)HeapAlloc(GetProcessHeap(), 0, len);
    if (!self->filename) return 0;
    memcpy(self->filename, wi->pFilename, len);

    WAVEFORMATEX *fmt  = NULL;
    BYTE         *pcm  = NULL;
    DWORD         pcm_sz = 0;

    if (!parse_wav(self->filename, &fmt, &pcm, &pcm_sz)) {
        log_write("CStream::Prepare: parse_wav failed for '%s'\n", self->filename);
        goto fail;
    }

    self->dwBuffer_size = pcm_sz;

    {
        DSBUFFERDESC desc = {};
        desc.dwSize        = sizeof(DSBUFFERDESC);
        desc.dwFlags       = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
        desc.dwBufferBytes = pcm_sz;
        desc.lpwfxFormat   = fmt;

        HRESULT hr = self->pDirectsound->CreateSoundBuffer(
                         &desc, &self->pSoundbuffer, NULL);
        if (FAILED(hr)) {
            log_write("CStream::Prepare: CreateSoundBuffer failed hr=0x%lx\n",
                      (unsigned long)hr);
            goto fail;
        }
    }

    {
        void  *ptr1 = NULL, *ptr2 = NULL;
        DWORD  bytes1 = 0,   bytes2 = 0;
        HRESULT hr = self->pSoundbuffer->Lock(
                         0, pcm_sz, &ptr1, &bytes1, &ptr2, &bytes2, 0);
        if (FAILED(hr)) {
            log_write("CStream::Prepare: Lock failed hr=0x%lx\n",
                      (unsigned long)hr);
            goto fail;
        }
        memcpy(ptr1, pcm, bytes1);
        if (ptr2 && bytes2)
            memcpy(ptr2, (BYTE*)pcm + bytes1, bytes2);
        self->pSoundbuffer->Unlock(ptr1, bytes1, ptr2, bytes2);
    }

    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    log_write("CStream::Prepare: OK (%lu bytes)\n", (unsigned long)pcm_sz);
    return 1;

fail:
    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    CStream_ReleaseResources_impl(self);
    return 0;
}

static void CStream_Play_impl(CStreamSoundbuffer *self)
{
    log_write("CStream::Play(this=%p)\n", self);
    if (!self->pSoundbuffer) return;

    if (self->watcher_thread) CStream_Stop_impl(self);

    if (!self->stop_event)
        self->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    else
        ResetEvent(self->stop_event);

    self->pSoundbuffer->SetCurrentPosition(0);
    self->dwThread_done = 0;
    self->pSoundbuffer->Play(0, 0, 0);

    DWORD tid;
    self->watcher_thread = CreateThread(NULL, 0, WatcherProc, self, 0, &tid);
    log_write("CStream::Play: watcher tid=%lu\n", (unsigned long)tid);
}

static void CStream_Stop_impl(CStreamSoundbuffer *self)
{
    log_write("CStream::Stop(this=%p)\n", self);
    if (self->watcher_thread) {
        if (self->pSoundbuffer) self->pSoundbuffer->Stop();
        if (self->stop_event)   SetEvent(self->stop_event);
        WaitForSingleObject(self->watcher_thread, 5000);
        CloseHandle(self->watcher_thread);
        self->watcher_thread = NULL;
    }
    self->dwThread_done = 1;
}

static void CStream_ReleaseResources_impl(CStreamSoundbuffer *self)
{
    log_write("CStream::ReleaseResources(this=%p)\n", self);
    CStream_Stop_impl(self);

    if (self->pSoundbuffer) {
        self->pSoundbuffer->Release();
        self->pSoundbuffer = NULL;
    }
    if (self->stop_event) {
        CloseHandle(self->stop_event);
        self->stop_event = NULL;
    }
    if (self->filename) {
        HeapFree(GetProcessHeap(), 0, self->filename);
        self->filename = NULL;
    }
    self->pDirectsound  = NULL;
    self->dwBuffer_size = 0;
    self->dwThread_done = 1;
}

static void CStream_DeinitInstance_impl(CStreamSoundbuffer *self)
{
    log_write("CStream::DeinitInstance(this=%p)\n", self);
    self->vtable = CStream_Vtable();
    CStream_ReleaseResources_impl(self);
    DeleteCriticalSection(&self->cs);
}


CStreamSoundbuffer*CStream_Initialize(CStreamSoundbuffer *self)
    { return CStream_Initialize_impl(self); }

int CStream_Prepare(CStreamSoundbuffer *self, WaveInfo *wi)
    { return CStream_Prepare_impl(self, wi); }

void CStream_Play(CStreamSoundbuffer *self)
    { CStream_Play_impl(self); }

void CStream_Stop(CStreamSoundbuffer *self)
    { CStream_Stop_impl(self); }

void CStream_ReleaseResources(CStreamSoundbuffer *self)
    { CStream_ReleaseResources_impl(self); }

void CStream_DeinitInstance(CStreamSoundbuffer *self)
    { CStream_DeinitInstance_impl(self); }

