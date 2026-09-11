/* GAMETICK_PLAN.md Band B reopened — the one-shot fixed-sound load.
 *
 *   Game::AcquireFixedSoundBuffersAndMaybeReport  0x0041a280
 *     1 E8 site (0x00414DFC, the first thing GameTick does)
 *
 * __thiscall(Game*), bare RET.  Transcribed from the LISTING.  Guarded by
 * Game+0x13cc80 (sound.dwSoundsLoaded): runs once, then sets it.
 *
 *   Config_Save(Game+0x28ab2e, "Karoo.cfg")  -> log 1 "saved" / 3 "error"
 *   +0x175524/28 <- dt accumulator; +0x17552c = 1
 *   if CD on: PlayCDStuf("Main");   +0x2235a = FindThemeIndex("Main")
 *   no SoundManager (+0x13cc34 == 0): log the warning, CD off, skip to end
 *   three passes, suffix 'A'+i, base ESI = Game+0x175327 + 4i, ten buffers
 *   each Reset-if-set then AcquireSoundBuffer(sm, path, 0):
 *     ESI-0x386b3 add02 (= Game+0x13cc74+4i, the three banks GameTick picks
 *                        by rand()%3 on crystal completion)
 *     ESI+0x00 add08  +0x0c add06  +0x18 add07  +0x24 add03  +0x30 add05
 *     ESI+0x48 add04  +0x54 add10  +0x60 add09  +0x3c add01   (listing order)
 *   +0x196050 = 5, WORD +0x196054 = 5, +0x196044 = +0x13cbc8 (the
 *   IDirectSound), +0x196048 = 0, +0x48ba0 = +0x195b3f = &SoundManager
 *   GetAsyncKeyState(VK_L) three times; if the THIRD reads down,
 *     WriteLevelReport(this, "LevelReport.txt")
 *   fixed buffers: TimeOut +0x13cc5c, LastSeconds +0x13cc6c, Count +0x13cc68,
 *   MenuUpDown VoicePool(5) +0x13cc64, Switch +0x13cc60, LevelCompleted
 *   +0x13cc70, splat (mode 1) +0x175270;  SoundSetup(sm, mode_3d)
 *   +0x13cc80 = 1
 *
 * The VK_L polls go through hooks_GetAsyncKeyState, which is where
 * levelreport.cpp answers them under KAROO_LEVEL_REPORT=1 -- so
 * tools/levelreport.py passing is itself the proof this path is ours.
 *
 * Callbacks kept (the sound manager is not ours): AcquireSoundBuffer
 * 0x443660, AcquireVoicePool 0x443810, SoundSetup 0x4439d0.
 *
 * Control: KAROO_SIM_FX=reportkey -- the VK_L polls are skipped, so the
 * report never runs.  levelreport.py must then FAIL (no report written);
 * the replay suite, which never presses L, must still pass.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "soundmanager.h"

struct CStaticSoundbuffer;
struct VoicePool;
typedef VoicePool *(__attribute__((thiscall)) *acquire_pool_fn)(void *sm, int count,
                                                                 const char *name,
                                                                 int mode);
typedef void (__attribute__((thiscall)) *sound_setup_fn)(void *sm, int mode3d);
#define ORIG_ACQUIRE_POOL  ((acquire_pool_fn)0x00443810)   /* named callback */
#define ORIG_SOUND_SETUP   ((sound_setup_fn) 0x004439d0)   /* named callback */

extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Config_Save(void *self, const char *path);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Report_WriteLevelReport(void *self, const char *pathname);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_PlayCDStuf(void *self, const char *caption);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_FindThemeIndexByThemeName(void *self, const char *name);
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(void *self, int level, const char *fmt, ...);

#define GAMELOGGER ((void *)0x0046c4c0)
#define GAMEDIR    ((const char *)0x004e01c4)
#define S_CFG      ((const char *)0x004652cc)   /* "Karoo.cfg" */
#define F_CFG_ERR  ((const char *)0x00465458)
#define F_CFG_OK   ((const char *)0x004654b0)
#define F_NOSOUND  ((const char *)0x00465bfc)
#define S_MAIN     ((const char *)0x00465510)   /* "Main" */
#define S_REPORT   ((const char *)0x00465afc)   /* "LevelReport.txt" */

#define G16(o) (*(unsigned short *)(B + (o)))
#define G32(o) (*(unsigned int *)(B + (o)))
#define GP(o)  (*(void **)(B + (o)))

static int s_fx = -1;

