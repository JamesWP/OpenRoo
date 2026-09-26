/* framepose.cpp -- see framepose.h.
 *
 * Both builders share one idiom per actor: a facing angle from the facing
 * byte (1 -> 0, 2 -> -pi/2, 3 -> pi, 4 -> pi/2; anything else leaves the
 * previous value -- for the player, whatever the block held last frame),
 * and, while the actor is stepping (moveDir != 0 or anim >= 0xfa), a step
 * fraction (clock - animStart) / animDuration.  Past 10 on moveDir -- which
 * is only ever 0..4, so never -- the fraction turns the angle by pi/2 for
 * turnKind 2 (minus) or 4 (plus).  The test is kept as the listing has it.
 *
 * Kept: the fraction is computed in double and stored as a float, then
 * re-read as that float; the wrap of the player's yaw target is fmod by the
 * double 2pi, then + the float 2pi if negative (the FCOM decides on the
 * unrounded value).
 */
#include <math.h>
#include "framepose.h"
#include "camera.h"
#include "game.h"
#include "config.h"
#include "player.h"
#include "foe.h"
FoePose g_foePoses[500];   /* was 0x004dc7c8 */

static const float K_HALF_PI  = 1.5707964f;   /* 0x45d2cc */
static const double K_WRAP    = 6.2831854820251465;   /* 0x45d300 (double) */
static const float K_TWO_PI   = 6.2831855f;   /* 0x45d2f8 */
static const float K_SPIN     = 0.001f;       /* 0x45d308, cameraMode 2's yaw per ms */

/* Facing byte -> yaw.  Returns false for a byte outside 1..4. */
static bool facing_angle(unsigned char facing, float *a)
{
    switch (facing) {
    case 1: *a = 0.0f;          return true;
    case 2: *a = -1.5707964f;   return true;   /* 0xbfc90fdb / 0x45d310 */
    case 3: *a = 3.1415927f;    return true;   /* 0x40490fdb / 0x45d30c */
    case 4: *a = 1.5707964f;    return true;   /* 0x3fc90fdb / 0x45d2cc */
    }
    return false;
}

static bool stepping(const MovableEntity *e)
{
    return e->moveDir() != 0 || e->anim() >= 0xfa;
}

static float step_fraction(Game *g, const MovableEntity *e)
{
    return (float)((*g->clock() - e->animStart()) / e->animDuration());
}

/* The turn term, applied only past moveDir 10. */
static float turned(const MovableEntity *e, float angle, float frac)
{
    if (e->moveDir() > 10) {
        if (e->turnKind() == 2)
            return (float)(angle - (double)frac * K_HALF_PI);
        if (e->turnKind() == 4)
            return (float)(angle + (double)frac * K_HALF_PI);
    }
    return angle;
}

/* fmod(v, 2pi), then + 2pi if that went negative. */
static float wrap(double v)
{
    double y = fmod(v, K_WRAP);
    return (y < 0.0) ? (float)(y + K_TWO_PI) : (float)y;
}

extern "C" {

__declspec(dllexport) void __cdecl
FramePose_Player(Game *g, double /*t*/, double dt, CameraFocus *out)
{
    const Player *p = g->player();
    float *f = out->f;

    f[0] = 0.0f;
    facing_angle(p->facing(), &f[1]);

    const unsigned char mode = g->cameraMode();
    if (mode == 2) {
        f[5] = wrap(dt * K_SPIN + f[5]);
    } else if (mode == 0) {
        if (stepping(p)) {
            f[0] = step_fraction(g, p);
            f[1] = turned(p, f[1], f[0]);
        }
        const Config *c = g->config();
        if (c->cameraTurnsWithPlayer() == 1) {
            if (p->gliding() != 0)
                f[5] = f[1];
            else
                f[5] = c->cameraYaw() + f[1];
            f[5] = wrap(f[5]);
        } else {
            f[5] = c->cameraYaw();
        }
    }

    f[2] = p->posU();
    f[3] = p->posY();
    f[4] = -p->posV();

    if (mode == 0) {
        f[6] = f[2];  f[7] = f[3];  f[8] = f[4];
    } else {
        f[6] = g->cameraEye(0);
        f[7] = g->cameraEye(1);
        f[8] = -g->cameraEye(2);
    }
}

__declspec(dllexport) void __cdecl
FramePose_Foes(Game *g, double /*t*/, double /*dt*/, FoePose *out)
{
    const unsigned n = g->foeCount();
    for (unsigned i = 0; i < n; i++) {
        const Foe *e = g->foeSlot(g->foeId(i));
        FoePose *r = &out[i];

        float angle = 0.0f;
        facing_angle(e->facing(), &angle);
        if (stepping(e)) {
            float frac = step_fraction(g, e);
            r->stepFrac = frac;
            angle = turned(e, angle, frac);
        }

        r->pos[0] = e->posU();
        r->pos[1] = e->posY();
        r->pos[2] = -e->posV();
        r->rotX = 0.0f;
        r->rotY = angle;
        r->rotZ = 0.0f;
        r->kind = e->kind();
    }
}

}
