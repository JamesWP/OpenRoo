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
 * (0x4661e0) it(self), like the .jjm reader appends ".jjm".
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
 * 1. THE ACCUMULATION INDEX IS MASKED TO 16 BITS while the counter it(self) is
 *    32-bit (`MOV EDX,EBX; AND EDX,0xffff; INC EBX`).  The original's buffer
 *    is 1000 bytes, so any entry longer than that writes past it into its own
 *    frame; at 65536 characters the index wraps instead.
 *
 *    Reproduced faithfully up to the point where the original would corrupt
 *    it(self): the buffer here is a full 64 KB, so the masked index can never
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
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "scriptplayer.h"
#include "stream.h"

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
