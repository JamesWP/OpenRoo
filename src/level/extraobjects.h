/* ExtraObjects: the level's extra 3D objects, from its .leo file
 * (Level3DExtraObjects\<level>.leo), a sub-object of Game.  Up to 256 records,
 * each a model, particle system, billboard or positioned sound, optionally
 * moving along a spline.  Construction zeroes the sound handle of records
 * 0..254 only, not 255. */
#pragma once

#include <stddef.h>
 

class SoundManager;
class CStaticSoundbuffer;

enum ExtraObjectKind {
    EXTRA_MODEL = 0, EXTRA_PARTICLE = 1, EXTRA_BILLBOARD = 2, EXTRA_SOUND = 3
};

struct __attribute__((packed)) ExtraObjectRecord {
     

    // FORMAT: "<game dir>\<name>" of the model, particle system or sound.
    char                 file[0x100];
    float                position[3];           // position
    float                field_10c[3];          // models and particles only
    char                 animationFile[0x100];  // model; zeroed when none
    char                 textureFile[0x100];
    int                  lit;   // model: lighting on
    unsigned char        kind;  // ExtraObjectKind
    float                billboardSize;
    int                  srcBlend;   // D3DBLEND 1..13; 0 none or unknown
    int                  destBlend;  // written only when srcBlend != 0
    unsigned int         textureAddress;
    unsigned char        splineMode;              // 0 none, 1 dynamic, 2 static
    int                  splineTime;              // ms
    float                splinePoints[0x100][3];  // from the file's spline line
    unsigned short       splinePointCount;
    double               soundParam;  // sound: zeroed, then optional
    CStaticSoundbuffer  *sound;

     
};

/* The record's fields tile its 0xf40 bytes. */
 

class __attribute__((packed)) ExtraObjects {
public:
     

    enum { RECORD_MAX = 256, RELEASE_COUNT = 255 };

    // Construction and the destructor body.
    void construct();
    void destruct();
    // Parses <name>.leo.
    int  openFile(const char *name);
    // Halts and releases each record's sound.  PRESERVED: the first 255 only.
    void releaseSounds();
    // Sounds released so far, for KAROO_LEVELPARSE_DIAG.
    static unsigned releasedCount();

    // Non-zero when the level's .leo loaded.
    int            loaded() const                    { return loaded_; }
    void           setLoaded(int l)                  { loaded_ = l; }
    void           setSoundManager(SoundManager *sm) { soundManager_ = sm; }
    // Objects built from the file; the level report prints it.
    unsigned short objectCount() const               { return objectCount_; }
    ExtraObjectRecord *record(unsigned int i)        { return &records_[i]; }

    static ExtraObjects * 
    scalarDeletingDtor(ExtraObjects *self, unsigned char flags);

private:
    ExtraObjects() = delete;  // only ever reached through the Game
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
     

    const void        *vtable_;  // our one-slot table
    int                loaded_;
    SoundManager      *soundManager_;
    unsigned short     entries_;  // entries seen
    ExtraObjectRecord  records_[RECORD_MAX];
    unsigned short     objectCount_;
};
