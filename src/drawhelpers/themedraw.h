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
class Texture;
struct VertexBuffer;
struct ParticlePath;

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
    const Texture   *shadow;   // the shadows' texture
    Vec3             eye;      // billboards and particle systems face
    Vec3             target;   // along target - eye
    Options          opts;
    double           now;      // ms, the frame clock
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
    float    effectAge = -1.0f;  // dying looks: seconds since the death or blast
    uint32_t seed = 0;           // which of a look's effects this is: its debris
                                 // and particles are drawn from it
    const ParticlePath *path = nullptr;  // where it was lately: a particle record
                                         // that moves only its generator emits
                                         // along it
};

/* A field record's quad: position, colour and one UV set. */
typedef Diffuse1Vertex FieldVertex;

/* Sets the device state a pass's draws share, all of it, so no pass depends
 * on what the one before left: depth test on, writing except for the
 * particles; the stencil (the opaque draws mark every pixel with 1, the
 * shadows each darken a marked pixel once); no specular.  Records still set
 * what is theirs -- their depth write, their blend, their texture. */
void beginPass(Frame &f, Pass pass);

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

/* How long a one-shot effect's particle systems emit, from its start. */
const float FIRE_SECONDS = 0.05f;

/* A look's particle-system records at each instance.  They emit all the
 * while on the frame clock; or, for an instance with an effectAge, for
 * FIRE_SECONDS from that many seconds ago. */
void particles(Frame &f, ThemeObjectType look, const Instance *inst, unsigned n = 1);

/* A burst: a look's first particle system, emitting at a point for
 * FIRE_SECONDS from `age` seconds ago, with the given blend or (NULL) the
 * look's own.  `seed` tells it from every other burst. */
void burst(Frame &f, ThemeObjectType look, const Vec3 &at, float age, uint32_t seed,
           const BlendState *blend = nullptr);

/* A trail: a look's first particle system emitting along `path` where it was
 * on, streaming out the way it faced. */
void trail(Frame &f, ThemeObjectType look, const ParticlePath &path);

}  // namespace draw
