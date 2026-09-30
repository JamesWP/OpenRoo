/* CdThemes: the music theme table, a sub-object of Game.  CDTracks\<name>.cdt
 * lists, one per line, a CD track number and a theme name ("main", "gameover",
 * a level's theme...).  play() plays a theme by name and remembers its track;
 * replay() plays that track again.  The CD is recognised by its track lengths
 * before the game starts. */
#pragma once

class CdThemes {
public:
     

    enum { THEME_MAX = 255, NAME_SIZE = 255 };

    // Construction leaves the table uninitialised until readTrackThemeTable
    // fills it.
    CdThemes();
    virtual ~CdThemes();
    CdThemes(const CdThemes &) = delete;
    CdThemes &operator=(const CdThemes &) = delete;

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
     

    int           trackCount_;          // set by validateTrackLengths
    unsigned char trackOf_[THEME_MAX];  // theme i's CD track
    unsigned char currentTrack_;
    unsigned char count_;                        // themes read
    char          names_[THEME_MAX][NAME_SIZE];  // theme i's name
};

