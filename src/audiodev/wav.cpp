#include "sdl_internal.h"
#include <utility>
#include <fstream>
#include <string.h>

namespace audiodev {

LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }

bool Wav::spec(SDL_AudioSpec *out) const
{
    if (bitsPerSample != 8 && bitsPerSample != 16)
        return false;
    out->format   = bitsPerSample == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
    out->channels = channels;
    out->freq     = sampleRate;
    return channels > 0 && sampleRate > 0;
}

/* The .wav parser: RIFF/WAVE with a PCM fmt and a data chunk; other chunks
 * are skipped (padded to even length).  The file is little-endian, as is
 * every machine this runs on. */

#define FOURCC(a,b,c,d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

static const uint32_t ID_RIFF = FOURCC('R','I','F','F');
static const uint32_t ID_WAVE = FOURCC('W','A','V','E');
static const uint32_t ID_FMT  = FOURCC('f','m','t',' ');
static const uint32_t ID_DATA = FOURCC('d','a','t','a');

static const int WAVE_FORMAT_PCM_TAG = 1;

static bool readBytes(std::istream &f, void *dst, size_t size) {
    f.read(static_cast<char *>(dst), (std::streamsize)size);
    return (size_t)f.gcount() == size;
}

static bool read_u32(std::istream &f, uint32_t *out) {
    return readBytes(f, out, 4);
}

/* The chunk walk.  Fills *out only when both the format and the data are
 * found; a data chunk bigger than the file is a truncated file. */
static bool parseWav(std::istream &f, Wav *out)
{
    f.seekg(0, std::ios::end);
    std::streamoff file_size = f.tellg();
    f.seekg(0, std::ios::beg);

    uint32_t id, size, wave_id;
    if (!read_u32(f, &id)   || id      != ID_RIFF) return false;
    if (!read_u32(f, &size))                       return false;
    if (!read_u32(f, &wave_id) || wave_id != ID_WAVE) return false;

    Wav wav;
    bool have_fmt = false, have_pcm = false;
    while (!have_fmt || !have_pcm) {
        uint32_t chunk_id, chunk_size;
        if (!read_u32(f, &chunk_id) || !read_u32(f, &chunk_size))
            break;
        if (chunk_id == ID_FMT) {
            // tag, channels, rate, bytes/s, block align, bits
            uint8_t fmt[16];
            if (chunk_size < sizeof(fmt) || !readBytes(f, fmt, sizeof(fmt)))
                return false;
            uint16_t tag, channels, bits;
            uint32_t rate;
            memcpy(&tag, fmt, 2);  memcpy(&channels, fmt + 2, 2);
            memcpy(&rate, fmt + 4, 4);  memcpy(&bits, fmt + 14, 2);
            if (tag != WAVE_FORMAT_PCM_TAG) {
                AD_LOG("audiodev: wav format tag %u is not PCM\n", tag);
                return false;
            }
            wav.channels = channels;
            wav.sampleRate = (int)rate;
            wav.bitsPerSample = bits;
            have_fmt = true;
            uint32_t skip = chunk_size - sizeof(fmt);
            if (skip) f.seekg((std::streamoff)(skip + (skip & 1)), std::ios::cur);
        } else if (chunk_id == ID_DATA) {
            if ((std::streamoff)chunk_size > file_size) return false;
            wav.pcm.assign(chunk_size, 0);
            if (!readBytes(f, wav.pcm.data(), chunk_size)) return false;
            have_pcm = true;
        } else {
            uint32_t skip = chunk_size + (chunk_size & 1);
            f.seekg((std::streamoff)skip, std::ios::cur);
        }
    }
    if (!have_fmt || !have_pcm)
        return false;
    *out = std::move(wav);
    return true;
}

bool loadWav(const char *path, Wav *out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        AD_LOG("audiodev: can't open '%s'\n", path);
        return false;
    }
    return parseWav(f, out);
}

}  // namespace audiodev
