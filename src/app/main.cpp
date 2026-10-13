/* WinMain: the window, the game log, the Game object, the CD check, the
 * launcher dialog, the Direct3D device (three attempts), input, sound, CD
 * audio, render state and clock, the intro movie, then the message loop, which
 * renders a frame whenever no movie is playing.  Shutdown releases in a fixed
 * order. */

#include <strings.h>
#include <stdio.h>
#include <string>
#include "sysdev.h"
#include <new>
#include "main.h"
#include <stdlib.h>
#include "logger.h"
#include "game.h"
#include "config.h"
#include "cdthemes.h"
#include <memory>
#include "renderdevice.h"
#include "glrenderdevice.h"
#include "inputsetup.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "inputdev.h"
#include "keys.h"
#include "cdm.h"
#include "renderstate.h"
#include "renderer.h"
#include "menuart.h"
#include "clock.h"
#include <string.h>
#include "videodev.h"
#include "record.h"
#include "gameboot.h"
#include "gamestate.h"
#include "levelplacements.h"
#include "theme.h"
#include "frame.h"
#include "launcher.h"
#include "launcherdialogs.h"
#include "windev.h"
#include "debugui.h"
#include "prof.h"
#include "progctrl.h"
#include "texture.h"
#include "scene.h"
#include "textrenderer.h"
#include "menuscreens.h"
#include "keypress.h"
#include "levelselect.h"


/* The render state (renderer.h).  app owns it and passes it down; nothing
 * below app reaches it through a global. */
static Renderer s_renderer;

/* The menu's art (menuart.h).  app owns it and passes it down, as it does
 * the render state. */
static MenuArt s_menuArt;

/* The programmable control (progctrl.h).  app owns it and passes it down; it
 * outlives the Game, so the bindings can be saved after the Game is deleted. */
static ProgableControl s_progCtrl;

static void delete_game(Game *g)       { Game_Shutdown(g); delete g; }


/* Draws the intro's current frame, when there is a new one. */
static void present_movie_frame(videodev::Player &movie, RenderDevice *dev)
{
    if (!movie.update()) {
        sysdev::sleepMs(2);  // nothing due yet
        return;
    }
    int w, h;
    const unsigned char *rgba = movie.frame(&w, &h);
    if (!rgba)
        return;
    static Image image;
    if (image.width != w || image.height != h) {
        image.width = w;
        image.height = h;
        image.sourceBits = 24;
        snprintf(image.name, sizeof(image.name), "intro");
        image.rgba.resize((size_t)w * h * 4);
    }
    memcpy(image.rgba.data(), rgba, image.rgba.size());
    dev->PresentImage(image);
}

/* Between messages: a frame, or the intro's while it plays. */
static void idle(Game *game, videodev::Player &movie)
{
    inputdev::setKeyboardSuppressed(windev::debugUiCapturesKeyboard());
    // Cleared when the movie finishes or is skipped (a headless one, at once).
    if (movie.playing())
        present_movie_frame(movie, s_renderer.device);
    else
        Core_RunFrame(game, &s_renderer, &s_menuArt, &s_progCtrl);
}

/* The window's events: surfaces and the intro movie on focus changes, and the
 * keys that skip the intro.
 *
 * Surfaces are restored on activation in this order: the two font atlases, the
 * six sky textures, the theme's ten images in a fixed shuffled order (skipping
 * empty ones), both texture managers, then the logo and the menu's nine
 * textures. */
static const int IMAGE_PTR_ORDER[10] = { 2, 5, 0, 6, 1, 4, 7, 3, 8, 9 };


static void restore_surfaces(Renderer *renderer)
{
    RenderDevice *dev = renderer->device;
    renderer->fontMain.atlas()->reupload(dev);
    renderer->fontNumbers.atlas()->reupload(dev);
    for (int i = 0; i < 6; i++)
        renderer->themeBlock.sky().textures()[i].reupload(dev);
    for (int i = 0; i < 10; i++) {
        Texture *img = renderer->themeBlock.image(IMAGE_PTR_ORDER[i]);
        if (img)
            img->reupload(dev);
    }
    renderer->textures.reuploadAll(dev);
    renderer->scene.textures()->reuploadAll(dev);
    Texture *const tail[10] = {
        &renderer->texKaroo128, &s_menuArt.tex1, &s_menuArt.tex2, &s_menuArt.tex3, &s_menuArt.tex4,
        &s_menuArt.selector, &s_menuArt.on, &s_menuArt.off, &s_menuArt.knob, &s_menuArt.scale,
    };
    for (int i = 0; i < 10; i++)
        tail[i]->reupload(dev);
}

class MainWindow : public windev::WindowHandler {
public:
    explicit MainWindow(videodev::Player &movie) : movie_(movie) {}

    void onDestroyed() override
    {
        windev::quit(1);
    }

    void onActivate(bool active) override
    {
        if (active) {
            restore_surfaces(&s_renderer);
            if (movie_.playing())
                movie_.play();
        } else {
            if (movie_.playing())
                movie_.pause();
        }
    }

    // Any key skips the intro -- one pressed during it: the release of the
    // key that started the game (the launcher's Enter) must not.
    void onKeyDown() override
    {
        keyDownDuringIntro_ = movie_.playing();
    }

    void onKeyUp() override
    {
        if (movie_.playing() && keyDownDuringIntro_)
            movie_.skip();
    }

private:
    videodev::Player &movie_;
    bool keyDownDuringIntro_ = false;
};

/* Run at every clock_advance(), which ends a frame: the state log, the recorder,
 * and the end of a replay.  A replay ends on the recording's length, never on
 * wall time.  The state is dumped while the level is still live (teardown
 * clears the score), then the window is closed so the game shuts down
 * normally. */
