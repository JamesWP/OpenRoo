/* The camera's focus on the player (framepose.h).  The yaw target is wrapped
 * into [0, 2pi). */

#include <math.h>
#include "framepose.h"
#include "camera.h"
#include "world.h"
#include "camerarig.h"
#include "config.h"
#include "player.h"
#include "themedraw.h"
#include "entitydraw.h"

static const float K_SPIN = 0.001f;  // mode 2's yaw per ms

static float wrap(double v)
{
    const double two_pi = 6.283185307179586;
    double y = fmod(v, two_pi);
    return (float)(y < 0.0 ? y + two_pi : y);
}

void FramePose_Player(const World *world, const CameraRig *camera, const Config *config,
                      double dt, CameraFocus *out)
{
    const Player *p = world->player();
    const draw::Instance in = pose(*p, world->now());
    float *f = out->f;

    f[0] = in.animTime;
    f[1] = in.rot.y;
    f[2] = in.pos.x;
    f[3] = in.pos.y;
    f[4] = in.pos.z;

    const unsigned char mode = camera->mode();
    if (mode == 2) {
        f[5] = wrap(dt * K_SPIN + f[5]);
    } else if (mode == 0) {
        if (config->cameraTurnsWithPlayer() == 1)
            f[5] = wrap(p->gliding() != 0 ? f[1] : config->cameraYaw() + f[1]);
        else
            f[5] = config->cameraYaw();
    }

    if (mode == 0) {
        f[6] = f[2];  f[7] = f[3];  f[8] = f[4];
    } else {
        f[6] = camera->focus(0);
        f[7] = camera->focus(1);
        f[8] = -camera->focus(2);
    }
}
