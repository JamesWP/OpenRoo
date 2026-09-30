/* FORMAT: Karoo.cfg is the persisted blob followed by a 10-byte tag holding
 * "End".  Both directions use text mode, as the game does. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include <stdlib.h>
#include <math.h>
#include "bytes.h"

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

void Config::encode(unsigned char out[PERSISTED_SIZE]) const
{
    unsigned char *p = out;
    put_u32(p, field_00_);
    put_bytes(p, videoOptions_, sizeof(videoOptions_));
    put_bytes(p, &cameraDistanceSetting_, 4);
    put_bytes(p, &adapterGuid_, sizeof(adapterGuid_));
    put_u32(p, displayModeIndex_);
    put_u32(p, field_20_);
    put_u32(p, (unsigned int)musicOn_);
    put_u8(p, cdVolume_);
    put_u32(p, savedCdMixerVolume_);
    put_u32(p, cdMixerVolume_);
    put_bytes(p, undecoded_, sizeof(undecoded_));
    put_u32(p, (unsigned int)sound3D_);
    put_u8(p, waveVolume_);
    put_u32(p, savedWaveOutVolume_);
    put_u32(p, waveOutVolume_);
    put_u8(p, cameraTurnsWithPlayer_);
    put_bytes(p, &cameraYaw_, 4);
    put_bytes(p, &activeCameraPitch_, 4);
    put_bytes(p, &cameraPitch_, 4);
    put_u16(p, joyDeadzone_);
}

void Config::decode(const unsigned char in[PERSISTED_SIZE])
{
    const unsigned char *p = in;
    field_00_ = get_u32(p);
    get_bytes(p, videoOptions_, sizeof(videoOptions_));
    get_bytes(p, &cameraDistanceSetting_, 4);
    get_bytes(p, &adapterGuid_, sizeof(adapterGuid_));
    displayModeIndex_   = get_u32(p);
    field_20_           = get_u32(p);
    musicOn_            = (int)get_u32(p);
    cdVolume_           = get_u8(p);
    savedCdMixerVolume_ = get_u32(p);
    cdMixerVolume_      = get_u32(p);
    get_bytes(p, undecoded_, sizeof(undecoded_));
    sound3D_            = (int)get_u32(p);
    waveVolume_         = get_u8(p);
    savedWaveOutVolume_ = get_u32(p);
    waveOutVolume_      = get_u32(p);
    cameraTurnsWithPlayer_ = get_u8(p);
    get_bytes(p, &cameraYaw_, 4);
    get_bytes(p, &activeCameraPitch_, 4);
    get_bytes(p, &cameraPitch_, 4);
    joyDeadzone_        = get_u16(p);
}

int Config::loadValues(const char *path)
{
    char tag[CFG_TAG_SIZE];
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        ps_log("cfg load", path, 0);
        return 0;
    }
    // A short file leaves the rest of the blob as it was.
    unsigned char blob[CFG_BLOB_SIZE];
    encode(blob);
    fread(blob, CFG_BLOB_SIZE, 1, fp);
    decode(blob);
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
    unsigned char blob[CFG_BLOB_SIZE];
    encode(blob);
    fwrite(blob, CFG_BLOB_SIZE, 1, fp);
    strcpy(tag, CFG_TAG);  // FORMAT: 4 bytes of 10; the rest are whatever the stack held
    fwrite(tag, CFG_TAG_SIZE, 1, fp);
    fclose(fp);

    ps_log("cfg save", path, 1);
    return 1;
}

Config::Config()
{
}

Config::~Config()
{
}


/* The CD mixer default is pow(2, 16) * 50, truncated, / 100: 32768. */
void Config::fillDefaults()
{
    field_00_              = 1;
    videoOptions_[1]       = 1;
    videoOptions_[2]       = 2;
    cameraDistanceSetting_ = 5.0f;
    videoOptions_[3]       = 2;
    videoOptions_[0]       = 2;
    sound3D_               = 1;
    field_20_              = 1;
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
