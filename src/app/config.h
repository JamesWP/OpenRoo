/* Config: the game's settings, a sub-object of Game.  Its tail is the
 * persisted blob Karoo.cfg holds (config.cpp); the fields named here are the
 * ones the code uses.  Most of the object, and parts of the blob, are not
 * decoded and nothing reads them. */
#pragma once

#include <stddef.h>
#include <string.h>
#include "layout.h"

class __attribute__((packed)) Config {
public:
    static const int ORIGIN = 0;

    // FORMAT: the persisted blob, what Karoo.cfg holds byte for byte.
    enum { PERSISTED_OFFSET = 0x1f604, PERSISTED_SIZE = 0x144e };
    unsigned char *persisted()
    {
        return (unsigned char *)this + PERSISTED_OFFSET;
    }

    // Copied into the Game's camera distance on load; default 5.0.

    int            musicOn() const                     { return musicOn_; }
    void           setMusicOn(int on)                  { musicOn_ = on; }
    unsigned char  cdVolume() const                    { return cdVolume_; }
    void           setCdVolume(unsigned char p)        { cdVolume_ = p; }
    void           setCdMixerVolume(unsigned int v)    { cdMixerVolume_ = v; }
    int            sound3D() const                     { return sound3D_; }
    void           setSound3D(int on)                  { sound3D_ = on; }
    unsigned char  waveVolume() const                  { return waveVolume_; }
    void           setWaveVolume(unsigned char p)      { waveVolume_ = p; }
    unsigned int   waveOutVolume() const               { return waveOutVolume_; }
    void           setWaveOutVolume(unsigned int v)    { waveOutVolume_ = v; }
    unsigned char  cameraTurnsWithPlayer() const       { return cameraTurnsWithPlayer_; }
    void           setCameraTurnsWithPlayer(unsigned char on) { cameraTurnsWithPlayer_ = on; }
    unsigned short joyDeadzone() const                 { return joyDeadzone_; }

/* The launcher's display device and mode choice. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    GUID          *adapterGuid()                       { return &adapterGuid_; }
#pragma GCC diagnostic pop
    unsigned int   displayModeIndex() const            { return displayModeIndex_; }
    void           setDisplayModeIndex(unsigned int i) { displayModeIndex_ = i; }
    void           setJoyDeadzone(unsigned short p)    { joyDeadzone_ = p; }

    // The four video-quality options, one persisted byte each, defaults
    // 2,1,2,2.  Shadows, Highlights and Particles are 0..2 sliders; Reflection
    // is a 0/1 toggle.  Highlights gates specular lighting.
    unsigned char &videoShadows()                      { return videoOptions_[0]; }
    unsigned char &videoReflection()                   { return videoOptions_[1]; }
    unsigned char &videoHighlights()                   { return videoOptions_[2]; }
    unsigned char &videoParticles()                    { return videoOptions_[3]; }
    // The orbit camera's angles, in degrees.
    float          cameraYaw() const                   { return cameraYaw_; }
    void           setCameraYaw(float a)               { cameraYaw_ = a; }
    float          cameraPitch() const                 { return cameraPitch_; }
    void           setCameraPitch(float a)             { cameraPitch_ = a; }
    // The tilt in effect, in degrees: the level builder seeds 60, the tick
    // copies cameraPitch() in every tick, and the view eases towards it.
    float          activeCameraPitch() const           { return activeCameraPitch_; }
    void           setActiveCameraPitch(float a)       { activeCameraPitch_ = a; }

    // Called only by the Game's construction and destruction.
    void construct();
    void destruct();
    // The defaults, which data/Karoo.cfg.default also holds.
    void fillDefaults();
    unsigned int   savedCdMixerVolume() const          { return savedCdMixerVolume_; }
    void           setSavedCdMixerVolume(unsigned int v) { savedCdMixerVolume_ = v; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    unsigned int  *savedWaveOutVolumeRef()             { return &savedWaveOutVolume_; }
#pragma GCC diagnostic pop
    unsigned int   savedWaveOutVolume() const          { return savedWaveOutVolume_; }
    void           setCameraDistanceSetting(float d)   { cameraDistanceSetting_ = d; }
    unsigned int   cdMixerVolume() const               { return cdMixerVolume_; }
    float          cameraDistanceSetting() const       { return cameraDistanceSetting_; }

    /* The one slot of Config's vtable. */
    static Config *__attribute__((thiscall))
    scalarDeletingDtor(Config *self, unsigned char flags);

private:
    Config() = delete;  // only ever reached through the Game
    KAROO_LAYOUT_REGISTER(Config);

