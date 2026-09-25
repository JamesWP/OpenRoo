/* ASSET_PLAN.md Phase 4 (first cycle) — the gameplay .jjs reader.
 *
 *   0x41d720  ReadInstructionScriptForLevel(this, const char *pathNoExt) -> BOOL
 *             __thiscall, ret 4.  1 E8 call site (0x41d80e's caller is
 *             OpenLevelFile at 0x4187xx); no E9, no PUSH, no vtable slot.
 *             UD2-stubbed.
 *
 * THE OTHER .jjs READER IS NOT REPLACED YET.  Phase 0 established there are
 * two: this one, called from OpenLevelFile when a level loads, and
 * ReadInstructionScriptTexts (0x41e8b0), called only from WriteLevelReport to
 * build ScriptTexts.txt.  They do NOT share a parser -- the report one reads
 * whole lines with fgets, matches keywords, and writes formatted output --
 * and its decompile still uses an uninitialised local as a pointer base,
 * which per CLAUDE.md means its signature is wrong and must be settled from
 * `ret N` before anything is written against it.  Left for the next cycle.
 *
 * ─── The format, as this reader sees it ───────────────────────────────────
 *
 * A .jjs is plain text, opened "r" (TEXT mode, 0x464200), read one character
 * at a time with fgetc.  ';' terminates an entry.  Each entry is stored as a
 * fixed 1000-byte record:
 *
 *     record(n) = this + 0x11b4 + n * 1000        (stride confirmed in the
 *                                                  disassembly: n*5*5*5*8)
 *     count     = this + 0xdc8   (WORD, incremented per entry)
 *
 * On ';' the accumulated text is NUL-terminated, "\n" (the `newline` global at
 * 0x465160) is appended, the whole thing is copied into the record, the count
 * is bumped, and ONE MORE CHARACTER IS CONSUMED AND DISCARDED -- the newline
 * that follows the ';' in every shipped script.
 *
 * The path argument arrives WITHOUT an extension; this reader appends ".jjs"
 * (0x4661e0) itself, like the .jjm reader appends ".jjm".
 *
 * ─── Calls into the game binary ───────────────────────────────────────────
 *
 * ONE, and it is a scope decision rather than a necessity:
 * ReleaseScriptStreamBuffers (0x41e840) is called at the top, exactly where
 * the original calls it.  It is not file I/O at all -- it walks 255
 * CStreamSoundbuffer pointers at this+0x40f, stopping, releasing and deleting
 * each.  Reimplementing it means reaching our own stream.cpp replacements and
 * the objects' virtual destructors, which is audio-lifetime work belonging to
 * the sound subsystem, not to the asset plan.  Named here and in the commit
 * message per ASSET_PLAN.md's no-callback rule, which asks for exactly that
 * when a game-logic call is left in place.
 *
 * Everything else -- open, read, close, all the string work -- is ours.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. THE ACCUMULATION INDEX IS MASKED TO 16 BITS while the counter itself is
 *    32-bit (`MOV EDX,EBX; AND EDX,0xffff; INC EBX`).  The original's buffer
 *    is 1000 bytes, so any entry longer than that writes past it into its own
 *    frame; at 65536 characters the index wraps instead.
 *
 *    Reproduced faithfully up to the point where the original would corrupt
 *    itself: the buffer here is a full 64 KB, so the masked index can never
 *    leave it.  For every input the original handles without smashing its
 *    stack -- which is every entry up to 998 characters -- the bytes stored
 *    and the record written are identical.  Measured over the shipped
 *    content: 1499 entries across all 74 .jjs files, longest 196 bytes, so
 *    the divergence is unreachable with any shipped script.
 * 2. THE EOF CHARACTER IS STORED.  The EOF flag is tested before each fgetc
 *    and again at the bottom of the loop, so the -1 returned by the read that
 *    hits EOF is truncated to 0xFF and appended to the pending entry.  It is
 *    harmless only because a pending entry with no ';' is never committed.
 * 3. A TRAILING ENTRY WITHOUT ';' IS SILENTLY DISCARDED, for the same reason.
 * 4. There is no bound on the number of entries: the count and the record
 *    base grow without limit, so a script with enough ';' writes past the
 *    record array.  Unreachable in shipped content; not "fixed".
 * 5. The path buffer is 128 bytes, strcpy + strcat, unchecked.
 * 6. Failure returns 0 having ALREADY cleared every field and released the
 *    stream buffers, so a missing .jjs leaves the object blank rather than
 *    untouched.
 *
 * ─── Visual proof ─────────────────────────────────────────────────────────
 *
 * KAROO_JJS_FX=blank stores every entry as a single "." instead of its text,
 * so the in-game instruction panels come up empty while everything else about
 * the level is unchanged.  Only this code path fills those records.
 */
