#include <windows.h>
#include <mmsystem.h>
#include "audiodev.h"

namespace audiodev {

unsigned masterVolume()
{
    DWORD volume = 0;
    waveOutGetVolume(NULL, &volume);
    return volume;
}

void setMasterVolume(unsigned packed)
{
    waveOutSetVolume(NULL, packed);
}

void playSystemSound(const char *path, bool async)
{
    sndPlaySoundA(path, (async ? SND_ASYNC : 0) | SND_NODEFAULT);
}

}  // namespace audiodev
