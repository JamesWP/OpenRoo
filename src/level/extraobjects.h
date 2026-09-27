/* ExtraObjects: the level's extra 3D objects, from its .leo file
 * (Level3DExtraObjects\<level>.leo), a sub-object of Game.  Up to 256 records,
 * each a model, particle system, billboard or positioned sound, optionally
 * moving along a spline.  Construction zeroes the sound handle of records
 * 0..254 only, not 255. */
#pragma once

#include <stddef.h>
#include "layout.h"

class SoundManager;
struct CStaticSoundbuffer;

enum ExtraObjectKind {
    EXTRA_MODEL = 0, EXTRA_PARTICLE = 1, EXTRA_BILLBOARD = 2, EXTRA_SOUND = 3
};

struct __attribute__((packed)) ExtraObjectRecord {
    static const int ORIGIN = 0;

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

    KAROO_LAYOUT_REGISTER(ExtraObjectRecord);
};

/* The record's fields tile its 0xf40 bytes. */
KAROO_LAYOUT_CHECKS(ExtraObjectRecord)
{
    KAROO_LAYOUT_AT(position,         0x100);
    KAROO_LAYOUT_AT(field_10c,        0x10c);
    KAROO_LAYOUT_AT(animationFile,    0x118);
    KAROO_LAYOUT_AT(textureFile,      0x218);
    KAROO_LAYOUT_AT(lit,              0x318);
    KAROO_LAYOUT_AT(kind,             0x31c);
    KAROO_LAYOUT_AT(billboardSize,    0x31d);
    KAROO_LAYOUT_AT(srcBlend,         0x321);
    KAROO_LAYOUT_AT(destBlend,        0x325);
    KAROO_LAYOUT_AT(textureAddress,   0x329);
    KAROO_LAYOUT_AT(splineMode,       0x32d);
    KAROO_LAYOUT_AT(splineTime,       0x32e);
    KAROO_LAYOUT_AT(splinePoints,     0x332);
    KAROO_LAYOUT_AT(splinePointCount, 0xf32);
    KAROO_LAYOUT_AT(soundParam,       0xf34);
    KAROO_LAYOUT_AT(sound,            0xf3c);
    KAROO_LAYOUT_SIZE(0xf40);
}

class __attribute__((packed)) ExtraObjects {
public:
    static const int ORIGIN = 0;

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
    KAROO_LAYOUT_REGISTER(ExtraObjects);

    const void        *vtable_;  // our one-slot table
    int                loaded_;
    SoundManager      *soundManager_;
    unsigned short     entries_;  // entries seen
    ExtraObjectRecord  records_[RECORD_MAX];
    unsigned short     objectCount_;
};

KAROO_LAYOUT_CHECKS(ExtraObjects)
{
    KAROO_LAYOUT_AT(loaded_,       0x04);
    KAROO_LAYOUT_AT(soundManager_, 0x08);
    KAROO_LAYOUT_AT(entries_,      0x0c);
    KAROO_LAYOUT_AT(records_,      0x0e);
    KAROO_LAYOUT_AT(objectCount_,  0xf400e);
    KAROO_LAYOUT_SIZE(0xf4010);
}

extern "C" __declspec(dllexport) ExtraObjects *__attribute__((thiscall))
Leo_Construct(ExtraObjects *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Leo_Destruct(ExtraObjects *self);
extern "C" __declspec(dllexport) ExtraObjects *__attribute__((thiscall))
Leo_ScalarDestructor(ExtraObjects *self, unsigned char flags);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Leo_OpenExtraObjectsFile(ExtraObjects *self, const char *name);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Leo_ReleaseExtraObjectSoundBuffers(ExtraObjects *self);