#include "camera.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "scriptplayer.h"
#include "stream.h"
#include "alloc.h"
#include "gamelog.h"
#include "gameglobals.h"
#include "splinepath.h"
#include <stdlib.h>
#include <math.h>

/* ─── ReleaseScriptStreamBuffers 0x0041e840 ─────────────────────────────
 *
 * __fastcall, `this` in ECX, bare RET; 5 E8 sites (Game::Load 0x414CAD,
 * GameTick x2, SetupLevelObjects, the reader) -- all rerouted by patch.py,
 * the original UD2-stubbed.  Transcribed from the LISTING:
 *
 *   for each of the 255 slots at +0x40f:
 *     if slot:
 *       if slot->dwThread_done == 0: CStreamSoundbuffer::Stop   (0x444f10)
 *       CStreamSoundbuffer::ReleaseResources(slot)               (0x443e00)
 *       if slot: slot->vtable[0](1)   -- the scalar deleting dtor
 *       slot = 0
 *
 * The slot is re-read from memory before each step, as the listing does.
 * Stop and ReleaseResources are stream.cpp's (both originals already
 * stubbed); the deleting dtor is reached through the object's own vtable,
 * so whichever class built the stream destroys it.  Brought in at James's
 * request (the callback census of 2026-09-14). */
void ScriptPlayer::releaseStreams()
{
    typedef void (__attribute__((thiscall)) *deleting_dtor_fn)(void *, int);

    for (int i = 0; i < 255; ++i) {
        if (streams_[i] == NULL)
            continue;
        if (streams_[i]->dwThread_done == 0)
            CStream_Stop(streams_[i]);
        CStream_ReleaseResources(streams_[i]);
        if (streams_[i] != NULL)
            (*(deleting_dtor_fn *)*(void **)streams_[i])(streams_[i], 1);
        streams_[i] = NULL;
    }
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
JJScript_ReleaseScriptStreamBuffers(ScriptPlayer *self)
{
    self->releaseStreams();
}

/* A global scratch string the game copies into the script object before the
 * parse.  A DATA read, not a call. */
#define GLOBAL_SCRATCH_STR ((const char *)0x0046c290)

/* The record table, count, loaded flag and scratch string are ScriptPlayer
 * fields (scriptplayer.h): lines_ at +0x11b4 (1000 x 1000), lineCount_ at
 * +0xdc8, loaded_ at +0x9b1, scratch_ at +0xdca. */

/* 64 KB: the exact range the original's 16-bit-masked index can address.
 * Static, not on the stack -- this reader is not reentrant in the original
 * either (it writes fixed fields of a single object). */
static char s_entry[0x10000];

#define JJS_LOG_FIRST 6

static bool fx_blank(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_JJS_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "blank") == 0);
        log_write("jjscript: FX mode = %s\n", cached ? "blank" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
JJScript_ReadForLevel(ScriptPlayer *self, const char *path)
{
    return self->readForLevel(path);
}

int ScriptPlayer::readForLevel(const char *path)
{
    char name[128];                 /* 128 and unchecked, as the original */
    FILE *fp;
    unsigned idx = 0;               /* 32-bit counter, masked when indexing */
    static int logged = 0;

    /* Field clear, in the original's order. */
    againLine_   = 0;
    field_92d_   = 0;
    field_955_   = 0;
    loaded_      = 0;
    cursor_      = 0;
    waiting_     = 0;
    cameraMode_  = 0;
    running_     = 0;
    moving_      = 0;
    splineActive_ = 0;
    waitingOnStream_ = 0;
    streamReady_ = 0;

    releaseStreams();

    lineCount_ = 0;
    strcpy(scratch_, GLOBAL_SCRATCH_STR);

    strcpy(name, path);
    strcat(name, ".jjs");

    fp = fopen(name, "r");
    if (fp == NULL)
        return 0;                   /* defect 6: object already cleared */

    while (!feof(fp)) {
        int c = fgetc(fp);          /* defect 2: EOF's -1 is stored as 0xFF */

        if ((char)c == ';') {
            WORD  n   = lineCount_;
            char *rec = lines_[0] + n * LINE_SIZE;   /* defect 4: unbounded */

            s_entry[idx & 0xffff] = '\0';
            strcat(s_entry, "\n");  /* the `newline` global, 0x465160 */
            idx = 0;

            if (fx_blank())
                strcpy(rec, ".\n");
            else
                strcpy(rec, s_entry);

            lineCount_ = (WORD)(n + 1);   /* defect 4 */

            fgetc(fp);              /* the discarded character after ';' */
        } else {
            s_entry[idx & 0xffff] = (char)c;    /* defect 1 */
            idx++;
        }
    }

    fclose(fp);
    loaded_ = 1;

    /* KAROO_JJS_DUMP=<path> -- path, entry count, and an FNV-1a 32 hash over
     * every record's text, so an independent parse of the same .jjs can be
     * compared against what actually landed in the object.  ASSET_PLAN.md
     * Phase 4. */
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_JJS_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                unsigned long hash = 2166136261UL;
                unsigned n = lineCount_;
                for (unsigned i = 0; i < n; i++) {
                    const char *rec = lines_[i];
                    for (const char *q = rec; *q; q++) {
                        hash ^= (unsigned char)*q;
                        hash *= 16777619UL;
                    }
                }
                char line[512];
                int len = wsprintfA(line, "%s entries=%u hash=%08lx\r\n",
                                    name, n, hash);
                DWORD w = 0;
                WriteFile(h, line, (DWORD)len, &w, NULL);
                CloseHandle(h);
            }
        }
    }

    if (logged < JJS_LOG_FIRST) {
        logged++;
        log_write("jjscript: '%s' entries=%u\n",
                  name, (unsigned)lineCount_);
    }
    return 1;
}

