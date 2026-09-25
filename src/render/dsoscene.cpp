/* DrawSceneObjects (0x420f50) reimplementation.
 *
 * SIGNATURE: __cdecl with SIX dword slots, not the two Ghidra shows.  The one
 * call site (0x0042848A) pushes six and cleans with `add esp,0x18`, and the
 * body reads a double at argument offset +0x10.  a3/a4 are never read; they
 * are declared so the stack shape matches exactly.
 *
 * All math goes through karoo-hooks/d3dmath.h -- our own code, never the
 * game's helpers.  The shipped build uses the standard backend
 * (d3dmath_std.cpp): plain float and the C library.  It is not bit-exact
 * against the original and does not need to be; it was accepted on the bar
 * that it looks and plays the same, and the replay suite passes 5/5 with every
 * end-state field intact because these matrices feed rendering only.
 *
 * It was accepted by reproducing the original byte for byte over 13 synthetic
 * fixture cases under the (since removed) bit-exact backend.
 *
 * WORLD MATRIX, verified byte-for-byte against captured output from the
 * original:
 *
 *   static path      M = RotX(f19d + pi/2) . RotY(f1a1)  . RotZ(f1a5) . T(f191,f195,f199)
 *   spline, plain    same rotations,                                   . T(bezier(t))
 *   spline, oriented M = RotX(pitch + pi/2) . RotY(heading) . RotZ(0)  . T(bezier(t))
 *
 * The two spline angles come from the path tangent -- see heading_angles().
 *
 * Preserved deliberately:
 *  - The blend test is `srcblend != 0 && destblend != 0`; either being zero
 *    disables blending entirely rather than defaulting one of them.
 *  - TEXTUREADDRESSU and TEXTUREADDRESSV are both set from the SAME field
 *    (+0x1b5).  There is no separate V mode.
 *  - The spline path issues a SECOND SetTexture just before the transform.
 *    The static path does not -- it jumps past that check.
 *  - Objects whose type is neither 0 nor 2 still run the whole render-state
 *    prologue before being skipped, as do type-0 objects with a NULL mesh.
 *    The state changes are real and observable, so they are not hoisted.
 */
#include "d3dmath.h"
#include "log.h"
#include "faktmesh.h"
#include "ani.h"
#include "scene.h"
#include "texture.h"
#include "extraobjects.h"
#include "particles.h"
#include "camera.h"
#include "framepose.h"
#include "game.h"

/* The scene list is GG_SCENE->objects; each value is a SceneObject
 * (scene.h, layout-checked against the 0x1da-byte allocation). */

static const float K_HALF_PI = 1.5707964f;   /* 0x45d2cc */
static const float K_TWO_PI  = 6.2831855f;   /* 0x45d2f8 */
static const double K_PATH_MODULUS = 1.0;    /* 0x45d2e8 */
static const double K_TANGENT_STEP = 10.0;   /* 0x45d448 */
static const double K_ORIENT_ROLL  = 0.0;    /* 0x45d390 */
static const double K_ANIM_SCALE   = 0.001;  /* 0x45d368 */

/* d = left * right in the row-vector convention.  m4_mul mirrors the original
 * helper's argument order, which is (dst, right, left) -- keeping that in one
 * place stops the inversion leaking into every call below. */
static void compose(Mat4 *d, const Mat4 *left, const Mat4 *right)
{
    m4_mul(d, right, left);
}

/* fmod(t / period, 1.0), the path parameter.  `period` is divided in as a
 * 32-bit integer (fidiv), so a zero period is a divide fault in the original
 * too -- not guarded here, because guarding it would change behaviour. */
static float path_param(double t, DWORD period, double bias)
{
    double v = (t + bias) / (double)(int)period;
    return (float)m_fmod((double)v, K_PATH_MODULUS);
}

static void eval_path(const SceneObject *o, float t, Vec3 *out)
{
    const LinkedList *cp = &o->spline.controlPointList;
    bezier_eval((const ListNodeM *)cp->pHead, cp->dwCount, t, out);
}

/* The object's texture as the device wants it. */
static void select_texture(IDirect3DDevice3 *dev, const SceneObject *o)
{
    dev->SetTexture(0, ((const SceneTexture *)o->texture)->pTexture2);
}

