/* Draws every record of the theme slot passed in: for every sub-object and
 * every position, gate against the tile and player, then dispatch on the
 * record's kind.
 *
 *   per record    SPECULARENABLE on if the record asks and Highlights is
 *                 on, off again once its sub-objects are drawn;
 *                 ZWRITEENABLE from bNoZWrite.
 *   per sub       skipped whole if its effect is 5 and Reflection is off;
 *                 TEXTUREADDRESSU/V from dwTexAddress, 0 meaning CLAMP.
 *   per position  gated on the tile under it and the player; textured,
 *                 alpha blended if both blend factors are set, then drawn
 *                 as the record's kind:
 *     model      World = Scale * RotX * RotY * RotZ * Translate, the frame
 *                taken from the AnimTable, the sub-object's vertex effect,
 *                then either the explode debris or the mesh;
 *     billboard  a camera-facing quad (Math_BuildBillboardQuad);
 *     quad       the caller's four vertices, UV/colour-animated by the
 *                sub-object's effect.
 *
 * Rotation matrices are built row-major, in the transpose of D3DX's usual
 * convention; multiplication and SetTransform follow that convention
 * throughout.  The tile under a position is Tile::at(map, (int)x, -(int)z). */

#include "renderdevice.h"
#include <windows.h>
#include <math.h>
#include <string.h>

#include "sceneobjects.h"
#include "scenequad.h"
#include "game.h"
#include "config.h"
#include "levelmap.h"
#include "tile.h"
#include "player.h"
#include "theme.h"
#include "levelobject.h"
#include "scenetexture.h"
#include "camera.h"
#include "ani.h"
#include "faktmesh.h"
#include "wrapperobject.h"
#include "explodedebris.h"
#include "d3dmath.h"

/* ThemeLevelObject is packed, so its embedded members (wrapper, explode,
 * animTable, the sub-objects) are only 1-aligned by declaration -- the record
 * stride makes that unavoidable. */
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

enum { KIND_MODEL = 1, KIND_QUAD = 2, KIND_BILLBOARD = 3 };
static const float HALF_PI   = 1.5707963705062866f;
static const float PHASE_K   = 4.0f;
static const float QUAD_HALF = 0.4f;

/* R = A * B, row-major; each element summed k = 0..3 from zero. */
static void mat_mul(Mat4 *r, const Mat4 *a, const Mat4 *b)
{
    Mat4 t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0.0;
            for (int k = 0; k < 4; k++)
                s += (double)a->m[i * 4 + k] * b->m[k * 4 + j];
            t.m[i * 4 + j] = (float)s;
        }
    *r = t;
}

static void mat_translate(Mat4 *m, float x, float y, float z)
{
    m4_identity(m);
    m->m[12] = x;
    m->m[13] = y;
    m->m[14] = z;
}

/* The sub-object's visibility gate against the tile and the player. */
static bool gate_passes(DWORD gate, const Tile *tile, const Player *pl)
{
    switch (gate) {
    case 0: return true;
    case 1: return tile->busy() != 0;
    case 2: return tile->busy() == 0;
    case 3: return pl->anim() == 10;
    case 4: return pl->anim() != 10;
    case 5: return pl->anim() != 10 && (pl->glides() != 0 || pl->gliding() != 0);
    case 6: return pl->anim() != 10 && pl->effectDActive() != 0;
    default: return false;
    }
}

/* The record's vertical oscillation, added to `y`. */
static float oscillate(const ThemeLevelObject *rec, const Tile *tile, double now, float y)
{
    if (rec->flOscillationAmplitude == 0.0f)
        return y;
    double arg = rec->flOscillationFrequency * now;
    if (rec->bOscillateRandom)
        arg += (double)tile->itemPhase() * PHASE_K;
    arg += rec->flOscillationPhase;
    return (float)(sin(arg) * rec->flOscillationAmplitude + y);
}

/* The mesh frame.  With bNoMoveStates the record loops its own code-0x14 range
 * on the clock; otherwise it plays the caller's code at the caller's time.
 * The frame counts are read as unsigned. */
