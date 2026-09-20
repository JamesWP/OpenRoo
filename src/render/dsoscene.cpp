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

#define SCENE_LIST_HEAD (*(struct SceneNode **)0x0046c45c)

struct SceneNode { unsigned char *pValue; SceneNode *pNextNode; };

/* Field accessors.  The offsets are unaligned -- the type byte at +0x00 shifts
 * everything by one -- so every read goes through memcpy rather than a struct,
 * which would need packing attributes to be correct here. */
static inline DWORD ob_d(const unsigned char *o, int off)
{ DWORD v; memcpy(&v, o + off, 4); return v; }
static inline float ob_f(const unsigned char *o, int off)
{ float v; memcpy(&v, o + off, 4); return v; }

#define O_MESH      0x001
#define O_SIZE      0x009
#define O_ANIMGATE  0x00d
#define O_ANIMDESC  0x011
#define O_POS       0x191
#define O_ROT       0x19d
#define O_TEXTURE   0x1a9
#define O_SRCBLEND  0x1ad
#define O_DESTBLEND 0x1b1
#define O_TEXADDR   0x1b5
#define O_USEPATH   0x1b9
#define O_FRAMED    0x1bd
#define O_ORIENT    0x1c1
#define O_PERIOD    0x1c2
#define O_SPLINE    0x1c6

/* SplinePath: pVtable, then LinkedList{vtable, pHead, pTail, dwCount}. */
#define SP_HEAD  (O_SPLINE + 0x08)
#define SP_COUNT (O_SPLINE + 0x10)

#define RS_SRCBLEND         19
#define RS_DESTBLEND        20
#define RS_ALPHABLENDENABLE 27
#define RS_SPECULARENABLE   29
#define RS_TEXADDR_U        44
#define RS_TEXADDR_V        45

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

typedef void (__attribute__((thiscall)) *mesh_draw_fn)(void *mesh, void *dev, DWORD frame);

/* Minimal view of the device vtable.  Only the four slots this function
 * dispatches are named; confirmed empirically by the golden fixture, whose
 * recording device traps all 42 and never sees another one. */
struct DevVtbl {
    void *slot[42];
};
struct Dev { DevVtbl *lpVtbl; };

typedef HRESULT (WINAPI *SetRS_fn)(void *, DWORD, DWORD);
typedef HRESULT (WINAPI *SetTX_fn)(void *, DWORD, Mat4 *);
typedef HRESULT (WINAPI *DrawP_fn)(void *, DWORD, DWORD, void *, DWORD, DWORD);
typedef HRESULT (WINAPI *SetTex_fn)(void *, DWORD, void *);

static inline void set_rs(void *dev, DWORD s, DWORD v)
{ ((SetRS_fn)((Dev *)dev)->lpVtbl->slot[0x58 / 4])(dev, s, v); }
static inline void set_xf(void *dev, DWORD s, Mat4 *m)
{ ((SetTX_fn)((Dev *)dev)->lpVtbl->slot[0x64 / 4])(dev, s, m); }
static inline void draw_prim(void *dev, DWORD pt, DWORD fvf, void *v, DWORD n, DWORD f)
{ ((DrawP_fn)((Dev *)dev)->lpVtbl->slot[0x70 / 4])(dev, pt, fvf, v, n, f); }
static inline void set_tex(void *dev, DWORD stage, void *t)
{ ((SetTex_fn)((Dev *)dev)->lpVtbl->slot[0x98 / 4])(dev, stage, t); }

/* fmod(t / period, 1.0), the path parameter.  `period` is divided in as a
 * 32-bit integer (fidiv), so a zero period is a divide fault in the original
 * too -- not guarded here, because guarding it would change behaviour. */
static float path_param(double t, DWORD period, double bias)
{
    double v = (t + bias) / (double)(int)period;
    return (float)m_fmod((double)v, K_PATH_MODULUS);
}

