/* ASSET_PLAN.md Phase 4 (second cycle) — the .leo reader.
 *
 *   0x4235f0  OpenExtraObjectsFile(this, const char *name) -> BOOL
 *             __thiscall, ret 4.  UD2-stubbed.
 *
 * Level3DExtraObjects: the per-level list of extra scene objects (models,
 * billboards, particle systems, sounds).  Phase 0's asset log attributed all
 * 81 .leo opens in a level-report run to the fopen site inside this function.
 *
 * ─── It is the same parser as the .jjs reader ─────────────────────────────
 *
 * Character-at-a-time fgetc, ';' terminates an entry, the entry is
 * NUL-terminated and "\n" appended, and ONE MORE CHARACTER is consumed and
 * discarded after the ';'.  Identical in shape to
 * ReadInstructionScriptForLevel (0x41d720, karoo-hooks/scriptplayer.cpp), down to
 * the 16-bit-masked accumulation index.
 *
 * The one structural difference is what happens to a finished entry: the .jjs
 * reader COPIES it into a 1000-byte record, while this one HANDS IT to
 * ParseExtraObjectEntry (0x423700) and keeps no copy.
 *
 * ─── Where this replacement stops, and why ────────────────────────────────
 *
 * ParseExtraObjectEntry is called, not reimplemented.  It is ~5.8 KB of scene
 * construction: it strtok's the entry, dispatches on "Sound" / "Particle" /
 * "Model" / "Billboard", parses floats and spline points, formats the model,
 * texture and animation paths, and builds the object records at
 * this + n*0xf40.  It also opens the .ani file itself (via FUN_00401070 in
 * the same family), which is why ASSET_PLAN.md moved .ani here from Phase 3.
 *
 * That is object construction, not file reading, and taking it over is a
 * different piece of work with a different acceptance test.  Under the
 * no-callback rule this is a "game logic" call left in place as a deliberate
 * scope decision, named here and in the commit message.  What this file owns
 * is the boundary the plan is actually about: the bytes coming off the disk
 * and the splitting of them into entries.
 *
 * So .ani is NOT yet ours either.  The status table says so.
 *
 * ─── The two counters are different things ────────────────────────────────
 *
 *   this + 0x0c      WORD, incremented HERE, once per ';' -- entries SEEN
 *   this + 0xf400e   WORD, incremented by the handler -- objects BUILT
 *
 * They diverge whenever an entry is malformed enough that the handler bails
 * before committing an object, so neither is a substitute for the other.
 * Both are zeroed before the parse.
 *
 * ─── Defects and oddities preserved deliberately ──────────────────────────
 *
 * 1. The accumulation index is masked to 16 bits against a ~984-byte stack
 *    buffer, so a longer entry smashes the original's frame.  As in
 *    scriptplayer.cpp the replacement uses a 64 KB buffer: identical for every
 *    input the original survives, merely safe beyond it.  Measured over the
 *    shipped content: 33 .leo files, 459 entries, longest 270 bytes.
 * 2. The EOF flag is tested before each read, so the -1 from the read that
 *    hits EOF is stored as 0xFF into the pending entry.
 * 3. A trailing entry with no ';' is discarded -- the handler never sees it.
 * 4. The entry count is bumped BEFORE the handler runs, so it counts entries
 *    submitted, including any the handler rejects.
 * 5. A failed open returns 0 with both counters already zeroed.
 * 6. The path is built with sprintf into a stack buffer with no bounds check.
 *
 * ─── Visual proof ─────────────────────────────────────────────────────────
 *
 * KAROO_LEO_FX=nomodels drops every entry whose first token is "Model", so
 * the level's extra models vanish while its billboards, particles and sounds
 * stay.  A selective change only this code path can make.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "extraobjects.h"
#include "static.h"
#include "soundmanager.h"

/* ParseExtraObjectEntry (0x423700) -- scene construction, deliberately still
 * the game's.  __thiscall(this, char *entry). */
typedef int (__attribute__((thiscall)) *leoentry_fn)(ExtraObjects *self,
                                                     char *entry);
#define ORIG_PARSE_LEO_ENTRY ((leoentry_fn)0x00423700)

#define GAME_DIR         ((const char *)0x004e01c4)
#define LEO_LOG_FIRST    6

