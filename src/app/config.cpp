/* openroo.ini: the settings, in [video], [audio], [camera] and [input]. */
#include <stdio.h>
#include <string.h>
#include "logger.h"
#include "config.h"
#include "ini.h"

static int s_logged = 0;

static void ps_log(const char *what, const char *path, int ok)
{
    if (s_logged < 6) {
        s_logged++;
        g_logger.write("config: %s '%s' -> %s\n", what, path, ok ? "ok" : "FAILED");
    }
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The adapter id as 32 hex digits; empty for the primary adapter (all zero). */
static std::string adapter_text(const AdapterId &id)
{
    static const AdapterId none = {};
    if (!memcmp(&id, &none, sizeof(id))) return "";
    char buf[sizeof(id.bytes) * 2 + 1];
    for (size_t i = 0; i < sizeof(id.bytes); i++)
        snprintf(buf + 2 * i, 3, "%02x", id.bytes[i]);
    return buf;
}

static void adapter_parse(const std::string &text, AdapterId *id)
{
    AdapterId parsed = {};
    if (text.size() == sizeof(parsed.bytes) * 2) {
        for (size_t i = 0; i < sizeof(parsed.bytes); i++) {
            unsigned v;
            if (sscanf(text.c_str() + 2 * i, "%2x", &v) != 1) { parsed = AdapterId(); break; }
            parsed.bytes[i] = (uint8_t)v;
        }
    }
    *id = parsed;
}

int Config::loadValues(const char *path)
{
    fillDefaults();
    IniFile ini;
    if (!ini.load(path)) {
        ps_log("ini load", path, 0);
        return 0;
    }
    adapter_parse(ini.get("video", "adapter"), &adapterId_);
    displayModeIndex_ = (unsigned)clamp(ini.getInt("video", "mode", (int)displayModeIndex_), 0, 255);
    videoOptions_[0] = (unsigned char)clamp(ini.getInt("video", "shadows", videoOptions_[0]), 0, 2);
    videoOptions_[1] = (unsigned char)clamp(ini.getInt("video", "reflection", videoOptions_[1]), 0, 1);
    videoOptions_[2] = (unsigned char)clamp(ini.getInt("video", "highlights", videoOptions_[2]), 0, 2);
    videoOptions_[3] = (unsigned char)clamp(ini.getInt("video", "particles", videoOptions_[3]), 0, 2);

    sound3D_    = clamp(ini.getInt("audio", "sound3d", sound3D_), 0, 1);
    musicOn_    = clamp(ini.getInt("audio", "music", musicOn_), 0, 1);
    cdVolume_   = (unsigned char)clamp(ini.getInt("audio", "music_volume", cdVolume_), 0, 100);
    waveVolume_ = (unsigned char)clamp(ini.getInt("audio", "effects_volume", waveVolume_), 0, 100);

    cameraDistanceSetting_ = ini.getFloat("camera", "distance", cameraDistanceSetting_);
    cameraYaw_             = ini.getFloat("camera", "yaw", cameraYaw_);
    cameraPitch_           = ini.getFloat("camera", "pitch", cameraPitch_);
    cameraTurnsWithPlayer_ = (unsigned char)clamp(ini.getInt("camera", "turns_with_player",
                                                             cameraTurnsWithPlayer_), 0, 1);
    joyDeadzone_ = (unsigned short)clamp(ini.getInt("input", "joy_deadzone", joyDeadzone_), 0, 100);
    activeCameraPitch_ = cameraPitch_;
    ps_log("ini load", path, 1);
    return 1;
}

int Config::save(const char *path)
{
    IniFile ini;
    ini.load(path);  // keep the other sections; a missing file starts empty

    ini.set("video", "adapter", adapter_text(adapterId_));
    ini.setInt("video", "mode", (int)displayModeIndex_);
    ini.setInt("video", "shadows", videoOptions_[0]);
    ini.setInt("video", "reflection", videoOptions_[1]);
    ini.setInt("video", "highlights", videoOptions_[2]);
    ini.setInt("video", "particles", videoOptions_[3]);

    ini.setInt("audio", "sound3d", sound3D_);
    ini.setInt("audio", "music", musicOn_);
    ini.setInt("audio", "music_volume", cdVolume_);
    ini.setInt("audio", "effects_volume", waveVolume_);

    ini.setFloat("camera", "distance", cameraDistanceSetting_);
    ini.setFloat("camera", "yaw", cameraYaw_);
    ini.setFloat("camera", "pitch", cameraPitch_);
    ini.setInt("camera", "turns_with_player", cameraTurnsWithPlayer_);

    ini.setInt("input", "joy_deadzone", joyDeadzone_);

    int ok = ini.save(path) ? 1 : 0;
    ps_log("ini save", path, ok);
    return ok;
}

Config::Config()
{
    fillDefaults();
}

Config::~Config()
{
}

void Config::fillDefaults()
{
    videoOptions_[0]       = 2;
    videoOptions_[1]       = 1;
    videoOptions_[2]       = 2;
    videoOptions_[3]       = 2;
    cameraDistanceSetting_ = 5.0f;
    adapterId_             = AdapterId();
    for (size_t i = 0; i < sizeof(adapterId_.bytes); i++) adapterId_.bytes[i] = 0;
    displayModeIndex_      = 3;  // 1024x768x32
    sound3D_               = 1;
    waveVolume_            = 100;
    musicOn_               = 1;
    cdVolume_              = 50;
    cameraPitch_           = 50.0f;
    activeCameraPitch_     = 50.0f;
    cameraYaw_             = 0.0f;
    cameraTurnsWithPlayer_ = 1;
    joyDeadzone_           = 50;
}
