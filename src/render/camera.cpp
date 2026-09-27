/* camera.cpp -- the orbit camera: UpdateViewTransform and the LookAt
 * builder it calls (camera.h).
 *
 * Each frame, from RenderGameFrame:
 *   1. target.x/z snap to the focus point, target.y eases towards it
 *      (rate 0.004 * dt).
 *   2. yaw: eases towards focus[5] by the short way round (0.006 * dt) when
 *      cameraMode == 0 and the "camera turns with player" option is 1,
 *      after first wrapping itself into [0, 2pi); otherwise it is set.
 *   3. distance: the current |target - eye| eases towards
 *      Game::cameraDistance (0.005 * dt), then < 0 (or NaN) -> 1, > 255 -> 255.
 *   4. pitch target = Config::activeCameraPitch() degrees -> radians.  In cameraMode 0
 *      an occlusion probe may force it to 1.569051 (just under pi/2, straight
 *      down): the ray target -> target + P, P = RotX(-p0).RotY(yaw) applied to
 *      (0,0,-d) with p0 the configured tilt, is stepped one height unit at a
 *      time from the player's height cell + 1 up to the eye's height; a cell
 *      whose kind byte is nonzero and whose height equals the step (or the
 *      step + 1) blocks it -- unless the player is on a stair or sliding.  Then the
 *      same ray is tested against every scene model (SegmentHitsAnyModel),
 *      which forces it unconditionally.
 *   5. pitch eases towards that (0.005 * dt), clamped above at 1.569051.
 *   6. eye = target + RotX(-pitch).RotY(yaw) applied to (0,0,-d); VIEW =
 *      LookAt(eye, target, +Y).
 *
 * Details that decide a branch: the probe tilts by
 * the CONFIGURED angle, not the eased pitch the eye uses; the grid cell is
 * floor(x + 0.5) by U and -floor(z + 0.5) by V, each
 * tested against [0, extent) as floats and then truncated and tested < 256
 * unsigned; the step counter is unsigned against a truncated (ey + 1); and
 * which way each comparison goes on NaN.
 */
#include <math.h>
#include "camera.h"
#include "d3dmath.h"
#include "renderdevice.h"
#include "game.h"
#include "config.h"
#include "levelmap.h"
#include "player.h"
#include "scene.h"
CameraFocus g_cameraFocus;
CameraGlobals g_camera;

static const float K_TARGET_RATE = 0.004f;
static const double K_YAW_WRAP   = 6.2831854820251465;
static const double K_PI         = 3.1415927410125732;
static const float K_TWO_PI      = 6.2831855f;
static const float K_YAW_RATE    = 0.006f;
static const float K_EASE_RATE   = 0.005f;  // distance and pitch
static const float K_DIST_MIN    = 1.0f;
static const float K_DIST_MAX    = 255.0f;
static const float K_NEG_DEG     = -0.017453292f;
static const float K_DEG         = 0.017453292f;
static const float K_EYE_CEIL    = 1000.0f;
static const float K_HALF        = 0.5f;
static const float K_PITCH_MAX   = 1.569051f;  // also the forced pitch

/* d = a * b in the row-vector convention (m4_mul takes its operands the
 * other way round -- see dsoscene.cpp's compose()). */
static void mul(Mat4 *d, const Mat4 *a, const Mat4 *b) { m4_mul(d, b, a); }

/* (0, 0, z) through RotX(pitch).RotY(yaw), w-divided. */
static Vec3 orbit_offset(float pitch, float yaw, float z)
{
    Mat4 rx, ry, m;
    m4_rot_x(&rx, pitch);
    m4_rot_y(&ry, yaw);
    mul(&m, &rx, &ry);
    Vec3 v = { 0.0f, 0.0f, z }, out;
    Math_Vec3TransformPoint(&out, m, v);
    return out;
}

