/* WinMain: the window, the game log, the Game object, the CD check, the
 * launcher dialog, the Direct3D device (three attempts), input, sound, CD
 * audio, render state and clock, the intro movie, then the message loop, which
 * renders a frame whenever no movie is playing.  Shutdown releases in a fixed
 * order.
 *
 * KAROO_WINMAIN_FX=norender is a negative control: the loop never renders, as
 * if the intro never ended.  Messages are still pumped, so --auto-exit still
 * ends the run; every recording fails on frames_run. */

#include <windows.h>
#include <stdio.h>
#include "main.h"
#include <stdlib.h>
#include "gameglobals.h"
#include "gamelog.h"
#include "game.h"
#include "config.h"
#include "cdthemes.h"
#include "renderdevice.h"
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
#include "scenetexture.h"
#include "scene.h"
#include "textrenderer.h"
#include "menuscreens.h"
#include "log.h"
#include "resources.h"
#include "staticinit.h"

/* Shared by WinMain and the window procedure; nothing else reads either. */
static volatile int g_moviePlaying;
static void *g_movieSurface;  // the primary surface, for the movie player

static const unsigned GAME_ALLOC_SIZE = 0x51790d;

static bool winmain_fx_norender()
{
    char buf[16];
    bool on = GetEnvironmentVariableA("KAROO_WINMAIN_FX", buf, sizeof(buf))
              && lstrcmpiA(buf, "norender") == 0;
    log_write("winmain: FX mode = %s\n", on ? "norender" : "off");
    return on;
}

/* The scalar deleting destructors free() their object, which is why both
 * objects come from malloc. */
static void delete_game(Game *g)       { if (g) Game::scalarDeletingDtor(g, 1); }

/* The window procedure: input devices and surfaces on focus changes, the intro
 * movie's events, and CD track repeats.  Every path ends in DefWindowProcA,
 * the handled ones included.
 *
 * Surfaces are restored on activation in this order: the two font atlases, the
 * six sky textures, the theme's ten images in a fixed shuffled order (skipping
 * empty ones), both texture managers, then the logo and the menu's nine
 * textures. */
static const int IMAGE_PTR_ORDER[10] = { 2, 5, 0, 6, 1, 4, 7, 3, 8, 9 };
static SceneTexture *const TAIL_TEXTURES[10] = {
    &g_texKaroo128, &g_menuTex1, &g_menuTex2, &g_menuTex3, &g_menuTex4,
    &g_menuTexSelector, &g_menuTexOn, &g_menuTexOff, &g_menuTexKnob, &g_menuTexScale,
};

/* The movie's "finished" state (movie.h). */
static const DWORD MOVIE_STATE_FINISHED = 1;
static const UINT  WM_MOVIE_EVENT       = 0x464;

static void restore_surfaces()
{
    g_fontMain.atlas()->load();
    g_fontNumbers.atlas()->load();
    for (int i = 0; i < 6; i++)
        g_themeBlock.sky.textures()[i].load();
    for (int i = 0; i < 10; i++) {
        SceneTexture *img = g_themeBlock.images[IMAGE_PTR_ORDER[i]];
        if (img)
            img->load();
    }
    g_textureManager.loadAll();
    g_scene.textures()->loadAll();
    for (int i = 0; i < 10; i++)
        TAIL_TEXTURES[i]->load();
}

/* KAROO_WNDPROC_FX=noquit is a negative control: closing the window does not
 * post WM_QUIT, so the run never ends by itself. */
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
        // Exactly WA_ACTIVE: a click activation (2) counts as losing focus.
        if ((WORD)wParam == WA_ACTIVE) {
            g_progCtrl.acquireAll();
            restore_surfaces();
            if (g_moviePlaying) {
                g_movie.setWindow(g_movieSurface);
                g_movie.play();
            }
        } else {
            g_progCtrl.unacquireAll();
            if (g_moviePlaying)
                g_movie.pause();
        }
        break;

    case WM_KEYUP:  // any key skips the intro
        if (g_moviePlaying) {
            g_movie.stop();
            g_movie.teardown();
            g_moviePlaying = 0;
        }
        break;

    case MM_MCINOTIFY:  // a track ended: restart it if it repeats
        if (wParam == MCI_NOTIFY_SUCCESSFUL && g_cdAudio.repeating())
            g_cdAudio.playTrack(g_cdAudio.track(), true);
        break;

    case WM_MOVIE_EVENT:
        if (g_moviePlaying)
            g_movie.notify((DWORD)hWnd, wParam, lParam);
        // Checked whether or not a movie was playing.
        if (g_movie.movieState() == MOVIE_STATE_FINISHED)
            *&g_moviePlaying = 0;
        break;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static int winmain_body(HINSTANCE hInstance, LPSTR lpCmdLine);

