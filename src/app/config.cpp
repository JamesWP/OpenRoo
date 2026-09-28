/* FORMAT: Karoo.cfg is the persisted blob followed by a 10-byte tag holding
 * "End".  Both directions use text mode, as the game does. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include <stdlib.h>
#include <math.h>

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

int Config::loadValues(const char *path)
{
    char tag[CFG_TAG_SIZE];
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        ps_log("cfg load", path, 0);
        return 0;
    }
    fread(persisted(), CFG_BLOB_SIZE, 1, fp);
    fread(tag, CFG_TAG_SIZE, 1, fp);
    fclose(fp);

    // Only the bytes up to the tag's NUL are compared.
    ps_log("cfg load", path, 1);
    return strcmp(tag, CFG_TAG) == 0 ? 1 : 0;
}

int Config::save(const char *path)
{
    char tag[CFG_TAG_SIZE];  // PRESERVED: uninitialised
    FILE *fp = fopen(path, "w");

    if (fp == NULL) {
        ps_log("cfg save", path, 0);
        return 0;
    }
    fwrite(persisted(), CFG_BLOB_SIZE, 1, fp);
    strcpy(tag, CFG_TAG);  // FORMAT: 4 bytes of 10; the rest are whatever the stack held
    fwrite(tag, CFG_TAG_SIZE, 1, fp);
    fclose(fp);

    ps_log("cfg save", path, 1);
    return 1;
}

static void *const g_ConfigVtable[1] = { (void *)&Config::scalarDeletingDtor };

void Config::construct()
{
    vtable_      = g_ConfigVtable;
    field_1f404_ = 0;
}

void Config::destruct()
{
    vtable_ = g_ConfigVtable;
}

Config *__attribute__((thiscall))
Config::scalarDeletingDtor(Config *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* The CD mixer default is pow(2, 16) * 50, truncated, / 100: 32768. */
void Config::fillDefaults()
{
    field_1f604_           = 1;
    videoOptions_[1]       = 1;
    videoOptions_[2]       = 2;
    cameraDistanceSetting_ = 5.0f;
    videoOptions_[3]       = 2;
    videoOptions_[0]       = 2;
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
