#include <string.h>
#include <stdio.h>
#include "cdm.h"
#include "log.h"
#include <stdlib.h>

CDM::CDM()
{
    log_write("CDM::construct(this=%p)\n", this);
    window_ = NULL;
    mcibuff[0] = '\0';
}

CDM::~CDM()
{
    log_write("CDM::stopAndClose(this=%p)\n", this);
    music_.stop();
    log_write("CDM::stopAndClose done\n");
}

void CDM::setWindowHandle(void *hwnd)
{
    log_write("CDM::setWindowHandle(hwnd=0x%p)\n", hwnd);
    window_ = hwnd;
    music_.setWindow(hwnd);
}

bool CDM::handleWindowMessage(unsigned msg, unsigned long wParam, long lParam)
{
    return music_.handleWindowMessage(msg, wParam, lParam);
}

int CDM::getTrackCount()
{
    log_write("CDM::getTrackCount → 9\n");
    return 9;  // the CD has 9 tracks, 1 data and 8 audio; the check wants exactly 9
}

/* The CD's track lengths, which the game checks to recognise its CD. */
static const char *track_len(int track)
{
    static const char *lens[] = {
         NULL,
         "04:38:71",  // never compared
         "04:38:71",
         "02:59:12",
         "03:28:39",
         "02:46:68",
         "03:08:54",
         "03:32:60",
         "04:00:74",
         "02:17:74",
    };
    if (track < 1 || track > 9) return "00:00:00";
    return lens[track];
}

int CDM::getTrackLength(char **out_ptr, int track)
{
    const char *s = track_len(track);
    strncpy(mcibuff, s, sizeof(mcibuff) - 1);
    mcibuff[sizeof(mcibuff) - 1] = '\0';
    if (out_ptr) *out_ptr = mcibuff;
    log_write("CDM::getTrackLength(track=%d) → \"%s\"\n", track, s);
    return 1;
}

void CDM::playTrack(int track, bool loop)
{
    log_write("CDM::playTrack(track=%d, loop=%d)\n", track, (int)loop);

    // CD track 2 is CDTracks\Track 1.wav, and so on.
    int wav = track - 1;
    if (wav < 1 || wav > 8) {
        log_write("CDM::playTrack: track %d out of WAV range\n", track);
        music_.stop();
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "CDTracks\\Track %d.wav", wav);
    music_.play(path, loop);
}

void CDM::stop()
{
    log_write("CDM::stop\n");
    music_.stop();
    log_write("CDM::stop done\n");
}

/* There is no mixer line to drive. */
void CDM::setMixerVolume(unsigned level)
{
    log_write("CDM::setMixerVolume(level=0x%X) — not implemented\n", level);
}

  void KarooHooksLoad() {}  // unused

unsigned int CDM::getMixerDetails()
{
    return 0;
}