static void frame_boundary(void)
{
    static bool replayEnded;

    gamestate_tick();
    record_frame_boundary();
    if (record_replaying() && record_replay_finished() && !replayEnded) {
        replayEnded = true;
        gamestate_dump("replay-finished");
        launcher_end_run("replay finished");
    }
}

/* Where the game's files are.  Data paths are relative to the current
 * directory, which nothing changes; an absolute prefix could overflow the
 * fixed path buffers on a deep install. */
static const char *const kGameDir = ".";

int Main_WinMain(const char *lpCmdLine)
{
    videodev::Player movie;
    clock_setFrameHook(frame_boundary);
    audiodev::setLog(log_sink);
    audiodev::setPathResolver([](const char *path) { return sysdev::nativePath(path); });
    inputdev::setLog(log_sink);
    videodev::setLog(log_sink);
    windev::setLog(log_sink);

    MainWindow handler(movie);
    windev::Window window;
    std::unique_ptr<RenderDevice> device = createGLRenderDevice();
    windev::WindowConfig wc;
    wc.title       = "Ka'roo";
    wc.width       = 400;
    wc.height      = 300;
    wc.messageOnly = device->headless();
    if (!window.create(&handler, wc))
        return 0;
    void *hWnd = window.handle();

    // The game file is the command line; with none, the one the game ships with.
    const char *gameName = lpCmdLine[0] ? lpCmdLine : "JJ";

    Game *game = new (std::nothrow) Game();
    if (game != NULL)
        Game_Boot(game, gameName, kGameDir);
    Game::set_instance(game);
    if (game == NULL)
        return 0;

    while (!game->audio()->cdThemes()->validateTrackLengths()) {
        if (!windev::messageBox(hWnd, "Please insert the Ka'Roo - CD-ROM!",
                                "Ka'Roo", windev::Buttons::OkCancel)) {
            delete_game(game);
            return 0;
        }
    }

    if (!game->initialised()) {
        delete_game(game);
        return 0;
    }

    // Cancel: the one early exit that destroys the window, and returns 1.
    bool play = true;
    if (launcher_skipped())
        g_logger.write("launcher: dialog skipped\n");
    else
        play = LauncherDlg_Show(NULL, *device);
    if (!play) {
        window.destroy();
        delete_game(game);
        return 1;
    }

    window.show(false);

    RenderDevice *d3d = s_renderer.device = device.get();

    // Three attempts: the launcher's adapter and mode, the default adapter in
    // that mode, the default adapter in mode 0.
    Config *cfg = game->config();
    const int mode = (int)cfg->displayModeIndex();
    if (!d3d->Create(&window, cfg->adapterId(), mode)
        && !d3d->Create(&window, NULL, mode)
        && !d3d->Create(&window, NULL, 0)) {
        g_logger.logSourceLocation(4,
            "src/app/main.cpp", __LINE__,
            "Creation of the render device failed");
        windev::messageBox(NULL, d3d->lastError(), "Error!",
                           windev::Buttons::Ok, windev::Icon::Error);
        delete_game(game);
        return 1;
    }

    // The game's keys: the keyboard, or a replay in its place; a recording
    // sees what the game is given.
    input::addSource(&inputdev::keyboard());
    input::addSource(replay_source());
    input::addObserver(record_observer());
    Input_Setup(game, &s_progCtrl);

    // 3D sound, 22050 Hz, 16-bit stereo; rolloff 0.3 if it came up.
    SoundManager *snd = game->audio()->soundManager();
    if (snd->init(1, 2, 22050, 16))
        snd->device()->setListenerRolloff(0.3f, true);

    window.show(true);
    if (!Render_ConfigureRenderState(&s_renderer, game->world(), game->camera()->pose(), kGameDir))
        windev::quit(1);
    Menu_BuildMenuGeometry(&s_menuArt, s_renderer.device, kGameDir);

    // KAROO_START_LEVEL: straight into a fresh game at that level, past the
    // menus, as the bot harness runs one level at a time.
    const std::string startLevel = sysdev::getEnv("KAROO_START_LEVEL");
    if (!startLevel.empty()) {
        const int level = LevelSelect_Find(game->session(), startLevel.c_str());
        if (level < 0) {
            g_logger.logMessage(4, "MAIN: KAROO_START_LEVEL=%s names no level", startLevel.c_str());
            windev::quit(1);
        } else {
            g_logger.logMessage(1, "MAIN: starting at level %d (%s)", level,
                                game->session()->levelNameTableEntry((unsigned char)level));
            Flow_StartFreshGame(game, level);
        }
    }
    clock_start();

    // The intro is only shown to a person at a display: a headless or replayed
    // run takes it as ending at once, so its frame counts do
    // not depend on it.
    const bool show = !d3d->headless() && !record_replaying();
    const std::string path = std::string(kGameDir) + "/video/INTRO.AVI";
    if (movie.load(sysdev::nativePath(path.c_str()).c_str(), show))
        movie.play();
    else
        g_logger.logMessage(3, "MAIN: Couldn't load %s .", path.c_str());

    if (!d3d->headless())
        debugui::init(*d3d);

    prof::initSlowFrames();
    const int exitCode = windev::runMessageLoop([game, &movie] { idle(game, movie); });
    prof::dumpSlowFrames();
    debugui::shutdown(*d3d);

    // Settings are saved after the Game is deleted; the device goes last.
    s_renderer.levelPlacements.release();
    delete_game(Game::instance());
    Input_TrySaveSettings(&s_progCtrl);
    s_renderer.themeBlock.release(&s_renderer.textures, &s_renderer.models);
    s_renderer.device = NULL;
    device.reset();
    return exitCode;
}
