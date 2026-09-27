/* The asset log: the game's file opens, reads and closes, through our CRT,
 * logged to KAROO_ASSET_LOG when it is set.  Each open is logged with its
 * caller and size; each close with how many of the file's bytes came through
 * fread.  A "SHORT" close is expected, not a fault: loaders that read with
 * getc, fgets or fscanf bypass fread.  The log shows which files a run reads,
 * and which code opened them. */

#include <windows.h>
#include "log.h"
#include <stdio.h>

/* Not log_write(): this log is meant to be parsed, and karoo_hooks.log carries
 * everything else.  Win32 file calls only, so the log never touches stdio. */
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
    n = wvsprintfA(buf, fmt, ap);  // no %zu or %f; the callers use %s, %lu and %d
    va_end(ap);
    if (n <= 0)
        return;

    EnterCriticalSection(&s_lock);
    DWORD written = 0;
    WriteFile(s_log, buf, (DWORD)n, &written, NULL);
    LeaveCriticalSection(&s_lock);
}

/* Fixed and small: only a handful of files are ever open at once.  An overflow
 * is logged rather than grown, since it would itself be worth knowing. */
#define ASSET_SLOTS 32

struct AssetSlot {
    void         *fp;
    char          path[MAX_PATH];
    DWORD         size;   // from the filesystem, at open
    unsigned      read;   // bytes seen through fread
    unsigned      calls;  // fread calls
};

static AssetSlot s_slots[ASSET_SLOTS];

static DWORD file_size_of(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
        return 0;
    return fad.nFileSizeLow;  // no game file is near 4 GB
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

extern "C" __declspec(dllexport) void * __cdecl
hooks_fopen(const char *path, const char *mode)
{
    void *caller = __builtin_return_address(0);
    void *fp = fopen(path, mode);

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
    unsigned got = fread(buf, size, count, (FILE *)fp);

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
            // The size less what fread saw: the bytes read some other way.
            long delta = (long)s->size - (long)s->read;
            asset_log("CLOSE %-52s size=%-8lu fread=%-8lu calls=%-4lu %s%ld\r\n",
                      s->path, (unsigned long)s->size, (unsigned long)s->read,
                      (unsigned long)s->calls,
                      delta > 0 ? "SHORT -" : (delta < 0 ? "OVER +" : "exact "),
                      delta > 0 ? delta : -delta);
            s->fp = NULL;
        }
    }

    return fclose((FILE *)fp);
}