static unsigned int anim_frame(ThemeLevelObject *rec, double now, float animTime,
                               unsigned int animCode)
{
    if (rec->bNoMoveStates) {
        AnimSlot *a = Ani_LookupAnimDescriptor(&rec->animTable, 0x14);
        if (a == NULL)
            return 0;
        double n = (double)(unsigned int)a->numFrames;
        double v = (double)(unsigned int)a->fps * now * 0.001 / n;
        return (unsigned int)(long long)(fmod(v, 1.0) * n);
    }
    AnimSlot *a = Ani_LookupAnimDescriptor(&rec->animTable, animCode);
    if (a == NULL || a->numFrames == 0)
        return 0;
    double v;
    if (a->reverse != 0)
        v = (double)(unsigned int)a->firstFrame
          - (double)(unsigned int)a->numFrames * animTime;
    else
        v = (double)(unsigned int)a->numFrames * animTime + (double)a->firstFrame;
    return (unsigned int)(long long)v;
}

static void draw_model(Game *game, ThemeLevelObject *rec, SceneSubObject *sub,
                       const Tile *tile, const Vec3 *pos, const Vec3 *rot,
                       RenderDevice *dev, double now, float animTime,
                       unsigned int animCode, unsigned int dtMs)
{
    Vec3 P = { rec->flPosX, rec->flPosY, rec->flPosZ };
    float h  = rec->bRandomYAngle ? tile->itemPhase() : 0.0f;
    float rx = (float)(now * rec->flRotRateX);
    float ry = (float)(now * rec->flRotRateY + h);
    float rz = (float)(now * rec->flRotRateZ);
    P.y = oscillate(rec, tile, now, P.y);

    Vec3 S;
    if (rec->flPump[3] != 0.0f) {
        double w = (sin(rec->flPump[3] * now) + 1.0) * 0.5;
        S.x = (float)(w * rec->flPump[0]) + rec->flScaleX;
        S.y = (float)(w * rec->flPump[1]) + rec->flScaleY;
        S.z = (float)(w * rec->flPump[2] + rec->flScaleZ);
    } else {
        S = (Vec3){ rec->flScaleX, rec->flScaleY, rec->flScaleZ };
    }

    Mat4 M, R;
    m4_identity(&M);
    M.m[0] = S.x; M.m[5] = S.y; M.m[10] = S.z;

    double a = (double)rx + rot->x + HALF_PI;
    float c = (float)cos(a), s = (float)sin(a);
    m4_identity(&R);
    R.m[5] = c; R.m[6] = -s; R.m[9] = s; R.m[10] = c;
    mat_mul(&M, &M, &R);

    a = (double)ry + rot->y;
    c = (float)cos(a); s = (float)sin(a);
    m4_identity(&R);
    R.m[0] = c; R.m[2] = s; R.m[8] = -s; R.m[10] = c;
    mat_mul(&M, &M, &R);

    a = (double)rz + rot->z;
    c = (float)cos(a); s = (float)sin(a);
    m4_identity(&R);
    R.m[0] = c; R.m[1] = -s; R.m[4] = s; R.m[5] = c;
    mat_mul(&M, &M, &R);

    mat_translate(&R, P.x + pos->x, P.y + pos->y, P.z + pos->z);
    mat_mul(&M, &M, &R);
    dev->SetTransform(Transform::World, &M);

    unsigned int frame = anim_frame(rec, now, animTime, animCode);
    const float *p = sub->flEffectParams;
    switch (sub->effect) {
    case 0:
        rec->wrapper.flush();
        break;
    case 4:
        rec->wrapper.applySineWave((unsigned int)(long long)now, p[0], p[1], p[2]);
        break;
    case 5:
        if (!game->config()->videoReflection())
            return;
        rec->wrapper.updateObjectTransform(dev, (unsigned short)frame);
        break;
    case 6:
        rec->wrapper.scrollUVs((unsigned int)(long long)now, p[0] != 0.0f ? 1 : 0, p[1]);
        break;
    default:
        break;
    }

    if (rec->bExplode) {
        rec->explode.advance((float)((double)dtMs * (double)0.001f));
        rec->explode.draw(dev);
        return;
    }
    if (rec->bLit)
        rec->pMesh->drawFramedModel(dev, frame);
    else
        rec->pMesh->drawMeshBuffer(dev, frame);
}

