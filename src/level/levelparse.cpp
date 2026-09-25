/* GAMETICK_PLAN.md Band B — the level loaders, four functions:
 *
 *     Game::ParseLevelFiles                                0x00418910
 *     Game::OpenLevelFile                                  0x004186f0
 *     Game::SetCurrentLevelName                            0x004186b0
 *     Level3DExtraObjects::ReleaseExtraObjectSoundBuffers  0x00425210
 *
 * ─── Why one cycle ───────────────────────────────────────────────────────
 *
 * Callee lists, read from Ghidra this session per the plan's standing rule:
 *
 *   ParseLevelFiles   ReadLevelMapFile                0x41f190  ours
 *                     ReadInstructionScriptForLevel   0x41d720  ours
 *                     Log_Message                     0x441b10  ours
 *                     ReleaseExtraObjectSoundBuffers  0x425210  TAKEN HERE
 *                     MaybeSprintf                    0x450655  CRT
 *                     PostQuitMessage                 user32
 *
 *   OpenLevelFile     the same set, plus
 *                     SetCurrentLevelName             0x4186b0  TAKEN HERE
 *
 *   SetCurrentLevelName             (none — a true leaf)
 *
 *   ReleaseExtraObjectSoundBuffers
 *                     HaltPlayback                    0x4429a0  ours
 *                     ReleaseStaticSoundBufferForOwner 0x4432f0 kept callback
 *
 * Everything the loaders stand on was already ours except two helpers, so the
 * helpers come with them: leaving either in the binary would make our code
 * call back into game code, which the no-callback rule forbids.  The same
 * shape as the `gamereset.cpp` cycle.
 *
 * ─── OpenLevelFile came into this cycle because the CONTROL demanded it ──
 *
 * The cycle began as `ParseLevelFiles` alone.  It replaced cleanly, the suite
 * passed 16/16 — and so did the negative control, which should not have
 * happened.  `KAROO_LEVELPARSE_DIAG=1` explained it in one line: a whole
 * recording calls `ParseLevelFiles` exactly **once**, for the menu's
 * `DemoLevelForest` backdrop.  The gameplay level it then loads —
 * `Space\Bridge01` — never goes through it.
 *
 * `OpenLevelFile 0x004186f0` is where it goes instead, and it turns out to be
 * a near-**duplicate** of `ParseLevelFiles`: same two stack buffers at the
 * same offsets, same save-then-read-then-compare of the map name, same
 * +0x10 flag, same script read, same `XOR AL,AL` return.  The differences are
 * that it is keyed by a level **number** rather than a name (hence
 * `SetCurrentLevelName`), it logs that number in both messages, and it carries
 * the bonus-peek prologue described below.
 *
 * Replacing one of a duplicated pair and testing the one the suite does not
 * execute is how a cycle passes every gate while proving nothing.  The diag
 * was what caught it; the control passing was the symptom.
 *
 * `ReleaseStaticSoundBufferForOwner` stays a named callback at its original
 * address — the same standing ruling `objectremove.cpp`, `gamereset.cpp` and
 * `player.cpp` recorded: a shared asset service with call sites across
 * unrelated subsystems is not simulation, and replacing it is a separate
 * decision from replacing its callers.
 *
 * ─── ReleaseExtraObjectSoundBuffers 0x00425210, from the LISTING ─────────
 *
 * Its `this` is NOT `Game`.  All four call sites load it the same way —
 *
 *     8D 8D/8E/AE 98 8B 04 00     LEA ECX,[reg + 0x48b98]
 *
 * at 0x004187F3 (OpenLevelFile), 0x004189A3 (ParseLevelFiles), 0x00416481
 * (SetupLevelObjects) and 0x00414CBA (Destruct) — so it is a method on the
 * `Level3DExtraObjects` sub-object at Game+0x48b98, the .leo reader's own
 * class (`karoo-hooks/extraobjects.cpp`).  That identification is not an analogy: the
 * loop's stride is 0xf40, and extraobjects.cpp already documents the extra-object
 * records as living at `this + n*0xf40`.
 *
 *     00425215  MOV EBX,0xff              255 iterations, NOT 256
 *     0042521a  LEA ESI,[EDI + 0xf4a]     first handle = base + 0xf4a
 *     00425220  MOV ECX,[ESI]             the record's sound handle
 *     00425224  JZ  skip                  null handles skipped
 *     00425226  CALL HaltPlayback
 *     0042522d  MOV ECX,[EDI + 0x8]       the sound manager, from +0x8
 *     00425233  CALL ReleaseStaticSoundBufferForOwner(handle, 1)
 *     00425238  MOV [ESI],0               clear the slot
 *     0042523e  ADD ESI,0xf40             next record
 *
 * Two details worth stating because they are easy to "tidy" away:
 *
 *   - the counter starts at 0xff and the loop is a DEC/JNZ, so it runs
 *     **255** times, not 256.  If the record array really holds 256 entries
 *     the last one is never released.  Preserved.
 *   - the handle is re-read from memory after HaltPlayback (0x0042522b),
 *     not kept in a register across the call.
 *
 * Unlike the four `Purge*Objects`, this one DOES null-test the handle before
 * using it.
 *
 * ─── ParseLevelFiles 0x00418910, from the LISTING ────────────────────────
 *
 * __thiscall on `Game`, one pushed argument (RET 4): the bare level name,
 * e.g. `Space\Bridge01`.
 *
 *   1. strcpy(this+0x173483, name)                 the current level name
 *   2. sprintf(path, "%s\Levels\%s", GameDir, name)
 *   3. strcpy(prev, this+0x2ab69d)                 SAVE the previously loaded
 *                                                  map name, BEFORE the read
 *   4. ok = ReadLevelMapFile(this+0x2ab58d, path)
 *      ok  -> ReleaseExtraObjectSoundBuffers(this+0x48b98)
 *             Log_Message(logger, 1, "...loaded...", *(u32*)(this+0x2ab599),
 *                         path)
 *      !ok -> Log_Message(logger, 4, "...could not load %s", path)
 *             PostQuitMessage(1)   -- and then FALLS THROUGH, it does not
 *                                     return
 *   5. this+0x10 = (strcmp(prev, this+0x2ab69d) != 0)
 *   6. sprintf(path, "%s\InstructionScripts\%s", GameDir, name)
 *      this+0x1960e6 = 0
 *      ReadInstructionScriptForLevel(this+0x195735, path)
 *      Log_Message(logger, 1, this+0x1960e6 ? "loaded" : "could not load",
 *                  path)
 *   7. return 0
 *
 * Three things the listing settles that a summary would get wrong:
 *
 * **The +0x10 flag is "the map changed", and it is computed by comparing a
 * saved copy against the field the reader has just overwritten.**  `prev` is
 * copied out at step 3 — before `ReadLevelMapFile` — and compared at step 5
 * against the same address, which now holds the newly loaded map's name.  The
 * order is load-bearing: copy it after the read and the flag is always 0.
 *
 * **The failure path does not return.**  `PostQuitMessage(1)` posts a message;
 * it does not terminate anything.  The function carries on and reads the
 * instruction script for a level whose map failed to load.  Preserved.
 *
 * **It always returns 0.**  `00418a99 XOR AL,AL` is the only write to the
 * return register, on both paths, so success and failure are indistinguishable
 * to the caller.  The upper three bytes of EAX are whatever `Log_Message` left
 * there — Ghidra renders this as `extraout_EAX & 0xffffff00`.  We return a
 * clean 0; no caller can read the garbage bytes meaningfully, and this is
 * noted rather than silently "cleaned up".
 *
 * Both string copies are the MSVC inline `strcpy` (SCASB/REP MOVSD/MOVSB) and
 * the compare is the inline `strcmp` that returns -1/0/1.  Both destinations
 * are unbounded, and `path` is a 256-byte stack buffer built from the game
 * directory plus a level name with no length check.  Preserved as-is.
 *
 * ─── SetCurrentLevelName 0x004186b0, from the LISTING ────────────────────
 *
 *   004186c4  LEA EDI,[EAX + ECX + 0x3215e]   EAX = (levelNo & 0xff) << 8
 *
 * so it is `strcpy(this+0x173483, this + 0x3215e + levelNo*0x100)` — the
 * level-name table, 0x100 bytes per entry, indexed by the level number, into
 * the same "current level name" buffer both loaders then format into a path.
 * `XOR AL,AL` at 0x004186e3: it returns 0, and the upper three bytes of EAX
 * are the string length the copy computed.  A true leaf.
 *
 * ─── OpenLevelFile's bonus-peek prologue ─────────────────────────────────
 *
 * Before loading the level it was asked for, and only when the flag byte at
 * +0x4220b is zero AND `currentLevel + 1 != levelCount` (+0x4215e), it:
 *
 *     SetCurrentLevelName(this, currentLevel + 1)
 *     sprintf(path, "%s\Levels\%s", GameDir, this+0x173483)
 *     ReadLevelMapFile(this+0x2ab58d, path)
 *     this+0x14 = *(u32 *)(this+0x2ab599)
 *
 * — it loads the **next** level's map purely to read its bonus value out, then
 * loads the real one over the top.  Two consequences are preserved and worth
 * naming, because both look like bugs and neither is safe to "fix":
 *
 *   - the peek leaves the map-name field at +0x2ab69d holding the NEXT
 *     level's name.  `prev` is saved after the peek, so the peek is part of
 *     what the +0x10 changed-flag compares against.
 *   - the peek's `ReadLevelMapFile` result is not tested at all.
 *
 * The `sprintf(path, "%s.gam", this+0x4215f)` at the very top is a leftover:
 * `path` is overwritten before it is read.  Kept, because it is a call.
 *
 * ─── Control ─────────────────────────────────────────────────────────────
 *
 * Neither control here is a strong one, and the reasons differ.  Both are
 * recorded, because between them they say exactly what this cycle proved.
 *
 * KAROO_SIM_FX=levelshift  `SetCurrentLevelName` reads table entry N+1, so
 *                          every load opens the NEXT level.  It fails **all
 *                          16** recordings with substantial field diffs
 *                          (`bridge01`: gems_required 25->8, items_available
 *                          26->9, time_limit_s 300->140, vitality 87->0) and
 *                          no crash — a real divergence, not a `bridgeaxis`.
 *
 *                          That IS the acceptance evidence for this cycle,
 *                          and it is what the 16/16 baseline is worth reading
 *                          against: our loaders are demonstrably the live
 *                          path — every recording's level comes through
 *                          `Sim_OpenLevelFile` and `Sim_SetCurrentLevelName`,
 *                          and our table indexing lands on exactly the right
 *                          entry, because shifting it by ONE breaks all 16
 *                          and leaving it alone passes all 16.
 *
 *                          What it does not do is isolate a decision INSIDE
 *                          the loaders: a replay replays keypresses, so any
 *                          change of level fails everything.  In this
 *                          document's terms it is closer to a colour tint
 *                          than a direction change, so it is read as "the
 *                          replacement is the live, correctly-indexed path"
 *                          rather than "the loader's internal logic is
 *                          confirmed".  The control below was written to
 *                          reach that second claim, and cannot.
 *
 * KAROO_SIM_FX=samelevel   the discriminating control, and it CANNOT FAIL.
 *                          BOTH loaders copy the previously-loaded map name
 *                          AFTER the read instead of before, so `prev` and
 *                          the live field are always equal and the +0x10
 *                          "map changed" flag is stuck at 0.  It is the one
 *                          edit that changes a decision rather than a value,
 *                          and it targets exactly the ordering this header
 *                          calls load-bearing — so it tests that claim rather
 *                          than perturbing a number.
 *
 * KAROO_LEVELPARSE_DIAG=1  logs every call to EITHER loader with the level
 *                          name or number, whether the map read succeeded,
 *                          the computed +0x10 flag and the script-loaded
 *                          counter, plus the bonus peek, the first sound
 *                          release and a released-handle count.  It is how
 *                          the duplicated-loader problem above was found, and
 *                          it is the first thing to run if a control on this
 *                          file ever passes when it should not.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "game.h"
#include "levelparse.h"
#include "soundmanager.h"
#include "gamestr.h"
#include "gameglobals.h"

/* ─── Game / Level3DExtraObjects field offsets ───────────────────────────── */

