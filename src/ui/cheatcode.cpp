/* GAMETICK_PLAN.md Band B reopened — the typed cheat-code handler.
 *
 *   HandleTypedCheatCode  0x0041aca0   1 E8 site (0x00415226, GameTick)
 *
 * __fastcall, Game base in ECX, bare RET.  Transcribed from the LISTING.
 *
 * Callees and where they go:
 *   __ftol                         inlined (truncate the dt accumulator)
 *   PollTextEntryKeys  0x4209a0    Sim_PollTextEntryKeys      (textentry.cpp)
 *   CalculateLevelScore 0x41a760   Score_CalculateLevelScore  (levelscore.cpp)
 *   PlayCDStuf 0x403360            Sim_PlayCDStuf             (themeindex.cpp)
 *   CDM::Stop 0x402d50             CDM_StopTrack              (cdm.cpp)
 *   Push/Pop/RewindMenu...         Sim_*                      (menustack.cpp)
 *   SetCurrentLevelName/OpenLevelFile/ParseLevelFiles  Sim_*  (levelparse.cpp)
 *   SetupLevelObjects 0x416420     Sim_SetupLevelObjects      (levelsetup.cpp)
 *   Log_Message 0x441b10           GameLog_LogMessage         (gamelog.cpp)
 *   sprintf/fopen/fclose/atoi      this DLL's CRT -- the game's are the same
 *                                  MSVC routines; the FILE* is only tested
 *                                  and closed, and atoi runs in the C locale
 *                                  (0x00450521's ctype path)
 *   LinkedList::Append 0x4254a0    KEPT as a named callback, as tileeffects.cpp
 *                                  keeps it (GAMETICK_PLAN.md Band A)
 *
 * Structure, in listing order, all gated on the entry widget going INACTIVE
 * this tick (Game+0x13cdb7 == 0 after the poll):
 *
 *   "kaputo"    every foe in the ID list gets +0x11f = 4
 *   "supa"      last level -> state 2 (game over), score reason 0x28;
 *               else -> state 3 + the menu-node 0x28 dance, score reason =
 *               the state byte; both then add elapsed ms to +0x170a44
 *   "jjmapnr"   a 7-byte PREFIX compare (buffer copied into a zeroed 256-byte
 *               frame, 4+2+1 bytes, byte 7 forced 0), taken only for strlen>8;
 *               level = atoi(buffer+8) - 1, loads it by number
 *   "jjmap"     a 5-byte PREFIX compare, strlen>6; loads buffer+6 by name.
 *               NOTE "jjmapnr..." also matches this prefix -- it misses only
 *               because the jjmapnr branch clears buffer[0] first, and only
 *               when that branch was taken (strlen>8).  Order preserved.
 *   "mausuruh"  lives (+0x175402) += 1
 *   "sportsman" +0x1752b2 += 1
 *   "boommaker" +0x1752b1 += 10
 *   "notme"     invulnerable: Append(+0x1753e5, 0xd) the first time, then
 *               flags, tile-marker 3 under the player, and a timestamp
 *   then clear the 256-byte buffer and re-arm the widget.
 *
 * Control: KAROO_SIM_FX=cheatlife -- "mausuruh" grants two lives.  No
 * recording types a cheat, so this is expected NOT to fail the suite; it is
 * here so a future cheat recording has a control ready.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "textentry.h"
#include "foe.h"
#include "player.h"
#include "tile.h"

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Score_CalculateLevelScore(void *self, char endReason);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_PlayCDStuf(void *self, const char *caption);
struct CDM;
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CDM_StopTrack(CDM *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PushMenuNodeOnStack(void *self, unsigned int nodeArg);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PopMenuNodeFromStack(void *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RewindMenuStackToRootNode(void *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SetCurrentLevelName(void *self, unsigned int levelNo);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_OpenLevelFile(void *self, unsigned int levelNo);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ParseLevelFiles(void *self, const char *name);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SetupLevelObjects(void *self);
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(void *self, int level, const char *fmt, ...);


#define CDAUDIO     ((CDM *)0x004dc640)
#define GAMELOGGER  ((void *)0x0046c4c0)
#define GAMEDIR     ((const char *)0x004e01c4)
#define S_GAMEOVER  ((const char *)0x00465574)   /* "gameover"  */
#define S_COMPLETED ((const char *)0x00465548)   /* "completed" */
#define F_COMPLETED ((const char *)0x00465554)   /* "GAME: completed at level %d/%d" */
#define F_CSL       ((const char *)0x00465cbc)   /* "GAME: c - sl" */
#define F_LVLPATH   ((const char *)0x00465ca0)   /* "%s\\Levels\\%s.jjm" */
#define F_LCNUM     ((const char *)0x00465c80)   /* "GAME: lc by number %d name:%s" */
#define F_LC        ((const char *)0x00465c6c)   /* "GAME: lc %s" */

