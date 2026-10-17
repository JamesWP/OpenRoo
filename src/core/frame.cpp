/* The frame driver and the update half of a frame (frame.h); the draw half is
 * the sequencer's (sequencer/drawframe.cpp). */
#include <math.h>
#include "frame.h"
#include "prof.h"
#include "renderdevice.h"
#include "game.h"
#include "levelentry.h"
#include "camera.h"
#include "framepose.h"
#include "clock.h"
#include "scriptplayer.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "gametick.h"
#include "renderer.h"

/* The scripted camera (the script player's running spline): eye from
 * scriptedEye(), target from focus(), both copied into the camera
 * pose; pitch and yaw recovered by acos; a plain LookAt with +Y up. */
static void scripted_camera(Game *g, RenderDevice *d3d)
{
    CameraPose *cam = g->camera()->pose();
    for (int i = 0; i < 3; ++i) {
        cam->eye()[i]    = g->camera()->scriptedEye(i);
        cam->target()[i] = g->camera()->focus(i);
    }
    const float dx = cam->target()[0] - cam->eye()[0];
    const float dy = cam->target()[1] - cam->eye()[1];
    const float dz = cam->target()[2] - cam->eye()[2];
    const double horiz2 = (double)dz * dz + (double)dx * dx;
    const double len = sqrt(((double)dy * dy + (double)dz * dz) + (double)dx * dx);
    const double horiz = sqrt(horiz2);
    cam->setPitch((float)acos(horiz2 / (len * horiz)));
    cam->setYaw((float)acos(dz / (horiz * sqrt(1.0))));

    Mat4 view;
    Camera_BuildLookAt(&view, cam->eye()[0], cam->eye()[1], cam->eye()[2],
                       cam->target()[0], cam->target()[1], cam->target()[2],
                       0.0f, 1.0f, 0.0f, 0.0f);
    d3d->SetView(view);
}

/* The listener follows the camera: position = eye, front = target - eye,
 * top = (0, 1, 0, 1) through A*B, where A is identity with
 * m5 = m10 = cos(-pitch), m6 = -sin(-pitch), m9 = sin(-pitch) and B is
 * identity with m0 = m10 = cos(yaw), m2 = sin(yaw), m8 = -sin(yaw); w-divided
 * unless it is exactly 1. */
static void update_listener(Game *g)
{
    CameraPose *cam = g->camera()->pose();
    float front[3] = { cam->target()[0] - cam->eye()[0],
                    cam->target()[1] - cam->eye()[1],
                    cam->target()[2] - cam->eye()[2] };

    const float cp = (float)cos(-cam->pitch()), sp = (float)sin(-cam->pitch());
    const float cy = (float)cos(cam->yaw()),    sy = (float)sin(cam->yaw());
    float a[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float b[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    a[5] = cp;  a[6] = -sp;  a[9] = sp;  a[10] = cp;
    b[0] = cy;  b[2] = sy;   b[8] = -sy; b[10] = cy;

    float r[16] = { 0 };
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            float acc = r[row * 4 + col];
            for (int k = 0; k < 4; ++k)
                acc = a[row * 4 + k] * b[k * 4 + col] + acc;
            r[row * 4 + col] = acc;
        }

    const float v[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    float o[4] = { 0, 0, 0, 0 };
    for (int col = 0; col < 4; ++col) {
        float acc = o[col];
        for (int k = 0; k < 4; ++k)
            acc = v[k] * r[k * 4 + col] + acc;
        o[col] = acc;
    }
    float top[3];
    if (o[3] != 1.0f) {
        top[0] = o[0] / o[3];
        top[1] = o[1] / o[3];
        top[2] = o[2] / o[3];
    } else {
        top[0] = o[0]; top[1] = o[1]; top[2] = o[2];
    }

    audiodev::Device *snd = g->audio()->soundManager()->device();
    snd->setListenerPosition(cam->eye(), true);
    snd->setListenerOrientation(front, top, true);
    snd->commit();
}

void Core_RunFrame(Game *g, Renderer *renderer, const MenuArt *art, ProgableControl *progCtrl)
{
    PROF_SCOPE("frame");
    FrameTime t;
    if (Core_UpdateFrame(g, renderer, progCtrl, &t))
        Seq_DrawFrame(g, renderer, art, progCtrl, t);
}

bool Core_UpdateFrame(Game *g, Renderer *renderer, ProgableControl *progCtrl, FrameTime *t)
{
    if (g->levelPrepPending() != 0) {
        {
            PROF_SCOPE("level prepare");
            LevelEntry_PrepareAssets(g, renderer);
        }
        g->setLevelPrepPending(0);
        return false;
    }

    const double prevMs = clock_previous_seconds() * 1000.0;
    double now;
    {
        PROF_SCOPE("clock + test hooks");
        now = clock_advance() * 1000.0;
    }
    t->now = now;
    t->elapsed = now - prevMs;
    const double dt = t->dt = now - renderer->lastTickMs;

    if (dt > 0.0) {
        PROF_SCOPE("update");
        renderer->lastTickMs = now;
        {
            PROF_SCOPE("game tick");
            Core_GameTick(g, dt, now, progCtrl);
        }
        if (g->levelPrepPending() != 0)
            return false;
        {
            PROF_SCOPE("camera focus");
            FramePose_Player(g->world(), g->camera(), g->config(), dt, &renderer->cameraFocus);
        }
        {
            PROF_SCOPE("camera");
            if (g->scriptPlayer()->splineActive() == 0)
                Camera_UpdateViewTransform(renderer->device, g->world(), g->camera(), g->config(),
                                           &renderer->scene, renderer->cameraFocus, dt);
            else
                scripted_camera(g, renderer->device);
        }
        if (g->audio()->soundCreated() != 0) {
            PROF_SCOPE("audio");
            update_listener(g);
        }
    }
    return true;
}
