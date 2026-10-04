/* Shadow projection for one theme object type's placed instances: for each
 * non-hidden record, each of its sub-objects that gates open, and each
 * placement, the record's mesh (or its explode debris) is drawn a second time,
 * flattened onto the ground plane through its own position and lifted clear of
 * it.  A record with more than one open sub-object therefore casts its shadow
 * once per open sub-object. */

#include <stdint.h>
#include <math.h>
#include "objectshadows.h"
#include "game.h"
#include "player.h"
#include "levelmap.h"
#include "tile.h"
#include "theme.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "ani.h"
#include "faktmesh.h"
#include "explodedebris.h"

static const float  K_HALF_PI    = 1.5707963705062866f;
static const float  K_LIGHT_OFF  = 1000.0f;
static const float  K_LIFT       = 0.02f;
static const float  K_PHASE_MUL  = 4.0f;
static const float  K_DEBRIS_MS  = 0.001f;

/* Row-vector composition: m4_mul's own argument order is (dst, right, left).
 */
static void compose(Mat4 *d, const Mat4 *left, const Mat4 *right)
{
    m4_mul(d, right, left);
}

static bool condition_holds(uint32_t gate, const Game *g, const Tile *cell)
{
    const Player *p = g->player();
    switch (gate) {
    case 0:  return true;
    case 1:  return cell->busy() != 0;
    case 2:  return cell->busy() == 0;
    case 3:  return p->anim() == 10;
    case 4:  return p->anim() != 10;
    case 6:  return p->effectDActive() != 0;
    case 5:  return p->glides() != 0 || p->gliding() != 0;
    default: return false;
    }
}

/* Planar shadow matrix for a point light 1000 units off along (-1, +1, -1)
 * from the record's position, and the ground plane through that same position.
 * The zero coefficients are written as -0.0f to match every other entry's sign
 * convention. */
static void shadow_matrix(Mat4 *m, const float *pos)
{
    float lx = pos[0] - K_LIGHT_OFF;
    float ly = pos[1] + K_LIGHT_OFF;
    float lz = pos[2] - K_LIGHT_OFF;
    float d  = -pos[1];
    float dot = ly;

    m->m[0]  = dot;      m->m[1]  = -0.0f;     m->m[2]  = -0.0f;    m->m[3]  = 0.0f;
    m->m[4]  = -lx;      m->m[5]  = dot - ly;  m->m[6]  = -lz;      m->m[7]  = -1.0f;
    m->m[8]  = -0.0f;    m->m[9]  = -0.0f;     m->m[10] = dot;      m->m[11] = 0.0f;
    m->m[12] = -(d * lx); m->m[13] = -(ly * d); m->m[14] = -(lz * d); m->m[15] = dot - d;
}

static int animation_frame(ThemeLevelObject *rec, double t, float phase,
                           unsigned int animKey)
{
    if (rec->noMoveStates() != 0) {
        // No frame-count guard here: a zero-frame slot still draws frame 0,
        // which DrawMeshBuffer clamps an out-of-range frame to anyway.
        AnimSlot *s = rec->animTable().lookup(0x14);
        return s != NULL ? AnimSlot::frameOnClock(s, t) : 0;
    }
    AnimSlot *s = rec->animTable().lookup(animKey);
    if (s == NULL || s->numFrames() == 0)
        return 0;
    return AnimSlot::frameAtPhase(s, phase);
}

  void  
Shadows_DrawObjectShadows(Game *game, LevelPlacements *,
                          const float *pos, const float *rot, unsigned int count,
                          ThemeObjectTypeSlot *slot, RenderDevice *d3d, double t,
                          float phase, unsigned int animKey, unsigned int debrisMs)
{
    RenderDevice *dev = d3d;
    dev->SetSpecular(false);

    for (uint32_t i = 0; i < slot->instanceCount(); i++) {
        ThemeLevelObject *rec = &slot->records()[i];
        if (rec->noShadow() != 0)
            continue;
        DepthState depth = dev->depth();
        depth.write = rec->noZWrite() == 0;
        dev->SetDepth(depth);

        for (uint32_t s = 0; s < rec->subObjectCount(); s++) {
            for (unsigned int k = 0; k < count; k++) {
                const float *p = pos + k * 3;
                const float *r = rot + k * 3;
                int u = (int)p[0], v = -(int)p[2];
                const Tile *cell = game->map()->tile(u, v);

                if (!condition_holds(rec->subObjects()[s].dwVisibilityGate, game, cell))
                    continue;
                if (rec->kind() != THEME_KIND_MODEL)
                    continue;

                float cellPhase = rec->randomYAngle() != 0 ? cell->itemPhase() : 0.0f;
                float angX = (float)(t * rec->rotRateX());
                float angY = (float)(t * rec->rotRateY() + cellPhase);
                float angZ = (float)(t * rec->rotRateZ());

                float y = rec->posY();
                if (rec->oscillationAmplitude() != 0.0f) {
                    double a = rec->oscillationFrequency() * t;
                    if (rec->oscillateRandom() != 0)
                        a += cell->itemPhase() * K_PHASE_MUL;
                    y = (float)(sin(a + rec->oscillationPhase())
                                * rec->oscillationAmplitude() + y);
                }

                float sx = rec->scaleX(), sy = rec->scaleY(), sz = rec->scaleZ();
                if (rec->pump()[3] != 0.0f) {
                    double pump = (sin(rec->pump()[3] * t) + 1.0) * 0.5;
                    sx = (float)(pump * rec->pump()[0]) + sx;
                    sy = (float)(pump * rec->pump()[1]) + sy;
                    sz = (float)(pump * rec->pump()[2] + sz);
                }

                Mat4 world, m, tmp;
                m4_identity(&world);
                world.m[0] = sx;  world.m[5] = sy;  world.m[10] = sz;

                m4_rot_x_biased(&m, angX + r[0], K_HALF_PI);
                compose(&tmp, &world, &m);
                m4_rot_y(&m, angY + r[1]);
                compose(&world, &tmp, &m);
                m4_rot_z(&m, angZ + r[2]);
                compose(&tmp, &world, &m);
                m4_translate(&m, rec->posX() + p[0], y + p[1], rec->posZ() + p[2]);
                compose(&world, &tmp, &m);
                shadow_matrix(&m, p);
                compose(&tmp, &world, &m);
                m4_translate(&m, 0.0f, K_LIFT, 0.0f);
                compose(&world, &tmp, &m);

                dev->SetWorld(world);

                int frame = animation_frame(rec, t, phase, animKey);
                if (rec->explodes() == 0) {
                    rec->mesh()->drawMeshBuffer(dev, (uint32_t)frame);
                } else {
                    rec->explodeDebris().advance((float)(unsigned long long)debrisMs * K_DEBRIS_MS);
                    rec->explodeDebris().draw(dev);
                }
            }
        }
    }
}
