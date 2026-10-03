#define DIRECTSOUND_VERSION 0x0800
#include "dsound_internal.h"
#include <utility>
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

/* The chunk walk.  Fills *out only when both the format and the data are
 * found; a data chunk bigger than the file is a truncated file. */
static bool parseWav(FILE *f, Wav *out)
{
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    DWORD id, size, wave_id;
    if (!read_dword(f, &id)   || id      != ID_RIFF) return false;
    if (!read_dword(f, &size))                        return false;
    if (!read_dword(f, &wave_id) || wave_id != ID_WAVE) return false;

    Wav wav;
    bool have_fmt = false, have_pcm = false;
    while (!have_fmt || !have_pcm) {
        DWORD chunk_id, chunk_size;
        if (!read_dword(f, &chunk_id) || !read_dword(f, &chunk_size))
            break;
        if (chunk_id == ID_FMT) {
            DWORD to_read = chunk_size < sizeof(WAVEFORMATEX)
                          ? chunk_size : sizeof(WAVEFORMATEX);
            wav.format = WAVEFORMATEX();
            if (fread(&wav.format, 1, to_read, f) != to_read) return false;
            have_fmt = true;
            DWORD skip = chunk_size - to_read;
            if (skip) fseek(f, (long)(skip + (skip & 1)), SEEK_CUR);
        } else if (chunk_id == ID_DATA) {
            if (chunk_size > (DWORD)file_size) return false;
            wav.pcm.assign(chunk_size, 0);
            if (fread(wav.pcm.data(), 1, chunk_size, f) != chunk_size) return false;
            have_pcm = true;
        } else {
            DWORD skip = chunk_size + (chunk_size & 1);
            fseek(f, (long)skip, SEEK_CUR);
        }
    }
    if (!have_fmt || !have_pcm)
        return false;
    *out = std::move(wav);
    return true;
}

bool loadWav(const char *path, Wav *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        AD_LOG("audiodev: can't open '%s'\n", path);
        return false;
    }
    bool ok = parseWav(f, out);
    fclose(f);
    return ok;
}

bool fillWavBuffer(IDirectSoundBuffer *buf, const Wav &wav)
{
    void  *ptr1 = NULL, *ptr2 = NULL;
    DWORD  bytes1 = 0,   bytes2 = 0;
    HRESULT hr = buf->Lock(0, (DWORD)wav.pcm.size(), &ptr1, &bytes1, &ptr2, &bytes2, 0);
    if (FAILED(hr)) {
        AD_LOG("audiodev: Lock failed hr=0x%lx\n", (unsigned long)hr);
        return false;
    }
    memcpy(ptr1, wav.pcm.data(), bytes1);
    if (ptr2 && bytes2) memcpy(ptr2, wav.pcm.data() + bytes1, bytes2);
    buf->Unlock(ptr1, bytes1, ptr2, bytes2);
    return true;
}

IDirectSoundBuffer *createWavBuffer(IDirectSound *ds, DWORD flags,
                                    const Wav &wav)
{
    DSBUFFERDESC desc = {};
    desc.dwSize        = sizeof(DSBUFFERDESC);
    desc.dwFlags       = flags;
    desc.dwBufferBytes = (DWORD)wav.pcm.size();
    desc.lpwfxFormat   = const_cast<WAVEFORMATEX *>(&wav.format);

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

}  // namespace audiodev
