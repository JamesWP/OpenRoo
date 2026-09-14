/* ExtraObjects -- the level's extra 3D objects (.leo), embedded in Game at
 * +0x48b98 (COHESION_PLAN.md Band 3 follow-on; the eighth sub-object;
 * Ghidra's Level3DExtraObjects).
 *
 * 0xf4010 bytes: 256 records of 0xf40 from +0xe end at the object count
 * +0xf400e, and the object ends exactly where the SoundManager begins
 * (Game +0x13cba8).  KAROO_LAYOUT_SIZE asserts both it and the record.
 *
 *   BuildExtraObjectTable  0x423560  ctor: an EH-vector init of each record's
 *                                    spline points, vtable 0x45d450,
 *                                    objectCount = 0, and the sound handle
 *                                    of records 0..254 -- NOT 255 -- zeroed
 *   ParseExtraObjectEntry  0x423700  builds one record per .leo entry; still
 *                                    the game's (leo.cpp's named callback)
 *   OpenExtraObjectsFile   ours: openFile      (leo.cpp)
 *   ReleaseExtraObjectSoundBuffers 0x425210   ours: releaseSounds (levelparse.cpp)
 *
 * The record's fields are named from ParseExtraObjectEntry, which writes
 * every one and logs most of them by name ("Model lit", "Spline mode",
 * "Billboard Size", ...).  Offsets are record-relative: the handler's
 * this-relative offset minus 0xe.
 */
#pragma once

#include <stddef.h>
#include "layout.h"

class SoundManager;
struct CStaticSoundbuffer;

enum ExtraObjectKind {
    EXTRA_MODEL = 0, EXTRA_PARTICLE = 1, EXTRA_BILLBOARD = 2, EXTRA_SOUND = 3
};

struct __attribute__((packed)) ExtraObjectRecord {
    /* The object's file: "<GameDir>\<name>" for a model, particle system
     * or sound (InitLevelBasedSounds acquires a sound by it). */
    char                 file[0x100];
    float                position[3];             /* +0x100 */
    float                field_10c[3];            /* +0x10c  models and particles only */
    char                 animationFile[0x100];    /* +0x118  model; zeroed when none */
    char                 textureFile[0x100];      /* +0x218 */
    int                  lit;                     /* +0x318  model */
    unsigned char        kind;                    /* +0x31c  ExtraObjectKind */
    float                billboardSize;           /* +0x31d */
    unsigned char        gap_321[0x329 - 0x321];
    unsigned int         textureAddress;          /* +0x329 */
    unsigned char        splineMode;              /* +0x32d  0 none, 1 dynamic, 2 static */
    int                  splineTime;              /* +0x32e */
    float                splinePoints[0x100][3];  /* +0x332  built by the ctor */
    unsigned short       splinePointCount;        /* +0xf32 */
    double               soundParam;              /* +0xf34  sound; zeroed, then optional */
    CStaticSoundbuffer  *sound;                   /* +0xf3c */
};

class __attribute__((packed)) ExtraObjects {
public:
    static const int ORIGIN = 0;

    enum { RECORD_MAX = 256, RELEASE_COUNT = 255 };

    /* Parse <name>.leo (leo.cpp). */
    int  openFile(const char *name);
    /* Halt and release each record's sound -- the first 255 only, as the
     * original (levelparse.cpp). */
    void releaseSounds();

    /* Nonzero when the level's .leo loaded (SetupLevelObjects sets it). */
    int            loaded() const                    { return loaded_; }
    void           setLoaded(int l)                  { loaded_ = l; }
    void           setSoundManager(SoundManager *sm) { soundManager_ = sm; }
    /* Objects the entry handler built (a WORD; the report prints it). */
    unsigned short objectCount() const               { return objectCount_; }
    ExtraObjectRecord *record(unsigned int i)        { return &records_[i]; }

private:
    ExtraObjects() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(ExtraObjects);

    const void        *vtable_;                    /* +0x00  0x45d450 */
    int                loaded_;                    /* +0x04 */
    SoundManager      *soundManager_;              /* +0x08 */
    unsigned short     entries_;                   /* +0x0c  entries seen */
    ExtraObjectRecord  records_[RECORD_MAX];       /* +0x0e */
    unsigned short     objectCount_;               /* +0xf400e */
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

/* The record tiles 0xf40 with every field ParseExtraObjectEntry writes. */
static_assert(offsetof(ExtraObjectRecord, position)         == 0x100, "position");
static_assert(offsetof(ExtraObjectRecord, animationFile)    == 0x118, "animationFile");
static_assert(offsetof(ExtraObjectRecord, textureFile)      == 0x218, "textureFile");
static_assert(offsetof(ExtraObjectRecord, lit)              == 0x318, "lit");
static_assert(offsetof(ExtraObjectRecord, kind)             == 0x31c, "kind");
static_assert(offsetof(ExtraObjectRecord, billboardSize)    == 0x31d, "billboardSize");
static_assert(offsetof(ExtraObjectRecord, textureAddress)   == 0x329, "textureAddress");
static_assert(offsetof(ExtraObjectRecord, splineMode)       == 0x32d, "splineMode");
static_assert(offsetof(ExtraObjectRecord, splineTime)       == 0x32e, "splineTime");
static_assert(offsetof(ExtraObjectRecord, splinePoints)     == 0x332, "splinePoints");
static_assert(offsetof(ExtraObjectRecord, splinePointCount) == 0xf32, "splinePointCount");
static_assert(offsetof(ExtraObjectRecord, soundParam)       == 0xf34, "soundParam");
static_assert(offsetof(ExtraObjectRecord, sound)            == 0xf3c, "sound");
static_assert(sizeof(ExtraObjectRecord)                     == 0xf40, "record stride");

/* The exports patch.py binds; shims onto the methods. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Leo_OpenExtraObjectsFile(ExtraObjects *self, const char *name);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Leo_ReleaseExtraObjectSoundBuffers(ExtraObjects *self);