/* ═══ The level report's reader (was jjsreport.cpp) ═══════════════════════
 *
   ASSET_PLAN.md Phase 4 (fourth cycle) — the report-side .jjs reader.
 *
 *   0x41e8b0  ReadInstructionScriptTexts(this, const char *pathNoExt, FILE *sink)
 *             __thiscall, ret 8.  1 E8 call site (0x41c1cf, inside
 *             WriteLevelReport); no E9, no PUSH, no vtable slot.  UD2-stubbed.
 *
 * This is the second of the two .jjs readers and the last file reader in
 * Phase 4.  It is NOT a parser for gameplay: it is the ScriptTexts.txt
 * generator, run only by the all-levels report.
 *
 * ─── The signature the decompile could not give ───────────────────────────
 *
 * Three cycles ago this function was deferred because its decompile used an
 * uninitialised local (`local_304`) as a pointer base and incremented fields
 * through it -- which CLAUDE.md says means the `this` type is wrong.  It was
 * exactly that:
 *
 *     0041e8b0  SUB ESP,0x314
 *     0041e8b9  MOV dword ptr [ESP+0x1c],ECX     <- ECX spilled...
 *     ...
 *     0041ea03  MOV EAX,dword ptr [ESP+0x20]     <- ...and read back here
 *     0041ea07  INC word ptr [EAX + 0x6]
 *     0041eb6a  RET 0x8
 *
 * The two references are the same slot at different ESP depths (three pushes
 * vs four), so the "uninitialised local" IS `this`, and `ret 8` fixes the rest:
 * __thiscall with two stack arguments.  Ghidra had it as
 * `__stdcall(char *, int *)`, which is why nothing about the body made sense.
 *
 * ─── What it does ─────────────────────────────────────────────────────────
 *
 * Opens <path>.jjs in mode "r+t" (0x464378 -- text UPDATE, not "r"; the only
 * reader in this plan that asks for write access it never uses), reads 0x80-byte
 * lines with fgets, and copies "text" blocks to the sink:
 *
 *   - a line whose first four characters are "text" (0x466360) opens a block;
 *   - inside a block, a line starting ';' closes it and bumps this->+0x6;
 *   - other lines of 3+ characters are written to the sink, preceded once per
 *     block by a header formatted "\n%d. Text:\n" (0x4663cc) with a 1-based,
 *     16-bit-masked counter;
 *   - a line whose LAST character before the newline is ';' closes the block
 *     too: the ';' is overwritten with NUL, the line and a "\n" are written,
 *     and the text counter advances;
 *   - independently of all that, any line whose first nine characters are
 *     "splinexyz" (0x466340) bumps this->+0x4.
 *
 * The two counters are 16-bit fields of the caller's object: +0x4 counts
 * spline lines, +0x6 counts blocks closed by a leading ';'.
 *
 * ─── Calls into the game binary: NONE ────────────────────────────────────
 *
 * It was not always so, and the history is worth keeping.  When this reader
 * was first written the sink was the GAME'S FILE * -- WriteLevelReport opened
 * ScriptTexts.txt with the game's fopen and passed the handle in -- and an
 * MSVC FILE cannot be written by this DLL's mingw CRT.  Using our own fputs on
 * it hung the level report: no crash, no UD2, just a run that never logged
 * "level report created".  The reader therefore had to call the game's fputs
 * (0x451776), which no amount of work inside this file could have avoided:
 * whoever OPENS a file decides which CRT owns it.
 *
 * karoo-hooks/reportwriter.cpp now replaces WriteLevelReport, so both output
 * streams are opened by us and the sink is an ordinary FILE * from our own
 * CRT.  The callback is gone and this reader is fully self-contained.
 *
 * The general rule the episode established stands: a FILE * that crosses the
 * DLL boundary must be used by the CRT that created it, in either direction --
 * so the fix is always to move the open, never to reach across.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. THE RETURN VALUE IS ALWAYS 0, on every path, including a failed open.
 *    The caller cannot distinguish "no such script" from "read it fine".
 * 2. THE LINE BUFFER IS PRE-FILLED with the global scratch string at
 *    0x46c290 before the first read -- and immediately overwritten by the
 *    first fgets.  Pointless, and kept.
 * 3. fgets' result is never checked, so at EOF the previous line's contents
 *    are re-examined once before the loop's EOF test ends it.
 * 4. A SECOND, REDUNDANT in-block FLAG.  The original keeps both a register
 *    flag and a stack mirror, and the stack mirror is never cleared on the
 *    trailing-';' path -- yet it can only be read while the register flag is
 *    set, which always implies the mirror is 1.  Both are modelled here; the
 *    mirror genuinely cannot change behaviour.
 * 5. The header counter is masked to 16 bits before the +1, so the 65536th
 *    text block would print as "1.".
 * 6. Both the path buffer and the sprintf scratch are fixed-size and
 *    unchecked.
 *
 * ─── Proof ────────────────────────────────────────────────────────────────
 *
 * This one needs no FX mode: it writes ScriptTexts.txt, which
 * tools/levelreport.py compares byte-for-byte against a committed baseline
 * over all 80 levels.  A single wrong byte anywhere in the block detection,
 * the header numbering or the spline count fails the run and names the file.
 * That is a stronger acceptance test than any of this plan's other readers
 * get, and it is why this reader needed no dump-and-compare harness.
 */


