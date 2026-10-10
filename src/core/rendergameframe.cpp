/* rendergameframe.cpp -- the per-frame body of WinMain's message loop.
 * In order:
 *
 *   1. a pending level entry: prepare it and return (nothing else runs);
 *   2. timing: now/elapsed in ms, dt against the last tick; when dt > 0,
 *      GameTick, the player/foe poses, the camera (the orbit camera, or the
 *      scripted camera while a spline runs), and the 3D sound listener;
 *   3. clear, BeginFrame, the sky, then the opaque passes: the static
 *      placement lists, the bombable blocks, the player, the grid items, the
 *      bombs, the foes;
 *   4. scene objects, bridges, the player's effect models, the stencil
 *      shadows;
 *   5. the translucent passes (particle effects) when particles are on;
 *   6. the HUD and the menus, EndFrame, the present.
 */
#include "prof.h"
#include <math.h>
#include "rendergameframe.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "game.h"
#include "theme.h"
#include "levelentry.h"
#include "camera.h"
#include "framepose.h"
#include "clock.h"
#include "scriptplayer.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "sky.h"
#include "gametick.h"
#include "scenepasses.h"
#include "hud.h"
#include "menuscreens.h"
#include "menuart.h"
#include "scoreoverlay.h"
#include "textrenderer.h"
#include "dbg.h"
#include "renderstate.h"
#include "renderer.h"

/* ─── Section 2: timing, tick, camera, listener ─────────────────────────── */

/* The scripted camera (the script player's running spline): eye from
 * field13cc94(), target from cameraEye(), both copied into the camera
 * pose; pitch and yaw recovered by acos; a plain LookAt with +Y up. */