/* The map-changed flag (+0x10) and the peeked next-level bonus (+0x14) are
 * Game::mapChanged() and Game::nextLevelBonus() (game.h). */

/* The map (levelmap.h): its reader, the map name it last loaded, and the
 * bonus DWORD (the %d in the "loaded" line). */



/* ─── Game globals and string constants, at their original addresses ─────── */



/* OpenLevelFile's own message set -- different strings from ParseLevelFiles's,
 * because they carry the level NUMBER as well as the path. */

/* ─── Callbacks and CRT helpers kept at their original addresses ─────────── */

struct CStaticSoundbuffer;



/* ─── Already ours -- called as exports, the originals carry UD2 stubs ───── */

struct GameLogger;
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...);

extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_HaltPlayback(CStaticSoundbuffer *self);


/* ─── FX / diag ──────────────────────────────────────────────────────────── */

static int s_fx_samelevel  = 0;
static int s_fx_crtpath    = 0;
static int s_fx_levelshift = 0;
static int s_diag         = 0;
static int s_init         = 0;

static unsigned s_parses    = 0;
static unsigned s_opens     = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "samelevel") == 0) {
            s_fx_samelevel = 1;
            log_write("levelparse: KAROO_SIM_FX=samelevel -- the previous map "
                      "name is copied AFTER the read, so the +0x10 "
                      "map-changed flag is stuck at 0\n");
        } else if (strcmp(buf, "levelshift") == 0) {
            s_fx_levelshift = 1;
            log_write("levelparse: KAROO_SIM_FX=levelshift -- the level-name "
                      "lookup reads entry N+1, so every load opens the NEXT "
                      "level's map and script\n");
        }
    }

    /* KAROO_CRT_FX=path -- the negative control for CRT_PLAN.md Stage C.
     *
     * Stage C moved 24 sprintf sites off the game's CRT onto ours.  Most of
     * them format log lines, which go to a stderr this process discards, so
     * a control there would prove nothing.  These level paths are the
     * exception: a gate reads them, because a path that does not resolve is
     * a level that does not load.
     *
     * It swaps the two %s arguments rather than perturbing a character --
     * for a pure function, break the structure, not a value.  The result is
     * "<name>\Levels\<gamedir>", so every recording fails at load. */
    n = GetEnvironmentVariableA("KAROO_CRT_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "path") == 0) {
        s_fx_crtpath = 1;
        log_write("levelparse: KAROO_CRT_FX=path -- the two %%s arguments to "
                  "our sprintf are swapped, so every level path is "
                  "nonsense\n");
    }

    n = GetEnvironmentVariableA("KAROO_LEVELPARSE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

/* ─── The MSVC inline string primitives, transcribed ─────────────────────── */

static void inline_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != '\0')
        ;
}

