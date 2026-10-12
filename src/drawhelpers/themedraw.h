/* Drawing theme objects.  A theme (theme.h) gives each kind of object in the
 * level -- the player, a foe, a switch, a crystal -- a "look": up to eight
 * records, each a model, a field (a textured quad), a billboard or a particle
 * system, with up to eight sub-objects of texture, blend and visibility each.
 * Everything here draws a look at a list of instances by the one walk every
 * look shares: record, then sub-object, then instance.
 *
 * Nothing here knows the game.  What a draw depends on -- where the instances
 * stand, how far through an animation they are, which of the theme's
 * visibility conditions hold for them -- is the caller's to say.
 */
#pragma once
#include <stdint.h>
#include "vecmath.h"
#include "rendertypes.h"
#include "theme.h"

class RenderDevice;
struct VertexBuffer;

namespace draw {

/* The frame's passes, in the order the sequencer runs them.  Each pass sets
 * the device state its draws share (the shadows' stencil, the particles'
 * depth write); an object draws whatever it has for the pass it is given. */
enum class Pass {
    Opaque,      // the models, fields and billboards
    Effects,     // overlays drawn after the scene objects: bridges, auras
    Shadows,     // planar stencil shadows
    Particles,   // particle systems and bursts
};

/* The video options the draws honour. */
struct Options {
    bool reflection;   // environment-mapped sub-objects
    bool highlights;   // specular records
    int  shadows;      // 0 none, 1 the actors, 2 the items too
    int  particles;    // 0 none, 1 effects, 2 pickups and the speed trail too
};

/* What every draw needs of the frame. */
struct Frame {
    RenderDevice    *dev;
    ThemeAssetBlock *theme;    // the looks; drawing advances their animators
    Vec3             eye;      // billboards and particle systems face
    Vec3             target;   // along target - eye
    Options          opts;
    double           now;      // ms, the frame clock
    double           elapsed;  // ms since the previous frame
};

/* The visibility conditions a theme sub-object can name (the theme file's
 * `condition`).  The caller says which hold for an instance -- an object's
 * own state, or its cell's -- and a gated sub-object draws only where its
 * condition does. */
enum Condition : uint8_t {
    COND_ACTIVE    = 1 << 0,   // `active` (set) / `inactive` (clear): the cell is busy
    COND_DEAD      = 1 << 1,   // `dead` (set) / `alive` (clear)
    COND_GLIDING   = 1 << 2,   // `paraglide`
    COND_PROTECTED = 1 << 3,   // `protection`
};

/* One placement of a look. */
struct Instance {
    Vec3     pos{};              // (u, height, -v)
    Vec3     rot{};              // radians about X, Y and Z
    float    phase = 0.0f;       // a per-placement random phase, offsetting
                                 // the records that spin or bob at random
    uint8_t  conditions = 0;     // the Condition bits that hold here
    uint16_t animCode = 0;       // models: the animation to play (AnimCode)
    float    animTime = 0.0f;    // and how far through it, 0..1
};

/* A field record's quad: position, colour and one UV set. */
typedef Diffuse1Vertex FieldVertex;

/* A look's model, billboard and field records at each instance.  A field
 * record draws the unit tile-top quad at each instance's position, animated
 * by its sub-object's effect; or, given quads, instance i's quads[i] as they
 * are (the caller owns their placement and any animation). */
void objects(Frame &f, ThemeObjectType look, const Instance *inst, unsigned n = 1,
             const FieldVertex (*quads)[4] = nullptr);

/* A look's field records over a prebuilt vertex buffer of LitVertex
 * triangles (the level's floor and walls). */
void fieldBuffer(Frame &f, ThemeObjectType look, const VertexBuffer *vb, unsigned vertices);

/* A look's model records as planar shadows on the ground under each
 * instance.  The caller sets the stencil state. */
void shadows(Frame &f, ThemeObjectType look, const Instance *inst, unsigned n = 1);

/* A look's particle-system records at each instance. */
void particles(Frame &f, ThemeObjectType look, const Instance *inst, unsigned n = 1);

/* A dying look's effects: while fire is set its particle systems emit, and
 * the frame it is set its exploding records start their debris. */
void fireEffects(Frame &f, ThemeObjectType look, bool fire);

/* Bursts: one-shot particle effects that play at a point for a second.  A
 * look whose first record is a particle system holds a burst per system;
 * spawnBurst starts the first free one, bursts() draws the live ones, with
 * the given blend or (NULL) the look's own. */
void spawnBurst(Frame &f, ThemeObjectType look, const Vec3 &at);
void bursts(Frame &f, ThemeObjectType look, const BlendState *blend = nullptr);

/* A trail: a look's first particle system emitting from `at`, streaming out
 * behind something facing `yaw`, while on.  Drawn every frame either way, so
 * what it has emitted plays out. */
void trail(Frame &f, ThemeObjectType look, bool on, const Vec3 &at, float yaw);

}  // namespace draw
