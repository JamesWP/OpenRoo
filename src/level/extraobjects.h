/* ExtraObjects: the level's extra 3D objects, from its .leo file
 * (Level3DExtraObjects\<level>.leo), a sub-object of Game.  Up to 256 records,
 * each a model, particle system, billboard or positioned sound, optionally
 * moving along a spline.  Construction zeroes the sound handle of records
 * 0..254 only, not 255. */
#pragma once

#include <stddef.h>
 

class SoundLibrary;
class SoundVoice;

enum ExtraObjectKind {
    EXTRA_MODEL = 0, EXTRA_PARTICLE = 1, EXTRA_BILLBOARD = 2, EXTRA_SOUND = 3
};

struct ExtraObjectRecord {
     

    // FORMAT: "<game dir>\<name>" of the model, particle system or sound.
    char                 file[0x100]{};
    float                position[3]{};           // position
    float                field_10c[3]{};          // models and particles only
    char                 animationFile[0x100]{};  // model; zeroed when none
    char                 textureFile[0x100]{};
    int                  lit{};   // model: lighting on
    unsigned char        kind{};  // ExtraObjectKind
    float                billboardSize{};
    int                  srcBlend{};   // D3DBLEND 1..13; 0 none or unknown
    int                  destBlend{};  // written only when srcBlend != 0
    unsigned int         textureAddress{};
    unsigned char        splineMode;              // 0 none, 1 dynamic, 2 static
    int                  splineTime{};              // ms
    float                splinePoints[0x100][3]{};  // from the file's spline line
    unsigned short       splinePointCount{};
    double               soundParam{};  // sound: zeroed, then optional
    SoundVoice  *sound;

     
};

class ExtraObjects {
public:
     

    enum { RECORD_MAX = 256, RELEASE_COUNT = 255 };

    ExtraObjects();
    virtual ~ExtraObjects();
    ExtraObjects(const ExtraObjects &) = delete;
    ExtraObjects &operator=(const ExtraObjects &) = delete;
    // Parses <dir>\Level3DExtraObjects\<name>.leo; the paths the file names
    // are relative to dir, which the parse keeps until the next openFile.
    int  openFile(const char *dir, const char *name);
    // Halts and releases each record's sound.  PRESERVED: the first 255 only.
    void releaseSounds();
    // Sounds released so far, for KAROO_LEVELPARSE_DIAG.
    static unsigned releasedCount();

    // Non-zero when the level's .leo loaded.
    int            loaded() const                    { return loaded_; }
    void           setLoaded(int l)                  { loaded_ = l; }
    void           setSoundLibrary(SoundLibrary *sm) { soundLibrary_ = sm; }
    // Objects built from the file; the level report prints it.
    unsigned short objectCount() const               { return objectCount_; }
    ExtraObjectRecord *record(unsigned int i)        { return &records_[i]; }


private:
    char dir_[64]{};
    void recDump(const char *path);
    // The entry parser and its helpers.
    int  parseEntry(const char *entry);
    void parseSound();
    void parseParticle();
    void parseModel();
    void parseBillboard();
    void parseSpline(const char *mode);
    bool readSixFloats();
    void setBlend(const char *src, const char *dest);
    static int blendFromName(const char *name);
    static unsigned int addressFromName(const char *name);
    // The record being built, re-read at every use: a billboard bumps the
    // count in the middle of its entry.
    ExtraObjectRecord *current() { return &records_[objectCount_]; }
     

    int                loaded_{};
    SoundLibrary      *soundLibrary_{};
    unsigned short     entries_{};  // entries seen
    ExtraObjectRecord  records_[RECORD_MAX];
    unsigned short     objectCount_{};
};