#define JJSR_LINE_MAX   0x80
/* Its two counters are ScriptPlayer fields: splineLines_ (+0x4, lines
 * beginning "splinexyz") and textBlocks_ (+0x6, blocks closed by a leading
 * ';'). */
#define JJSR_LOG_FIRST  4

extern "C" __declspec(dllexport) int __attribute__((thiscall))
JJScript_ReadTextsForReport(ScriptPlayer *self, const char *path, FILE *sink)
{
    return self->readTextsForReport(path, sink);
}

int ScriptPlayer::readTextsForReport(const char *path, FILE *sink)
{
    char name[0x80];
    char line[JJSR_LINE_MAX];
    char scratch[0x100];
    FILE *fp;
    int inBlock = 0, inBlockMirror = 0, wroteHeader = 0;
    unsigned textIndex = 0;
    static int logged = 0;

    strcpy(name, path);
    strcat(name, ".jjs");

    fp = fopen(name, "r+t");                 /* text UPDATE mode, as the original */

    strcpy(line, GLOBAL_SCRATCH_STR);        /* defect 2: pointless pre-fill */

    if (fp == NULL)
        return 0;                            /* defect 1: always 0 */

    while (!feof(fp)) {
        char prefix4[5], prefix9[10];
        size_t len;

        fgets(line, JJSR_LINE_MAX, fp);      /* defect 3: result unchecked */

        memcpy(prefix4, line, 4);
        prefix4[4] = '\0';

        if (!inBlock) {
            if (strcmp(prefix4, "text") == 0) {
                inBlock       = 1;
                inBlockMirror = 1;           /* defect 4 */
                wroteHeader   = 0;
            }
        } else if (line[0] == ';') {
            textBlocks_ = (WORD)(textBlocks_ + 1);
            inBlock       = 0;
            inBlockMirror = 0;
        } else {
            len = strlen(line);
            if (len > 1 && inBlockMirror) {   /* original: CMP ECX,1 / JBE skip */
                if (!wroteHeader) {
                    sprintf(scratch, "\n%d. Text:\n", (int)((textIndex & 0xffff) + 1));
                    fputs(scratch, sink);
                    wroteHeader = 1;
                }
                if (line[len - 2] == ';') {
                    inBlock     = 0;         /* note: the mirror is NOT cleared */
                    wroteHeader = 0;
                    line[len - 2] = '\0';
                    fputs(line, sink);
                    fputs("\n", sink);
                    textIndex++;
                } else {
                    fputs(line, sink);
                }
            }
        }

        memcpy(prefix9, line, 9);
        prefix9[9] = '\0';
        if (strcmp(prefix9, "splinexyz") == 0)
            splineLines_ = (WORD)(splineLines_ + 1);
    }

    fclose(fp);

    if (logged < JJSR_LOG_FIRST) {
        logged++;
        log_write("jjsreport: '%s' texts=%u splines=%u\n", name, textIndex,
                  (unsigned)splineLines_);
    }
    return 0;                                /* defect 1 */
}

