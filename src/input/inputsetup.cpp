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
 * the Game.  Default keys (inputdev scan codes) are bound only when no
 * saved bindings load. */
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

    if (!pc->readBindings()) {
        pc->bindKey(1, GS_ACT_MOVE_FORWARD, inputdev::SCAN_UP, 100);  // Up
        pc->bindKey(1, GS_ACT_MOVE_BACK,    0xd0, 100);  // Down
        pc->bindKey(1, GS_ACT_TURN_LEFT,    0xcb, 100);  // Left
        pc->bindKey(1, GS_ACT_TURN_RIGHT,   0xcd, 100);  // Right
        pc->bindKey(1, GS_ACT_ZOOM_IN,      0x1e, 100);  // A
        pc->bindKey(1, GS_ACT_ZOOM_OUT,     0x2c, 100);  // Z (Y on a German keyboard)
        pc->bindKey(1, GS_ACT_RELEASE_BOMB, inputdev::SCAN_B, 100);  // B
        pc->bindKey(1, GS_ACT_HARAKIRI,     0xd3, 100);  // Delete
        pc->bindKey(1, GS_ACT_OVERVIEW,     0x0f, 100);  // Tab
        pc->bindKey(1, GS_ACT_CAM_RIGHT,    0x2e, 100);  // C
        pc->bindKey(1, GS_ACT_CAM_LEFT,     0x2d, 100);  // X
        pc->bindKey(1, GS_ACT_CAM_UP,       0xc9, 100);  // Page Up
        pc->bindKey(1, GS_ACT_CAM_DOWN,     0xd1, 100);  // Page Down
    }
    if (!pc->acquireAll()) {
        windev::messageBox(NULL, GS_CONTROL_NO_DEVICES, GS_CONTROL_ERROR_CAPTION,
                           windev::Buttons::Ok, windev::Icon::Error);
        return 0;
    }
    return 1;
}