/* Heading and pitch from the path tangent.
 *
 * Two angles, each acos of a normalised dot product, each with a `2*pi - a`
 * correction on one side of a sign test:
 *   heading  between (0,0,1) and (dx,0,dz)  -- corrected when dx  > 0
 *   pitch    between (dx,dy,dz) and (dx,0,dz) -- corrected when dy <= 0
 *
 * The dot products are summed x, then z, then y, which is the original's order
 * and matters for the last bit. */
static void heading_angles(const Vec3 *d, float *heading, float *pitch)
{
    Vec3 axis  = { 0.0f, 0.0f, 1.0f };
    Vec3 flat  = { d->x, 0.0f, d->z };
    Vec3 full  = { d->x, d->y, d->z };

    float h = v3_angle_between(&axis, &flat);
    if (d->x > 0.0f)
        h = K_TWO_PI - h;
    *heading = h;

    float p = v3_angle_between(&full, &flat);
    if (!(d->y > 0.0f))
        p = K_TWO_PI - p;
    *pitch = p;
}

/* Rotations shared by the static path and the plain spline path. */
static void object_rotation(Mat4 *m, float rx, float ry, float rz)
{
    Mat4 a, b, c, t;
    m4_rot_x_biased(&a, rx, K_HALF_PI);
    m4_rot_y(&b, ry);
    compose(&t, &a, &b);
    m4_rot_z(&c, rz);
    compose(m, &t, &c);
}

static DWORD animation_frame(const SceneObject *o, double t)
{
    /* +0xd is set to 1 by BuildSceneObjectList when the object's .ani loaded;
     * +0x11 is the AnimTable it loaded into (ani.h), which runs to +0x191 --
     * exactly where the position below begins. */
    if (o->animLoaded == 0)
        return 0;
    /* LookupAnimDescriptor(o + 0x11, 0x14) is a switch whose walk_forward arm
     * is the table's first slot, so the descriptor is o + 0x11 and no lookup
     * is needed.  It can never be NULL, so that check cannot fire. */
    const AnimSlot *slot = (const AnimSlot *)&o->anim;
    if (slot->numFrames == 0)
        return 0;
    /* Anim_FrameOnClock is the same function written as the other three call
     * sites write it, fmod(v/n, 1)*n; this one is fmod(v, n) and is kept in
     * its own form because it is the arithmetic this function was verified
     * against. */
    double v = t * K_ANIM_SCALE * (double)(unsigned)slot->fps;
    double r = m_fmod(v, (double)(unsigned)slot->numFrames);
    return (DWORD)(unsigned short)(int)r;      /* __ftol, then truncated to 16 bits */
}

/* KAROO_CAM_DIAG=1: log the camera globals (eye, target, yaw, pitch) as raw
 * bits every frame.  Called here because RenderGameFrame reaches this after
 * UpdateViewTransform whether that is the original or camera.cpp's, so two
 * runs -- one with camera.cpp's patch entries withdrawn -- diff directly. */
static void cam_diag(const float *cam)
{
    static int on = -1;
    if (on < 0) {
        char buf[8];
        DWORD n = GetEnvironmentVariableA("KAROO_CAM_DIAG", buf, sizeof(buf));
        on = (n > 0 && n < sizeof(buf) && buf[0] == '1') ? 1 : 0;
    }
    if (!on)
        return;
    const DWORD *b = (const DWORD *)cam;
    log_write("CAM %08lx %08lx %08lx  %08lx %08lx %08lx  %08lx %08lx\n",
              b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    /* framepose.cpp's outputs: the focus block, and an FNV-1a hash over the
     * live foes' pose records. */
    const DWORD *f = (const DWORD *)GG_CAMERA_FOCUS->f;
    log_write("FOCUS %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
              f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8]);
    const Game *game = Game::instance();
    unsigned n = game ? game->foeCount() : 0;
    const unsigned char *r = (const unsigned char *)GG_FOE_POSES;
    DWORD h = 2166136261u;
    for (unsigned i = 0; i < n * 0x1d; i++)
        h = (h ^ r[i]) * 16777619u;
    log_write("FOES %u %08lx\n", n, h);
}