/* Reset-if-set, then acquire from a "%s\\waves\\...%s.wav" format. */
static void bank(unsigned char *B, unsigned int slot, unsigned int fmt,
                 const char *suffix)
{
    char path[256];
    if (GP(slot) != NULL)
        CStatic_Reset((CStaticSoundbuffer *)GP(slot));
    sprintf(path, (const char *)fmt, GAMEDIR, suffix);
    GP(slot) = ((SoundManager *)(B + 0x13cba8))->acquireStatic(path, 0);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_AcquireFixedSoundBuffersAndMaybeReport(void *self)
{
    unsigned char *B = (unsigned char *)self;
    void *sm = B + 0x13cba8;
    char path[256];

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "reportkey") == 0);
        if (s_fx)
            log_write("fixedsounds: KAROO_SIM_FX=reportkey -- VK_L not polled\n");
    }

    if (G32(0x13cc80) != 0)
        return;

    if ((Config_Save(B + 0x28ab2e, S_CFG) & 0xff) != 0)
        GameLog_LogMessage(GAMELOGGER, 1, F_CFG_OK);
    else
        GameLog_LogMessage(GAMELOGGER, 3, F_CFG_ERR);
    G32(0x175524) = G32(0x170a54);
    G32(0x175528) = G32(0x170a58);
    G32(0x17552c) = 1;
    if (G32(0x2aa156) != 0)
        Sim_PlayCDStuf(B + 0x2223f, S_MAIN);
    B[0x2235a] = (unsigned char)Sim_FindThemeIndexByThemeName(B + 0x2223f, S_MAIN);

    if (G32(0x13cc34) == 0) {
        GameLog_LogMessage(GAMELOGGER, 1, F_NOSOUND);
        G32(0x2aa156) = 0;
        if (G32(0x13cc34) == 0) {
            G32(0x13cc80) = 1;
            return;
        }
    }

    for (unsigned int i = 0; i < 3; ++i) {
        char suffix[2] = { (char)('A' + i), 0 };
        unsigned int esi = 0x175327 + i * 4;
        bank(B, esi - 0x386b3, 0x465be4, suffix);   /* add02 */
        bank(B, esi + 0x00,    0x465bcc, suffix);   /* add08 */
        bank(B, esi + 0x0c,    0x465bb4, suffix);   /* add06 */
        bank(B, esi + 0x18,    0x465b9c, suffix);   /* add07 */
        bank(B, esi + 0x24,    0x465b84, suffix);   /* add03 */
        bank(B, esi + 0x30,    0x465b6c, suffix);   /* add05 */
        bank(B, esi + 0x48,    0x465b54, suffix);   /* add04 */
        bank(B, esi + 0x54,    0x465b3c, suffix);   /* add10 */
        bank(B, esi + 0x60,    0x465b24, suffix);   /* add09 */
        bank(B, esi + 0x3c,    0x465b0c, suffix);   /* add01 */
    }

    G32(0x196050) = 5;
    G16(0x196054) = 5;
    G32(0x196044) = G32(0x13cbc8);
    G32(0x196048) = 0;
    GP(0x48ba0) = sm;
    GP(0x195b3f) = sm;

    if (!s_fx) {
        hooks_GetAsyncKeyState(0x4c);
        hooks_GetAsyncKeyState(0x4c);
        if (hooks_GetAsyncKeyState(0x4c) != 0)
            Report_WriteLevelReport(B, S_REPORT);
    }

    sprintf(path, (const char *)0x00465ae4, GAMEDIR);
    GP(0x13cc5c) = ((SoundManager *)sm)->acquireStatic(path, 0);          /* TimeOut */
    sprintf(path, (const char *)0x00465ac8, GAMEDIR);
    GP(0x13cc6c) = ((SoundManager *)sm)->acquireStatic(path, 0);          /* LastSeconds */
    sprintf(path, (const char *)0x00465ab4, GAMEDIR);
    GP(0x13cc68) = ((SoundManager *)sm)->acquireStatic(path, 0);          /* Count */
    sprintf(path, (const char *)0x00465a9c, GAMEDIR);
    GP(0x13cc64) = ORIG_ACQUIRE_POOL(sm, 5, path, 0);        /* MenuUpDown */
    sprintf(path, (const char *)0x00465a88, GAMEDIR);
    GP(0x13cc60) = ((SoundManager *)sm)->acquireStatic(path, 0);          /* Switch */
    sprintf(path, (const char *)0x00465a6c, GAMEDIR);
    GP(0x13cc70) = ((SoundManager *)sm)->acquireStatic(path, 0);          /* LevelCompleted */
    sprintf(path, (const char *)0x00465a58, GAMEDIR);
    GP(0x175270) = ((SoundManager *)sm)->acquireStatic(path, 1);          /* splat */
    ORIG_SOUND_SETUP(sm, (int)G32(0x2ab564));

    G32(0x13cc80) = 1;
}