static void scripted_camera(Game *g, RenderDevice *d3d)
{
    CameraPose *cam = g->camera()->pose();
    for (int i = 0; i < 3; ++i) {
        cam->eye()[i]    = g->camera()->field13cc94(i);
        cam->target()[i] = g->camera()->cameraEye(i);
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

    audiodev::Device *snd = g->soundManager()->device();
    snd->setListenerPosition(cam->eye(), true);
    snd->setListenerOrientation(front, top, true);
    snd->commit();
}

/* Single fields of the device's state. */
static void set_stencil_enable(bool on)
{
    StencilState s = g_renderDevice->stencil();
    s.enable = on;
    g_renderDevice->SetStencil(s);
}

static void set_depth(bool test, bool write)
{
    g_renderDevice->SetDepth(DepthState{ test, write });
}


static void set_depth_test(bool on)
{
    DepthState d = g_renderDevice->depth();
    d.test = on;
    g_renderDevice->SetDepth(d);
}

static void set_fog_enable(bool on)
{
    FogState f = g_renderDevice->fog();
    f.enable = on;
    g_renderDevice->SetFog(f);
}

/* ─── RenderGameFrame ───────────────────────────────────────────────────── */

  void  
Render_RenderGameFrame(Renderer *renderer, const MenuArt *art)
{
    dbg::frameBegin();
    PROF_SCOPE("frame");
    Game *g = Game::instance();
    if (g->field_173584() != 0) {
        {
            PROF_SCOPE("level prepare");
            LevelEntry_PrepareAssets(renderer);
        }
        g->setField173584(0);
        return;
    }

    const double prevMs = clock_previous_seconds() * 1000.0;
    double now;
    {
        PROF_SCOPE("clock + test hooks");
        now = hooks_ClockSeconds() * 1000.0;
    }
    const double elapsed = now - prevMs;
    double dt = now - renderer->lastTickMs;

    if (dt > 0.0) {
        PROF_SCOPE("update");
        renderer->lastTickMs = now;
        {
            PROF_SCOPE("game tick");
            Sim_GameTick(g, dt, now);
        }
        if (g->field_173584() != 0)
            return;
        {
            PROF_SCOPE("poses");
            FramePose_Player(g->world(), g->camera(), g->config(), now, dt, &renderer->cameraFocus);
            FramePose_Foes(g->world(), now, dt, renderer->foePoses);
        }
        {
            PROF_SCOPE("camera");
            if (g->scriptPlayer()->splineActive() == 0)
                Camera_UpdateViewTransform(g_renderDevice, g->world(), g->camera(), g->config(),
                                           &renderer->scene, renderer->cameraFocus, dt);
            else
                scripted_camera(g, g_renderDevice);
        }
        if (g->soundCreated() != 0) {
            PROF_SCOPE("audio");
            update_listener(g);
        }
    }

    PROF_SCOPE("draw");
    RenderDevice *d3d = g_renderDevice;
    /* The sky covers the whole target, so only depth (and stencil) clear. */
    if (d3d->hasStencil())
        set_stencil_enable(true);
    {
        PROF_SCOPE("gpu / vsync wait");
        {
            PROF_SCOPE("clear");
            d3d->Clear(ClearFlag::Depth);
        }
        if (!d3d->BeginFrame())
            return;
    }

    set_stencil_enable(false);
    set_fog_enable(false);
    d3d->SetSpecular(false);
    d3d->SetBlend(BlendState::off());
    CameraPose *cam = g->camera()->pose();
    renderer->themeBlock.sky().draw(d3d, cam->eye()[0], cam->eye()[1], cam->eye()[2]);
    if (renderer->themeBlock.fogEnabled())
        set_fog_enable(true);
    d3d->SetStencil(StencilState{ true, CompareFunc::Always, 1, StencilOp::Keep,
                                  StencilOp::Keep, StencilOp::Replace });

    {
        PROF_SCOPE("opaque");
        Scene_OpaquePasses(renderer, g->world(), g->camera()->pose(), g->config(), now, elapsed);
    }
    {
        PROF_SCOPE("effects and shadows");
        Scene_EffectsAndShadows(renderer, g->world(), g->camera()->pose(), g->config(), now, dt);
    }
    {
        PROF_SCOPE("translucent");
        Scene_TranslucentPasses(renderer, g->world(), g->camera()->pose(), g->config(), now, elapsed, dt);
    }

    set_depth(false, true);
    d3d->SetSamplerAddress(0, AddressMode::Clamp, AddressMode::Clamp);

    const unsigned w = d3d->width(), h = d3d->height();
    const float W = (float)w, H = (float)h;
    const float hudH = H * 0.26666668f;
    const GameState st = g->state();
    if (st != GameState::NameEntry && st != GameState::GameOver && st != GameState::MainMenu && st != GameState::PauseMenu && st != GameState::LevelComplete &&
        g->scriptPlayer()->running() == 0)
    {
        PROF_SCOPE("hud");
        Hud_Draw(renderer, g->world(), g->session(), st, w, h, W, H, hudH);
    }
    {
        PROF_SCOPE("hud");
        Hud_DrawFps(W, H);
    }

    const float pad = H * 0.00625f;
    if (st != GameState::MainMenu && st != GameState::PauseMenu && st != GameState::LevelComplete) {
        PROF_SCOPE("hud");
        Hud_DrawMessages(&renderer->themeBlock, g->world(), g->scriptPlayer(), st, W, H, pad);
    }
    {
        PROF_SCOPE("hud");
        Hud_DrawLogo(st, H, hudH, pad);
    }

    const uint32_t ms = (uint32_t)(long long)now;
    if (st == GameState::MainMenu || st == GameState::PauseMenu || st == GameState::LevelComplete) {
        PROF_SCOPE("menu");
        Menu_DispatchGameState(art, g->frontEnd(), g->session(), g->config(),
                               &renderer->themeBlock, d3d, &g_fontMain, ms);
    }
    if (g->state() == GameState::GameOver) {
        PROF_SCOPE("menu");
        Score_DrawGameOverScore(g->session(), &renderer->themeBlock, d3d, &g_fontMain, ms);
    }
    if (g->frontEnd()->menu()->node() == 3 || g->state() == GameState::NameEntry) {
        PROF_SCOPE("menu");
        Score_DrawHighScoreTable(art, g->frontEnd(), &renderer->themeBlock, d3d, &g_fontMain, ms);
    }

    set_depth_test(true);
    d3d->EndFrame();
    PROF_SCOPE("present");
    if (g->state() == GameState::Quitting) {
        if (g->field_0c() != 0)
            g_renderDevice->PresentImage(renderer->demoImage);
        return;
    }
    d3d->Present();
}
