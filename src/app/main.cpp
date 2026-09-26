/* WinMain 0x0042d100 reimplementation (ENDGAME_PLAN.md E8).
 *
 * Window class and window, the log, Game::Load, the CD check, the launcher,
 * the three-rung device ladder, input/sound/CD/render/clock setup, the intro
 * movie, then the message loop that calls RenderGameFrame whenever no movie
 * is playing; WM_QUIT tears everything down in the original's order.
 *
 * Written from the listing, not the decompile: Ghidra's decompiler times out
 * on this function, and the exported decompile (decomp/WinMain.c) has a
 * wrong stack frame -- it overlays a MSG on the WNDCLASSA, and treats the two
 * indirect QueryInterface calls as not popping their arguments, so every
 * stack read after them is off by 0x18 (the `piVar9 = &DAT_0045d748` and
 * `_DAT_0046c704 = hIcon` lines are both artefacts of that).
 *
 * The window class proc, WindowsMessageProcessor 0x0042ce20, is here too
 * (Main_WindowProc): it was the same source file, and it is the other half of
 * the movie-playing flag 0x004dc7c0 and the movie surface 0x004e04a8.
 *
 * Every callee is ours, and so is everything WinMain hands to Windows: the
 * launcher's dialog proc is LauncherDlg_Proc (launcherdialogs.cpp).
 *
 * CreateWindowExA and DialogBoxParamA go through our hooks, as the two
 * CALL_IAT_REDIRECT_PATCHES inside the original did (headless window,
 * KAROO_SKIP_LAUNCHER).
 *
 * KAROO_WINMAIN_FX=norender -- the loop's one decision: never call
 * RenderGameFrame, as if the intro movie never ended.  Messages are still
 * pumped, so --auto-exit still ends the run; every recording fails on
 * frames_run, by far more than the music-on +1.
 */
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include "main.h"
#include <stdlib.h>
#include "gameglobals.h"
#include "gamelog.h"
#include "game.h"
#include "config.h"
#include "cdthemes.h"
#include "direct3d.h"
#include "createdevice.h"
#include "inputsetup.h"
#include "soundmanager.h"
#include "cfaktsound.h"
#include "cdm.h"
#include "renderstate.h"
#include "clock.h"
#include "movie.h"
#include "levelplacements.h"
#include "theme.h"
#include "rendergameframe.h"
#include "launcher.h"
#include "launcherdialogs.h"
#include "nullddraw.h"
#include "progctrl.h"
#include "texture.h"
#include "scenetexture.h"
#include "scene.h"
#include "textrenderer.h"
#include "menuscreens.h"
#include "log.h"
#include "resources.h"
#include "staticinit.h"

/* Shared by WinMain and the WndProc; nothing else reads either. */
static volatile int g_moviePlaying;   /* was 0x004dc7c0 */
static IDirectDrawSurface* g_movieSurface;   /* was 0x004e04a8 */

static const unsigned GAME_ALLOC_SIZE = 0x51790d;
static const unsigned D3D_ALLOC_SIZE  = 0x238;

static bool winmain_fx_norender()
{
    char buf[16];
    bool on = GetEnvironmentVariableA("KAROO_WINMAIN_FX", buf, sizeof(buf))
              && lstrcmpiA(buf, "norender") == 0;
    log_write("winmain: FX mode = %s\n", on ? "norender" : "off");
    return on;
}

/* The scalar deleting dtors free() their object, which is why both objects
 * come from malloc below. */
static void delete_game(Game *g)       { if (g) Game_ScalarDestructor(g, 1); }
static void delete_d3d(Direct3D *d3d)  { if (d3d) Direct3D_ScalarDestructor(d3d, 1); }

/* ── WindowsMessageProcessor 0x0042ce20 ──────────────────────────────────
 *
 * Everything the game does on focus changes, plus the movie and CD-audio
 * notifications.  Every path ends in DefWindowProcA, including the handled
 * ones -- the original never returns its own value.
 *
 * The surfaces restored on activation, in the original's order: two base
 * images, a SceneTexture[6] (0x4dc235, stride 0x1c), a table of ten image
 * pointers (0x4dc134) visited in a scrambled order and skipping NULLs, both
 * texture managers, then ten SceneTextures -- karoo128 and the menu's nine
 * (menuscreens.cpp names them). */
static const int IMAGE_PTR_ORDER[10] = { 2, 5, 0, 6, 1, 4, 7, 3, 8, 9 };
static SceneTexture *const TAIL_TEXTURES[10] = {
    &g_texKaroo128, &g_menuTex1, &g_menuTex2, &g_menuTex3, &g_menuTex4,
    &g_menuTexSelector, &g_menuTexOn, &g_menuTexOff, &g_menuTexKnob, &g_menuTexScale,
};

/* FaktMovie::state == 1 is "finished" (movie.h). */
static const DWORD MOVIE_STATE_FINISHED = 1;
static const UINT  WM_MOVIE_EVENT       = 0x464;