extern "C" {

__declspec(dllexport) Mat4 *__cdecl
Camera_BuildLookAt(Mat4 *out, float ex, float ey, float ez,
                   float ax, float ay, float az,
                   float ux, float uy, float uz, float roll)
{
    Vec3 f = { ax - ex, ay - ey, az - ez };
    Vec3 n;  v3_div(&n, &f, (float)sqrt(Math_Vec3SqLen(&f)));

    Vec3 s = { uy * n.z - uz * n.y, uz * n.x - ux * n.z, ux * n.y - uy * n.x };
    Vec3 u = { n.y * s.z - n.z * s.y, n.z * s.x - n.x * s.z, n.x * s.y - n.y * s.x };
    Vec3 t;
    v3_div(&t, &s, (float)sqrt(Math_Vec3SqLen(&s)));  s = t;
    v3_div(&t, &u, (float)sqrt(Math_Vec3SqLen(&u)));  u = t;

    Mat4 v;
    m4_identity(&v);
    v.m[0] = s.x;  v.m[1] = u.x;  v.m[2]  = n.x;
    v.m[4] = s.y;  v.m[5] = u.y;  v.m[6]  = n.y;
    v.m[8] = s.z;  v.m[9] = u.z;  v.m[10] = n.z;
    v.m[12] = (float)-(((double)ez * s.z + (double)ey * s.y) + (double)ex * s.x);
    v.m[13] = (float)-(((double)ez * u.z + (double)ey * u.y) + (double)ex * u.x);
    v.m[14] = (float)-(((double)ez * n.z + (double)ey * n.y) + (double)ex * n.x);

    if (!(roll == 0.0f || roll != roll)) {      // zero or NaN skips
        Mat4 r, t2;
        m4_rot_z(&r, -roll);
        mul(&t2, &v, &r);
        v = t2;
    }
    *out = v;
    return out;
}

__declspec(dllexport) void __cdecl
Camera_UpdateViewTransform(CameraGlobals *cam, RenderDevice *d3d, Game *g,
                           CameraFocus focus, double dt)
{
    const float dtf = (float)dt;

    /* 1. target */
    cam->target[0] = focus.f[6];
    cam->target[2] = focus.f[8];
    cam->target[1] = (float)(((double)focus.f[7] - cam->target[1]) * dtf * K_TARGET_RATE
                             + cam->target[1]);

    /* 2. yaw */
    if (g->cameraMode() == 0 && g->config()->cameraTurnsWithPlayer() == 1) {
        double y = fmod((double)cam->yaw, K_YAW_WRAP);
        cam->yaw = (y < 0.0) ? (float)(y + K_TWO_PI) : (float)y;
        double diff = (double)focus.f[5] - cam->yaw;
        if (fabs(diff) > K_PI)
            diff = (diff > 0.0) ? diff - K_TWO_PI : diff + K_TWO_PI;
        cam->yaw = (float)(dtf * diff * K_YAW_RATE + cam->yaw);
    } else {
        cam->yaw = focus.f[5];
    }

    /* 3. distance */
    double dx = (double)cam->target[0] - cam->eye[0];
    double dy = (double)cam->target[1] - cam->eye[1];
    double dz = (double)cam->target[2] - cam->eye[2];
    double len = sqrt((dx * dx + dz * dz) + dy * dy);
    double dist = (g->cameraDistance() - len) * dtf * K_EASE_RATE + len;
    if (dist < 0.0 || dist != dist)
        dist = K_DIST_MIN;
    else if (dist > K_DIST_MAX)
        dist = K_DIST_MAX;
    const float negdist = (float)-dist;

    /* 4. pitch target, and the occlusion probe */
    const float tilt = g->config()->activeCameraPitch();
    Vec3 probe = orbit_offset((float)(tilt * K_NEG_DEG), cam->yaw, negdist);
    float pitchTarget = (float)(tilt * K_DEG);

    if (g->cameraMode() == 0) {
        const float ey = cam->eye[1];
        if (ey > 0.0f && ey < K_EYE_CEIL) {
            LevelMap *map = g->map();
            unsigned i = (unsigned)((int)g->player()->heightCell() + 1);
            /* ey + 1 is summed in double before the truncation:
             * a float sum could round across an integer and move the bound. */
            while (i < (unsigned)(int)((double)ey + 1.0)) {
                if (!(probe.y == 0.0f || probe.y != probe.y)) {   // zero or NaN skips
                    float k = (float)(((double)i - cam->target[1]) / probe.y);
                    float fx = (float)floor((double)probe.x * k + cam->target[0] + K_HALF);
                    float fz = (float)-floor((double)probe.z * k + cam->target[2] + K_HALF);
                    if (fx >= 0.0f && (float)map->extentU() > fx &&
                        fz >= 0.0f && (float)map->extentV() > fz) {
                        unsigned cu = (unsigned)(int)fx, cv = (unsigned)(int)fz;
                        if (cu < 0x100 && cv < 0x100) {
                            const Tile *t = map->tile((int)cu, (int)cv);
                            if (t->objectMarker() != 0 &&
                                (t->height() == i || t->height() == i + 1)) {
                                if (g->player()->onStairOrSlide() == 0)
                                    pitchTarget = K_PITCH_MAX;
                                i = 2000;       /* ends the scan */
                            }
                        }
                    }
                }
                i++;
            }
        }
        if ((unsigned char)g_scene.segmentHitsModel(cam->target[0], cam->target[1], cam->target[2],
                                                  probe.x, probe.y, probe.z))
            pitchTarget = K_PITCH_MAX;
    }

    /* 5. pitch */
    cam->pitch = (float)((pitchTarget - (double)cam->pitch) * dtf * K_EASE_RATE + cam->pitch);
    if (cam->pitch > K_PITCH_MAX)
        cam->pitch = K_PITCH_MAX;

    /* 6. eye and view */
    Vec3 eo = orbit_offset(-cam->pitch, cam->yaw, negdist);
    cam->eye[0] = cam->target[0] + eo.x;
    cam->eye[1] = cam->target[1] + eo.y;
    cam->eye[2] = cam->target[2] + eo.z;

    Mat4 view;
    Camera_BuildLookAt(&view, cam->eye[0], cam->eye[1], cam->eye[2],
                       cam->target[0], cam->target[1], cam->target[2],
                       0.0f, 1.0f, 0.0f, 0.0f);
    d3d->SetTransform(Transform::View, &view);
}

}