/* Every return path goes through here, so the globals are torn down however
 * WinMain ends. */
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
    // Data paths are relative to the current directory, which nothing changes.
    // An absolute prefix could overflow the fixed path buffers on a deep
    // install.
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

    g_logger.openLogFile("JJ.log", NULL);

    // The first character is tested, not the pointer.  None of the early
    // returns below destroys the window.
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

    // Cancel: the one early exit that destroys the window, and returns 1.
    if (hooks_DialogBoxParamA(Resources_Module(), MAKEINTRESOURCEA(0x68), NULL,
                              LauncherDlg_Proc, 0) == 0) {
        DestroyWindow(hWnd);
        delete_game(game);
        return 1;
    }

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    RenderDevice *d3d = g_renderDevice = new RenderDevice();

    // Three attempts: the launcher's adapter and mode, the default adapter in
    // that mode, the default adapter in mode 0.
    Config *cfg = game->config();
    const int mode = (int)cfg->displayModeIndex();
    if (!d3d->Create(hWnd, cfg->adapterGuid(), mode, true)
        && !d3d->Create(hWnd, NULL, mode, true)
        && !d3d->Create(hWnd, NULL, 0, true)) {
        g_logger.logSourceLocation(4,
            "src/app/main.cpp", __LINE__,
            "Creation of Direct3D failed");
        d3d->RestoreDisplayMode();
        MessageBoxA(NULL, d3d->lastError(), "Error!", MB_ICONHAND);
        delete d3d;
        delete_game(game);
        return 1;
    }

    if (Input_DirectInputSetup(hInstance, hWnd, (DWORD)&g_logger, game) == 0) {
        delete d3d;
        delete_game(game);
        return 1;
    }

    // 3D sound, 22050 Hz, 16-bit stereo; rolloff 0.3 if it came up.
    SoundManager *snd = game->soundManager();
    if (snd->init(1, hWnd, 0, 2, 22050, 16, &g_logger))
        snd->cfaktSound()->apply3DRolloffParams(0.3f, DS3D_IMMEDIATE);

    g_cdAudio.setWindowHandle(hWnd);
    ShowWindow(hWnd, SW_SHOW);
    Render_ConfigureRenderState();
    hooks_ClockInit();

    // The movie draws through the DirectDraw 1 interface and the primary
    // surface's version-1 interface.
    void *dd1 = NULL;
    d3d->GetMovieTarget(&dd1, &g_movieSurface);

    // The movie's overlay colour key: on, CK_RGB, black to black.  Nothing
    // plays a movie yet (movie.cpp), so it is set for a player that would use
    // it.  The palette index is left zero; CK_RGB ignores it.
    g_movie.setColorKey(2 /* CK_RGB */, 0, 0);

    bool playing = false;
    if (g_movie.setup(&g_logger)) {
        // MAX_PATH-sized, so a long install path cannot overflow it.
        char path[MAX_PATH + 32];
        snprintf(path, sizeof(path), "%s\\Video\\intro.avi", g_gameDir);
        if (g_movie.loadVideo(hWnd, dd1, g_movieSurface, path) >= 0)
            playing = true;
        else
            g_logger.logMessage(3, "MAIN: Couldn't load %s .", path);
    }
    g_moviePlaying = playing ? 1 : 0;
    if (dd1)
        ((IUnknown *)dd1)->Release();
    if (playing) {
        g_movie.setWindow(g_movieSurface);
        g_movie.play();
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
        // Cleared by the window procedure when the movie finishes.
        if (g_moviePlaying == 0 && !norender)
            Render_RenderGameFrame();
    }

    // Settings are saved after the Game is deleted; the device goes last.
    LevelPlacements_Release(&g_levelPlacements);
    delete_game(Game::instance());
    Input_TrySaveSettings();
    Theme_ReleaseBlock(&g_themeBlock);
    delete g_renderDevice;
    g_renderDevice = NULL;
    return (int)msg.wParam;
}
