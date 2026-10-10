/* The per-frame pose builders (framepose.h).  Each actor gets a yaw from its
 * facing byte (1: 0, 2: -pi/2, 3: pi, 4: pi/2; any other value leaves the
 * previous yaw, for the player whatever the block held last frame), and, while
 * it is stepping (moveDir nonzero or anim at least 0xfa), a step fraction
 * (clock - animStart) / animDuration.
 *
 * PRESERVED: the turn term applies only when moveDir is past 10, which it
 * never is (it is 0 to 4), so it never applies.  The fraction is computed in
 * double, stored as a float and re-read as that float.  The player's yaw
 * target is wrapped by fmod with the double 2pi, then has the float 2pi added
 * if negative. */

#include <math.h>
#include "framepose.h"
#include "camera.h"
#include "game.h"
#include "config.h"
#include "player.h"
#include "foe.h"
FoePose g_foePoses[500];

static const float K_HALF_PI  = 1.5707964f;
static const double K_WRAP    = 6.2831854820251465;
static const float K_TWO_PI   = 6.2831855f;
static const float K_SPIN     = 0.001f;  // cameraMode 2's yaw per ms

/* Facing byte to yaw; false for a byte outside 1..4. */
static bool facing_angle(unsigned char facing, float *a)
{
    switch (facing) {
    case 1: *a = 0.0f;          return true;
    case 2: *a = -1.5707964f;   return true;
    case 3: *a = 3.1415927f;    return true;
    case 4: *a = 1.5707964f;    return true;
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

 

  void  
FramePose_Player(Game *g, double , double dt, CameraFocus *out)
{
    const Player *p = g->player();
    float *f = out->f;

    f[0] = 0.0f;
    facing_angle(p->facing(), &f[1]);

    const unsigned char mode = g->camera()->cameraMode();
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
        f[6] = g->camera()->cameraEye(0);
        f[7] = g->camera()->cameraEye(1);
        f[8] = -g->camera()->cameraEye(2);
    }
}

  void  
FramePose_Foes(Game *g, double , double , FoePose *out)
{
    const unsigned n = g->foeCount();
    for (unsigned i = 0; i < n; i++) {
        const Foe *e = g->foeSlot(g->foeId(i));
        FoePose *r = &out[i];

        float angle = 0.0f;
        facing_angle(e->facing(), &angle);
        if (stepping(e)) {
            float frac = step_fraction(g, e);
            r->setStepFrac(frac);
            angle = turned(e, angle, frac);
        }

        r->set(e->kind(), e->posU(), e->posY(), -e->posV(), angle);
    }
}

