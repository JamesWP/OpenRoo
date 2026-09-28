/* Per record: SPECULARENABLE is turned on for its render states if bSpecular
 * is set and video highlights are on, and always turned off again at the end
 * of the record's block.  A particle record (kind 4) ticks its chosen system
 * by dt/1000 once per record, not once per instance.  For every sub-object and
 * every instance, gate_open() then decides whether to bind its texture and
 * blend state; only a particle record goes on to place and render it. */

#include <math.h>
#include "themedraw.h"
#include "theme.h"
#include "levelobject.h"
#include "generators.h"
#include "particles.h"
#include "camera.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "game.h"
#include "levelmap.h"
#include "player.h"
#include "ani.h"

static const float K_PHASE_SCALE = 4.0f;
static const float K_HALF        = 0.5f;
static const float K_ONE         = 1.0f;
static const double K_MS         = 0.001;

enum { THEME_MOVE_TRANSLATE = 1, THEME_MOVE_GENERATOR = 2 };

/* d = a * b in the row-vector convention; m4_mul composes its arguments in the
 * opposite order. */
static void mul(Mat4 *d, const Mat4 *a, const Mat4 *b) { m4_mul(d, b, a); }

/* SceneSubObject::dwVisibilityGate, checked against the player and the
 * instance's tile. */
static bool gate_open(DWORD gate, const Player *p, const Tile *cell)
{
    const bool dead = p->anim() == ANIM_GHOST;
    switch (gate) {
    case 0: return true;
    case 1: return cell->busy() != 0;  // "active": the tile is occupied
    case 2: return cell->busy() == 0;  // "inactive": the tile is not occupied
    case 3: return dead;                                              // "dead" gate
    case 4: return !dead;                                             // "alive" gate
    case 5: return !dead && (p->glides() != 0 || p->gliding() != 0);  // paraglide gate
    case 6: return !dead && p->effectDActive() != 0;                  // "protection" gate
    }
    return false;
}

static void camera_view_dir(float d[3])
{
    const CameraGlobals *c = &g_camera;
    for (int i = 0; i < 3; i++)
        d[i] = c->target()[i] - c->eye()[i];
}

  void  
Theme_DrawParticleObjects(Game *g, void * , const float (*pos)[3],
                          const float (*rot)[3], DWORD count,
                          ThemeObjectTypeSlot *slot, RenderDevice *d3d,
                          double t, double dt, DWORD system)
{
    RenderDevice *dev = d3d;
    const Player *player = g->player();
    LevelMap *map = g->map();

    for (DWORD r = 0; r < slot->instanceCount(); r++) {
        ThemeLevelObject *rec = &slot->records()[r];

        if (rec->specular() != 0 && g->videoHighlights() != 0)
            dev->SetRenderState(RS::SpecularEnable, 1);

        if (rec->kind() == THEME_KIND_PARTICLESYSTEM)
            (rec->particleSystems()[system])->vtick((float)(dt * K_MS));

        for (DWORD s = 0; s < rec->subObjectCount(); s++) {
            // pSubObjects lives in a packed struct; sub is a copy so its
            // fields stay aligned.
            const SceneSubObject sub = rec->subObjects()[s];
            for (DWORD n = 0; n < count; n++) {
                const float *ip = pos[n], *ir = rot[n];
                // PRESERVED: the instance's cell is read with no bounds check,
                // so an instance placed outside the 100x100 grid reads past
                // it.
                const Tile *cell = map->tile((int)ip[0], -(int)ip[2]);
                if (!gate_open(sub.dwVisibilityGate, player, cell))
                    continue;

                dev->SetTexture(0, sub.pTexture);
                if (sub.dwBlendSrc != 0 && sub.dwBlendDst != 0) {
                    dev->SetRenderState(RS::AlphaBlendEnable, 1);
                    dev->SetRenderState(RS::SrcBlend, sub.dwBlendSrc);
                    dev->SetRenderState(RS::DestBlend, sub.dwBlendDst);
                } else {
                    dev->SetRenderState(RS::AlphaBlendEnable, 0);
                }
                if (rec->kind() != THEME_KIND_PARTICLESYSTEM)
                    continue;

                ParticleSystem *ps = rec->particleSystems()[system];

                // The record's offset and spin.
                float p[3] = { rec->posX(), rec->posY(), rec->posZ() };
                float o[3] = { 0.0f, 0.0f, 0.0f };
                if (rec->randomYAngle() != 0)
                    o[1] = cell->itemPhase();
                o[0] = (float)(rec->rotRateX() * t);
                o[1] = (float)(rec->rotRateY() * t + o[1]);
                o[2] = (float)(rec->rotRateZ() * t);

                const float amp = rec->oscillationAmplitude();
                if (!(amp == 0.0f || amp != amp)) {  // zero, or NaN, skips the oscillation
                    double arg = rec->oscillationFrequency() * t;
                    if (rec->oscillateRandom() != 0)
                        arg += (double)cell->itemPhase() * K_PHASE_SCALE;
                    p[1] = (float)(sin(arg) * amp + p[1]);
                }

                float scale[3] = { 1.0f, 1.0f, 1.0f };
                const float rate = rec->pump()[3];
                if (!(rate == 0.0f || rate != rate)) {
                    double w = (sin(t * rate) + K_ONE) * K_HALF;
                    for (int i = 0; i < 3; i++)
                        scale[i] = (float)(w * rec->pump()[i] + K_ONE);
                }

                float view[3];
                Mat4 world;
                const DWORD move = rec->movableType();
                // PRESERVED: movable types 1 and 2 place the generator through
                // ps->getGenerator(NULL), which returns NULL for a
                // system with none; the position write then derefs a null
                // pointer.
                if (move == THEME_MOVE_TRANSLATE) {
                    m4_translate(&world, ip[0], ip[1], ip[2]);
                    (ps->getGenerator(NULL))->vsetPosition(p[0], p[1], p[2]);
                    camera_view_dir(view);
                    ps->vsetVector(view[0], view[1], view[2]);
                    Mat4 corners;
                    m4_rot_y(&corners, (float)-((double)o[1] + ir[1]));
                    ps->vtransformCorners(corners.m);
                } else if (move == THEME_MOVE_GENERATOR) {
                    m4_identity(&world);
                    (ps->getGenerator(NULL))->vsetPosition(p[0] + ip[0], p[1] + ip[1], p[2] + ip[2]);
                    camera_view_dir(view);
                    ps->vsetVector(view[0], view[1], view[2]);
                } else {
                    Mat4 sc, rx, ry, rz, tr, a, b;
                    m4_identity(&sc);
                    sc.m[0] = scale[0];  sc.m[5] = scale[1];  sc.m[10] = scale[2];
                    m4_rot_x(&rx, (float)((double)o[0] + ir[0]));
                    m4_rot_y(&ry, (float)((double)o[1] + ir[1]));
                    m4_rot_z(&rz, (float)((double)o[2] + ir[2]));
                    m4_translate(&tr, p[0] + ip[0], p[1] + ip[1], p[2] + ip[2]);
                    mul(&a, &sc, &rx);
                    mul(&b, &a, &ry);
                    mul(&a, &b, &rz);
                    mul(&world, &a, &tr);
                    camera_view_dir(view);
                    ps->vsetVector(view[0], view[1], view[2]);
                    Mat4 corners;
                    m4_rot_y(&corners, (float)-((double)o[1] + ir[1]));
                    ps->vtransformCorners(corners.m);
                }

                dev->SetTransform(Transform::World, &world);
                ps->vrender(dev);
            }
        }
        dev->SetRenderState(RS::SpecularEnable, 0);
    }
}
