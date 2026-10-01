#define DIRECTSOUND_VERSION 0x0800
#include "dsound_internal.h"
#include <string.h>

namespace audiodev {

LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }

/* The .wav parser: RIFF/WAVE with a fmt and a data chunk; other chunks are
 * skipped (padded to even length). */

#define FOURCC(a,b,c,d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | \
     ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

static const DWORD ID_RIFF = FOURCC('R','I','F','F');
static const DWORD ID_WAVE = FOURCC('W','A','V','E');
static const DWORD ID_FMT  = FOURCC('f','m','t',' ');
static const DWORD ID_DATA = FOURCC('d','a','t','a');

static bool read_dword(HANDLE f, DWORD *out) {
    DWORD n;
    return ReadFile(f, out, 4, &n, NULL) && n == 4;
}

bool loadWav(const char *path, Wav *out)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        AD_LOG("audiodev: can't open '%s' err=%lu\n", path, GetLastError());
        return false;
    }

    WAVEFORMATEX *fmt = NULL;
    BYTE         *pcm = NULL;
    DWORD         pcm_sz = 0;

    DWORD id, size, wave_id;
    if (!read_dword(f, &id)   || id      != ID_RIFF) goto fail;
    if (!read_dword(f, &size))                        goto fail;
    if (!read_dword(f, &wave_id) || wave_id != ID_WAVE) goto fail;

    while (!fmt || !pcm) {
        DWORD chunk_id, chunk_size, n;
        if (!read_dword(f, &chunk_id) || !read_dword(f, &chunk_size))
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
        heapFree(fmt);
        heapFree(pcm);
        return false;
    }
    out->format = fmt;
    out->pcm    = pcm;
    out->size   = pcm_sz;
    return true;

fail:
    CloseHandle(f);
    heapFree(fmt);
    heapFree(pcm);
    return false;
}

void freeWav(Wav *wav)
{
    heapFree(wav->format);
    heapFree(wav->pcm);
    wav->format = NULL;
    wav->pcm    = NULL;
    wav->size   = 0;
}

bool fillWavBuffer(IDirectSoundBuffer *buf, const Wav &wav)
{
    void  *ptr1 = NULL, *ptr2 = NULL;
    DWORD  bytes1 = 0,   bytes2 = 0;
    HRESULT hr = buf->Lock(0, wav.size, &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (FAILED(hr)) {
        AD_LOG("audiodev: Lock failed hr=0x%lx\n", (unsigned long)hr);
        return false;
    }
    memcpy(ptr1, wav.pcm, bytes1);
    if (ptr2 && bytes2) memcpy(ptr2, wav.pcm + bytes1, bytes2);
    buf->Unlock(ptr1, bytes1, ptr2, bytes2);
    return true;
}

IDirectSoundBuffer *createWavBuffer(IDirectSound *ds, DWORD flags,
                                    const Wav &wav)
{
    DSBUFFERDESC desc = {};
    desc.dwSize        = sizeof(DSBUFFERDESC);
    desc.dwFlags       = flags;
    desc.dwBufferBytes = wav.size;
    desc.lpwfxFormat   = wav.format;

    IDirectSoundBuffer *buf = NULL;
    HRESULT hr = ds->CreateSoundBuffer(&desc, &buf, NULL);
    if (FAILED(hr)) {
        AD_LOG("audiodev: CreateSoundBuffer failed hr=0x%lx\n",
               (unsigned long)hr);
        return NULL;
    }
    if (!fillWavBuffer(buf, wav)) {
        buf->Release();
        return NULL;
    }
    return buf;
}

char *heapStrdup(const char *s)
{
    if (!s) return NULL;
    DWORD len = (DWORD)strlen(s) + 1;
    char *out = (char*)HeapAlloc(GetProcessHeap(), 0, len);
    if (out) memcpy(out, s, len);
    return out;
}

void heapFree(void *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

}  // namespace audiodev
