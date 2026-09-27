/* CdThemes: the music theme table, a sub-object of Game.  CDTracks\<name>.cdt
 * lists, one per line, a CD track number and a theme name ("main", "gameover",
 * a level's theme...).  play() plays a theme by name and remembers its track;
 * replay() plays that track again.  The CD is recognised by its track lengths
 * before the game starts. */
#pragma once

#include "layout.h"

class __attribute__((packed)) CdThemes {
public:
    static const int ORIGIN = 0;

    enum { THEME_MAX = 255, NAME_SIZE = 255 };

    // Construction only sets the vtable; the table stays uninitialised until
    // readTrackThemeTable fills it.
    void          construct();
    void          destruct();

    // The theme's track, or 0 (indistinguishable from theme 0) when not found.
    // Case-insensitive.
    unsigned int  findThemeIndex(const char *name);
    // Stops the music, looks the theme up and plays its track on repeat.
    // Returns 0 if no themes are loaded, else 1.
    unsigned int  play(const char *caption);
    // Plays the remembered track again; returns 0.
    unsigned int  replay();

    // Stores the CD's track count and returns 1 only for the game's own
    // nine-track CD, recognised by each track's length.
    int           validateTrackLengths();

    // Fills the table from <game dir>\CDTracks\<name>.cdt; returns the theme
    // count.
    unsigned char readTrackThemeTable(const char *name);
    // Stores the track count and logs each track's length.
    int           listTrackLengths();

    // The track play() last picked, 0 for none.
    unsigned char currentTrack() const               { return currentTrack_; }
    void          setCurrentTrack(unsigned char t)   { currentTrack_ = t; }

private:
    CdThemes() = delete;  // only ever reached through the Game
    KAROO_LAYOUT_REGISTER(CdThemes);

    void         *vtable_;
    unsigned char gap_04[0x18 - 0x04];  // never written
    int           trackCount_;          // set by validateTrackLengths
    unsigned char trackOf_[THEME_MAX];  // theme i's CD track
    unsigned char currentTrack_;
    unsigned char count_;                        // themes read
    char          names_[THEME_MAX][NAME_SIZE];  // theme i's name
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

unsigned int Sim_FindThemeIndexByThemeName(CdThemes *self, const char *name);
unsigned int Sim_PlayCDStuf(CdThemes *self, const char *caption);
unsigned int Sim_PlayCDStuf_2(CdThemes *self);
int Sim_ValidateCDTrackLengths(CdThemes *self);
unsigned char Sim_ReadCdTrackThemeTable(CdThemes *self, const char *name);
int Sim_ListTrackLengths(CdThemes *self);
CdThemes *Sim_CdThemesConstruct(CdThemes *self);
void Sim_CdThemesDestruct(CdThemes *self);
CdThemes *Sim_CdThemesScalarDeletingDtor(CdThemes *self, unsigned int flags);

/* The one-slot vtable: the deleting destructor. */
extern const void *const CDTHEMES_VTABLE;
