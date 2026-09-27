/* The scene-object passes: DrawSceneObjects (models, types 0 and 2) and
 * DrawSceneParticleSystems (particle systems, type 1), over the same list.
 *
 * DrawSceneObjects is __cdecl with six dword slots; the two unnamed dwords
 * are never read and are declared so the stack shape matches.
 *
 * World matrix:
 *
 *   static path      M = RotX(rx + pi/2) . RotY(ry)      . RotZ(rz) . T(pos)
 *   spline, plain    same rotations,                                . T(bezier(t))
 *   spline, oriented M = RotX(pitch + pi/2) . RotY(heading) . RotZ(0) . T(bezier(t))
 *
 * The two spline angles come from the path tangent -- see heading_angles().
 *
 * Behaviour that looks optional but is observable: - The blend test is
 * `srcblend != 0 && destblend != 0`; either being zero
 *    disables blending rather than defaulting one of them.
 * - TEXTUREADDRESSU and TEXTUREADDRESSV are both set from the same field.  -
 * The spline path issues a second SetTexture just before the transform;
 *    the static path does not.
 * - Objects whose type is neither 0 nor 2, and type-0 objects with a NULL
 *    mesh, still run the whole render-state prologue before being skipped. */

#include "dsoscene.h"
#include "d3dmath.h"
#include "log.h"
#include "faktmesh.h"
#include "ani.h"
#include "scene.h"
#include "texture.h"
#include "extraobjects.h"
#include "particles.h"
#include "camera.h"
#include "renderdevice.h"
#include "framepose.h"
#include "game.h"

/* The scene list is GG_SCENE->objects; each value is a SceneObject (scene.h).
 */

static const float K_HALF_PI = 1.5707964f;
static const float K_TWO_PI  = 6.2831855f;
static const double K_PATH_MODULUS = 1.0;
static const double K_TANGENT_STEP = 10.0;
static const double K_ORIENT_ROLL  = 0.0;
static const double K_ANIM_SCALE   = 0.001;

/* d = left * right in the row-vector convention.  m4_mul's argument order is
 * (dst, right, left); keeping that in one place stops the inversion leaking
 * into every call below. */
static void compose(Mat4 *d, const Mat4 *left, const Mat4 *right)
{
    m4_mul(d, right, left);
}

/* fmod(t / period, 1.0), the path parameter.  PRESERVED: `period` is divided
 * in as a 32-bit integer, so a zero period is a divide fault. */
static float path_param(double t, DWORD period, double bias)
{
    double v = (t + bias) / (double)(int)period;
    return (float)m_fmod((double)v, K_PATH_MODULUS);
}

static void eval_path(const SceneObject *o, float t, Vec3 *out)
{
    const LinkedList *cp = &o->spline.controlPointList;
    bezier_eval((const ListNodeM *)cp->head(), cp->count(), t, out);
}

static void select_texture(RenderDevice *dev, const SceneObject *o)
{
    dev->SetTexture(0, (const SceneTexture *)o->texture);
}

/* Heading and pitch from the path tangent.
 *
 * Two angles, each acos of a normalised dot product, each with a `2*pi - a`
 * correction on one side of a sign test:
 *   heading  between (0,0,1) and (dx,0,dz)  -- corrected when dx  > 0
 *   pitch    between (dx,dy,dz) and (dx,0,dz) -- corrected when dy <= 0 */
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
    // animLoaded is set by BuildSceneObjectList when the object's .ani loaded;
    // the AnimTable (ani.h) runs up to exactly where the position begins.
    if (o->animLoaded == 0)
        return 0;
    // LookupAnimDescriptor(anim, 0x14) returns the table's first slot, so no
    // lookup is needed, and it can never be NULL.
    const AnimSlot *slot = (const AnimSlot *)&o->anim;
    if (slot->numFrames == 0)
        return 0;
    // The same frame as Anim_FrameOnClock, written as fmod(v, n) rather than
    // fmod(v/n, 1)*n.
    double v = t * K_ANIM_SCALE * (double)(unsigned)slot->fps;
    double r = m_fmod(v, (double)(unsigned)slot->numFrames);
    return (DWORD)(unsigned short)(int)r;  // truncated to 16 bits
}

/* KAROO_CAM_DIAG=1: log the camera globals (eye, target, yaw, pitch) as raw
 * bits every frame, after UpdateViewTransform has run. */
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
    // framepose.cpp's outputs: the focus block, and an FNV-1a hash over the
    // live foes' pose records.
    const DWORD *f = (const DWORD *)g_cameraFocus.f;
    log_write("FOCUS %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
              f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8]);
    const Game *game = Game::instance();
    unsigned n = game ? game->foeCount() : 0;
    const unsigned char *r = (const unsigned char *)g_foePoses;
    DWORD h = 2166136261u;
    for (unsigned i = 0; i < n * 0x1d; i++)
        h = (h ^ r[i]) * 16777619u;
    log_write("FOES %u %08lx\n", n, h);
}