static void eval_path(const unsigned char *o, float t, Vec3 *out)
{
    bezier_eval((const ListNodeM *)ob_d(o, SP_HEAD), ob_d(o, SP_COUNT), t, out);
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

static DWORD animation_frame(const unsigned char *o, double t)
{
    /* +0xd is set to 1 by BuildSceneObjectList when the object's .ani loaded;
     * +0x11 is the AnimTable it loaded into (ani.h), which runs to +0x191 --
     * exactly where the position below begins. */
    if (ob_d(o, O_ANIMGATE) == 0)
        return 0;
    /* LookupAnimDescriptor(o + 0x11, 0x14) is a switch whose walk_forward arm
     * is the table's first slot, so the descriptor is o + 0x11 and no lookup
     * is needed.  It can never be NULL, so that check cannot fire. */
    const AnimSlot *slot = (const AnimSlot *)(o + O_ANIMDESC);
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

extern "C" __declspec(dllexport) void __cdecl
Scene_DrawSceneObjects(void *dev, float *cam, DWORD /*a3*/, DWORD /*a4*/, double t)
{
    for (SceneNode *node = SCENE_LIST_HEAD; node != NULL; node = node->pNextNode) {
        unsigned char *o = node->pValue;
        if (o == NULL)
            continue;

        DWORD src = ob_d(o, O_SRCBLEND), dst = ob_d(o, O_DESTBLEND);
        if (src != 0 && dst != 0) {
            set_rs(dev, RS_ALPHABLENDENABLE, 1);
            set_rs(dev, RS_SRCBLEND, src);
            set_rs(dev, RS_DESTBLEND, dst);
        } else {
            set_rs(dev, RS_ALPHABLENDENABLE, 0);
        }

        DWORD ta = ob_d(o, O_TEXADDR);
        set_rs(dev, RS_TEXADDR_U, ta);
        set_rs(dev, RS_TEXADDR_V, ta);

        DWORD tex = ob_d(o, O_TEXTURE);
        if (tex != 0)
            set_tex(dev, 0, (void *)ob_d((const unsigned char *)tex, 0x18));

        unsigned char type = o[0];
        Mat4 world;

        if (type == 0) {
            void *mesh = (void *)ob_d(o, O_MESH);
            if (mesh == NULL)
                continue;

            if (ob_d(o, O_USEPATH) == 0) {
                object_rotation(&world, ob_f(o, O_ROT), ob_f(o, O_ROT + 4),
                                ob_f(o, O_ROT + 8));
                Mat4 tr;
                m4_translate(&tr, ob_f(o, O_POS), ob_f(o, O_POS + 4),
                             ob_f(o, O_POS + 8));
                compose(&world, &world, &tr);
            } else {
                DWORD period = ob_d(o, O_PERIOD);
                Vec3 pos;
                eval_path(o, path_param(t, period, 0.0), &pos);

                if (o[O_ORIENT] == 1) {
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
                    object_rotation(&world, ob_f(o, O_ROT), ob_f(o, O_ROT + 4),
                                    ob_f(o, O_ROT + 8));
                }

                Mat4 tr;
                m4_translate(&tr, pos.x, pos.y, pos.z);
                compose(&world, &world, &tr);

                /* The spline path re-selects the texture; the static path
                 * jumps past this. */
                if (tex != 0)
                    set_tex(dev, 0, (void *)ob_d((const unsigned char *)tex, 0x18));
            }

            set_xf(dev, D3DTRANSFORMSTATE_WORLD, &world);
            DWORD frame = animation_frame(o, t);
            set_rs(dev, RS_SPECULARENABLE, 0);

            if (ob_d(o, O_FRAMED) != 0)
                FaktMesh_DrawFramedModel((CFaktMesh *)mesh, (IDirect3DDevice3 *)dev, frame);
            else
                FaktMesh_DrawMeshBuffer((CFaktMesh *)mesh, (IDirect3DDevice3 *)dev, frame);

        } else if (type == 2) {
            Vec3 target = { cam[3], cam[4], cam[5] };
            Vec3 eye    = { cam[0], cam[1], cam[2] };
            Vec3 dir;   v3_sub(&dir, &target, &eye);

            Vec3 corner[4];
            billboard_corners(corner, dir.x, dir.y, dir.z, ob_f(o, O_SIZE));

            BbVertex quad[4];
            billboard_vertex(&quad[0], &corner[0], 0xffffff, 0, 0.0f, 1.0f);
            billboard_vertex(&quad[1], &corner[1], 0xffffff, 0, 0.0f, 0.0f);
            billboard_vertex(&quad[2], &corner[2], 0xffffff, 0, 1.0f, 1.0f);
            billboard_vertex(&quad[3], &corner[3], 0xffffff, 0, 1.0f, 0.0f);

            float px, py, pz;
            if (ob_d(o, O_USEPATH) == 0) {
                px = ob_f(o, O_POS);  py = ob_f(o, O_POS + 4);  pz = ob_f(o, O_POS + 8);
            } else {
                Vec3 pos;
                eval_path(o, path_param(t, ob_d(o, O_PERIOD), 0.0), &pos);
                px = pos.x;  py = pos.y;  pz = pos.z;
            }

            m4_translate(&world, px, py, pz);
            set_xf(dev, D3DTRANSFORMSTATE_WORLD, &world);
            set_rs(dev, RS_SPECULARENABLE, 0);
            draw_prim(dev, D3DPT_TRIANGLESTRIP, 0x1e2, quad, 4, 0);
        }
    }
}
