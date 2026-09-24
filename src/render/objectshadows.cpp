/* DrawObjectShadows (0x0043b790) -- projected planar shadows.
 *
 * Called by RenderGameFrame for three theme object types (John's slot, and
 * the slots at 0x475664 / 0x4d3464), once per frame each, after it has set
 * the shadow render states.  Every model is drawn a second time, squashed
 * onto the horizontal plane through its own position by a projection from a
 * point light 1000 units off along (-1, +1, -1), then lifted 0.02 to clear
 * the floor.  No stencil: the one state set here, 0x1d, is SPECULARENABLE.
 *
 * Per record of the slot (skipped when bNoShadow; Z-write = !bNoZWrite),
 * per sub-object, per placement instance whose sub-object condition holds
 * and whose record is a MODEL:
 *
 *   world = Scale(scale, pumped) * RotX(t*rateX + rot.x + pi/2)
 *         * RotY(t*rateY [+ cell phase] + rot.y) * RotZ(t*rateZ + rot.z)
 *         * Translate(recPos + pos, y oscillating) * Shadow * Translate(0, .02, 0)
 *
 * then the mesh at its animation frame, or -- when the record explodes --
 * the debris (advanced by debrisMs * 0.001, i.e. not at all: every caller
 * passes 0) drawn in its two passes.
 *
 * Kept: the sub-objects only GATE the draw -- each one whose condition
 * holds draws the whole record mesh again, so a record with two passing
 * sub-objects casts its shadow twice.
 *
 * Written from the disassembly: Ghidra's decompile reads every stack
 * argument three slots off.  Float intermediates are plain float/double C,
 * not the original's x87 chains (a last-bit difference in a shadow vertex).
 */
#include <math.h>
#include "objectshadows.h"
#include "game.h"
#include "player.h"
#include "levelmap.h"
#include "tile.h"
#include "theme.h"
#include "direct3d.h"
#include "d3dmath.h"
#include "ani.h"
#include "faktmesh.h"
#include "explodedebris.h"

static const float  K_HALF_PI    = 1.5707963705062866f;  /* 0x45d2cc */
static const float  K_LIGHT_OFF  = 1000.0f;              /* 0x45d31c */
static const float  K_LIFT       = 0.02f;                /* 0x3ca3d70a */
static const float  K_PHASE_MUL  = 4.0f;                 /* 0x45d370 */
static const float  K_DEBRIS_MS  = 0.001f;               /* 0x45d308 */

/* d = left * right, row vectors -- m4_mul's own order is (dst, right, left),
 * as dsoscene.cpp's compose() records. */
static void compose(Mat4 *d, const Mat4 *left, const Mat4 *right)
{
    m4_mul(d, right, left);
}

/* SceneSubObject::dwVisibilityGate (levelobject.h's names). */
static bool condition_holds(DWORD gate, const Game *g, const Tile *cell)
{
    const Player *p = g->player();
    switch (gate) {
    case 0:  return true;
    case 1:  return cell->busy() != 0;          /* active */
    case 2:  return cell->busy() == 0;          /* inactive */
    case 3:  return p->anim() == 10;            /* dead */
    case 4:  return p->anim() != 10;            /* alive */
    case 6:  return p->effectDActive() != 0;    /* protection */
    case 5:  return p->glides() != 0 || p->gliding() != 0;   /* paraglide */
    default: return false;
    }
}

/* The shadow projection: dot(P, L) * I - L (x) P, for the plane
 * P = (0, 1, 0, -pos.y) and the point light L = pos + (-1000, +1000, -1000).
 * Written entry by entry as the original stores them, the zero-coefficient
 * products folded to the -0.0 it stores. */
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
    if (rec->bNoMoveStates != 0) {
        /* No numFrames guard in the original: 0 frames gives NaN, __ftol's
         * 0x80000000, which DrawMeshBuffer clamps to frame 0 -- the same
         * frame Anim_FrameOnClock returns for it. */
        AnimSlot *s = Ani_LookupAnimDescriptor(&rec->animTable, 0x14);
        return s != NULL ? Anim_FrameOnClock(s, t) : 0;
    }
    AnimSlot *s = Ani_LookupAnimDescriptor(&rec->animTable, animKey);
    if (s == NULL || s->numFrames == 0)
        return 0;
    return Anim_FrameAtPhase(s, phase);
}

extern "C" __declspec(dllexport) void __cdecl
Shadows_DrawObjectShadows(Game *game, LevelPlacements * /*placements*/,
                          const float *pos, const float *rot, unsigned int count,
                          ThemeObjectTypeSlot *slot, Direct3D *d3d, double t,
                          float phase, unsigned int animKey, unsigned int debrisMs)
{
    IDirect3DDevice3 *dev = d3d->pDevice;
    dev->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);

    for (DWORD i = 0; i < slot->dwInstanceCount; i++) {
        ThemeLevelObject *rec = &slot->records[i];
        if (rec->bNoShadow != 0)
            continue;
        dev->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, rec->bNoZWrite == 0);

        for (DWORD s = 0; s < rec->dwSubObjectCount; s++) {
            for (unsigned int k = 0; k < count; k++) {
                const float *p = pos + k * 3;
                const float *r = rot + k * 3;
                int u = (int)p[0], v = -(int)p[2];
                const Tile *cell = game->map()->tile(u, v);

                if (!condition_holds(rec->pSubObjects[s].dwVisibilityGate, game, cell))
                    continue;
                if (rec->kind != THEME_KIND_MODEL)
                    continue;

                float cellPhase = rec->bRandomYAngle != 0 ? cell->itemPhase() : 0.0f;
                float angX = (float)(t * rec->flRotRateX);
                float angY = (float)(t * rec->flRotRateY + cellPhase);
                float angZ = (float)(t * rec->flRotRateZ);

                float y = rec->flPosY;
                if (rec->flOscillationAmplitude != 0.0f) {
                    double a = rec->flOscillationFrequency * t;
                    if (rec->bOscillateRandom != 0)
                        a += cell->itemPhase() * K_PHASE_MUL;
                    y = (float)(sin(a + rec->flOscillationPhase)
                                * rec->flOscillationAmplitude + y);
                }

                float sx = rec->flScaleX, sy = rec->flScaleY, sz = rec->flScaleZ;
                if (rec->flPump[3] != 0.0f) {
                    double pump = (sin(rec->flPump[3] * t) + 1.0) * 0.5;
                    sx = (float)(pump * rec->flPump[0]) + sx;
                    sy = (float)(pump * rec->flPump[1]) + sy;
                    sz = (float)(pump * rec->flPump[2] + sz);
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
                m4_translate(&m, rec->flPosX + p[0], y + p[1], rec->flPosZ + p[2]);
                compose(&world, &tmp, &m);
                shadow_matrix(&m, p);
                compose(&tmp, &world, &m);
                m4_translate(&m, 0.0f, K_LIFT, 0.0f);
                compose(&world, &tmp, &m);

                dev->SetTransform(D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)&world);

                int frame = animation_frame(rec, t, phase, animKey);
                if (rec->bExplode == 0) {
                    FaktMesh_DrawMeshBuffer(rec->pMesh, dev, (DWORD)frame);
                } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
                    ExplodeDebris_Advance(&rec->explode,
                                          (float)(unsigned long long)debrisMs * K_DEBRIS_MS);
                    ExplodeDebris_Draw(&rec->explode, dev);
#pragma GCC diagnostic pop
                }
            }
        }
    }
}
