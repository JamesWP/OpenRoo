#define DIRECTSOUND_VERSION 0x0800
#define INITGUID
#include <windows.h>
#include <dsound.h>
#include <string.h>
#include <stdio.h>
#include "static.h"
#include <stdlib.h>
#include "log.h"

/* The .wav parser: RIFF/WAVE with a fmt and a data chunk; other chunks are
 * skipped (padded to even length). */

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

/* Returns true on success; the caller frees *fmt_out and *pcm_out with
 * HeapFree. */
static bool parse_wav(const char *path,
                      WAVEFORMATEX **fmt_out, BYTE **pcm_out, DWORD *pcm_sz_out)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("CStatic: parse_wav: can't open '%s' err=%lu\n",
                  path, GetLastError());
        return false;
    }

    WAVEFORMATEX *fmt = NULL;
    BYTE         *pcm = NULL;
    DWORD         pcm_sz = 0;

    DWORD id, size, wave_id;
    if (!wav_read_dword(f, &id)   || id      != ID_RIFF) goto fail;
    if (!wav_read_dword(f, &size))                        goto fail;
    if (!wav_read_dword(f, &wave_id) || wave_id != ID_WAVE) goto fail;

    while (!fmt || !pcm) {
        DWORD chunk_id, chunk_size, n;
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
    if (!fmt || !pcm) goto free_and_fail;
    *fmt_out   = fmt;
    *pcm_out   = pcm;
    *pcm_sz_out = pcm_sz;
    return true;

fail:
    CloseHandle(f);
free_and_fail:
    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    return false;
}

static char *heap_strdup(const char *s)
{
    if (!s) return NULL;
    DWORD len = (DWORD)strlen(s) + 1;
    char *out = (char*)HeapAlloc(GetProcessHeap(), 0, len);
    if (out) memcpy(out, s, len);
    return out;
}

/* Creates a buffer and fills it from the parsed file.  Frees fmt and pcm
 * either way; returns NULL on failure. */
static IDirectSoundBuffer *create_ds_buffer(IDirectSound *pDS,
                                            DWORD dwFlags,
                                            WAVEFORMATEX *fmt,
                                            BYTE *pcm, DWORD pcm_sz)
{
    DSBUFFERDESC desc = {};
    desc.dwSize        = sizeof(DSBUFFERDESC);
    desc.dwFlags       = dwFlags;
    desc.dwBufferBytes = pcm_sz;
    desc.lpwfxFormat   = fmt;

    IDirectSoundBuffer *pBuf = NULL;
    HRESULT hr = pDS->CreateSoundBuffer(&desc, &pBuf, NULL);
    if (FAILED(hr)) {
        log_write("CStatic: CreateSoundBuffer failed hr=0x%lx\n", (unsigned long)hr);
        HeapFree(GetProcessHeap(), 0, fmt);
        HeapFree(GetProcessHeap(), 0, pcm);
        return NULL;
    }

    void  *ptr1 = NULL, *ptr2 = NULL;
    DWORD  bytes1 = 0,   bytes2 = 0;
    hr = pBuf->Lock(0, pcm_sz, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (SUCCEEDED(hr)) {
        memcpy(ptr1, pcm, bytes1);
        if (ptr2 && bytes2) memcpy(ptr2, (BYTE*)pcm + bytes1, bytes2);
        pBuf->Unlock(ptr1, bytes1, ptr2, bytes2);
    } else {
        log_write("CStatic: Lock failed hr=0x%lx\n", (unsigned long)hr);
        pBuf->Release();
        pBuf = NULL;
    }

    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    return pBuf;
}

/* KAROO_SOUND_DIAG=1 logs the first call to each destructor entry point, to
 * tell "ran" from "never ran". */
static bool sound_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = (GetEnvironmentVariableA("KAROO_SOUND_DIAG", buf, sizeof(buf))
                  && buf[0] == '1') ? 1 : 0;
    }
    return cached != 0;
}

extern "C" {

/* First call only, then a running tally: a periodic sample reads zero forever
 * for anything first reached late. */
__declspec(dllexport) void CStatic_SoundFirstCall(const char *who,
                                                  unsigned long *seen)
{
    if (sound_diag() && (*seen)++ == 0)
        log_write("sound: DIAG first call -- %s\n", who);
}

}

static void *const g_CStaticVtable[1] = { (void *)&CStaticSoundbuffer::scalarVectorDtor };

/* For an array, the count header sits four bytes below the first element and
 * the elements are destroyed in reverse, as MSVC's vector destructor does.
 * Voice pools destroy with flag 3, so the array path is the one taken. */
