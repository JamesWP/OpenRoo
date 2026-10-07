/* Config: the game's settings, a sub-object of Game.  They live in the
 * [video], [audio], [camera] and [input] sections of openroo.ini (config.cpp);
 * the key bindings share the file (ProgableControl). */
#pragma once
#include <stdint.h>
#include "renderdevice.h"

class Config {
public:
    Config();
    virtual ~Config();
    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;

    /* The defaults: what a first run, or a missing key, gets. */
    void fillDefaults();

    /* Defaults, then whatever the file at path sets.  Returns 1 if the file
     * was read, 0 if it is missing (the defaults stand). */
    int loadValues(const char *path);

    /* Writes the settings into the file at path, leaving its other sections
     * (the key bindings) as they are.  Returns 1 on success. */
    int save(const char *path);

    int            musicOn() const                     { return musicOn_; }
    void           setMusicOn(int on)                  { musicOn_ = on; }
    /* Music and effects volume, percent in steps of 10. */
    unsigned char  cdVolume() const                    { return cdVolume_; }
    void           setCdVolume(unsigned char p)        { cdVolume_ = p; }
    unsigned char  waveVolume() const                  { return waveVolume_; }
    void           setWaveVolume(unsigned char p)      { waveVolume_ = p; }
    int            sound3D() const                     { return sound3D_; }
    void           setSound3D(int on)                  { sound3D_ = on; }
    unsigned char  cameraTurnsWithPlayer() const       { return cameraTurnsWithPlayer_; }
    void           setCameraTurnsWithPlayer(unsigned char on) { cameraTurnsWithPlayer_ = on; }
    unsigned short joyDeadzone() const                 { return joyDeadzone_; }
    void           setJoyDeadzone(unsigned short p)    { joyDeadzone_ = p; }

    /* The launcher's display device and mode choice. */
    AdapterId     *adapterId()                         { return &adapterId_; }
    unsigned int   displayModeIndex() const            { return displayModeIndex_; }
    void           setDisplayModeIndex(unsigned int i) { displayModeIndex_ = i; }

    // The four video-quality options, defaults 2,1,2,2.  Shadows, Highlights
    // and Particles are 0..2 sliders; Reflection is a 0/1 toggle.  Highlights
    // gates specular lighting.
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
    // copies cameraPitch() in every tick, and the view eases towards it.  Not
    // saved.
    float          activeCameraPitch() const           { return activeCameraPitch_; }
    void           setActiveCameraPitch(float a)       { activeCameraPitch_ = a; }
    // Copied into the Game's camera distance on load; default 5.0.
    void           setCameraDistanceSetting(float d)   { cameraDistanceSetting_ = d; }
    float          cameraDistanceSetting() const       { return cameraDistanceSetting_; }

private:
    unsigned char  videoOptions_[4]{};        // Shadows, Reflection, Highlights, Particles
    float          cameraDistanceSetting_{};
    // PRESERVED: the mode index is a uint32_t, but WinMain passes only its low byte.
    AdapterId      adapterId_;              // all zero: the primary adapter
    unsigned int   displayModeIndex_{};
    int            musicOn_{};                // 0 or 1
    unsigned char  cdVolume_{};
    int            sound3D_{};                // 0 or 1
    unsigned char  waveVolume_{};
    unsigned char  cameraTurnsWithPlayer_{};  // 0 or 1
    float          cameraYaw_{};
    float          activeCameraPitch_{};
    float          cameraPitch_{};            // 50..89, default 50
    unsigned short joyDeadzone_{};            // percent, default 50
};