extern "C" __declspec(dllexport) void __cdecl
Scene_DrawSceneObjects(IDirect3DDevice3 *dev, float *cam, DWORD /*a3*/, DWORD /*a4*/, double t)
{
    cam_diag(cam);
    for (LinkedListNode *node = GG_SCENE->objects.pHead; node != NULL; node = node->pNextNode) {
        const SceneObject *o = (const SceneObject *)node->pValue;
        if (o == NULL)
            continue;

        DWORD src = o->srcBlend, dst = o->destBlend;
        if (src != 0 && dst != 0) {
            dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
            dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, src);
            dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, dst);
        } else {
            dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
        }

        DWORD ta = o->textureAddress;
        dev->SetRenderState(D3DRENDERSTATE_TEXTUREADDRESSU, ta);
        dev->SetRenderState(D3DRENDERSTATE_TEXTUREADDRESSV, ta);

        if (o->texture != NULL)
            select_texture(dev, o);

        unsigned char type = o->type;
        Mat4 world;

        if (type == EXTRA_MODEL) {
            CFaktMesh *mesh = o->mesh;
            if (mesh == NULL)
                continue;

            if (o->onPath == 0) {
                object_rotation(&world, o->rot[0], o->rot[1], o->rot[2]);
                Mat4 tr;
                m4_translate(&tr, o->pos[0], o->pos[1], o->pos[2]);
                compose(&world, &world, &tr);
            } else {
                DWORD period = o->splineTime;
                Vec3 pos;
                eval_path(o, path_param(t, period, 0.0), &pos);

                if (o->splineMode == 1) {
                    Vec3 ahead;
                    eval_path(o, path_param(t, period, K_TANGENT_STEP), &ahead);
                    Vec3 d;  v3_sub(&d, &ahead, &pos);

                    float heading, pitch;
                    heading_angles(&d, &heading, &pitch);

                    Mat4 a, b, c, tmp;
                    m4_rot_x_biased(&a, pitch, K_HALF_PI);
                    m4_rot_y(&b, heading);
                    compose(&tmp, &a, &b);
                    m4_rot_z(&c, (float)K_ORIENT_ROLL);
                    compose(&world, &tmp, &c);
                } else {
                    object_rotation(&world, o->rot[0], o->rot[1], o->rot[2]);
                }

                Mat4 tr;
                m4_translate(&tr, pos.x, pos.y, pos.z);
                compose(&world, &world, &tr);

                /* The spline path re-selects the texture; the static path
                 * jumps past this. */
                if (o->texture != NULL)
                    select_texture(dev, o);
            }

            dev->SetTransform(D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)&world);
            DWORD frame = animation_frame(o, t);
            dev->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);

            if (o->lit != 0)
                FaktMesh_DrawFramedModel(mesh, dev, frame);
            else
                FaktMesh_DrawMeshBuffer(mesh, dev, frame);

        } else if (type == EXTRA_BILLBOARD) {
            Vec3 target = { cam[3], cam[4], cam[5] };
            Vec3 eye    = { cam[0], cam[1], cam[2] };
            Vec3 dir;   v3_sub(&dir, &target, &eye);

            Vec3 corner[4];
            billboard_corners(corner, dir.x, dir.y, dir.z, o->billboardRadius);

            BbVertex quad[4];
            billboard_vertex(&quad[0], &corner[0], 0xffffff, 0, 0.0f, 1.0f);
            billboard_vertex(&quad[1], &corner[1], 0xffffff, 0, 0.0f, 0.0f);
            billboard_vertex(&quad[2], &corner[2], 0xffffff, 0, 1.0f, 1.0f);
            billboard_vertex(&quad[3], &corner[3], 0xffffff, 0, 1.0f, 0.0f);

            float px, py, pz;
            if (o->onPath == 0) {
                px = o->pos[0];  py = o->pos[1];  pz = o->pos[2];
            } else {
                Vec3 pos;
                eval_path(o, path_param(t, o->splineTime, 0.0), &pos);
                px = pos.x;  py = pos.y;  pz = pos.z;
            }

            m4_translate(&world, px, py, pz);
            dev->SetTransform(D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)&world);
            dev->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);
            dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0x1e2, quad, 4, 0);
        }
    }
}