void * 
CStaticSoundbuffer::scalarVectorDtor(CStaticSoundbuffer *self, unsigned int flags)
{
    static unsigned long seen;
    CStatic_SoundFirstCall("CStaticSoundbuffer::ScalarVectorDtor", &seen);

    if (flags & 2) {
        // The count header, four bytes below the first element.
        void *base  = (char *)self - 4;
        int   count = *(int *)base;

        // Reverse order.
        for (int i = count - 1; i >= 0; --i)
            self[i].reinitBuffer();

        if (flags & 1)
            free(base);
        return base;
    }

    self->reinitBuffer();
    if (flags & 1)
        free(self);
    return self;
}

CStaticSoundbuffer *CStaticSoundbuffer::init()
{
    memset(this, 0, sizeof(*this));
    vtable_ = (void *)g_CStaticVtable;
    return this;
}

void CStaticSoundbuffer::reinitBuffer()
{
    vtable_ = (void *)g_CStaticVtable;
    reset();
}

void CStaticSoundbuffer::reset()
{
    if (threeDBuffer_) {
        threeDBuffer_->Release();
        threeDBuffer_ = NULL;
    }
    if (soundbuffer_) {
        soundbuffer_->Release();
        soundbuffer_ = NULL;
    }
    if (filename_) {
        HeapFree(GetProcessHeap(), 0, filename_);
        filename_ = NULL;
    }
    logger_   = NULL;
    dwDsFlags_ = 0;
}

int CStaticSoundbuffer::createAndLoadFile(IDirectSound *pDS, DWORD dwDsFlags,
                                          const char *filename, void *logger)
{

    reset();

    if (!pDS || !filename) {
        log_write("CStatic::CreateAndLoadFile: null pDS or filename\n");
        return 0;
    }

    logger_    = logger;
    dwDsFlags_ = dwDsFlags;
    filename_  = heap_strdup(filename);
    if (!filename_) return 0;

    WAVEFORMATEX *fmt  = NULL;
    BYTE         *pcm  = NULL;
    DWORD         pcm_sz = 0;
    if (!parse_wav(filename, &fmt, &pcm, &pcm_sz)) {
        log_write("CStatic::CreateAndLoadFile: parse_wav failed for '%s'\n", filename);
        reset();
        return 0;
    }

    // Always GETCURRENTPOSITION2, GLOBALFOCUS and STATIC; CTRL3D if asked for.
    DWORD bufFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_STATIC;
    if (dwDsFlags & DSBCAPS_CTRL3D)
        bufFlags |= DSBCAPS_CTRL3D;

    soundbuffer_ = create_ds_buffer(pDS, bufFlags, fmt, pcm, pcm_sz);
    if (!soundbuffer_) {
        log_write("CStatic::CreateAndLoadFile: create_ds_buffer failed for '%s'\n", filename);
        reset();
        return 0;
    }

    return 1;
}

int CStaticSoundbuffer::createAndLoad3DSoundFile(IDirectSound *pDS, DWORD dwDsFlags,
                                                 const char *filename, void *logger)
{

    // Forces CTRL3D, then queries the 3D interface.
    int ok = createAndLoadFile(pDS, dwDsFlags | DSBCAPS_CTRL3D,
                                            filename, logger);
    if (!ok) return 0;

    GUID iid3D = IID_IDirectSound3DBuffer;
    HRESULT hr = soundbuffer_->QueryInterface(iid3D, (void**)threeDBufferSlot());
    if (FAILED(hr)) {
        log_write("CStatic::CreateAndLoad3DSoundFile: QueryInterface IID_IDirectSound3DBuffer hr=0x%lx\n",
                  (unsigned long)hr);
        reset();
        return 0;
    }

    return 1;
}

/* Returns other on success.  flag != 0: no reload from the file if duplication
 * fails; flag == 0: reload, and return self. */
void *CStaticSoundbuffer::copy(IDirectSound *pDS,
                                CStaticSoundbuffer *other,
                                int flag)
{

    reset();

    if (!other || !other->soundbuffer_) return NULL;

    HRESULT hr = pDS->DuplicateSoundBuffer(other->soundbuffer_, soundbufferSlot());
    if (SUCCEEDED(hr)) {
        // Duplicated: copy the file details and query the 3D interface if the
        // source had one.
        filename_  = heap_strdup(other->filename_);
        logger_    = other->logger_;
        dwDsFlags_ = other->dwDsFlags_;

        if (other->threeDBuffer_) {
            GUID iid3D = IID_IDirectSound3DBuffer;
            hr = soundbuffer_->QueryInterface(iid3D, (void**)threeDBufferSlot());
            if (FAILED(hr)) {
                log_write("CStatic::Copy: QueryInterface 3D failed hr=0x%lx\n",
                          (unsigned long)hr);
                reset();
                return NULL;
            }
        }
        return other;  // not self: see static.h
    }

    log_write("CStatic::Copy: DuplicateSoundBuffer failed hr=0x%lx\n", (unsigned long)hr);
    if (flag != 0) {
        reset();
        return NULL;
    }

    // Reload from the file.
    int ok;
    if (other->threeDBuffer_) {
        ok = createAndLoad3DSoundFile(pDS,
                 other->dwDsFlags_ | DSBCAPS_CTRL3D, other->filename_, other->logger_);
    } else {
        ok = createAndLoadFile(pDS,
                 other->dwDsFlags_ & ~DSBCAPS_CTRL3D, other->filename_, other->logger_);
    }
    if (!ok) {
        reset();
        return NULL;
    }
    return this;
}