static void restore_surfaces()
{
    Texture_Load(&g_fontMain.atlas()->base);
    Texture_Load(&g_fontNumbers.atlas()->base);
    for (int i = 0; i < 6; i++)
        Texture_Load(&g_themeBlock.sky.Textures[i].base);
    for (int i = 0; i < 10; i++) {
        SceneTexture *img = g_themeBlock.images[IMAGE_PTR_ORDER[i]];
        if (img)
            Texture_Load(&img->base);
    }
    TextureManager_LoadAll(&g_textureManager);
    TextureManager_LoadAll(&g_scene.textures);
    for (int i = 0; i < 10; i++)
        Texture_Load(&TAIL_TEXTURES[i]->base);
}

/* KAROO_WNDPROC_FX=noquit -- WM_DESTROY skips PostQuitMessage, so the
 * window goes but WinMain's loop never sees WM_QUIT.  Only this WndProc
 * turns a closed window into the end of the run. */
static bool wndproc_fx_noquit()
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_WNDPROC_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "noquit") == 0;
        log_write("wndproc: FX mode = %s\n", cached ? "noquit" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) LRESULT CALLBACK
Main_WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_DESTROY:
        if (!wndproc_fx_noquit())
            PostQuitMessage(1);
        break;

    case WM_ACTIVATE:
        /* Exactly WA_ACTIVE: a click-activation (2) counts as losing focus. */
        if ((WORD)wParam == WA_ACTIVE) {
            ProgCtrl_AcquireAll(&g_progCtrl);
            restore_surfaces();
            if (g_moviePlaying) {
                Movie_SetWindow(&g_movie, g_movieSurface);
                Movie_Play(&g_movie);
            }
        } else {
            ProgCtrl_UnacquireAll(&g_progCtrl);
            if (g_moviePlaying)
                Movie_Pause(&g_movie);
        }
        break;

    case WM_KEYUP:   /* any key skips the intro */
        if (g_moviePlaying) {
            Movie_Stop(&g_movie);
            Movie_Teardown(&g_movie);
            g_moviePlaying = 0;
        }
        break;

    case MM_MCINOTIFY:   /* a track ended: restart it if it repeats */
        if (wParam == MCI_NOTIFY_SUCCESSFUL && g_cdAudio.repeat)
            CDM_PlayTrack(&g_cdAudio, g_cdAudio.tracknumber, true);
        break;

    case WM_MOVIE_EVENT:
        if (g_moviePlaying)
            Movie_Notify(&g_movie, (DWORD)hWnd, wParam, lParam);
        /* Checked whether or not a movie was playing. */
        if (g_movie.state == MOVIE_STATE_FINISHED)
            *&g_moviePlaying = 0;
        break;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static int winmain_body(HINSTANCE hInstance, LPSTR lpCmdLine);

/* The original's global objects were built before WinMain and torn down
 * after it returned (staticinit.h); every return path goes through here. */
extern "C" __declspec(dllexport) int WINAPI
Main_WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    StaticInit_Construct();
    int r = winmain_body(hInstance, lpCmdLine);
    StaticInit_Destruct();
    return r;
}

