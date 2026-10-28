#include "inputsetup.h"
#include "progctrl.h"
#include "logger.h"
#include "keycodes.h"
#include "game.h"
#include "player.h"
#include "camerainput.h"

  void  
Input_TrySaveSettings(ProgableControl *progCtrl)
{
    g_logger.logMessage(1, "CONTROL: trying to save settings");
    progCtrl->writeBindings();
    g_logger.logMessage(1, "CONTROL: saved settings\n");
}

/* The player's actions (progctrl.h's ActionCallback: key, strength and
 * context, here the Game), each done with the game's events. */
static Player *player_of(void *game) { return static_cast<Game *>(game)->world()->player(); }
static PlayEvents &events_of(void *game) { return *static_cast<Game *>(game)->soundEvents(); }

static void act_move_forward(int, int, void *g) { player_of(g)->actMoveForward(events_of(g)); }
static void act_move_back(int, int, void *g)    { player_of(g)->actMoveBack(events_of(g)); }
static void act_turn_left(int, int, void *g)    { player_of(g)->actTurnLeft(events_of(g)); }
static void act_turn_right(int, int, void *g)   { player_of(g)->actTurnRight(events_of(g)); }
static void act_harakiri(int, int, void *g)     { player_of(g)->actHarakiri(); }
static void act_release_bomb(int, int, void *g) { player_of(g)->actReleaseBomb(); }

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
