/* themedraw.cpp -- see themedraw.h.
 *
 * For each record of the slot:
 *   - SPECULARENABLE on if the record asks (bSpecular) and the video
 *     "highlights" option is on; off again after the record, unconditionally.
 *   - A particle record (kind 4) ticks its chosen system once, by dt/1000.
 *   - For each sub-object, for each instance: the sub-object's visibility
 *     gate (below); if it passes, its texture (or none) and blend state are
 *     set -- for EVERY record kind, though only kind 4 then draws.
 *
 * A particle instance is placed from the record's offset p (flPos, plus a
 * sine oscillation on y), its spin o = flRotRate * t (plus the cell's item
 * phase on y when bRandomYAngle), a pump scale, and the instance's pos/rot:
 *   movable type 1: generator at p, world = T(pos), corners RotY(-(o.y+rot.y))
 *   movable type 2: generator at p + pos, world = identity, corners untouched
 *   otherwise:      world = Scale.RotX(o.x+rot.x).RotY(o.y+rot.y)
 *                         .RotZ(o.z+rot.z).T(p + pos),
 *                   corners RotY(-(o.y+rot.y)); the generator is not moved
 * then the system's vector is set to the view direction and it renders.
 *
 * The instance's cell is (trunc(pos.x), -trunc(pos.z)), read with no bounds
 * check, as the original does.  A particle record whose system has no
 * generator calls through NULL in types 1 and 2, as the original does.
 */
#include <math.h>
#include "themedraw.h"
#include "theme.h"
#include "levelobject.h"
#include "generators.h"
#include "particles.h"
#include "camera.h"
#include "direct3d.h"
#include "d3dmath.h"
#include "game.h"
#include "levelmap.h"
#include "player.h"
#include "ani.h"

static const float K_PHASE_SCALE = 4.0f;    /* 0x45d370 */
static const float K_HALF        = 0.5f;    /* 0x45d318 */
static const float K_ONE         = 1.0f;    /* 0x45d298 */
static const double K_MS         = 0.001;   /* 0x45d368 */

enum { THEME_MOVE_TRANSLATE = 1, THEME_MOVE_GENERATOR = 2 };

/* d = a * b, row-vector convention (m4_mul takes the original's order). */
static void mul(Mat4 *d, const Mat4 *a, const Mat4 *b) { m4_mul(d, b, a); }

/* SceneSubObject::dwVisibilityGate against the player and the cell. */
static bool gate_open(DWORD gate, const Player *p, const Tile *cell)
{
    const bool dead = p->anim() == ANIM_GHOST;
    switch (gate) {
    case 0: return true;
    case 1: return cell->busy() != 0;                  /* "active"     */
    case 2: return cell->busy() == 0;                  /* "inactive"   */
    case 3: return dead;                               /* "dead"       */
    case 4: return !dead;                              /* "alive"      */
    case 5: return !dead && (p->glides() != 0 || p->gliding() != 0);  /* paraglide */
    case 6: return !dead && p->effectDActive() != 0;   /* "protection" */
    }
    return false;
}

static void camera_view_dir(float d[3])
{
    const CameraGlobals *c = GG_CAMERA;
    for (int i = 0; i < 3; i++)
        d[i] = c->target[i] - c->eye[i];
}

extern "C" __declspec(dllexport) void __cdecl
Theme_DrawParticleObjects(Game *g, void * /*unused*/, const float (*pos)[3],
                          const float (*rot)[3], DWORD count,
                          ThemeObjectTypeSlot *slot, Direct3D *d3d,
                          double t, double dt, DWORD system)
{
    IDirect3DDevice3 *dev = d3d->pDevice;
    const Player *player = g->player();
    LevelMap *map = g->map();

    for (DWORD r = 0; r < slot->dwInstanceCount; r++) {
        ThemeLevelObject *rec = &slot->records[r];

        if (rec->bSpecular != 0 && g->videoHighlights() != 0)
            dev->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 1);

        if (rec->kind == THEME_KIND_PARTICLESYSTEM)
            ps_vtick(rec->pParticleSystems[system], (float)(dt * K_MS));

        for (DWORD s = 0; s < rec->dwSubObjectCount; s++) {
            const SceneSubObject *sub = &rec->pSubObjects[s];
            for (DWORD n = 0; n < count; n++) {
                const float *ip = pos[n], *ir = rot[n];
                const Tile *cell = map->tile((int)ip[0], -(int)ip[2]);
                if (!gate_open(sub->dwVisibilityGate, player, cell))
                    continue;

                dev->SetTexture(0, sub->pTexture ? sub->pTexture->pTexture2 : NULL);
                if (sub->dwBlendSrc != 0 && sub->dwBlendDst != 0) {
                    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
                    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, sub->dwBlendSrc);
                    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, sub->dwBlendDst);
                } else {
                    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
                }
                if (rec->kind != THEME_KIND_PARTICLESYSTEM)
                    continue;

                ParticleSystem *ps = rec->pParticleSystems[system];

                /* The record's offset and spin. */
                float p[3] = { rec->flPosX, rec->flPosY, rec->flPosZ };
                float o[3] = { 0.0f, 0.0f, 0.0f };
                if (rec->bRandomYAngle != 0)
                    o[1] = cell->itemPhase();
                o[0] = (float)(rec->flRotRateX * t);
                o[1] = (float)(rec->flRotRateY * t + o[1]);
                o[2] = (float)(rec->flRotRateZ * t);

                const float amp = rec->flOscillationAmplitude;
                if (!(amp == 0.0f || amp != amp)) {       /* FCOMP: equal or NaN skips */
                    double arg = rec->flOscillationFrequency * t;
                    if (rec->bOscillateRandom != 0)
                        arg += (double)cell->itemPhase() * K_PHASE_SCALE;
                    p[1] = (float)(sin(arg) * amp + p[1]);
                }

                float scale[3] = { 1.0f, 1.0f, 1.0f };
                const float rate = rec->flPump[3];
                if (!(rate == 0.0f || rate != rate)) {
                    double w = (sin(t * rate) + K_ONE) * K_HALF;
                    for (int i = 0; i < 3; i++)
                        scale[i] = (float)(w * rec->flPump[i] + K_ONE);
                }

                float view[3];
                Mat4 world;
                const DWORD move = rec->dwMovableType;
                if (move == THEME_MOVE_TRANSLATE) {
                    m4_translate(&world, ip[0], ip[1], ip[2]);
                    gen_vset_position(Particle_GetGenerator(ps, NULL), p[0], p[1], p[2]);
                    camera_view_dir(view);
                    ps_vset_vector(ps, view[0], view[1], view[2]);
                    Mat4 corners;
                    m4_rot_y(&corners, (float)-((double)o[1] + ir[1]));
                    ps_vtransform_corners(ps, corners.m);
                } else if (move == THEME_MOVE_GENERATOR) {
                    m4_identity(&world);
                    gen_vset_position(Particle_GetGenerator(ps, NULL),
                                      p[0] + ip[0], p[1] + ip[1], p[2] + ip[2]);
                    camera_view_dir(view);
                    ps_vset_vector(ps, view[0], view[1], view[2]);
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
                    ps_vset_vector(ps, view[0], view[1], view[2]);
                    Mat4 corners;
                    m4_rot_y(&corners, (float)-((double)o[1] + ir[1]));
                    ps_vtransform_corners(ps, corners.m);
                }

                dev->SetTransform(D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)&world);
                ps_vrender(ps, dev);
            }
        }
        dev->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);
    }
}
