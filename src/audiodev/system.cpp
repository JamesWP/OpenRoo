#include "sdl_internal.h"

namespace audiodev {

/* A launcher sound: the file on its own stream of the default output.  The
 * last one is kept until the next replaces it. */
void playSystemSound(const char *path, bool async)
{
    static SDL_AudioStream *current = NULL;
    if (current) {
        SDL_DestroyAudioStream(current);
        current = NULL;
    }
    // Left initialised: quitting would cut off an async sound.
    static bool audioUp = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audioUp)
        return;

    SDL_AudioSpec spec;
    Uint8 *data = NULL;
    Uint32 len = 0;
    if (SDL_LoadWAV(path, &spec, &data, &len)) {
        current = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                            &spec, NULL, NULL);
        if (current) {
            SDL_PutAudioStreamData(current, data, (int)len);
            SDL_FlushAudioStream(current);
            SDL_ResumeAudioStreamDevice(current);
            // Played to the end before returning, for the quit button.
            for (int waited = 0; !async && waited < 5000
                 && SDL_GetAudioStreamAvailable(current) > 0; waited += 10)
                SDL_Delay(10);
        }
        SDL_free(data);
    }
}

}  // namespace audiodev
