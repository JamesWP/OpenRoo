/* ExtraObjects -- the level's extra 3D objects (.leo), embedded in Game at
 * +0x48b98 (COHESION_PLAN.md Band 3 follow-on; the eighth sub-object;
 * Ghidra's Level3DExtraObjects).
 *
 * 0xf4010 bytes: 256 records of 0xf40 from +0xe end at the object count
 * +0xf400e, and the object ends exactly where the SoundManager begins
 * (Game +0x13cba8).  KAROO_LAYOUT_SIZE asserts both it and the record.
 *
 *   BuildExtraObjectTable  0x423560  ours: construct.  An EH-vector init of
 *                                    each record's spline points (element
 *                                    ctor 0x424de0 is `mov eax,ecx; ret`,
 *                                    so a no-op), the vtable, objectCount =
 *                                    0, and the sound handle of records
 *                                    0..254 -- NOT 255 -- zeroed
 *   dtor 0x4235e0 / scalar 0x4235c0  ours: destruct / Leo_ScalarDestructor.
 *                                    The vtable is OURS, one slot; the
 *                                    game's 0x45d450 is left as a tripwire
 *   ParseExtraObjectEntry  0x423700  ours: parseEntry, one record per entry
 *                                    (extraobjects.cpp, with its three helpers)
 *   OpenExtraObjectsFile   ours: openFile      (extraobjects.cpp)
 *   ReleaseExtraObjectSoundBuffers 0x425210   ours: releaseSounds (extraobjects.cpp)
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
    static const int ORIGIN = 0;

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
    int                  srcBlend;                /* +0x321  D3DBLEND 1..13, 0 = NONE/unknown */
    int                  destBlend;               /* +0x325  written only when srcBlend != 0 */
    unsigned int         textureAddress;          /* +0x329 */
    unsigned char        splineMode;              /* +0x32d  0 none, 1 dynamic, 2 static */
    int                  splineTime;              /* +0x32e */
    float                splinePoints[0x100][3];  /* +0x332  built by the ctor */
    unsigned short       splinePointCount;        /* +0xf32 */
    double               soundParam;              /* +0xf34  sound; zeroed, then optional */
    CStaticSoundbuffer  *sound;                   /* +0xf3c */

    KAROO_LAYOUT_REGISTER(ExtraObjectRecord);
};

/* The record tiles 0xf40 with every field ParseExtraObjectEntry writes. */
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

    /* BuildExtraObjectTable 0x423560 / the dtor body 0x4235e0. */
    void construct();
    void destruct();
    /* Parse <name>.leo. */
    int  openFile(const char *name);
    /* Halt and release each record's sound -- the first 255 only, as the
     * original. */
    void releaseSounds();
    /* Sounds releaseSounds has released so far (KAROO_LEVELPARSE_DIAG). */
    static unsigned releasedCount();

    /* Nonzero when the level's .leo loaded (SetupLevelObjects sets it). */
    int            loaded() const                    { return loaded_; }
    void           setLoaded(int l)                  { loaded_ = l; }
    void           setSoundManager(SoundManager *sm) { soundManager_ = sm; }
    /* Objects the entry handler built (a WORD; the report prints it). */
    unsigned short objectCount() const               { return objectCount_; }
    ExtraObjectRecord *record(unsigned int i)        { return &records_[i]; }

private:
    ExtraObjects() = delete;   /* game-owned; only ever reached by pointer */
    void recDump(const char *path);
    /* ParseExtraObjectEntry 0x423700 and its helpers (extraobjects.cpp). */
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
    /* The record being built: re-read at every use, as the original does --
     * it matters once, where a Billboard bumps the count mid-entry. */
    ExtraObjectRecord *current() { return &records_[objectCount_]; }
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

/* The exports patch.py binds; shims onto the methods. */
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