static char s_entry[0x10000];      /* the 16-bit index's full range */

static bool fx_nomodels(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_LEO_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "nomodels") == 0);
        log_write("leo: FX mode = %s\n", cached ? "nomodels" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Leo_OpenExtraObjectsFile(ExtraObjects *self, const char *name)
{
    return self->openFile(name);
}

/* The counters are ExtraObjects fields: entries_ (+0xc, entries seen) and
 * objectCount_ (+0xf400e, objects the handler built). */
int ExtraObjects::openFile(const char *name)
{
    char path[512];
    FILE *fp;
    unsigned idx = 0;
    unsigned long s_hash = 2166136261UL;
    static int logged = 0;

    objectCount_ = 0;
    entries_    = 0;

    sprintf(path, "%s\\Level3DExtraObjects\\%s.leo", GAME_DIR, name);

    fp = fopen(path, "r");
    if (fp == NULL)
        return 0;                          /* defect 5 */

    while (!feof(fp)) {
        int c = fgetc(fp);                 /* defect 2 */

        if ((char)c == ';') {
            s_entry[idx & 0xffff] = '\0';
            strcat(s_entry, "\n");
            idx = 0;

            entries_ =
                (WORD)(entries_ + 1);   /* defect 4 */

            for (const char *q = s_entry; *q; q++) {
                s_hash ^= (unsigned char)*q;
                s_hash *= 16777619UL;
            }

            if (!fx_nomodels() || strncmp(s_entry, "Model", 5) != 0)
                ORIG_PARSE_LEO_ENTRY(this, s_entry);

            fgetc(fp);                     /* the discarded character */
        } else {
            s_entry[idx & 0xffff] = (char)c;   /* defect 1 */
            idx++;
        }
    }

    fclose(fp);

    /* KAROO_LEO_DUMP=<path> -- entry count and an FNV-1a 32 hash over every
     * entry text handed to the handler, for comparison against an independent
     * Python split of the same file.  ASSET_PLAN.md Phase 4. */
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_LEO_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                char line[768];
                int len = wsprintfA(line, "%s entries=%u objects=%u hash=%08lx\r\n",
                                    path,
                                    (unsigned)entries_,
                                    (unsigned)objectCount_,
                                    s_hash);
                DWORD w = 0;
                WriteFile(h, line, (DWORD)len, &w, NULL);
                CloseHandle(h);
            }
        }
    }

    if (logged < LEO_LOG_FIRST) {
        logged++;
        log_write("leo: '%s' entries=%u objects=%u\n", path,
                  (unsigned)entries_,
                  (unsigned)objectCount_);
    }
    return 1;
}

/* ═══ 0x00425210 -- Level3DExtraObjects::ReleaseExtraObjectSoundBuffers ════
 * (was levelparse.cpp).  Halts and releases the first 255 records' sounds
 * -- not the 256th, which the ctor never zeroes either.  Its diag is
 * KAROO_LEVELPARSE_DIAG's, read here as levelparse.cpp reads it; the
 * running release count is what that file's DIAG lines print. */
static int      s_rel_diag       = -1;
static unsigned s_released       = 0;
static int      s_logged_release = 0;

static void diag_init(void)
{
    char buf[64];
    DWORD n;

    if (s_rel_diag >= 0)
        return;
    n = GetEnvironmentVariableA("KAROO_LEVELPARSE_DIAG", buf, sizeof(buf));
    s_rel_diag = (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0);
}

unsigned ExtraObjects::releasedCount()
{
    return s_released;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Leo_ReleaseExtraObjectSoundBuffers(ExtraObjects *self)
{
    self->releaseSounds();
}

void ExtraObjects::releaseSounds()
{
    diag_init();

    for (int k = 0; k < RELEASE_COUNT; k++) {
        ExtraObjectRecord *r = &records_[k];
        /* the handle is re-read after HaltPlayback, exactly as the original */
        if (r->sound != 0) {
            CStatic_HaltPlayback(r->sound);
            soundManager_->releaseStaticForOwner(r->sound, 1);
            r->sound = 0;

            s_released++;
            if (s_rel_diag && !s_logged_release) {
                s_logged_release = 1;
                log_write("extraobjects: first extra-object sound release "
                          "(record %d of %d)\n", k, (int)RELEASE_COUNT);
            }
        }
    }
}