/* returns -1 / 0 / 1, as the SBB EAX,EAX / SBB EAX,-1 pair does */
static int inline_strcmp(const unsigned char *a, const unsigned char *b)
{
    while (*a == *b) {
        if (*a == 0)
            return 0;
        a++;
        b++;
    }
    return (*a < *b) ? -1 : 1;
}


/* ═══ 0x00418910 -- Game::ParseLevelFiles ══════════════════════════════════ */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_ParseLevelFiles(Game *self, const char *name)
{
    char path[256];      /* [ESP+0x10]  -- 256 bytes, unbounded sprintf */
    char prev[256];      /* [ESP+0x110] -- the map name before the read */
    int ok;

    fx_init();

    inline_strcpy(self->levelNameBuffer(), name);

    if (s_fx_crtpath)
        sprintf(path, GS_OPEN_FMT_LEVELS, name, g_gameDir);
    else
        sprintf(path, GS_OPEN_FMT_LEVELS, g_gameDir, name);

    /* SAVE the previously loaded map name BEFORE the read overwrites it.
     * The ordering is the whole mechanism of the +0x10 flag below; the
     * `samelevel` control moves this copy after the read. */
    if (!s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    ok = self->map()->readFile(path);

    if (s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    if ((char)ok != 0) {
        self->extraObjects()->releaseSounds();
        GameLog_LogMessage(&g_logger, 1, GS_OPEN_LOADED_NAME,
                           self->map()->bonus(), path);
    } else {
        GameLog_LogMessage(&g_logger, 4, GS_OPEN_FAILED_NAME, path);
        PostQuitMessage(1);
        /* and FALLS THROUGH -- the original does not return here */
    }

    self->setMapChanged(0);
    if (inline_strcmp((const unsigned char *)prev,
                      (const unsigned char *)self->map()->mapName()) != 0)
        self->setMapChanged(1);

    sprintf(path, GS_OPEN_FMT_SCRIPTS, g_gameDir, name);
    self->scriptPlayer()->setLoaded(0);
    self->scriptPlayer()->readForLevel(path);

    GameLog_LogMessage(&g_logger, 1,
                       self->scriptPlayer()->loaded() ? GS_OPEN_SCRIPT_OK_NAME
                                                             : GS_OPEN_SCRIPT_BAD_NAME,
                       path);

    s_parses++;
    if (s_diag)
        log_write("levelparse: DIAG parse #%u name=\"%s\" map=%s changed=%u "
                  "script=%u released=%u\n",
                  s_parses, name, (char)ok ? "ok" : "FAILED",
                  self->mapChanged(),
                  self->scriptPlayer()->loaded(), ExtraObjects::releasedCount());

    /* XOR AL,AL on both paths -- success and failure are indistinguishable */
    return 0;
}

/* ═══ 0x004186b0 -- Game::SetCurrentLevelName ══════════════════════════════ */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_SetCurrentLevelName(Game *self, unsigned int levelNo)
{

    if (s_fx_levelshift)
        levelNo = levelNo + 1;

    inline_strcpy(self->levelNameBuffer(),
                  self->levelNameTableEntry((unsigned char)(levelNo & 0xff)));

    /* XOR AL,AL -- the upper bytes are the copied length, and unreadable */
    return 0;
}

/* ═══ 0x004186f0 -- Game::OpenLevelFile ════════════════════════════════════ */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_OpenLevelFile(Game *self, unsigned int levelNo)
{
    char path[256];      /* [ESP+0x8]   -- the same two buffers, same offsets */
    char prev[256];      /* [ESP+0x110] -- as ParseLevelFiles                 */
    int ok;

    fx_init();

    self->setNextLevelBonus(0);

    /* A leftover: `path` is overwritten before it is ever read. */
    sprintf(path, GS_OPEN_FMT_GAM, self->gameFileName());

    /* The bonus peek -- load the NEXT level's map just to read its bonus. */
    if (self->restartCount() == 0 &&
        (unsigned int)(self->levelIndex()) + 1 != (unsigned int)self->levelCount()) {
        Sim_SetCurrentLevelName(self, (unsigned char)(self->levelIndex() + 1));
        sprintf(path, GS_OPEN_FMT_LEVELS, g_gameDir,
                           self->levelName());
        /* the result is deliberately not tested, as in the original */
        self->map()->readFile(path);
        self->setNextLevelBonus(self->map()->bonus());

        if (s_diag)
            log_write("levelparse: bonus peek for level %u -> bonus=%u\n",
                      (unsigned)(unsigned char)(self->levelIndex() + 1),
                      self->nextLevelBonus());
    }

    Sim_SetCurrentLevelName(self, levelNo);
    sprintf(path, GS_OPEN_FMT_LEVELS, g_gameDir,
                       self->levelName());

    /* Saved BEFORE the read -- see the header, and the `samelevel` control. */
    if (!s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    ok = self->map()->readFile(path);

    if (s_fx_samelevel)
        inline_strcpy(prev, self->map()->mapName());

    if ((char)ok != 0) {
        self->extraObjects()->releaseSounds();
        GameLog_LogMessage(&g_logger, 1, GS_OPEN_LOADED_NUM,
                           self->map()->bonus(),
                           levelNo & 0xff, path);
    } else {
        GameLog_LogMessage(&g_logger, 4, GS_OPEN_FAILED_NUM,
                           levelNo & 0xff, path);
        PostQuitMessage(1);
        /* and FALLS THROUGH, as in ParseLevelFiles */
    }

    self->setMapChanged(0);
    if (inline_strcmp((const unsigned char *)prev,
                      (const unsigned char *)self->map()->mapName()) != 0)
        self->setMapChanged(1);

    sprintf(path, GS_OPEN_FMT_SCRIPTS, g_gameDir,
                       self->levelName());
    self->scriptPlayer()->setLoaded(0);
    self->scriptPlayer()->readForLevel(path);

    GameLog_LogMessage(&g_logger, 1,
                       self->scriptPlayer()->loaded()
                           ? GS_OPEN_SCRIPT_OK_NUM : GS_OPEN_SCRIPT_BAD_NUM,
                       path);

    s_opens++;
    if (s_diag)
        log_write("levelparse: DIAG open #%u level=%u name=\"%s\" map=%s "
                  "changed=%u script=%u released=%u\n",
                  s_opens, levelNo & 0xff, self->levelName(),
                  (char)ok ? "ok" : "FAILED",
                  self->mapChanged(),
                  self->scriptPlayer()->loaded(), ExtraObjects::releasedCount());

    return 0;
}
