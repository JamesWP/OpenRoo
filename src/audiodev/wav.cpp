#define DIRECTSOUND_VERSION 0x0800
#include "dsound_internal.h"
#include <new>
#include <stdio.h>
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

static bool read_dword(FILE *f, DWORD *out) {
    return fread(out, 1, 4, f) == 4;
}

bool loadWav(const char *path, Wav *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        AD_LOG("audiodev: can't open '%s'\n", path);
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
        DWORD chunk_id, chunk_size;
        if (!read_dword(f, &chunk_id) || !read_dword(f, &chunk_size))
            break;
        if (chunk_id == ID_FMT) {
            DWORD to_read = chunk_size < sizeof(WAVEFORMATEX)
                          ? chunk_size : sizeof(WAVEFORMATEX);
            fmt = new (std::nothrow) WAVEFORMATEX();
            if (!fmt) goto fail;
            if (fread(fmt, 1, to_read, f) != to_read) goto fail;
            DWORD skip = chunk_size - to_read;
            if (skip) fseek(f, (long)(skip + (skip & 1)), SEEK_CUR);
        } else if (chunk_id == ID_DATA) {
            pcm_sz = chunk_size;
            pcm    = new (std::nothrow) BYTE[pcm_sz];
            if (!pcm) goto fail;
            if (fread(pcm, 1, pcm_sz, f) != pcm_sz) goto fail;
        } else {
            DWORD skip = chunk_size + (chunk_size & 1);
            fseek(f, (long)skip, SEEK_CUR);
        }
    }
    fclose(f);
    if (!fmt || !pcm) {
        delete fmt;
        delete[] pcm;
        return false;
    }
    out->format = fmt;
    out->pcm    = pcm;
    out->size   = pcm_sz;
    return true;

fail:
    fclose(f);
    delete fmt;
    delete[] pcm;
    return false;
}

void freeWav(Wav *wav)
{
    delete wav->format;
    delete[] wav->pcm;
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
    size_t len = strlen(s) + 1;
    char *out = new (std::nothrow) char[len];
    if (out) memcpy(out, s, len);
    return out;
}

void heapFree(char *p)
{
    delete[] p;
}

}  // namespace audiodev