/* ─── The player: TickScriptPlayer 0x41d920 and PlayScript 0x41dbe0 ───────
 *
 * The script is read whole at level load (readForLevel above) and executed a
 * line at a time as the level plays.  The original's two functions split the
 * same way this code does, just less visibly:
 *
 *   playScript()   runs ONE line.  It only fills in state -- a glide target,
 *                  a wait, a new spline, a camera value -- and returns.
 *   the updates    advance that state by one frame: the stream wait, the
 *                  spline, the timed wait, the glide.
 *   tick()         orders them.
 *
 * The order in tick() is the original's and it is observable, so it is kept
 * exactly: the spline runs every frame *alongside* later commands (only the
 * stream wait, the timed wait and the glide hold the next line back); at
 * most one line runs per frame; and a frame that fetches a line does no
 * waiting or gliding, while a wait that expires this frame fetches the next
 * line only on the following one.
 *
 * Game bugs kept on purpose (CLAUDE.md "preserve bugs"), each marked BUG:
 * movetoxyz truncates its target to 0..255; the spline's end negates eye y;
 * "distance" compares the raw line rather than the token; "text" on a line
 * of under six characters copies a negative length; initwave keeps a pointer
 * into a stack buffer.  An all-delimiter line reaches strcmp with NULL, as it
 * does in the original.
 *
 * Every callee is ours: the SplinePath methods, the stream exports, the
 * logger.  The one game-heap site is initwave's operator new(0xd4): the
 * stream's deleting dtor frees with game_free2 because InitStreamSoundBuffer
 * 0x443c70 -- still the game's -- allocates the same class.  It retires with
 * that function (ENDGAME_PLAN "alloc.h retires by attrition").
 *
 * KAROO_JJS_FX=glide reverses every movetoxyz direction (a result only this
 * code can produce).  KAROO_JJS_DIAG=1 logs each executed command.
 */


