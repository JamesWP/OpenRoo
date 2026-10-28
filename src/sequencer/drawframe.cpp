/* The draw half of a frame (drawframe.h), in order:
 *
 *   1. clear, BeginFrame, the sky;
 *   2. the scene, pass by pass (themedraw.h's Pass): the level's fixed
 *      picture and its objects opaque; the scene objects, then the effects
 *      (bridges, auras); the stencil shadows; the particles;
 *   3. the HUD and the menus, EndFrame, the present.
 *
 * Each pass sets the device state its draws share; the level (render's
 * LevelPlacements) and its objects (entitydraw.h) are drawn in it.
 */
#include "prof.h"
#include "drawframe.h"
#include "renderstates.h"
#include "renderdevice.h"
#include "game.h"
#include "theme.h"
#include "themedraw.h"
#include "dsoscene.h"
#include "hud.h"
#include "menuscreens.h"
#include "menuart.h"
#include "scoreoverlay.h"
#include "textrenderer.h"
#include "renderer.h"
#include "entitydraw.h"
#include "keys.h"

/* What every draw needs of this frame. */
static draw::Frame frame_for(Game *g, Renderer *renderer, const FrameTime &t)
{
    const CameraPose *cam = g->camera()->pose();
    const Config *c = g->config();
    draw::Frame f;
    f.dev     = renderer->device;
    f.theme   = &renderer->themeBlock;
    f.shadow  = &renderer->texShadow;
    f.eye     = { cam->eye()[0], cam->eye()[1], cam->eye()[2] };
    f.target  = { cam->target()[0], cam->target()[1], cam->target()[2] };
    f.opts    = { c->videoReflection() != 0, c->videoHighlights() != 0,
                  (int)c->videoShadows(), (int)c->videoParticles() };
    f.now     = t.now;
    return f;
}

/* The scene, pass by pass. */
static void draw_scene(Game *g, Renderer *renderer, draw::Frame &f)
{
    const World *world = g->world();
    LevelPlacements *level = &renderer->levelPlacements;
    RenderDevice *d3d = renderer->device;
    float *eye = g->camera()->pose()->eye();

    draw::beginPass(f, draw::Pass::Opaque);
    {
        PROF_SCOPE("opaque");
        level->draw(f, draw::Pass::Opaque, world->map(), world->now());
        drawWorld(f, draw::Pass::Opaque, *world);
        // The scene objects are part of the opaque picture.  They set no depth
        // state of their own, and the last record drawn may have had none to
        // write, so the pass's state is set again.
        draw::beginPass(f, draw::Pass::Opaque);
        Scene_DrawSceneObjects(renderer, d3d, eye, f.now);
    }
    draw::beginPass(f, draw::Pass::Effects);
    {
        PROF_SCOPE("effects");
        drawWorld(f, draw::Pass::Effects, *world);
    }
    /* Stencil shadows: a stencil buffer, more than 16 bpp, and the option. */
    if (d3d->hasStencil() && d3d->bitDepth() > 16 && f.opts.shadows > 0) {
        PROF_SCOPE("shadows");
        draw::beginPass(f, draw::Pass::Shadows);
        drawWorld(f, draw::Pass::Shadows, *world);
        level->draw(f, draw::Pass::Shadows, world->map(), world->now());
    }
    draw::beginPass(f, draw::Pass::Particles);
    {
        PROF_SCOPE("translucent");
        level->draw(f, draw::Pass::Particles, world->map(), world->now());
        drawWorld(f, draw::Pass::Particles, *world);
        Scene_DrawParticleSystems(&renderer->scene, renderer->showSplinePaths,
                                  renderer->showSplineControlPolygons, d3d, eye, f.now);
    }
}

void Seq_DrawFrame(Game *g, Renderer *renderer, const MenuArt *art, ProgableControl *progCtrl,
                   const FrameTime &t)
{
    const double now = t.now;
    PROF_SCOPE("draw");
    RenderDevice *d3d = renderer->device;
    /* The sky covers the whole target, so only depth (and stencil) clear. */
    if (d3d->hasStencil())
        set_stencil_enable(d3d, true);
    {
        PROF_SCOPE("gpu / vsync wait");
        {
            PROF_SCOPE("clear");
            d3d->Clear(ClearFlag::Depth);
        }
        if (!d3d->BeginFrame())
            return;
    }

    set_stencil_enable(d3d, false);
    set_fog_enable(d3d, false);
    d3d->SetSpecular(false);
    d3d->SetBlend(BlendState::off());
    CameraPose *cam = g->camera()->pose();
    renderer->themeBlock.sky().draw(d3d, cam->eye()[0], cam->eye()[1], cam->eye()[2]);
    if (renderer->themeBlock.fogEnabled())
        set_fog_enable(d3d, true);
    d3d->SetStencil(StencilState{ true, CompareFunc::Always, 1, StencilOp::Keep,
                                  StencilOp::Keep, StencilOp::Replace });

    renderer->showSplinePaths = input::down(input::KEY_F3);
    renderer->showSplineControlPolygons = input::down(input::KEY_F4);

    draw::Frame f = frame_for(g, renderer, t);
    draw_scene(g, renderer, f);

    set_depth(d3d, false, true);
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
        Hud_DrawFps(d3d, &renderer->fontMain, W, H);
    }

    const float pad = H * 0.00625f;
    if (st != GameState::MainMenu && st != GameState::PauseMenu && st != GameState::LevelComplete) {
        PROF_SCOPE("hud");
        Hud_DrawMessages(d3d, &renderer->fontMain, &renderer->themeBlock, g->world(), g->scriptPlayer(), st, W, H,
                         pad);
    }
    {
        PROF_SCOPE("hud");
        Hud_DrawLogo(d3d, &renderer->texKaroo128, st, H, hudH, pad);
    }

    const uint32_t ms = (uint32_t)(long long)now;
    if (st == GameState::MainMenu || st == GameState::PauseMenu || st == GameState::LevelComplete) {
        PROF_SCOPE("menu");
        Menu_DispatchGameState(art, g->frontEnd(), g->session(), g->config(),
                               &renderer->themeBlock, d3d, &renderer->fontMain, ms, progCtrl);
    }
    if (g->state() == GameState::GameOver) {
        PROF_SCOPE("menu");
        Score_DrawGameOverScore(g->session(), &renderer->themeBlock, d3d, &renderer->fontMain, ms);
    }
    if (g->frontEnd()->menu()->node() == 3 || g->state() == GameState::NameEntry) {
        PROF_SCOPE("menu");
        Score_DrawHighScoreTable(art, g->frontEnd(), &renderer->themeBlock, d3d, &renderer->fontMain, ms);
    }

    set_depth_test(d3d, true);
    d3d->EndFrame();
    PROF_SCOPE("present");
    if (g->state() == GameState::Quitting)
        return;
    d3d->Present();
}
