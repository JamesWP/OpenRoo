/* CdThemes -- the CD-music theme table embedded in Game at +0x2223f
 * (COHESION_PLAN.md Band 3, the fifth sub-object; Ghidra's AutoClass5).
 *
 * ParseThemeFile 0x403040 reads CDTracks\<name>.cdt: each line is a CD
 * track number and a theme name ("main", "gameover", a map name ...).
 * Theme i's track is trackOf_[i], its name names_[i]; count_ is how many
 * were read.  PlayCDStuf plays a theme by name and remembers its track in
 * currentTrack_; PlayCDStuf_2 replays that.
 *
 * 0xff1e bytes, settled by the parser's failure path: it zeroes 0x3f80
 * dwords and one byte from +0x11d -- 0xfe01 = 255 names of 255 bytes --
 * which ends exactly at Game's parkedCameraOption (+0x3215d).
 *
 * The index is a byte, so a file with a 256th theme stores its track at
 * +0x1c + 255 = +0x11b, over currentTrack_, and its name past the object.
 * Unreachable with the shipped .cdt files; not "fixed".
 *
 * Nothing constructs it: +0x00..+0x18 are not written by any of its
 * functions, and +0x18 is the track count ValidateCDTrackLengths stores.
 */
#pragma once

#include "layout.h"

class __attribute__((packed)) CdThemes {
public:
    static const int ORIGIN = 0;

    enum { THEME_MAX = 255, NAME_SIZE = 255 };

    /* cdthemes.cpp -- behind the Sim_* export shims. */
    unsigned int  findThemeIndex(const char *name);   /* 0x403240 */
    unsigned int  play(const char *caption);          /* 0x403360 */
    unsigned int  replay();                           /* 0x4033e0 */

    /* ValidateCDTrackLengths 0x403420 (cdthemes.cpp): stores the CD's
     * track count at +0x18 and returns 1 only for the game's own 9-track
     * disc, recognised by each track's length string. */
    int           validateTrackLengths();

    /* The track PlayCDStuf last picked (0 = none). */
    unsigned char currentTrack() const               { return currentTrack_; }
    void          setCurrentTrack(unsigned char t)   { currentTrack_ = t; }

private:
    CdThemes() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(CdThemes);

    unsigned char gap_00[0x18 - 0x00];
    int           trackCount_;                         /* +0x18 */
    unsigned char trackOf_[THEME_MAX];                 /* +0x1c */
    unsigned char currentTrack_;                       /* +0x11b */
    unsigned char count_;                              /* +0x11c */
    char          names_[THEME_MAX][NAME_SIZE];        /* +0x11d */
};

KAROO_LAYOUT_CHECKS(CdThemes)
{
    KAROO_LAYOUT_AT(trackCount_,   0x18);
    KAROO_LAYOUT_AT(trackOf_,      0x1c);
    KAROO_LAYOUT_AT(currentTrack_, 0x11b);
    KAROO_LAYOUT_AT(count_,        0x11c);
    KAROO_LAYOUT_AT(names_,        0x11d);
    KAROO_LAYOUT_SIZE(0xff1e);
}

/* The exports patch.py binds; shims onto the methods. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_FindThemeIndexByThemeName(CdThemes *self, const char *name);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_PlayCDStuf(CdThemes *self, const char *caption);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_PlayCDStuf_2(CdThemes *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_ValidateCDTrackLengths(CdThemes *self);