static const char JJS_DELIMS[] = " ,\t\n;";

static bool jjs_fx_glide(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_JJS_FX", buf, sizeof(buf))
                 && lstrcmpiA(buf, "glide") == 0;
    }
    return cached != 0;
}

static bool jjs_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = GetEnvironmentVariableA("KAROO_JJS_DIAG", buf, sizeof(buf))
                 && buf[0] == '1';
    }
    return cached != 0;
}

/* __ftol: truncate toward zero to 64 bits; callers keep the bits they need. */
static long long ftol64(double v) { return (long long)v; }

static float next_float(void) { return (float)atof(strtok(NULL, JJS_DELIMS)); }

unsigned char ScriptPlayer::playScript(const char *line)
{
    char buf[LINE_SIZE];
    strcpy(buf, line);
    const char *cmd = strtok(buf, JJS_DELIMS);
    SplinePath *spline = &spline_;

    if (jjs_diag())
        log_write("jjscript: line %u cmd %s\n", cursor_, cmd ? cmd : "(null)");

    if (strcmp(cmd, "fromhere") == 0) {
        againLine_ = cursor_ + 1;
        return 0x37;
    }
    if (strcmp(cmd, "gotoxyz") == 0) {
        eye_[0] = next_float();
        eye_[1] = next_float();
        eye_[2] = next_float();
        return 2;
    }
    if (strcmp(cmd, "movetoxyz") == 0) {
        /* BUG: each target coordinate keeps only its low byte. */
        for (int i = 0; i < 3; ++i)
            target_[i] = (float)(int)(ftol64(atof(strtok(NULL, JJS_DELIMS))) & 0xff);
        float speed = next_float();
        moving_ = 1;
        speed_ = speed;
        memcpy(from_, eye_, sizeof(from_));
        start_ = now_;
        float dx = target_[0] - eye_[0];
        float dy = target_[1] - eye_[1];
        float dz = target_[2] - eye_[2];
        float len = (float)sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
        /* A 32-bit ftol result loaded as unsigned (FILD qword, high dword 0). */
        duration_ = (double)(unsigned int)ftol64(len / (speed * 0.001f));
        dir_[0] = dx / len;
        dir_[1] = dy / len;
        dir_[2] = dz / len;
        if (jjs_fx_glide())
            for (int i = 0; i < 3; ++i)
                dir_[i] = -dir_[i];
        return 3;
    }
    if (strcmp(cmd, "observe") == 0) {
        cameraMode_ = (unsigned char)ftol64(atof(strtok(NULL, JJS_DELIMS)));
        splineActive_ = (cameraMode_ == 0);
        return 5;
    }
    if (strcmp(cmd, "setcamposxyz") == 0) {
        splinePoint_[0] = next_float();
        splinePoint_[1] = (float)-atof(strtok(NULL, JJS_DELIMS));
        splinePoint_[2] = next_float();
        return 0xc;
    }
    if (strcmp(cmd, "setcamtargetxyz") == 0) {
        eye_[0] = next_float();
        eye_[1] = (float)-atof(strtok(NULL, JJS_DELIMS));
        eye_[2] = next_float();
        return 0xd;
    }
    if (strcmp(cmd, "again") == 0) {
        running_ = 1;
        cursor_ = againLine_;
        return 6;
    }
    if (strcmp(cmd, "break") == 0) {
        cursor_ = 0;
        running_ = 0;
        return 7;
    }
    /* BUG: compares the whole buffer, not the token -- the same unless the
     * line begins with a delimiter. */
    if (strcmp(buf, "distance") == 0) {
        cameraDistance_ = next_float();
        return 8;
    }
    if (strcmp(cmd, "text") == 0) {
        /* Drops "text " and the trailing "\n" the reader appended.
         * BUG: a line under six characters gives a negative length. */
        size_t n = strlen(line) - 6;
        memcpy(scratch_, line + 5, n);
        scratch_[n] = '\0';
        return 9;
    }
    if (strcmp(cmd, "wait") == 0) {
        waitStart_ = now_;
        waiting_ = 1;
        waitSeconds_ = atof(strtok(NULL, JJS_DELIMS));
        return 4;
    }
    if (strcmp(cmd, "anglexyz") == 0) {
        angle_[0] = next_float();
        angle_[1] = next_float();
        angle_[2] = next_float();
        return 10;
    }
    if (strcmp(cmd, "splinexyz") == 0) {
        field_92d_ = 1;
        Spline_PurgeControlPoints(spline);
        memcpy(splinePoint_, GG_CAMERA->eye, sizeof(splinePoint_));   /* camera.h */
        start_ = now_;
        /* An int product loaded as unsigned (FILD qword, high dword 0). */
        duration_ = (double)(unsigned int)(atoi(strtok(NULL, JJS_DELIMS)) * 1000);
        float x = next_float();
        float y = (float)-atof(strtok(NULL, JJS_DELIMS));
        char *tz = strtok(NULL, JJS_DELIMS);
        float z = (float)atof(tz);
        Spline_AddControlPoint(spline, x, z, y);      /* (x, z, -y) */
        /* Further triples; a point is added only when all three parsed, and
         * a missing one keeps the previous value.  Ends when z is absent. */
        while (tz != NULL) {
            int got = 0;
            char *t = strtok(NULL, JJS_DELIMS);
            if (t) { x = (float)atof(t); ++got; }
            t = strtok(NULL, JJS_DELIMS);
            if (t) { y = (float)-atof(t); ++got; }
            tz = strtok(NULL, JJS_DELIMS);
            if (tz) { z = (float)atof(tz); ++got; }
            if (got == 3)
                Spline_AddControlPoint(spline, x, z, y);
        }
        splineActive_ = 1;
        return 0xb;
    }
    if (strcmp(cmd, "initwave") == 0) {
        GameLog_LogMessage(GG_LOGGER, 1, "IS: initwave noticed");
        char *name = strtok(NULL, JJS_DELIMS);
        if (name) {
            if (soundManager_)
                streamWave_.pFilename = name;   /* BUG: points into buf */
            char *tid = strtok(NULL, JJS_DELIMS);
            if (tid) {
                unsigned char id = (unsigned char)atoi(tid);
                if (!soundManager_) {
                    durations_[id] = 10;
                    return 0xc;
                }
                if (streams_[id]) {
                    GameLog_LogMessage(GG_LOGGER, 3,
                        "IS: warning - Stream sound buffer width id %d already initialized!", id);
                    return 0xc;
                }
                void *mem = game_operator_new(0xd4);
                CStreamSoundbuffer *s = mem ? CStream_Initialize((CStreamSoundbuffer *)mem) : NULL;
                streams_[id] = s;
                streamReady_ = CStream_Prepare(s, &streamWave_);
                if (streamReady_)
                    GameLog_LogMessage(GG_LOGGER, 1,
                        "IS: Stream buffer width name %s successfully initialized, ID=%d",
                        streamWave_.pFilename, id);
                else
                    GameLog_LogMessage(GG_LOGGER, 1,
                        "IS: warning - Stream sound buffer width name %s could not initialized !",
                        streamWave_.pFilename);
            }
        }
        return 0xc;
    }
    if (strcmp(cmd, "playwave") == 0) {
        char *tid = strtok(NULL, JJS_DELIMS);
        if (tid) {
            unsigned char id = (unsigned char)atoi(tid);
            if (!soundManager_) {
                /* Soundless: "WAIT" waits the id's nominal duration. */
                char *w = strtok(NULL, JJS_DELIMS);
                if (w && strcmp(w, "WAIT") == 0) {
                    waitStart_ = now_;
                    waiting_ = 1;
                    waitSeconds_ = (double)durations_[id];
                }
            } else if (streams_[id]) {
                CStream_Play(streams_[id]);
                waitingOnStream_ = 0;
                char *w = strtok(NULL, JJS_DELIMS);
                if (w && strcmp(w, "WAIT") == 0) {
                    waitingOnStream_ = 1;
                    waitStream_ = id;
                }
                GameLog_LogMessage(GG_LOGGER, 1,
                    "IS: Stream sound buffer width ID=%d started,wait=%d", id, waitingOnStream_);
            }
        }
        return 0xd;
    }
    if (cmd[0] == '/' && cmd[1] == '/')
        return 0xff;
    return 0;
}

