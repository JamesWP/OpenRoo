/* The draw half of a frame (frame.h), in order:
 *
 *   1. clear, BeginFrame, the sky, then the opaque passes: the static
 *      placement lists, the bombable blocks, the player, the grid items, the
 *      bombs, the foes;
 *   2. scene objects, bridges, the player's effect models, the stencil
 *      shadows;
 *   3. the translucent passes (particle effects) when particles are on;
 *   4. the HUD and the menus, EndFrame, the present.
 */
#include "prof.h"
#include "frame.h"
#include "renderstates.h"
#include "renderdevice.h"
#include "game.h"
#include "theme.h"
#include "scenepasses.h"
#include "hud.h"
#include "menuscreens.h"
#include "menuart.h"
#include "scoreoverlay.h"
#include "textrenderer.h"
#include "renderer.h"
#include "keys.h"

void Core_DrawFrame(Game *g, Renderer *renderer, const MenuArt *art, ProgableControl *progCtrl,
                    const FrameTime &t)
{
    const double now = t.now, elapsed = t.elapsed, dt = t.dt;
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
