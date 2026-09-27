/* The animation table ani.cpp loads from a .ani file.
 *
 * THE SLOT.  Four dwords, and the shipped content says what they are.  Every
 * .ani in the tree carries the author's own header comment:
 *
 *     // Synatx eines Eintrages im ani-File:
 *     // -----------------------------------
 *     // Name    FirstFrame    NumFrames    FPS    Flag
 *
 * which matches the loader (token[1..3] -> +0x0, +0x4, +0x8) and matches
 * every consumer: +0x4 is tested for zero as "this animation is absent", it
 * is the modulus of the looping frame index, and +0x0 is the base the phase
 * is measured from.  `Flag` is the fifth token; the only value the loader
 * recognises is "r", which sets +0xc and plays the range backwards.
 *
 * THE TABLE.  24 slots, one per keyword, 0x180 bytes.  It is never allocated
 * on its own: it is embedded, unaligned, in the two objects that animate.
 *
 *   - the type-0x00 scene object, at +0x11, loaded by BuildSceneObjectList
 *     with +0xd set to 1 on success.  +0x11 + 0x180 = +0x191,
 *     which is exactly where the object's position begins (dsoscene.cpp's
 *     O_POS) -- the table tiles the gap.
 *   - ThemeLevelObject (stride 0x5dd, theme.h), at +0xc1, loaded by
 *     ThemeFileLoader.
 *
 * Both bases are odd addresses, so every field is potentially unaligned; the
 * struct is packed and must only ever be reached through a pointer.
 *
 * NOMOVESTATES.  ThemeLevelObject::bNoMoveStates (+0x5a9) is a flag
 * ThemeFileLoader sets from the .thm keyword "nomovestates".  It selects
 * which of the two evaluators below a model gets, and it is the whole reason
 * there are two.
 */
#pragma once

#include <stddef.h>
#include "layout.h"

class GameLogger;

/* One keyword's entry.  Named from the .ani header comment quoted above. */
struct __attribute__((packed)) AnimSlot {
    static const int ORIGIN = 0;

    int firstFrame;   /* +0x00  first mesh frame of the range            */
    int numFrames;    /* +0x04  frames in it; 0 means "no animation"     */
    int fps;          /* +0x08  playback rate, frames per second         */
    int reverse;      /* +0x0c  the "r" flag: play the range backwards   */

    KAROO_LAYOUT_REGISTER(AnimSlot);
};

KAROO_LAYOUT_CHECKS(AnimSlot)
{
    KAROO_LAYOUT_AT(firstFrame, 0x0);
    KAROO_LAYOUT_AT(numFrames,  0x4);
    KAROO_LAYOUT_AT(fps,        0x8);
    KAROO_LAYOUT_AT(reverse,    0xc);
    KAROO_LAYOUT_SIZE(0x10);
}

/* The 24 slots, in ADDRESS order.  The loader's compare chain visits four of
 * them out of sequence (ice, jump, fall, glue, ghost); that is a property of
 * the keyword search, not of the table, and it lives in ani.cpp.
 *
 * Five slots -- the four speed_/slow_ ones and celebration -- have no
 * animation code, so LookupAnimDescriptor can never return them.  They are
 * kept because the table's shape is the file's: the .ani files all carry the
 * keywords (bare, with no numbers, in every shipped file). */
struct __attribute__((packed)) AnimTable {
    static const int ORIGIN = 0;

    AnimSlot walkForward;      /* +0x000 */
    AnimSlot walkBackward;     /* +0x010 */
    AnimSlot speedForward;     /* +0x020  unreachable: no code */
    AnimSlot speedBackward;    /* +0x030  unreachable: no code */
    AnimSlot slowForward;      /* +0x040  unreachable: no code */
    AnimSlot slowBackward;     /* +0x050  unreachable: no code */
    AnimSlot celebration;      /* +0x060  unreachable: no code */
    AnimSlot jump;             /* +0x070 */
    AnimSlot glue;             /* +0x080 */
    AnimSlot ghost;            /* +0x090 */
    AnimSlot ice;              /* +0x0a0 */
    AnimSlot fall;             /* +0x0b0 */
    AnimSlot paraglide;        /* +0x0c0 */
    AnimSlot slide;            /* +0x0d0 */
    AnimSlot idle1;            /* +0x0e0 */
    AnimSlot idle2;            /* +0x0f0 */
    AnimSlot fieldStairUp;     /* +0x100 */
    AnimSlot fieldStairDown;   /* +0x110 */
    AnimSlot stairStairUp;     /* +0x120 */
    AnimSlot stairStairDown;   /* +0x130 */
    AnimSlot stairFieldUp;     /* +0x140 */
    AnimSlot stairFieldDown;   /* +0x150 */
    AnimSlot turnLeft;         /* +0x160 */
    AnimSlot turnRight;        /* +0x170 */