/* A "playwave <id> WAIT" stream wait ends when that stream's thread is done. */
void ScriptPlayer::updateStreamWait()
{
    if (waitingOnStream_ && streams_[waitStream_]->dwThread_done) {
        waitingOnStream_ = 0;
        streamReady_ = 0;
    }
}

/* The camera spline, evaluated at the elapsed fraction of its duration. */
void ScriptPlayer::updateSpline()
{
    if (!splineActive_ || !cameraMode_)
        return;
    double elapsed = now_ - start_;
    if (elapsed < duration_) {
        float out[3];
        const float *p = (const float *)Spline_EvalBezierPath(
            &spline_, out, (float)(elapsed / duration_));
        memcpy(splinePoint_, p, sizeof(splinePoint_));
    } else {
        eye_[1] = -eye_[1];                     /* BUG, kept */
        splineActive_ = 0;
    }
}

/* A "wait <seconds>" ends once that many seconds of the clock have passed. */
void ScriptPlayer::updateWait()
{
    if (waiting_ && waitSeconds_ * 1000.0 <= now_ - waitStart_)
        waiting_ = 0;
}

/* The movetoxyz glide: step along dir_, snap to the target when time is up. */
void ScriptPlayer::updateGlide()
{
    if (!moving_)
        return;
    if (duration_ <= now_ - start_) {
        moving_ = 0;
        memcpy(eye_, target_, sizeof(eye_));
        return;
    }
    float dt = (float)dt_;
    float step = speed_ * 0.001f;
    for (int i = 0; i < 3; ++i)
        eye_[i] = dt * dir_[i] * step + eye_[i];
}

