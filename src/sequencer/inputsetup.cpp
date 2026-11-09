#include "inputsetup.h"
#include "progctrl.h"
#include "logger.h"
#include "keycodes.h"
#include "game.h"
#include "presses.h"
#include "camerainput.h"

  void  
Input_TrySaveSettings(ProgableControl *progCtrl)
{
    g_logger.logMessage(1, "CONTROL: trying to save settings");
    progCtrl->writeBindings();
    g_logger.logMessage(1, "CONTROL: saved settings\n");
}

/* The player's actions (progctrl.h's ActionCallback: key, strength and
 * context, here the Game) each note their press; the World acts on them
 * once the dispatch is done (Flow_TickPlayingControls). */
static void press(void *game, unsigned p) { static_cast<Game *>(game)->addPress(p); }

static void act_move_forward(int, int, void *g) { press(g, PRESS_FORWARD); }
static void act_move_back(int, int, void *g)    { press(g, PRESS_BACK); }
static void act_turn_left(int, int, void *g)    { press(g, PRESS_LEFT); }
static void act_turn_right(int, int, void *g)   { press(g, PRESS_RIGHT); }
static void act_harakiri(int, int, void *g)     { press(g, PRESS_HARAKIRI); }
static void act_release_bomb(int, int, void *g) { press(g, PRESS_BOMB); }

/* The actions all get the Game as their context.  Default keys are bound
 * first; openroo.ini then overrides the actions it lists. */
  void
Input_Setup(Game *game, ProgableControl *progCtrl)
{
    ProgableControl *pc = progCtrl;
    pc->registerAction(1, "John_Turn_Left",    act_turn_left,         game);
    pc->registerAction(1, "John_Turn_Right",   act_turn_right,        game);
    pc->registerAction(1, "John_Move_Forward", act_move_forward,      game);
    pc->registerAction(1, "John_Move_Back",    act_move_back,         game);
    pc->registerAction(1, "John_Zoom_In",      Camera_ZoomIn,         game);
    pc->registerAction(1, "John_Zoom_Out",     Camera_ZoomOut,        game);
    pc->registerAction(1, "John_Release_Bomb", act_release_bomb,      game);
    pc->registerAction(1, "John_Harakiri",     act_harakiri,          game);
    pc->registerAction(1, "John_OverView",     Camera_Overview,       game);
    pc->registerAction(1, "CamModeRight",    Camera_RotateRight,    game);
    pc->registerAction(1, "CamModeLeft",     Camera_RotateLeft,     game);
    pc->registerAction(1, "CamModeUp",       Camera_TiltUp,         game);
    pc->registerAction(1, "CamModeDown",     Camera_TiltDown,       game);

    {
        using namespace input;
        pc->bindKey(1, "John_Move_Forward", KEY_UP,       100);
        pc->bindKey(1, "John_Move_Back",    KEY_DOWN,     100);
        pc->bindKey(1, "John_Turn_Left",    KEY_LEFT,     100);
        pc->bindKey(1, "John_Turn_Right",   KEY_RIGHT,    100);
        pc->bindKey(1, "John_Zoom_In",      KEY_A,        100);
        pc->bindKey(1, "John_Zoom_Out",     KEY_Z,        100);  // physical key: Y on a German keyboard
        pc->bindKey(1, "John_Release_Bomb", KEY_B,        100);
        pc->bindKey(1, "John_Harakiri",     KEY_DELETE,   100);
        pc->bindKey(1, "John_OverView",     KEY_TAB,      100);
        pc->bindKey(1, "CamModeRight",    KEY_C,        100);
        pc->bindKey(1, "CamModeLeft",     KEY_X,        100);
        pc->bindKey(1, "CamModeUp",       KEY_PAGEUP,   100);
        pc->bindKey(1, "CamModeDown",     KEY_PAGEDOWN, 100);
    }
    pc->readBindings();  // the file changes only the actions it lists
}
