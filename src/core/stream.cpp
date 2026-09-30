#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <string.h>
#include <stdio.h>
#include "stream.h"
#include "static.h"  // the shared KAROO_SOUND_FX / _DIAG helpers
#include <stdlib.h>
#include "log.h"

/* The class is packed (it was #pragma pack(1) before it had private members),
 * so taking a member's address for a Win32 or COM out-parameter draws the
 * packed-member warning.  The addresses are the same ones the pragma gave. */
 

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
DWORD WINAPI CStreamSoundbuffer::watcherProc(LPVOID param)
{
    CStreamSoundbuffer *s = static_cast<CStreamSoundbuffer*>(param);
    for (;;) {
        DWORD ret = WaitForSingleObject(s->stop_event_, 50);
        if (ret == WAIT_OBJECT_0) break;
        if (!s->pSoundbuffer_) { s->dwThread_done_ = 1; break; }
        DWORD status = 0;
        HRESULT hr = s->pSoundbuffer_->GetStatus(&status);
        if (FAILED(hr) || !(status & DSBSTATUS_PLAYING)) {
            s->dwThread_done_ = 1;
            break;
        }
    }
    return 0;
}

CStreamSoundbuffer::CStreamSoundbuffer()
    : filename_(NULL), pSoundbuffer_(NULL), pDirectsound_(NULL),
      dwBuffer_size_(0), dwThread_done_(1), watcher_thread_(NULL),
      stop_event_(NULL)
{
    log_write("CStream::Initialize(this=%p)\n", this);
    InitializeCriticalSection(&cs_);
}

int CStreamSoundbuffer::prepare(WaveInfo *wi)
{
    log_write("CStream::Prepare(this=%p, file='%s')\n",
              this, wi ? (wi->pFilename ? wi->pFilename : "<null>") : "<null wi>");

    releaseResources();

    if (!wi || !wi->pFilename || !wi->pDirectsound) {
        log_write("CStream::Prepare: bad WaveInfo\n");
        return 0;
    }

    pDirectsound_ = wi->pDirectsound;

    DWORD len = (DWORD)strlen(wi->pFilename) + 1;
    filename_ = (char*)HeapAlloc(GetProcessHeap(), 0, len);
    if (!filename_) return 0;
    memcpy(filename_, wi->pFilename, len);

    WAVEFORMATEX *fmt  = NULL;
    BYTE         *pcm  = NULL;
    DWORD         pcm_sz = 0;

    if (!parse_wav(filename_, &fmt, &pcm, &pcm_sz)) {
        log_write("CStream::Prepare: parse_wav failed for '%s'\n", filename_);
        goto fail;
    }

    dwBuffer_size_ = pcm_sz;

    {
        DSBUFFERDESC desc = {};
        desc.dwSize        = sizeof(DSBUFFERDESC);
        desc.dwFlags       = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
        desc.dwBufferBytes = pcm_sz;
        desc.lpwfxFormat   = fmt;

        HRESULT hr = pDirectsound_->CreateSoundBuffer(
                         &desc, &pSoundbuffer_, NULL);
        if (FAILED(hr)) {
            log_write("CStream::Prepare: CreateSoundBuffer failed hr=0x%lx\n",
                      (unsigned long)hr);
            goto fail;
        }
    }

    {
        void  *ptr1 = NULL, *ptr2 = NULL;
        DWORD  bytes1 = 0,   bytes2 = 0;
        HRESULT hr = pSoundbuffer_->Lock(
                         0, pcm_sz, &ptr1, &bytes1, &ptr2, &bytes2, 0);
        if (FAILED(hr)) {
            log_write("CStream::Prepare: Lock failed hr=0x%lx\n",
                      (unsigned long)hr);
            goto fail;
        }
        memcpy(ptr1, pcm, bytes1);
        if (ptr2 && bytes2)
            memcpy(ptr2, (BYTE*)pcm + bytes1, bytes2);
        pSoundbuffer_->Unlock(ptr1, bytes1, ptr2, bytes2);
    }

    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    log_write("CStream::Prepare: OK (%lu bytes)\n", (unsigned long)pcm_sz);
    return 1;

fail:
    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    releaseResources();
    return 0;
}

void CStreamSoundbuffer::play()
{
    log_write("CStream::Play(this=%p)\n", this);
    if (!pSoundbuffer_) return;

    if (watcher_thread_) stop();

    if (!stop_event_)
        stop_event_ = CreateEventA(NULL, TRUE, FALSE, NULL);
    else
        ResetEvent(stop_event_);

    pSoundbuffer_->SetCurrentPosition(0);
    dwThread_done_ = 0;
    pSoundbuffer_->Play(0, 0, 0);

    DWORD tid;
    watcher_thread_ = CreateThread(NULL, 0, watcherProc, this, 0, &tid);
    log_write("CStream::Play: watcher tid=%lu\n", (unsigned long)tid);
}

void CStreamSoundbuffer::stop()
{
    log_write("CStream::Stop(this=%p)\n", this);
    if (watcher_thread_) {
        if (pSoundbuffer_) pSoundbuffer_->Stop();
        if (stop_event_)   SetEvent(stop_event_);
        WaitForSingleObject(watcher_thread_, 5000);
        CloseHandle(watcher_thread_);
        watcher_thread_ = NULL;
    }
    dwThread_done_ = 1;
}

void CStreamSoundbuffer::releaseResources()
{
    log_write("CStream::ReleaseResources(this=%p)\n", this);
    stop();

    if (pSoundbuffer_) {
        pSoundbuffer_->Release();
        pSoundbuffer_ = NULL;
    }
    if (stop_event_) {
        CloseHandle(stop_event_);
        stop_event_ = NULL;
    }
    if (filename_) {
        HeapFree(GetProcessHeap(), 0, filename_);
        filename_ = NULL;
    }
    pDirectsound_  = NULL;
    dwBuffer_size_ = 0;
    dwThread_done_ = 1;
}

CStreamSoundbuffer::~CStreamSoundbuffer()
{
    log_write("CStream::DeinitInstance(this=%p)\n", this);
    releaseResources();
    DeleteCriticalSection(&cs_);
}

