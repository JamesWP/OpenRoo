#include "inputsetup.h"
#include "progctrl.h"
#include "logger.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "game.h"
#include "player.h"
#include "camerainput.h"
#include "windev.h"

  void  
Input_TrySaveSettings(void)
{
    g_logger.logMessage(1, "CONTROL: trying to save settings");
    g_progCtrl.writeBindings();
    g_progCtrl.shutdown();
    g_logger.logMessage(1, "CONTROL: saved settings\n");
}

/* The player actions get the Player as their context, the camera actions
 * the Game.  Default keys are bound first; openroo.ini then overrides the actions it lists. */
  int  
Input_Setup(void *hwnd, Game *game)
{
    ProgableControl *pc = &g_progCtrl;
    if (!pc->setupDevices(hwnd)) {
        windev::messageBox(NULL, GS_CONTROL_NO_INPUT, GS_CONTROL_ERROR_CAPTION,
                           windev::Buttons::Ok, windev::Icon::Error);
        return 0;
    }
    pc->setJoyRange(0, -100, 100);
    pc->setJoyRange(4, -100, 100);
    pc->setJoyRange(8, -100, 100);
    pc->setJoyDeadzone(0, 5000);
    pc->setJoyDeadzone(4, 5000);
    pc->setJoyDeadzone(8, 10000);

    void *player = game->player();
    pc->registerAction(1, GS_ACT_TURN_LEFT,    Player_ActTurnLeft,    player);
    pc->registerAction(1, GS_ACT_TURN_RIGHT,   Player_ActTurnRight,   player);
    pc->registerAction(1, GS_ACT_MOVE_FORWARD, Player_ActMoveForward, player);
    pc->registerAction(1, GS_ACT_MOVE_BACK,    Player_ActMoveBack,    player);
    pc->registerAction(1, GS_ACT_ZOOM_IN,      Camera_ZoomIn,         game);
    pc->registerAction(1, GS_ACT_ZOOM_OUT,     Camera_ZoomOut,        game);
    pc->registerAction(1, GS_ACT_RELEASE_BOMB, Player_ActReleaseBomb, player);
    pc->registerAction(1, GS_ACT_HARAKIRI,     Player_ActHarakiri,    player);
    pc->registerAction(1, GS_ACT_OVERVIEW,     Camera_Overview,       game);
    pc->registerAction(1, GS_ACT_CAM_RIGHT,    Camera_RotateRight,    game);
    pc->registerAction(1, GS_ACT_CAM_LEFT,     Camera_RotateLeft,     game);
    pc->registerAction(1, GS_ACT_CAM_UP,       Camera_TiltUp,         game);
    pc->registerAction(1, GS_ACT_CAM_DOWN,     Camera_TiltDown,       game);

    {
        using namespace inputdev;
        pc->bindKey(1, GS_ACT_MOVE_FORWARD, KEY_UP,       100);
        pc->bindKey(1, GS_ACT_MOVE_BACK,    KEY_DOWN,     100);
        pc->bindKey(1, GS_ACT_TURN_LEFT,    KEY_LEFT,     100);
        pc->bindKey(1, GS_ACT_TURN_RIGHT,   KEY_RIGHT,    100);
        pc->bindKey(1, GS_ACT_ZOOM_IN,      KEY_A,        100);
        pc->bindKey(1, GS_ACT_ZOOM_OUT,     KEY_Z,        100);  // physical key: Y on a German keyboard
        pc->bindKey(1, GS_ACT_RELEASE_BOMB, KEY_B,        100);
        pc->bindKey(1, GS_ACT_HARAKIRI,     KEY_DELETE,   100);
        pc->bindKey(1, GS_ACT_OVERVIEW,     KEY_TAB,      100);
        pc->bindKey(1, GS_ACT_CAM_RIGHT,    KEY_C,        100);
        pc->bindKey(1, GS_ACT_CAM_LEFT,     KEY_X,        100);
        pc->bindKey(1, GS_ACT_CAM_UP,       KEY_PAGEUP,   100);
        pc->bindKey(1, GS_ACT_CAM_DOWN,     KEY_PAGEDOWN, 100);
    }
    pc->readBindings();  // the file changes only the actions it lists
    if (!pc->acquireAll()) {
        windev::messageBox(NULL, GS_CONTROL_NO_DEVICES, GS_CONTROL_ERROR_CAPTION,
                           windev::Buttons::Ok, windev::Icon::Error);
        return 0;
    }
    return 1;
}