static void draw_billboard(ThemeLevelObject *rec, const Tile *tile, const Vec3 *pos,
                           RenderDevice *dev, double now)
{
    Vec3 corner[4];
    Math_BuildBillboardQuad(corner,
                            g_camera.target[0] - g_camera.eye[0],
                            g_camera.target[1] - g_camera.eye[1],
                            g_camera.target[2] - g_camera.eye[2],
                            rec->flBillboardScale);
    BbVertex v[4];
    billboard_vertex(&v[0], &corner[0], 0x00ffffff, 0, 0.0f, 1.0f);
    billboard_vertex(&v[1], &corner[1], 0x00ffffff, 0, 0.0f, 0.0f);
    billboard_vertex(&v[2], &corner[2], 0x00ffffff, 0, 1.0f, 1.0f);
    billboard_vertex(&v[3], &corner[3], 0x00ffffff, 0, 1.0f, 0.0f);

    float y = oscillate(rec, tile, now, rec->flPosY);
    Mat4 T;
    mat_translate(&T, rec->flPosX + pos->x, y + pos->y, rec->flPosZ + pos->z);
    dev->SetTransform(Transform::World, &T);
    dev->Draw(Prim::TriangleStrip, VertexFormat::Lit, v, 4, 0);
}

/* Effect 3's corner: (x, y, 0, 1) through M, divided by w unless w is 1,
 * recentred on (0.5, 0.5). */
static void rotated_corner(const Mat4 *M, float x, float y, float *u, float *v)
{
    const float in[4] = { x, y, 0.0f, 1.0f };
    float out[4];
    for (int j = 0; j < 4; j++) {
        double s = 0.0;
        for (int i = 0; i < 4; i++)
            s += (double)in[i] * M->m[i * 4 + j];
        out[j] = (float)s;
    }
    if ((double)out[3] != 1.0) {
        out[0] = out[0] / out[3];
        out[1] = out[1] / out[3];
    }
    *u = out[0] + 0.5f;
    *v = out[1] + 0.5f;
}

static void draw_quad(SceneQuadVertex *q, SceneSubObject *sub, const Vec3 *pos,
                      RenderDevice *dev, double now)
{
    Mat4 T;
    mat_translate(&T, pos->x, pos->y, pos->z);
    dev->SetTransform(Transform::World, &T);
    for (int i = 0; i < 4; i++)
        q[i].diffuse = 0x0fffffff;

    if (sub->effect == 0) {
        dev->Draw(Prim::TriangleStrip, VertexFormat::Diffuse2, q, 4, 0);
        return;
    }

    const float *p = sub->flEffectParams;
    switch (sub->effect) {
    case 1: {  // scroll, one of four directions
        float r = (float)fmod(p[1] * now, 1.0);
        float v = r * p[0];
        switch ((int)(long long)p[2]) {
        case 1:
            q[0].u1 = -v;     q[0].v1 = 1.0f;
            q[1].u1 = -v;     q[1].v1 = 0.0f;
            q[2].u1 = 1 - v;  q[2].v1 = 1.0f;
            q[3].u1 = 1 - v;  q[3].v1 = 0.0f;
            break;
        case 2:
            q[0].u1 = 0.0f;   q[0].v1 = v;
            q[1].u1 = 0.0f;   q[1].v1 = -v;
            q[2].u1 = 1.0f;   q[2].v1 = v;
            q[3].u1 = 1.0f;   q[3].v1 = -v;
            break;
        case 3:
            q[0].u1 = -v;     q[0].v1 = 1 + v;
            q[1].u1 = 0.0f;   q[1].v1 = 0.0f;
            q[2].u1 = 1.0f;   q[2].v1 = 1.0f;
            q[3].u1 = 1 + v;  q[3].v1 = -v;
            break;
        case 4:
            q[0].u1 = 0.0f;   q[0].v1 = 0.0f;
            q[1].u1 = -v;     q[1].v1 = 1 + v;
            q[2].u1 = 1 + v;  q[2].v1 = -v;
            q[3].u1 = 1.0f;   q[3].v1 = 1.0f;
            break;
        default:
            break;
        }
        break;
    }
    case 2: {  // a grey flash, alpha 0xff
        unsigned int b = (unsigned int)(long long)
            ((sin(p[0] * now + p[1]) + 1.0) * 0.5 * 255.0);
        unsigned int col = ((((b | 0xffffff00u) << 8) | b) << 8) | b;
        for (int i = 0; i < 4; i++)
            q[i].diffuse = col;
        q[0].u1 = 0.0f; q[0].v1 = 1.0f;
        q[1].u1 = 0.0f; q[1].v1 = 0.0f;
        q[2].u1 = 1.0f; q[2].v1 = 1.0f;
        q[3].u1 = 1.0f; q[3].v1 = 0.0f;
        break;
    }
    case 3: {  // the UV corners rotated about the centre
        double a = p[0] * now;
        float c = (float)cos(a), s = (float)sin(a);
        Mat4 M;
        m4_identity(&M);
        M.m[0] = c; M.m[1] = -s; M.m[4] = s; M.m[5] = c;
        rotated_corner(&M, -QUAD_HALF,  QUAD_HALF, &q[0].u1, &q[0].v1);
        rotated_corner(&M, -QUAD_HALF, -QUAD_HALF, &q[1].u1, &q[1].v1);
        rotated_corner(&M,  QUAD_HALF,  QUAD_HALF, &q[2].u1, &q[2].v1);
        rotated_corner(&M,  QUAD_HALF, -QUAD_HALF, &q[3].u1, &q[3].v1);
        break;
    }
    case 4: {  // rotation about a pivot
        double a = p[0] * now;
        float s = (float)sin(a), c = (float)cos(a);
        double pv = p[2], qv = p[1];
        double A  = (qv - pv) * s;
        float  B  = (float)((pv + qv) * s + 1.0);
        float  C1 = (float)(c * qv + pv * s);
        float  D  = (float)((c * qv + 1.0) - pv * s);
        q[0].u1 = (float)A; q[0].v1 = D;
        q[1].u1 = (float)A; q[1].v1 = C1;
        q[2].u1 = B;        q[2].v1 = D;
        q[3].u1 = B;        q[3].v1 = C1;
        break;
    }
    default:
        break;
    }

    StridedVertices sv;
    memset(&sv, 0, sizeof(sv));
    sv.position     = { &q[0].x,       sizeof(SceneQuadVertex) };
    sv.diffuse      = { &q[0].diffuse, sizeof(SceneQuadVertex) };
    sv.texCoords[0] = { &q[0].u1,      sizeof(SceneQuadVertex) };
    SceneQuad_Draw(dev, &sv, 4);
}