/* ─── DrawSceneParticleSystems, 0x421f30 (Ghidra's "DrawTerrainTiles") ─────
 *
 * The type-1 twin of the function above: same list, same object fields, but
 * the thing at +0x05 is a particle system driven through its own vtable.
 * cdecl with six dword slots, (dev, cam = 0x46c4a0, double dt_ms, double t);
 * the one call site (0x42abc0, RenderGameFrame) cleans 0x18.  Written from
 * the listing -- the decompiler would not complete on it.
 *
 * Differences from the type-0 path, all deliberate:
 *  - SPECULARENABLE is cleared once before the loop, not per object, and
 *    there is no TEXTUREADDRESS state.
 *  - Objects other than type 1, and type 1 with a NULL system, are skipped
 *    BEFORE any state change (type 0/2 run the whole prologue first) -- but
 *    a NULL system is tested after the blend and texture state is set.
 *  - The static path's RotX is NOT biased by pi/2: RotX(rx).RotY(ry).RotZ(rz).T.
 *    The plain spline path is unbiased too; only the oriented one adds pi/2
 *    (to the pitch), as type 0 does.
 *  - The spline path sets texture NULL and an identity world, polls F3/F4
 *    (debug draws of the path and its control polygon), then re-selects the
 *    object's texture before setting the real world.  The static path jumps
 *    past all of that.
 *
 * The system is driven through its own vtable (particles.h): tick with
 * (float)(dt_ms * 0.001), set-vector with the view direction
 * cam[3..5] - cam[0..2], then render. */
#include "splinepath.h"
#include "record.h"



static void rotation_xyz(Mat4 *m, float rx, float ry, float rz)
{
    Mat4 a, b, c, t;
    m4_rot_x(&a, rx);
    m4_rot_y(&b, ry);
    compose(&t, &a, &b);
    m4_rot_z(&c, rz);
    compose(m, &t, &c);
}

extern "C" __declspec(dllexport) void __cdecl
Scene_DrawParticleSystems(IDirect3DDevice3 *dev, float *cam, double dt_ms, double t)
{
    dev->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, 0);

    for (LinkedListNode *node = GG_SCENE->objects.pHead; node != NULL; node = node->pNextNode) {
        const SceneObject *o = (const SceneObject *)node->pValue;
        if (o == NULL || o->type != EXTRA_PARTICLE)
            continue;

        DWORD src = o->srcBlend, dst = o->destBlend;
        if (src != 0 && dst != 0) {
            dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
            dev->SetRenderState(D3DRENDERSTATE_SRCBLEND, src);
            dev->SetRenderState(D3DRENDERSTATE_DESTBLEND, dst);
        } else {
            dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
        }

        if (o->texture != NULL)
            select_texture(dev, o);

        ParticleSystem *ps = o->particle;
        if (ps == NULL)
            continue;

        Mat4 world;
        if (o->onPath == 0) {
            rotation_xyz(&world, o->rot[0], o->rot[1], o->rot[2]);
            Mat4 tr;
            m4_translate(&tr, o->pos[0], o->pos[1], o->pos[2]);
            compose(&world, &world, &tr);
        } else {
            DWORD period = o->splineTime;
            Vec3 pos;
            eval_path(o, path_param(t, period, 0.0), &pos);

            if (o->splineMode == 1) {
                Vec3 ahead;
                eval_path(o, path_param(t, period, K_TANGENT_STEP), &ahead);
                Vec3 d;  v3_sub(&d, &ahead, &pos);

                float heading, pitch;
                heading_angles(&d, &heading, &pitch);

                Mat4 a, b, c, tmp;
                m4_rot_x_biased(&a, pitch, K_HALF_PI);
                m4_rot_y(&b, heading);
                compose(&tmp, &a, &b);
                m4_rot_z(&c, (float)K_ORIENT_ROLL);
                compose(&world, &tmp, &c);
            } else {
                rotation_xyz(&world, o->rot[0], o->rot[1], o->rot[2]);
            }

            Mat4 tr;
            m4_translate(&tr, pos.x, pos.y, pos.z);
            compose(&world, &world, &tr);

            dev->SetTexture(0, NULL);
            Mat4 ident;
            m4_identity(&ident);
            dev->SetTransform(D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)&ident);

            SplinePath *sp = (SplinePath *)&o->spline;
            if (hooks_GetAsyncKeyState(VK_F3) & 0x8000)
                Spline_DrawSplinePath(sp, dev, 100, 0xffffffff);
            if (hooks_GetAsyncKeyState(VK_F4) & 0x8000)
                Spline_DrawControlPolygon(sp, dev, 0xff808080);

            if (o->texture != NULL)
                select_texture(dev, o);
        }

        dev->SetTransform(D3DTRANSFORMSTATE_WORLD, (D3DMATRIX *)&world);

        ps_vtick(ps, (float)(dt_ms * K_ANIM_SCALE));
        Vec3 dir = { cam[3] - cam[0], cam[4] - cam[1], cam[5] - cam[2] };
        ps_vset_vector(ps, dir.x, dir.y, dir.z);
        ps_vrender(ps, dev);
    }
}
