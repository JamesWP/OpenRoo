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
#include "alloc.h"
#include <math.h>

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

/* ─── Lifecycle and defaults (Game TU) ────────────────────────────────────── */
static void *const g_ConfigVtable[1] = { (void *)&Config_ScalarDestructor };

void Config::construct()
{
    vtable_      = g_ConfigVtable;
    field_1f404_ = 0;
}

void Config::destruct()
{
    vtable_ = g_ConfigVtable;
}

extern "C" __declspec(dllexport) Config *__attribute__((thiscall))
Config_ScalarDestructor(Config *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        game_free2(self);
    return self;
}

/* 0x41d520.  The CD mixer default is pow(2, 16) * 50, truncated, / 100 --
 * 32768, computed the original's way (_CIpow, ftol, unsigned divide). */
void Config::fillDefaults()
{
    field_1f604_           = 1;
    videoOptions_[1]       = 1;   /* reflection */
    videoOptions_[2]       = 2;   /* highlights */
    cameraDistanceSetting_ = 5.0f;
    videoOptions_[3]       = 2;   /* particles */
    videoOptions_[0]       = 2;   /* shadows */
    sound3D_               = 1;
    field_1f624_           = 1;
    waveVolume_            = 100;
    waveOutVolume_         = 0xffffffff;
    musicOn_               = 1;
    cdVolume_              = 50;
    cdMixerVolume_         = (unsigned int)(long long)(pow(2.0, 16.0) * 50.0) / 100;
    cameraPitch_           = 50.0f;
    cameraYaw_             = 0.0f;
    cameraTurnsWithPlayer_ = 1;
    joyDeadzone_           = 50;
}