/* set3D == 0: make the buffer 2D, reloading it without CTRL3D if it is 3D.
 * set3D != 0: make it 3D, reloading with CTRL3D if it is not. */
int CStaticSoundbuffer::createAndLoad(IDirectSound *pDS, DWORD set3D)
{
    if (!soundbuffer_) return 0;

    if (set3D == 0) {
        if (!threeDBuffer_) return 1;
        char *fn     = heap_strdup(filename_);
        void *logger = logger_;
        DWORD flags  = dwDsFlags_ & ~DSBCAPS_CTRL3D;
        reset();
        int ok = createAndLoadFile(pDS, flags, fn, logger);
        HeapFree(GetProcessHeap(), 0, fn);
        return ok;
    } else {
        if (threeDBuffer_) return 1;
        char *fn     = heap_strdup(filename_);
        void *logger = logger_;
        DWORD flags  = dwDsFlags_;
        reset();
        int ok = createAndLoad3DSoundFile(pDS, flags, fn, logger);
        HeapFree(GetProcessHeap(), 0, fn);
        return ok;
    }
}

int CStaticSoundbuffer::apply3DMode(int enable3D)
{
    if (!threeDBuffer_) return 0;
    DWORD mode = enable3D ? DS3DMODE_NORMAL : DS3DMODE_DISABLE;
    threeDBuffer_->SetMode(mode, 0 );  // DS3DAPPLY_NOW
    return 1;
}

/* Restores a lost buffer and refills it from the file. */
int CStaticSoundbuffer::restoreBuffer()
{
    if (!soundbuffer_) return 0;

    HRESULT hr = soundbuffer_->Restore();
    if (FAILED(hr)) {
        log_write("CStatic::RestoreBuffer: Restore failed hr=0x%lx\n", (unsigned long)hr);
        return 0;
    }

    WAVEFORMATEX *fmt  = NULL;
    BYTE         *pcm  = NULL;
    DWORD         pcm_sz = 0;
    if (!parse_wav(filename_, &fmt, &pcm, &pcm_sz)) {
        log_write("CStatic::RestoreBuffer: parse_wav failed for '%s'\n",
                  filename_ ? filename_ : "<null>");
        return 0;
    }

    void  *ptr1 = NULL, *ptr2 = NULL;
    DWORD  bytes1 = 0,   bytes2 = 0;
    hr = soundbuffer_->Lock(0, pcm_sz, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (FAILED(hr)) {
        log_write("CStatic::RestoreBuffer: Lock failed hr=0x%lx\n", (unsigned long)hr);
        HeapFree(GetProcessHeap(), 0, fmt);
        HeapFree(GetProcessHeap(), 0, pcm);
        return 0;
    }
    memcpy(ptr1, pcm, bytes1);
    if (ptr2 && bytes2) memcpy(ptr2, (BYTE*)pcm + bytes1, bytes2);
    soundbuffer_->Unlock(ptr1, bytes1, ptr2, bytes2);

    HeapFree(GetProcessHeap(), 0, fmt);
    HeapFree(GetProcessHeap(), 0, pcm);
    return 1;
}

int CStaticSoundbuffer::triggerPlayback(DWORD dwLoopFlags)
{
    if (!soundbuffer_) return 0;

    HRESULT hr = soundbuffer_->Play(0, 0, dwLoopFlags);
    // A lost buffer is restored, refilled and played again once.
    if (hr == DSERR_BUFFERLOST) {
        log_write("CStatic::TriggerPlayback: buffer lost, restoring '%s'\n",
                  filename_ ? filename_ : "<null>");
        if (!restoreBuffer()) return -1;
        hr = soundbuffer_->Play(0, 0, dwLoopFlags);
    }
    if (FAILED(hr)) {
        log_write("CStatic::TriggerPlayback: Play failed hr=0x%lx\n", (unsigned long)hr);
    }
    return (int)hr;
}

void CStaticSoundbuffer::haltPlayback()
{
    if (soundbuffer_)
        soundbuffer_->Stop();
}

void CStaticSoundbuffer::set3DPosition(float x, float y, float z, DWORD dwApply)
{
    if (threeDBuffer_)
        threeDBuffer_->SetPosition(x, y, z, dwApply);
}

