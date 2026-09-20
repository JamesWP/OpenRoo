/* CdThemes -- the CD-music theme table embedded in Game at +0x2223f
 * (COHESION_PLAN.md Band 3, the fifth sub-object; Ghidra's AutoClass5).
 *
 * ReadCdTrackThemeTable 0x403040 reads CDTracks\<name>.cdt: each line is a CD
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
 * It IS constructed, and the earlier note here that "nothing constructs it"
 * was wrong: 0x403000 writes the vtable 0x0045d2c4 at +0x00 and nothing else,
 * from Game::Load 0x004145f4 on the member at Game+0x2223f.  0x403030 is the
 * destructor (the same store, nothing more) and 0x403010 the slot-0 deleting
 * one.  All three sat in the CDM TU's address range purely by address; they
 * are this class's, and tu_map.txt now says so.  +0x04..+0x18 really are
 * untouched, and +0x18 is the track count ValidateCDTrackLengths stores.
 */
#pragma once

#include "layout.h"

class __attribute__((packed)) CdThemes {
public:
    static const int ORIGIN = 0;

    enum { THEME_MAX = 255, NAME_SIZE = 255 };

    /* cdthemes.cpp -- behind the Sim_* export shims. */
    void          construct();                        /* 0x403000 */
    void          destruct();                         /* 0x403030 */

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

    void         *vtable_;                             /* +0x00 */
    unsigned char gap_04[0x18 - 0x04];
    int           trackCount_;                         /* +0x18 */
    unsigned char trackOf_[THEME_MAX];                 /* +0x1c */
    unsigned char currentTrack_;                       /* +0x11b */
    unsigned char count_;                              /* +0x11c */
    char          names_[THEME_MAX][NAME_SIZE];        /* +0x11d */
};

KAROO_LAYOUT_CHECKS(CdThemes)
{
    KAROO_LAYOUT_AT(vtable_,       0x00);
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
extern "C" __declspec(dllexport) CdThemes * __attribute__((thiscall))
Sim_CdThemesConstruct(CdThemes *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_CdThemesDestruct(CdThemes *self);
extern "C" __declspec(dllexport) CdThemes * __attribute__((thiscall))
Sim_CdThemesScalarDeletingDtor(CdThemes *self, unsigned int flags);

/* CdThemes' vtable: ONE slot at 0x0045d2c4, immediately after CDM's three-slot
   table at 0x0045d2b8 and for a long time mistaken for its slot 3 (see cdm.h).
   It is this class's: the ctor 0x403000 and dtor 0x403030 both store 0x45d2c4,
   and slot 0 is 0x403010, the deleting dtor those two call.  patch.py
   redirects the slot at file offset 0x5D2C4. */
static const void *const CDTHEMES_VTABLE = reinterpret_cast<const void*>(0x0045d2c4);
