#include "camerainput.h"
#include "game.h"
#include "config.h"
#include "levelmap.h"
#include "player.h"

/* DETERMINISM: the rates are float constants widened to double, and each
 * step is computed in double against the double tick step before narrowing. */
static const double ZOOM_RATE  = (double)0.01f;
static const double TURN_RATE  = (double)0.001f;
static const double TILT_RATE  = (double)0.05f;
static const float  ZOOM_MAX   = 20.0f;
static const float  ZOOM_MIN   = 2.0f;
static const float  PITCH_MAX  = 89.0f;
static const float  PITCH_MIN  = 50.0f;

static inline double dt(Game *g) { return g->tickStep()->value; }

 

  void  
Camera_ZoomOut(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->camera()->zoomDistance() < ZOOM_MAX)
        g->camera()->setZoomDistance((float)(dt(g) * ZOOM_RATE + g->camera()->zoomDistance()));
    g->camera()->setField13cc90(1);  // PRESERVED: set on every zoom; nothing reads it
}

  void  
Camera_ZoomIn(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->camera()->zoomDistance() > ZOOM_MIN)
        g->camera()->setZoomDistance((float)(g->camera()->zoomDistance() - dt(g) * ZOOM_RATE));
    g->camera()->setField13cc90(1);  // PRESERVED: set on every zoom; nothing reads it
}

/* Only on a normal (non-bonus) level, and only while the player is alive. */
  void  
Camera_Overview(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->map()->bonus() != 0 || g->player()->moveState() != 0)
        return;
    g->camera()->setOverviewActive(1);
    g->camera()->setCameraDistance(40.0f);
}

  void  
Camera_RotateRight(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    Config *c = g->config();
    c->setCameraYaw((float)(dt(g) * TURN_RATE + c->cameraYaw()));
}

  void  
Camera_RotateLeft(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    Config *c = g->config();
    c->setCameraYaw((float)(c->cameraYaw() - dt(g) * TURN_RATE));
}

/* Only while playing.  Steps while at or below the limit, then clamps, so
 * one step may overshoot before the clamp. */
  void  
Camera_TiltUp(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->state() != 1)
        return;
    Config *c = g->config();
    if (c->cameraPitch() <= PITCH_MAX)
        c->setCameraPitch((float)(dt(g) * TILT_RATE + c->cameraPitch()));
    if (c->cameraPitch() > PITCH_MAX)
        c->setCameraPitch(PITCH_MAX);
}

  void  
Camera_TiltDown(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->state() != 1)
        return;
    Config *c = g->config();
    if (!(c->cameraPitch() < PITCH_MIN))
        c->setCameraPitch((float)(c->cameraPitch() - dt(g) * TILT_RATE));
    if (c->cameraPitch() < PITCH_MIN)
        c->setCameraPitch(PITCH_MIN);
}

