/* ASSET_PLAN.md Phase 0 — the asset log.
 *
 * Wraps the game's three CRT stdio entry points so every file the game opens
 * is recorded, with its caller, its size, and how many of its bytes arrived
 * through fread:
 *
 *   0x4507ba  fopen (24 E8 sites)   0x45158a  fread (130)   0x4504cb  fclose (30)
 *
 * xref.py finds only E8 references to all three -- no E9, no PUSH -- so
 * CALL_PATCHES catches every caller, and none of the three is UD2-stubbed.
 *
 * ─── This file calls into the game binary on purpose ──────────────────────
 *
 * Per ASSET_PLAN.md's no-callback rule, the exception is named here rather
 * than left to be discovered: every wrapper below calls the *original* CRT
 * function, and it must.
 *
 * The game's FILE is MSVC's, and the game reaches into it directly.  The .jjm
 * reader (FUN_0041f190) reads its whole cell array with MSVC-inlined getc:
 *
 *     iVar7 = pFVar2->_cnt - 1;  pFVar2->_cnt = iVar7;
 *     if (iVar7 < 0) _filbuf(pFVar2); else { c = *pFVar2->_ptr; pFVar2->_ptr++; }
 *
 * A FILE* handed back from this DLL's mingw CRT has a different layout, so
 * that inlined sequence would decrement a field that is not _cnt and
 * dereference a pointer that is not _ptr -- silent memory corruption, not a
 * clean failure.  Phase 0 is a measuring instrument, so it takes the callback
 * and keeps the game's FILE objects exactly as they are.
 *
 * The loader replacements of Phases 1-6 do NOT get this exemption: each one
 * opens its own file with our CRT, parses it, and never lets a mingw FILE*
 * reach the game.  That is the whole point of replacing the loader rather
 * than the primitive.
 *
 * ─── What the log is for ──────────────────────────────────────────────────
 *
 * Two things, both of which the plan's later phases depend on:
 *
 *  1. Attribution.  Four openers are named only as addresses in ASSET_PLAN's
 *     closure table (FUN_0041e8b0, FUN_004235f0, FUN_00413520, and whatever
 *     HandleTypedCheatCode opens).  The caller EIP on each open line says
 *     which of them ran and what it read.
 *
 *  2. Coverage.  Which of the 89 levels / 7 themes / 316 textures / 33 .leo
 *     files a given run actually touches, so a phase's acceptance can be
 *     stated against files that are genuinely exercised.
 *
 * ─── The byte counts are expected to come up short ────────────────────────
 *
 * A "SHORT" line is not a bug in this file.  fread is only one of the ways
 * the game reads: getc is inlined (above), and other loaders use fgets/fscanf,
 * none of which pass through here.  The shortfall is the measurement.  A .jjm
 * should be short by exactly width*height*4 bytes, and Phase 1 does not start
 * until that number is confirmed -- it is the check that this instrument is
 * honest, in the sense ASSET_PLAN.md's "the harness lied" section means.
 *
 * KAROO_ASSET_LOG=<path> turns it on.  Unset, every wrapper is a straight
 * tail-call to the original and nothing is written.
 */
#include <windows.h>
#include "log.h"
#include "gamecrt.h"
#include "theme.h"

/* ─── The originals ──────────────────────────────────────────────────────── */



/* ─── Log sink ───────────────────────────────────────────────────────────── */

/* Deliberately not log_write(): the manifest is meant to be parsed, and
 * karoo_hooks.log is full of other traffic.  Win32 file APIs only, so this
 * never touches either CRT's stdio state. */
static HANDLE  s_log     = INVALID_HANDLE_VALUE;
static bool    s_checked = false;
static CRITICAL_SECTION s_lock;
static bool    s_lock_ready = false;

static bool asset_log_enabled(void)
{
    if (!s_checked) {
        char path[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("KAROO_ASSET_LOG", path, sizeof(path));
        s_checked = true;
        if (n > 0 && n < sizeof(path)) {
            InitializeCriticalSection(&s_lock);
            s_lock_ready = true;
            s_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (s_log == INVALID_HANDLE_VALUE)
                log_write("asset: could not open KAROO_ASSET_LOG '%s' (err %lu)\n",
                          path, GetLastError());
            else
                log_write("asset: logging file I/O to '%s'\n", path);
        }
    }
    return s_log != INVALID_HANDLE_VALUE;
}

static void asset_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    if (!asset_log_enabled())
        return;

    va_start(ap, fmt);
    n = wvsprintfA(buf, fmt, ap);   /* no %zu/%f; every caller below uses %s/%lu/%d */
    va_end(ap);
    if (n <= 0)
        return;

    EnterCriticalSection(&s_lock);
    DWORD written = 0;
    WriteFile(s_log, buf, (DWORD)n, &written, NULL);
    LeaveCriticalSection(&s_lock);
}