    KAROO_LAYOUT_REGISTER(AnimTable);
};

KAROO_LAYOUT_CHECKS(AnimTable)
{
    KAROO_LAYOUT_AT(walkForward,    0x000);
    KAROO_LAYOUT_AT(jump,           0x070);
    KAROO_LAYOUT_AT(glue,           0x080);
    KAROO_LAYOUT_AT(ghost,          0x090);
    KAROO_LAYOUT_AT(ice,            0x0a0);
    KAROO_LAYOUT_AT(fall,           0x0b0);
    KAROO_LAYOUT_AT(paraglide,      0x0c0);
    KAROO_LAYOUT_AT(slide,          0x0d0);
    KAROO_LAYOUT_AT(idle1,          0x0e0);
    KAROO_LAYOUT_AT(idle2,          0x0f0);
    KAROO_LAYOUT_AT(fieldStairUp,   0x100);
    KAROO_LAYOUT_AT(stairFieldDown, 0x150);
    KAROO_LAYOUT_AT(turnLeft,       0x160);
    KAROO_LAYOUT_AT(turnRight,      0x170);
    KAROO_LAYOUT_SIZE(0x180);
}

/* The animation codes, as LookupAnimDescriptor dispatches them.
 * These are the values MovableEntity carries in anim_ (+0x9a; see
 * movableentity.h), which is why 0xfa/0xfb are already spelled there. */
enum AnimCode {
    ANIM_ICE               = 0x03,
    ANIM_SLIDE             = 0x04,
    ANIM_PARAGLIDE         = 0x05,
    ANIM_FALL              = 0x08,
    ANIM_GLUE              = 0x09,
    ANIM_GHOST             = 0x0a,
    ANIM_JUMP              = 0x0b,
    ANIM_WALK_FORWARD      = 0x14,
    ANIM_WALK_BACKWARD     = 0x15,
    ANIM_FIELD_STAIR_UP    = 0x16,
    ANIM_FIELD_STAIR_DOWN  = 0x17,
    ANIM_STAIR_STAIR_UP    = 0x18,
    ANIM_STAIR_STAIR_DOWN  = 0x19,
    ANIM_STAIR_FIELD_UP    = 0x1a,
    ANIM_STAIR_FIELD_DOWN  = 0x1b,
    ANIM_TURN_RIGHT        = 0x1e,
    ANIM_TURN_LEFT         = 0x1f,
    ANIM_IDLE1             = 0xfa,
    ANIM_IDLE2             = 0xfb
};

/* ani.cpp: __cdecl, and a null descriptor for a code the table has no slot
 * for. */
extern "C" __declspec(dllexport) int __cdecl
Ani_LoadAnimationFile(AnimTable *dest, const char *path, GameLogger *logger);

extern "C" __declspec(dllexport) AnimSlot * __cdecl
Ani_LookupAnimDescriptor(AnimTable *table, unsigned int code);

/* ── The two evaluators ───────────────────────────────────────────────────
 *
 * Both take a slot and return a mesh frame index.  Which one a model gets is
 * decided by ThemeLevelObject::bNoMoveStates ("nomovestates"),, and the call sites are
 * pairwise identical in RenderSceneObjects and DrawObjectShadows.
 * dsoscene.cpp open-codes the clock form as fmod(v, numFrames) rather than
 * fmod(v/n, 1)*n. */

/* The clock form: the model has no movement states, so its single
 * walk_forward range runs continuously off the millisecond clock. */
int Anim_FrameOnClock(const AnimSlot *slot, double timeMs);

/* The phase form: the entity's own animation state supplies a phase in
 * [0,1] across the slot's range, forwards or (with "r") backwards.  No
 * shipped .ani sets the flag, so the reverse arm is dead for the shipped
 * content. */
int Anim_FrameAtPhase(const AnimSlot *slot, float phase);