extern "C" __declspec(dllexport) void __cdecl
Scene_RenderSceneObjects(Game *game, SceneQuadVertex *quad, const Vec3 *positions,
                         const Vec3 *rotations, unsigned int count,
                         ThemeObjectTypeSlot *slot, RenderDevice *d3d, double now,
                         float animTime, unsigned int animCode, unsigned int dtMs)
{
    Config *cfg = game->config();
    const Player *pl = game->player();

    for (unsigned int i = 0; i < slot->dwInstanceCount; i++) {
        ThemeLevelObject *rec = &slot->records[i];
        RenderDevice *dev = d3d;
        if (rec->bSpecular && cfg->videoHighlights())
            dev->SetRenderState(RS::SpecularEnable, 1);
        dev->SetRenderState(RS::ZWriteEnable, rec->bNoZWrite ? 0 : 1);

        for (unsigned int s = 0; s < rec->dwSubObjectCount; s++) {
            SceneSubObject *sub = &rec->pSubObjects[s];
            if (sub->effect == 5 && !cfg->videoReflection())
                continue;
            DWORD addr = sub->dwTexAddress != 0 ? sub->dwTexAddress : (DWORD)TexAddress::Clamp;
            d3d->SetRenderState(RS::TextureAddressU, addr);
            d3d->SetRenderState(RS::TextureAddressV, addr);

            for (unsigned int k = 0; k < count; k++) {
                const Vec3 *pos = &positions[k];
                const Tile *tile = game->map()->tile((int)pos->x, -(int)pos->z);
                if (!gate_passes(sub->dwVisibilityGate, tile, pl))
                    continue;

                dev = d3d;
                dev->SetTexture(0, sub->pTexture);
                if (sub->dwBlendSrc != 0 && sub->dwBlendDst != 0) {
                    dev->SetRenderState(RS::AlphaBlendEnable, 1);
                    dev->SetRenderState(RS::SrcBlend, sub->dwBlendSrc);
                    dev->SetRenderState(RS::DestBlend, sub->dwBlendDst);
                } else {
                    dev->SetRenderState(RS::AlphaBlendEnable, 0);
                }

                switch (rec->kind) {
                case KIND_MODEL:
                    draw_model(game, rec, sub, tile, pos, &rotations[k], dev,
                               now, animTime, animCode, dtMs);
                    break;
                case KIND_BILLBOARD:
                    draw_billboard(rec, tile, pos, dev, now);
                    break;
                case KIND_QUAD:
                    draw_quad(quad, sub, pos, dev, now);
                    break;
                default:
                    break;
                }
            }
        }
        d3d->SetRenderState(RS::SpecularEnable, 0);
    }
}