    const void    *vtable_;
    unsigned char  gap_00004[0x1f404 - 0x00004];
    unsigned short field_1f404_;  // zeroed by the constructor; unread
    unsigned char  gap_1f406[0x1f604 - 0x1f406];
    // The persisted blob starts here.
    unsigned int   field_1f604_;            // default 1; unread
    unsigned char  videoOptions_[4];        // Shadows, Reflection, Highlights, Particles
    float          cameraDistanceSetting_;  // default 5.0
    // The launcher's device choice, written by its dialog on OK.  PRESERVED:
    // the mode index is a DWORD, but WinMain passes only its low byte.
    GUID           adapterGuid_;
    unsigned int   displayModeIndex_;
    unsigned int   field_1f624_;         // default 1; unread
    int            musicOn_;             // 0 or 1
    unsigned char  cdVolume_;            // percent, in steps of 10
    unsigned int   savedCdMixerVolume_;  // the mixer volume at startup, restored at exit
    unsigned int   cdMixerVolume_;       // 0..65536
    unsigned char  gap_1f635[0x20a36 - 0x1f635];
    int            sound3D_;                // 0 or 1
    unsigned char  waveVolume_;             // percent, in steps of 10
    unsigned int   savedWaveOutVolume_;     // the wave volume at startup, restored at exit
    unsigned int   waveOutVolume_;          // left and right 16-bit halves
    unsigned char  cameraTurnsWithPlayer_;  // 0 or 1
    float          cameraYaw_;              // degrees, default 0
    float          activeCameraPitch_;      // degrees
    float          cameraPitch_;            // degrees, 50..89, default 50
    unsigned short joyDeadzone_;            // percent, default 50
};

KAROO_LAYOUT_CHECKS(Config)
{
    KAROO_LAYOUT_AT(field_1f404_,           0x1f404);
    KAROO_LAYOUT_AT(field_1f604_,           0x1f604);
    KAROO_LAYOUT_AT(videoOptions_,          0x1f608);
    KAROO_LAYOUT_AT(cameraDistanceSetting_, 0x1f60c);
    KAROO_LAYOUT_AT(adapterGuid_,           0x1f610);
    KAROO_LAYOUT_AT(displayModeIndex_,      0x1f620);
    KAROO_LAYOUT_AT(field_1f624_,           0x1f624);
    KAROO_LAYOUT_AT(musicOn_,               0x1f628);
    KAROO_LAYOUT_AT(cdVolume_,              0x1f62c);
    KAROO_LAYOUT_AT(cdMixerVolume_,         0x1f631);
    KAROO_LAYOUT_AT(sound3D_,               0x20a36);
    KAROO_LAYOUT_AT(waveVolume_,            0x20a3a);
    KAROO_LAYOUT_AT(waveOutVolume_,         0x20a3f);
    KAROO_LAYOUT_AT(cameraTurnsWithPlayer_, 0x20a43);
    KAROO_LAYOUT_AT(cameraYaw_,           0x20a44);
    KAROO_LAYOUT_AT(activeCameraPitch_,     0x20a48);
    KAROO_LAYOUT_AT(cameraPitch_,           0x20a4c);
    KAROO_LAYOUT_AT(joyDeadzone_,           0x20a50);
    // The blob is the object's tail.
    KAROO_LAYOUT_SIZE(Config::PERSISTED_OFFSET + Config::PERSISTED_SIZE);
}

/* Karoo.cfg: returns 1 when the file loads and ends with the tag. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Config_LoadValues(Config *self, const char *path);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Config_Save(Config *self, const char *path);