/* ─── Open-file table ────────────────────────────────────────────────────── */

/* Small and fixed: the game never has more than a handful of files open at
 * once (the log, plus one asset).  Overflow is logged, not grown -- an
 * unexpected number of concurrent opens is itself a finding. */
#define ASSET_SLOTS 32

struct AssetSlot {
    void         *fp;
    char          path[MAX_PATH];
    DWORD         size;       /* from the filesystem, at open time */
    unsigned      read;       /* bytes seen through fread */
    unsigned      calls;      /* fread calls */
};

static AssetSlot s_slots[ASSET_SLOTS];

static DWORD file_size_of(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
        return 0;
    return fad.nFileSizeLow;   /* no game asset is anywhere near 4 GB */
}

static void slot_add(void *fp, const char *path, DWORD size)
{
    for (int i = 0; i < ASSET_SLOTS; i++) {
        if (s_slots[i].fp == NULL) {
            s_slots[i].fp    = fp;
            s_slots[i].size  = size;
            s_slots[i].read  = 0;
            s_slots[i].calls = 0;
            lstrcpynA(s_slots[i].path, path, MAX_PATH);
            return;
        }
    }
    asset_log("SLOTS-FULL %s\r\n", path);
}

static AssetSlot *slot_find(void *fp)
{
    for (int i = 0; i < ASSET_SLOTS; i++)
        if (s_slots[i].fp == fp)
            return &s_slots[i];
    return NULL;
}

/* ─── Wrappers ───────────────────────────────────────────────────────────── */

extern "C" __declspec(dllexport) void * __cdecl
hooks_fopen(const char *path, const char *mode)
{
    void *caller = __builtin_return_address(0);
    void *fp = GC_FOPEN(path, mode);

    /* ASSET_PLAN Phase 5's .thm reader.  Inert unless KAROO_THEME_DIAG is set;
     * it reopens the file itself and never touches this FILE or the game. */
    theme_diag_on_open(path);

    if (asset_log_enabled()) {
        DWORD size = (path != NULL) ? file_size_of(path) : 0;
        asset_log("OPEN  %-52s mode=%-4s size=%-8lu caller=0x%08lx %s\r\n",
                  path ? path : "<null>", mode ? mode : "?",
                  (unsigned long)size, (unsigned long)(ULONG_PTR)caller,
                  fp ? "ok" : "FAILED");
        if (fp != NULL && path != NULL)
            slot_add(fp, path, size);
    }
    return fp;
}

extern "C" __declspec(dllexport) unsigned __cdecl
hooks_fread(void *buf, unsigned size, unsigned count, void *fp)
{
    unsigned got = GC_FREAD(buf, size, count, (FILE *)fp);

    if (asset_log_enabled()) {
        AssetSlot *s = slot_find(fp);
        if (s != NULL) {
            s->read += got * size;
            s->calls++;
        }
    }
    return got;
}

extern "C" __declspec(dllexport) int __cdecl
hooks_fclose(void *fp)
{
    if (asset_log_enabled()) {
        AssetSlot *s = slot_find(fp);
        if (s != NULL) {
            /* The interesting column.  read == size means fread saw the whole
             * file; anything less went through getc/fgets/fscanf instead, and
             * the difference is what a Phase 1..6 replacement has to account
             * for.  See the header comment. */
            long delta = (long)s->size - (long)s->read;
            asset_log("CLOSE %-52s size=%-8lu fread=%-8lu calls=%-4lu %s%ld\r\n",
                      s->path, (unsigned long)s->size, (unsigned long)s->read,
                      (unsigned long)s->calls,
                      delta > 0 ? "SHORT -" : (delta < 0 ? "OVER +" : "exact "),
                      delta > 0 ? delta : -delta);
            s->fp = NULL;
        }
    }
    return GC_FCLOSE((FILE *)fp);
}
