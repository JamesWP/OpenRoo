#include "sdl_internal.h"

namespace audiodev {

/* The track plays on a logical device of its own, mixed by SDL with the
 * sound effects'; a repeating track refills its own stream, so nothing here
 * waits on the window. */
struct MusicState {
    std::unique_ptr<Voice> voice;
};

Music::Music() : state_(new MusicState())
{
}

Music::~Music()
{
    stop();
    delete state_;
}

void Music::setWindow(void *)
{
}

void Music::play(const char *path, bool repeat)
{
    stop();

    auto wav = std::make_shared<Wav>();
    if (!loadWav(path, wav.get()))
        return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        AD_LOG("audiodev: Music: SDL_INIT_AUDIO failed: %s\n", SDL_GetError());
        return;
    }
    state_->voice.reset(new Voice(0, wav));
    if (!state_->voice->ok()) {
        AD_LOG("audiodev: Music open failed\n");
        state_->voice.reset();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    state_->voice->start(repeat);
}

void Music::stop()
{
    if (state_->voice) {
        state_->voice.reset();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

bool Music::handleWindowMessage(unsigned, unsigned long, long)
{
    return false;
}

}  // namespace audiodev
