/* Config: the game's settings, a sub-object of Game.  Karoo.cfg holds them
 * as a fixed-layout blob (config.cpp); the fields named here are the ones the
 * code uses.  Part of the blob is not decoded: it is kept as bytes so that it
 * goes back to the file unchanged. */
#pragma once
#include <stdint.h>

#include <stddef.h>
#include <string.h>
#include "renderdevice.h"
 

class Config {
public:
     

    // FORMAT: the persisted blob, what Karoo.cfg holds before its tag.
    enum { PERSISTED_SIZE = 0x144e };
    void encode(unsigned char out[PERSISTED_SIZE]) const;
    void decode(const unsigned char in[PERSISTED_SIZE]);

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
    AdapterId     *adapterId()                         { return &adapterId_; }
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

    Config();
    virtual ~Config();
    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;
    // The defaults, which data/Karoo.cfg.default also holds.
    void fillDefaults();
    unsigned int   savedCdMixerVolume() const          { return savedCdMixerVolume_; }
    void           setSavedCdMixerVolume(unsigned int v) { savedCdMixerVolume_ = v; }
    void           setSavedWaveOutVolume(unsigned int v) { savedWaveOutVolume_ = v; }
    unsigned int   savedWaveOutVolume() const          { return savedWaveOutVolume_; }
    void           setCameraDistanceSetting(float d)   { cameraDistanceSetting_ = d; }
    unsigned int   cdMixerVolume() const               { return cdMixerVolume_; }
    float          cameraDistanceSetting() const       { return cameraDistanceSetting_; }


    /* Karoo.cfg: returns 1 when the file loads and ends with the tag. */
    int loadValues(const char *path);

    int save(const char *path);

private:
     

    // The persisted fields, in blob order.
    unsigned int   field_00_;               // default 1; unread
    unsigned char  videoOptions_[4];        // Shadows, Reflection, Highlights, Particles
    float          cameraDistanceSetting_;  // default 5.0
    // The launcher's device choice, written by its dialog on OK.  PRESERVED:
    // the mode index is a uint32_t, but WinMain passes only its low byte.
    AdapterId      adapterId_;
    unsigned int   displayModeIndex_;
    unsigned int   field_20_;            // default 1; unread
    int            musicOn_;             // 0 or 1
    unsigned char  cdVolume_;            // percent, in steps of 10
    unsigned int   savedCdMixerVolume_;  // the mixer volume at startup, restored at exit
    unsigned int   cdMixerVolume_;       // 0..65536
    // Blob bytes 0x31..0x1432, not decoded; loaded and saved unchanged.
    unsigned char  undecoded_[0x1401];
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