extern "C" __declspec(dllexport) void __cdecl
Scene_DrawSceneObjects(RenderDevice *dev, float *cam, DWORD , DWORD , double t)
{
    cam_diag(cam);
    for (LinkedListNode *node = g_scene.objects.head(); node != NULL; node = node->next()) {
        const SceneObject *o = (const SceneObject *)node->value();
        if (o == NULL)
            continue;

        DWORD src = o->srcBlend, dst = o->destBlend;
        if (src != 0 && dst != 0) {
            dev->SetRenderState(RS::AlphaBlendEnable, 1);
            dev->SetRenderState(RS::SrcBlend, src);
            dev->SetRenderState(RS::DestBlend, dst);
        } else {
            dev->SetRenderState(RS::AlphaBlendEnable, 0);
        }

        DWORD ta = o->textureAddress;
        dev->SetRenderState(RS::TextureAddressU, ta);
        dev->SetRenderState(RS::TextureAddressV, ta);

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

                // The spline path re-selects the texture; the static path
                // skips this.
                if (o->texture != NULL)
                    select_texture(dev, o);
            }

            dev->SetTransform(Transform::World, &world);
            DWORD frame = animation_frame(o, t);
            dev->SetRenderState(RS::SpecularEnable, 0);

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
            dev->SetTransform(Transform::World, &world);
            dev->SetRenderState(RS::SpecularEnable, 0);
            dev->Draw(Prim::TriangleStrip, VertexFormat::Lit, quad, 4, 0);
        }
    }
}

/* ─── DrawSceneParticleSystems ──────────────────────────────────────────────
 *
 * The type-1 twin of the function above: same list, same object fields, but
 * the object holds a particle system driven through its own vtable.  cdecl
 * with six dword slots, (dev, cam, double dt_ms, double t).
 *
 * Differences from the type-0 path: - SPECULARENABLE is cleared once before
 * the loop, not per object, and
 *    there is no TEXTUREADDRESS state.
 * - Objects other than type 1 are skipped before any state change; a NULL
 *    system is tested after the blend and texture state is set.
 * - The static path's RotX is not biased by pi/2:
 * RotX(rx).RotY(ry).RotZ(rz).T.
 *    The plain spline path is unbiased too; only the oriented one adds pi/2
 *    (to the pitch), as type 0 does.
 * - The spline path sets texture NULL and an identity world, polls F3/F4
 *    (debug draws of the path and its control polygon), then re-selects the
 *    object's texture before setting the real world.  The static path skips
 *    all of that.
 *
 * The system is ticked with (float)(dt_ms * 0.001), pointed along the view
 * direction cam[3..5] - cam[0..2], then rendered. */
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
Scene_DrawParticleSystems(RenderDevice *dev, float *cam, double dt_ms, double t)
{
    dev->SetRenderState(RS::SpecularEnable, 0);

    for (LinkedListNode *node = g_scene.objects.head(); node != NULL; node = node->next()) {
        const SceneObject *o = (const SceneObject *)node->value();
        if (o == NULL || o->type != EXTRA_PARTICLE)
            continue;

        DWORD src = o->srcBlend, dst = o->destBlend;
        if (src != 0 && dst != 0) {
            dev->SetRenderState(RS::AlphaBlendEnable, 1);
            dev->SetRenderState(RS::SrcBlend, src);
            dev->SetRenderState(RS::DestBlend, dst);
        } else {
            dev->SetRenderState(RS::AlphaBlendEnable, 0);
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

            dev->SetTexture(0, nullptr);
            Mat4 ident;
            m4_identity(&ident);
            dev->SetTransform(Transform::World, &ident);

            SplinePath *sp = (SplinePath *)&o->spline;
            if (hooks_GetAsyncKeyState(VK_F3) & 0x8000)
                Spline_DrawSplinePath(sp, dev, 100, 0xffffffff);
            if (hooks_GetAsyncKeyState(VK_F4) & 0x8000)
                Spline_DrawControlPolygon(sp, dev, 0xff808080);

            if (o->texture != NULL)
                select_texture(dev, o);
        }

        dev->SetTransform(Transform::World, &world);

        ps_vtick(ps, (float)(dt_ms * K_ANIM_SCALE));
        Vec3 dir = { cam[3] - cam[0], cam[4] - cam[1], cam[5] - cam[2] };
        ps_vset_vector(ps, dir.x, dir.y, dir.z);
        ps_vrender(ps, dev);
    }
}
