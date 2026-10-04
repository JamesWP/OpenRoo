#include "sdl_internal.h"

namespace audiodev {

/* A launcher sound: its own short-lived mixer on the default output, kept
 * until the next sound replaces it. */
void playSystemSound(const char *path, bool async)
{
    static std::shared_ptr<AudioRef> audio;
    static MIX_Track *track;
    static std::shared_ptr<MixerRef> mixer;

    if (track) { MIX_DestroyTrack(track); track = NULL; }
    audio.reset();
    mixer = std::make_shared<MixerRef>(nullptr);
    if (!mixer->mixer) { mixer.reset(); return; }
    MIX_SetMixerGain(mixer->mixer, effectsVolume());

    audio = loadAudio(mixer, path, true);
    track = audio ? MIX_CreateTrack(mixer->mixer) : NULL;
    if (!track || !MIX_SetTrackAudio(track, audio->audio))
        return;
    playTrack(track, false);
    // Played to the end before returning, for the quit button.
    for (int waited = 0; !async && waited < 5000 && MIX_TrackPlaying(track); waited += 10)
        SDL_Delay(10);
}

}  // namespace audiodev
