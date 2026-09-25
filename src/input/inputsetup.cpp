/* inputsetup.cpp -- see inputsetup.h. */
#include <windows.h>
#include "inputsetup.h"
#include "progctrl.h"
#include "gamelog.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "game.h"
#include "player.h"
#include "camerainput.h"

extern "C" __declspec(dllexport) void __cdecl
Input_TrySaveSettings(void)
{
    GameLog_LogMessage(&g_logger, 1, GS_CONTROL_SAVE_SETTINGS);
    ProgCtrl_WriteBindings(&g_progCtrl);
    ProgCtrl_Shutdown(&g_progCtrl);
}

/* The joystick's ranges and null zones are set only if its setup succeeds;
 * a failed keyboard or mouse setup is fatal, a missing joystick is not.
 * The player actions get the Player as context, the camera ones the Game.
 * The default keys (DIK_ codes) bind only when no saved bindings load. */
extern "C" __declspec(dllexport) int __cdecl
Input_DirectInputSetup(HINSTANCE hInstance, HWND hwnd, DWORD, Game *game)
{
    ProgableControl *pc = &g_progCtrl;
    if (!ProgCtrl_InitDInput(pc, hInstance) || !ProgCtrl_SetupKbd(pc, hwnd) ||
        !ProgCtrl_SetupMouse(pc, hwnd)) {
        MessageBoxA(NULL, GS_CONTROL_NO_INPUT, GS_CONTROL_ERROR_CAPTION, MB_ICONHAND);
        return 0;
    }
    if (ProgCtrl_SetupJoy(pc, hwnd)) {
        ProgCtrl_SetJoyRange(pc, 0, -100, 100);
        ProgCtrl_SetJoyRange(pc, 4, -100, 100);
        ProgCtrl_SetJoyRange(pc, 8, -100, 100);
        ProgCtrl_SetJoyDeadzone(pc, 0, 5000);
        ProgCtrl_SetJoyDeadzone(pc, 4, 5000);
        ProgCtrl_SetJoyDeadzone(pc, 8, 10000);
    }

    void *player = game->player();
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_TURN_LEFT,    Player_ActTurnLeft,    player);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_TURN_RIGHT,   Player_ActTurnRight,   player);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_MOVE_FORWARD, Player_ActMoveForward, player);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_MOVE_BACK,    Player_ActMoveBack,    player);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_ZOOM_IN,      Camera_ZoomIn,         game);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_ZOOM_OUT,     Camera_ZoomOut,        game);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_RELEASE_BOMB, Player_ActReleaseBomb, player);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_HARAKIRI,     Player_ActHarakiri,    player);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_OVERVIEW,     Camera_Overview,       game);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_CAM_RIGHT,    Camera_RotateRight,    game);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_CAM_LEFT,     Camera_RotateLeft,     game);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_CAM_UP,       Camera_TiltUp,         game);
    ProgCtrl_RegisterAction(pc, 1, GS_ACT_CAM_DOWN,     Camera_TiltDown,       game);

    if (!ProgCtrl_ReadBindings(pc)) {
        ProgCtrl_BindKey(pc, 1, GS_ACT_MOVE_FORWARD, 0xc8, 100);  /* Up */
        ProgCtrl_BindKey(pc, 1, GS_ACT_MOVE_BACK,    0xd0, 100);  /* Down */
        ProgCtrl_BindKey(pc, 1, GS_ACT_TURN_LEFT,    0xcb, 100);  /* Left */
        ProgCtrl_BindKey(pc, 1, GS_ACT_TURN_RIGHT,   0xcd, 100);  /* Right */
        ProgCtrl_BindKey(pc, 1, GS_ACT_ZOOM_IN,      0x1e, 100);  /* A */
        ProgCtrl_BindKey(pc, 1, GS_ACT_ZOOM_OUT,     0x2c, 100);  /* Z (Y on German) */
        ProgCtrl_BindKey(pc, 1, GS_ACT_RELEASE_BOMB, 0x30, 100);  /* B */
        ProgCtrl_BindKey(pc, 1, GS_ACT_HARAKIRI,     0xd3, 100);  /* Delete */
        ProgCtrl_BindKey(pc, 1, GS_ACT_OVERVIEW,     0x0f, 100);  /* Tab */
        ProgCtrl_BindKey(pc, 1, GS_ACT_CAM_RIGHT,    0x2e, 100);  /* C */
        ProgCtrl_BindKey(pc, 1, GS_ACT_CAM_LEFT,     0x2d, 100);  /* X */
        ProgCtrl_BindKey(pc, 1, GS_ACT_CAM_UP,       0xc9, 100);  /* PgUp */
        ProgCtrl_BindKey(pc, 1, GS_ACT_CAM_DOWN,     0xd1, 100);  /* PgDn */
    }
    if (!ProgCtrl_AcquireAll(pc)) {
        MessageBoxA(NULL, GS_CONTROL_NO_DEVICES, GS_CONTROL_ERROR_CAPTION, MB_ICONHAND);
        return 0;
    }
    return 1;
}