#define G8(o)   (*(unsigned char *)(B + (o)))
#define G32(o)  (*(unsigned int *)(B + (o)))
#define GD(o)   (*(double *)(B + (o)))

static int s_fx = -1;

/* The listing's inline strcmp: 0 on equal. */
static int streq(const unsigned char *a, const char *b)
{
    return strcmp((const char *)a, b) == 0;
}

/* GameTick/cheat tail shared by both loaders: the level is loaded, go to
 * state 4 with the flythrough armed. */
static void enter_loaded_state(unsigned char *B, FILE *fp)
{
    ((Game *)B)->setCameraDistance(7.0f);
    ((Game *)B)->setState(4);
    if (((Game *)B)->musicOn() != 0)
        CDM_StopTrack(CDAUDIO);
    G32(0x1964e3) = 1;
    ((Game *)B)->setCameraMode(1);
    G8(0x175517) = 0x0d;
    fclose(fp);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_HandleTypedCheatCode(void *self)
{
    unsigned char *B = (unsigned char *)self;
    Player *pl = ((Game *)B)->player();
    unsigned char *buf = B + 0x13ccac;
    unsigned char frame[256];
    char path[256];

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "cheatlife") == 0);
        if (s_fx)
            log_write("cheatcode: KAROO_SIM_FX=cheatlife -- mausuruh gives 2\n");
    }

    ((Game *)B)->cheatEntry()->poll((unsigned int)(long long)GD(0x170a54));
    if (((Game *)B)->cheatEntry()->active() != 0)
        return;

    /* kaputo */
    if (streq(buf, "kaputo")) {
        Game *g = (Game *)B;
        unsigned char i = 0;
        /* The count is re-read every pass, as the original's CMP is. */
        if (g->foeCount() != 0) {
            do {
                unsigned char id = g->foeId(i);
                ++i;
                g->foeSlot(id)->setMoveState(4);
            } while (i < g->foeCount());
        }
    }

    /* supa */
    if (streq(buf, "supa")) {
        if ((unsigned int)((Game *)B)->levelIndex() + 1 == (unsigned int)((Game *)B)->levelCount()) {
            ((Game *)B)->setState(2);
            if (((Game *)B)->musicOn() != 0)
                Sim_PlayCDStuf(B + 0x2223f, S_GAMEOVER);
            Score_CalculateLevelScore(B, 0x28);
            G8(0x175517) = 0x0d;
            GameLog_LogMessage(GAMELOGGER, 1, F_COMPLETED,
                               (unsigned int)((Game *)B)->levelIndex() + 1,
                               (unsigned int)((Game *)B)->levelCount());
        } else {
            G32(0x175524) = G32(0x170a4c);
            G32(0x175528) = G32(0x170a50);
            G32(0x17552c) = 1;
            ((Game *)B)->setState(3);
            if (((Game *)B)->musicOn() != 0)
                Sim_PlayCDStuf(B + 0x2223f, S_COMPLETED);
            ((Game *)B)->setCameraMode(2);
            Sim_RewindMenuStackToRootNode(B + 0x175518);
            Sim_PopMenuNodeFromStack(B + 0x175518);
            Sim_PushMenuNodeOnStack(B + 0x175518, 0x28);
            G32(0x175524) = G32(0x170a4c);
            G32(0x175528) = G32(0x170a50);
            G8(0x195734) = 0x28;
            G8(0x175535) = 0;
            G32(0x17552c) = 1;
            Score_CalculateLevelScore(B, (char)((Game *)B)->state());
            ((Game *)B)->setRestartCount(0);
            GameLog_LogMessage(GAMELOGGER, 1, F_CSL);
        }
        ((Game *)B)->setTotalPlayTime((double)(unsigned long long)((Game *)B)->timeElapsed() + ((Game *)B)->totalPlayTime());
    }

    /* jjmapnr -- 7-byte prefix, then load by number */
    memset(frame, 0, sizeof(frame));
    memcpy(frame, buf, 7);
    frame[7] = 0;
    if (streq(frame, "jjmapnr")) {
        size_t len = strlen((const char *)buf);
        if (len > 8) {
            char num[256];
            memcpy(num, buf + 8, len - 8);
            num[len - 8] = 0;
            unsigned char lvl = (unsigned char)(atoi(num) - 1);
            Sim_SetCurrentLevelName(B, lvl);
            if (lvl < ((Game *)B)->levelCount()) {
                sprintf(path, F_LVLPATH, GAMEDIR, (const char *)(B + 0x173483));
                GameLog_LogMessage(GAMELOGGER, 3, F_LCNUM, (unsigned int)lvl,
                                   (const char *)(B + 0x173483));
                ((Game *)B)->setLevelIndex(lvl);
                FILE *fp = fopen(path, "r");
                if (fp != NULL) {
                    pl->setGemsCollected(0);
                    Sim_OpenLevelFile(B, ((Game *)B)->levelIndex());
                    Sim_SetupLevelObjects(B);
                    enter_loaded_state(B, fp);
                }
            }
            buf[0] = 0;
        }
    }

    /* jjmap -- 5-byte prefix, then load by name */
    memset(frame, 0, sizeof(frame));
    memcpy(frame, buf, 5);
    frame[5] = 0;
    if (streq(frame, "jjmap")) {
        size_t len = strlen((const char *)buf);
        if (len > 6) {
            memcpy(frame, buf + 6, len - 6);
            frame[len - 6] = 0;
            GameLog_LogMessage(GAMELOGGER, 3, F_LC, (const char *)frame);
            sprintf(path, F_LVLPATH, GAMEDIR, (const char *)frame);
            FILE *fp = fopen(path, "r");
            if (fp != NULL) {
                pl->setGemsCollected(0);
                Sim_ParseLevelFiles(B, (const char *)frame);
                Sim_SetupLevelObjects(B);
                enter_loaded_state(B, fp);
            }
            buf[0] = 0;
        }
    }

    if (streq(buf, "mausuruh"))
        pl->setField239(pl->field239() + (s_fx ? 2 : 1));
    if (streq(buf, "sportsman"))
        pl->setFieldE9((unsigned char)(pl->fieldE9() + 1));
    if (streq(buf, "boommaker"))
        pl->setFieldE8((unsigned char)(pl->fieldE8() + 10));

    if (streq(buf, "notme")) {
        if (pl->field1f2() == 0)
            pl->appendEffect(0x0d);
        pl->setField1f2(1);
        pl->setKind(3);
        pl->curTile()->setField1a5(3);
        pl->setField1ea(*((Game *)B)->clock());
    }

    memset(buf, 0, 0x100);
    ((Game *)B)->cheatEntry()->setCursor(0);
    *(unsigned char **)(B + 0x13cdb0) = buf;
    ((Game *)B)->cheatEntry()->setActive(1);
    ((Game *)B)->cheatEntry()->setLastKey(0x0d);
}
