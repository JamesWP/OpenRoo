/* camerainput.cpp -- see camerainput.h.  Written from the listing.  The
 * step constants are the originals' floats (0.01f, 0.001f, 0.05f), widened
 * to double against the double dt as the x87 did. */
#include <windows.h>
#include "camerainput.h"
#include "game.h"
#include "config.h"
#include "levelmap.h"
#include "player.h"

static const double ZOOM_RATE  = (double)0.01f;
static const double TURN_RATE  = (double)0.001f;
static const double TILT_RATE  = (double)0.05f;
static const float  ZOOM_MAX   = 20.0f;
static const float  ZOOM_MIN   = 2.0f;
static const float  PITCH_MAX  = 89.0f;
static const float  PITCH_MIN  = 50.0f;

static inline double dt(Game *g) { return g->tickStep()->value; }

extern "C" {

__declspec(dllexport) void __cdecl
Camera_ZoomOut(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->zoomDistance() < ZOOM_MAX)
        g->setZoomDistance((float)(dt(g) * ZOOM_RATE + g->zoomDistance()));
    g->setField13cc90(1);
}

__declspec(dllexport) void __cdecl
Camera_ZoomIn(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->zoomDistance() > ZOOM_MIN)
        g->setZoomDistance((float)(g->zoomDistance() - dt(g) * ZOOM_RATE));
    g->setField13cc90(1);
}

/* Only on a level without the bonus flag, and only while the player is
 * alive (moveState 0). */
__declspec(dllexport) void __cdecl
Camera_Overview(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    if (g->map()->bonus() != 0 || g->player()->moveState() != 0)
        return;
    g->setOverviewActive(1);
    g->setCameraDistance(40.0f);
}

__declspec(dllexport) void __cdecl
Camera_RotateRight(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    Config *c = g->config();
    c->setCameraYaw((float)(dt(g) * TURN_RATE + c->cameraYaw()));
}

__declspec(dllexport) void __cdecl
Camera_RotateLeft(int, int, void *ctx)
{
    Game *g = (Game *)ctx;
    Config *c = g->config();
    c->setCameraYaw((float)(c->cameraYaw() - dt(g) * TURN_RATE));
}

/* Only in game state 1.  Step while at or below the limit, then clamp. */
__declspec(dllexport) void __cdecl
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

__declspec(dllexport) void __cdecl
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

}