static int winmain_body(HINSTANCE hInstance, LPSTR lpCmdLine)
{
    g_moduleInstance = hInstance;
    /* The original stored the absolute current directory here and prefixed
     * it onto every data path, which it then copied into fixed buffers as
     * small as 0x80 bytes: a long enough install path overflowed them (the
     * script-text reader's overflow reached its text counter).  Nothing
     * changes the current directory, so "." names the same files at any
     * install depth. */
    strcpy(g_gameDir, ".");

    WNDCLASSA wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = Main_WindowProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconA(Resources_Module(), MAKEINTRESOURCEA(0x6a));
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "Karoo";
    if (RegisterClassA(&wc) == 0)
        return 0;

    HWND hWnd = hooks_CreateWindowExA(WS_EX_APPWINDOW, "Karoo", "Ka'roo",
                                      WS_POPUP, 0, 0, 400, 300,
                                      NULL, NULL, hInstance, NULL);
    if (hWnd == NULL)
        return 0;

    GameLog_OpenLogFile(&g_logger, "JJ.log", NULL);

    /* The original tests the first character, not the pointer.  None of the
     * early returns below destroys the window. */
    if (lpCmdLine[0] == '\0') {
        MessageBoxA(hWnd, "Please append the name of the game file at the "
                    "prompt! (e.g.: Karoo.exe <gamefile>)", "Ka'Roo", MB_OK);
        return 0;
    }

    Game *game = (Game *)malloc(GAME_ALLOC_SIZE);
    Game::set_instance(game ? game->construct(lpCmdLine) : NULL);
    if (Game::instance() == NULL)
        return 0;
    game = Game::instance();

    while (!game->cdThemes()->validateTrackLengths()) {
        if (MessageBoxA(hWnd, "Please insert the Ka'Roo - CD-ROM!", "Ka'Roo",
                        MB_OKCANCEL) == IDCANCEL) {
            delete_game(game);
            return 0;
        }
    }

    if (!game->initialised()) {
        delete_game(game);
        return 0;
    }

    /* Cancel: the one early exit that destroys the window, and returns 1. */
    if (hooks_DialogBoxParamA(Resources_Module(), MAKEINTRESOURCEA(0x68), NULL,
                              LauncherDlg_Proc, 0) == 0) {
        DestroyWindow(hWnd);
        delete_game(game);
        return 1;
    }

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    Direct3D *d3d = (Direct3D *)malloc(D3D_ALLOC_SIZE);
    g_pDirect3D = d3d ? Direct3D_Construct(d3d) : NULL;
    d3d = g_pDirect3D;

    /* The ladder: the launcher's adapter and mode, then the default adapter
     * in that mode, then the default adapter in mode 0.  Game is re-read from
     * the global between rungs in the original; nothing can change it.  The
     * mode index is a DWORD field, but the original pushes only its low
     * byte (`mov dl, [game+0x2aa14e]`). */
    Config *cfg = game->config();
    const unsigned char mode = (unsigned char)cfg->displayModeIndex();
    if (!Direct3D_CreateD3DDevice(d3d, hWnd, cfg->adapterGuid(), mode, true)
        && !Direct3D_CreateD3DDevice(d3d, hWnd, NULL, mode, true)
        && !Direct3D_CreateD3DDevice(d3d, hWnd, NULL, 0, true)) {
        GameLog_LogSourceLocation(&g_logger, 4,
            "E:\\WORK\\VC++\\JumpinJohn\\JumpinJohn\\main.cpp", 0x73f,
            "Creation of Direct3D failed");
        d3d->pDD4->RestoreDisplayMode();
        MessageBoxA(NULL, d3d->pLastError, "Error!", MB_ICONHAND);
        Direct3D_ReleaseResources(d3d);
        delete_d3d(d3d);
        delete_game(game);
        return 1;
    }

    if (Input_DirectInputSetup(hInstance, hWnd, (DWORD)&g_logger, game) == 0) {
        Direct3D_ReleaseResources(d3d);
        delete_d3d(d3d);
        delete_game(game);
        return 1;
    }

    /* 3D sound, 22050 Hz, 16-bit stereo; rolloff 0.3 if it came up. */
    SoundManager *snd = game->soundManager();
    if (SoundMgr_Init(snd, 1, hWnd, 0, 2, 22050, 16, &g_logger))
        CFaktSound_Apply3DRolloffParams(snd->cfaktSound(), 0.3f, DS3D_IMMEDIATE);

    CDM_SetWindowHandle(&g_cdAudio, hWnd);
    ShowWindow(hWnd, SW_SHOW);
    Render_ConfigureRenderState();
    hooks_ClockInit();

    /* The movie draws through the DirectDraw1 interface and the primary's
     * surface-1 interface.  Both calls go through our COM proxies, exactly as
     * the original's did. */
    IDirectDraw *dd1 = NULL;
    d3d->pDD4->QueryInterface(IID_IDirectDraw, (void **)&dd1);
    d3d->pPrimary->QueryInterface(IID_IDirectDrawSurface,
                                  (void **)&g_movieSurface);

    /* FaktMovie's overlay colour key (movie.h): use it, CK_RGB, black..black.
     * The original MaybeLoadVideo hands it to IMixerPinConfig::SetColorKey;
     * our movie.cpp plays nothing, so it is unread today, but it is set so a
     * real player could use it.  The original's PaletteIndex store is
     * `mov eax,[esp+0x40]` -- the MSG's `message`, before the MSG is first
     * written, i.e. uninitialised stack.  CK_RGB ignores PaletteIndex, so it
     * is left as the ctor's zero. */
    g_movie.useColorKey = 1;
    g_movie.colorKey[0] = 2;   /* CK_RGB */
    g_movie.colorKey[2] = 0;
    g_movie.colorKey[3] = 0;

    bool playing = false;
    if (Movie_Setup(&g_movie, &g_logger)) {
        /* The original sprintfs into a 128-byte stack buffer and overruns it
         * for an install path past ~110 characters; ours is MAX_PATH-sized so
         * the same path loads instead of smashing the frame. */
        char path[MAX_PATH + 32];
        snprintf(path, sizeof(path), "%s\\Video\\intro.avi", g_gameDir);
        if (Movie_LoadVideo(&g_movie, hWnd, dd1, g_movieSurface, path) >= 0)
            playing = true;
        else
            GameLog_LogMessage(&g_logger, 3, "MAIN: Couldn't load %s .", path);
    }
    g_moviePlaying = playing ? 1 : 0;
    dd1->Release();
    if (playing) {
        Movie_SetWindow(&g_movie, g_movieSurface);
        Movie_Play(&g_movie);
    }

    const bool norender = winmain_fx_norender();
    MSG msg;
    for (;;) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        /* Cleared by WindowsMessageProcessor when the movie finishes. */
        if (g_moviePlaying == 0 && !norender)
            Render_RenderGameFrame();
    }

    /* Shutdown order is the original's: settings are saved after Game is
     * gone, and the device goes last. */
    LevelPlacements_Release(&g_levelPlacements);
    delete_game(Game::instance());
    Input_TrySaveSettings();
    Theme_ReleaseBlock(&g_themeBlock);
    Direct3D_ReleaseResources(g_pDirect3D);
    delete_d3d(g_pDirect3D);
    return (int)msg.wParam;
}
