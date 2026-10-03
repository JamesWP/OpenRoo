/* The animation table ani.cpp loads from a .ani file.
 *
 * THE SLOT.  Four dwords, and the shipped content says what they are.  Every
 * .ani in the tree carries the author's own header comment:
 *
 *     // Synatx eines Eintrages im ani-File:
 *     // -----------------------------------
 *     // Name    FirstFrame    NumFrames    FPS    Flag
 *
 which matches the loader (tokens 1..3 are the first three fields) and
 * matches every consumer: the frame count is tested for zero as "this
 * animation is absent", it is the modulus of the looping frame index, and the
 * first frame is the base the phase is measured from.  `Flag` is the fifth
 * token; the only value the loader recognises is "r", which sets the reverse
 * flag and plays the range backwards.
 *
 * THE TABLE.  24 slots, one per keyword.  It is never allocated on its own: it
 * is embedded in the two objects that animate: the type-0x00 scene object,
 * loaded by BuildSceneObjectList, and ThemeLevelObject (theme.h), loaded by
 * ThemeFileLoader.
 *
 * NOMOVESTATES.  ThemeLevelObject::bNoMoveStates is a flag
 * ThemeFileLoader sets from the .thm keyword "nomovestates".  It selects
 * which of the two evaluators below a model gets, and it is the whole reason
 * there are two.
 */
#pragma once

#include <stddef.h>
 


/* One keyword's entry.  Named from the .ani header comment quoted above. */
class AnimSlot {
public:
     

    /* The clock form: the model has no movement states, so its single
     * walk_forward range runs continuously off the millisecond clock. */
    static int frameOnClock(const AnimSlot *slot, double timeMs);

    /* The phase form: the entity's own animation state supplies a phase in
     * [0,1] across the slot's range, forwards or (with "r") backwards.  No
     * shipped .ani sets the flag, so the reverse arm is dead for the shipped
     * content. */
    static int frameAtPhase(const AnimSlot *slot, float phase);

    int firstFrame() const { return firstFrame_; }
    int numFrames() const { return numFrames_; }
    int fps() const { return fps_; }
    int reverse() const { return reverse_; }

private:
    friend class AnimTable;  // the loader fills the slots
    int firstFrame_;   /* first mesh frame of the range            */
    int numFrames_;    /* frames in it; 0 means "no animation"     */
    int fps_;          /* playback rate, frames per second         */
    int reverse_;      /* the "r" flag: play the range backwards   */
     
};

/* The 24 slots, in ADDRESS order.  The loader's compare chain visits four of
 * them out of sequence (ice, jump, fall, glue, ghost); that is a property of
 * the keyword search, not of the table, and it lives in ani.cpp.
 *
 * Five slots -- the four speed_/slow_ ones and celebration -- have no
 * animation code, so LookupAnimDescriptor can never return them.  They are
 * kept because the table's shape is the file's: the .ani files all carry the
 * keywords (bare, with no numbers, in every shipped file). */
class AnimTable {
public:
     

    /* Fills the table from a .ani file. */
    int load(const char *path);

    /* The slot for an animation code, or NULL for a code the table has no
     * slot for. */
    AnimSlot *lookup(unsigned int code);

private:
    AnimSlot walkForward_;      
    AnimSlot walkBackward_;     
    AnimSlot speedForward_;    
    AnimSlot speedBackward_;  
    AnimSlot slowForward_;   
    AnimSlot slowBackward_; 
    AnimSlot celebration_; 
    AnimSlot jump_;       
    AnimSlot glue_;      
    AnimSlot ghost_;    
    AnimSlot ice_;    
    AnimSlot fall_;  
    AnimSlot paraglide_;        
    AnimSlot slide_;           
    AnimSlot idle1_;          
    AnimSlot idle2_;         
    AnimSlot fieldStairUp_; 
    AnimSlot fieldStairDown_;   
    AnimSlot stairStairUp_;    
    AnimSlot stairStairDown_; 
    AnimSlot stairFieldUp_;     
    AnimSlot stairFieldDown_;  
    AnimSlot turnLeft_;       
    AnimSlot turnRight_;     
     
};

/* The animation codes, as LookupAnimDescriptor dispatches them.
 * These are the values MovableEntity carries in anim_ (see
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

/* ── The two evaluators ───────────────────────────────────────────────────
 *
 * Both take a slot and return a mesh frame index.  Which one a model gets is
 * decided by ThemeLevelObject::bNoMoveStates ("nomovestates"),, and the call sites are
 * pairwise identical in RenderSceneObjects and DrawObjectShadows.
 * dsoscene.cpp open-codes the clock form as fmod(v, numFrames) rather than
 * fmod(v/n, 1)*n. */