/* Fetch the line at the cursor and run it; log it if playScript refuses. */
void ScriptPlayer::runNextCommand()
{
    if (lineCount_ < cursor_)
        return;
    strcpy(currentLine_, lines_[cursor_]);
    ++cursor_;
    if (lineCount_ < cursor_)
        running_ = 0;
    if (playScript(currentLine_))
        return;
    currentLine_[strlen(currentLine_) - 1] = '\0';   /* drop the "\n" */
    GameLog_LogMessage(GG_LOGGER, 3, "IS:** error at command %d:%s **",
                       (int)cursor_, currentLine_);
}

void ScriptPlayer::tick(double now, double dt)
{
    now_ = now;
    dt_ = dt;
    updateStreamWait();
    updateSpline();
    if (waitingOnStream_)
        return;
    if (!waiting_ && !moving_) {
        runNextCommand();
        return;
    }
    updateWait();
    updateGlide();
}

/* ─── Lifecycle (Game TU) ─────────────────────────────────────────────────── */
static void *const g_ScriptPlayerVtable[1] = { (void *)&ScriptPlayer_ScalarDestructor };

/* 0x41d5e0: the stream and the spline, then these stores. */
void ScriptPlayer::construct()
{
    CStream_Initialize(&stream_);
    Spline_Construct(&spline_);
    cursor_       = 0;
    splineActive_ = 0;
    soundManager_ = NULL;
    vtable_       = g_ScriptPlayerVtable;
    clearStreams();
}

/* 0x41d680: a prepared stream still playing (dwThread_done == 0) is
 * stopped and released first; one that finished is left to
 * DeinitInstance. */
void ScriptPlayer::destruct()
{
    vtable_ = g_ScriptPlayerVtable;
    if (streamReady_ != 0 && stream_.dwThread_done == 0) {
        CStream_Stop(&stream_);
        CStream_ReleaseResources(&stream_);
        streamReady_ = 0;
    }
    Spline_Destruct(&spline_);
    CStream_DeinitInstance(&stream_);
}

void ScriptPlayer::clearStreams()
{
    memset(streams_, 0, sizeof(streams_));
}

extern "C" __declspec(dllexport) ScriptPlayer *__attribute__((thiscall))
ScriptPlayer_ScalarDestructor(ScriptPlayer *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        game_free2(self);
    return self;
}
