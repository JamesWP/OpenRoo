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

/* The player actions get the Player as their context, the camera actions
 * the Game.  Default keys are bound first; openroo.ini then overrides the actions it lists. */
  void
Input_Setup(Game *game, ProgableControl *progCtrl)
{
    ProgableControl *pc = progCtrl;
    void *player = game->world()->player();
    pc->registerAction(1, "John_Turn_Left",    Player_ActTurnLeft,    player);
    pc->registerAction(1, "John_Turn_Right",   Player_ActTurnRight,   player);
    pc->registerAction(1, "John_Move_Forward", Player_ActMoveForward, player);
    pc->registerAction(1, "John_Move_Back",    Player_ActMoveBack,    player);
    pc->registerAction(1, "John_Zoom_In",      Camera_ZoomIn,         game);
    pc->registerAction(1, "John_Zoom_Out",     Camera_ZoomOut,        game);
    pc->registerAction(1, "John_Release_Bomb", Player_ActReleaseBomb, player);
    pc->registerAction(1, "John_Harakiri",     Player_ActHarakiri,    player);
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
