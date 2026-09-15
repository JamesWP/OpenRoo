/* Karoo.cfg -- the settings object's persisted blob (config.h).  Split from
 * the old playerstate.cpp; ASSET_PLAN.md Phase 2's notes on the format --
 * TEXT mode, the "End" tag, the six uninitialised tag bytes -- are in
 * highscores.cpp's header.
 *
 *   0x41d3e0  LoadConfigValues   (this, path)   ret 4   1 site
 *   0x41d490  SaveConfig         (this, path)   ret 4   2 sites
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "config.h"

/* The blob is Config::persisted(), Config::PERSISTED_SIZE bytes. */
#define CFG_BLOB_SIZE  Config::PERSISTED_SIZE
#define CFG_TAG_SIZE   0xa
#define CFG_TAG        "End"

#define PS_LOG_FIRST   6

static int s_logged = 0;

static void ps_log(const char *what, const char *path, int ok)
{
    if (s_logged < PS_LOG_FIRST) {
        s_logged++;
        log_write("config: %s '%s' -> %s\n", what, path, ok ? "ok" : "FAILED");
    }
}

/* ─── Karoo.cfg ──────────────────────────────────────────────────────────── */

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Config_LoadValues(Config *self, const char *path)
{
    char tag[CFG_TAG_SIZE];
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        ps_log("cfg load", path, 0);
        return 0;
    }
    fread(self->persisted(), CFG_BLOB_SIZE, 1, fp);
    fread(tag, CFG_TAG_SIZE, 1, fp);
    fclose(fp);

    /* The original's inlined strcmp compares until a NUL, so only the first
     * four bytes of the tag can matter. */
    ps_log("cfg load", path, 1);
    return strcmp(tag, CFG_TAG) == 0 ? 1 : 0;
}

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Config_Save(Config *self, const char *path)
{
    char tag[CFG_TAG_SIZE];      /* deliberately uninitialised -- defect 1 */
    FILE *fp = fopen(path, "w");

    if (fp == NULL) {
        ps_log("cfg save", path, 0);
        return 0;
    }
    fwrite(self->persisted(), CFG_BLOB_SIZE, 1, fp);
    strcpy(tag, CFG_TAG);        /* 4 bytes of 10; the rest stay as they were */
    fwrite(tag, CFG_TAG_SIZE, 1, fp);
    fclose(fp);

    ps_log("cfg save", path, 1);
    return 1;
}

