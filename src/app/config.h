/* Config -- the game's settings object, embedded in Game at +0x28ab2e
 * (COHESION_PLAN.md Band 3 follow-on; the seventh sub-object).
 *
 * 0x20a52 bytes: it starts right after Game's cameraMode and its persisted
 * blob ends exactly at Game's cameraEye (+0x2ab580).  KAROO_LAYOUT_SIZE
 * asserts it.
 *
 *   InstallConfigVtable     0x41d390  ctor: vtable 0x45d414, zeroes WORD +0x1f404
 *   FillDefaultConfigValues 0x41d520  the defaults, all inside the blob
 *   LoadConfigValues        0x41d3e0  ours: Config_LoadValues (config.cpp)
 *   SaveConfig              0x41d490  ours: Config_Save       (config.cpp)
 *
 * Karoo.cfg is the 0x144e-byte blob +0x1f604..+0x20a52 (persisted()) plus a
 * tag.  What the first 0x1f600 bytes hold is not decoded; nothing of ours
 * touches them.  The fields below are the ones Game code reads -- each was
 * a Game accessor before this class existed, and Game still offers them,
 * delegating here.  Their meanings are on Game's accessors (game.h); the
 * defaults are FillDefaultConfigValues's.
 */
#pragma once

#include <stddef.h>
#include <string.h>
#include "layout.h"

class __attribute__((packed)) Config {
public:
    static const int ORIGIN = 0;

    /* The persisted blob: what Karoo.cfg holds, byte for byte. */
    enum { PERSISTED_OFFSET = 0x1f604, PERSISTED_SIZE = 0x144e };
    unsigned char *persisted()
    {
        return (unsigned char *)this + PERSISTED_OFFSET;
    }

    /* Game::Load copies it into Game's cameraDistance.  Default 5.0. */
    float          cameraDistanceSetting() const       { return cameraDistanceSetting_; }

    int            musicOn() const                     { return musicOn_; }
    void           setMusicOn(int on)                  { musicOn_ = on; }
    unsigned char  cdVolume() const                    { return cdVolume_; }
    void           setCdVolume(unsigned char p)        { cdVolume_ = p; }
    unsigned int   cdMixerVolume() const               { return cdMixerVolume_; }
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
    void           setJoyDeadzone(unsigned short p)    { joyDeadzone_ = p; }

    /* Undecoded blob fields Game code reaches: HandleKeypress toggles
     * +0x1f609 (0 <-> 1); GameTick copies the float +0x20a4c (default 50.0)
     * into +0x20a48, as one dword. */
    /* ── the four video-quality options ─────────────────────────────────
     * One byte each, persisted, defaults 2,1,2,2; the options screen
     * (RenderVideoOptions 0x0042e9f0) draws them in the order Reflection,
     * Shadows, Highlights, Particles, and HandleKeypress edits them.
     * Shadows/Highlights/Particles are 0..2 sliders on keys 0x48/0x49/0x4a;
     * Reflection is a 0/1 toggle on its own key.
     *
     * The pairing, and how it was settled (COHESION_PLAN.md Band 8c):
     * CONFIRMED -- 0x1f60a (Game +0x2aa138) gates D3DRENDERSTATE_SPECULARENABLE
     * in DrawBridgeSurfaces (0x0040964d) and at 0x0040b039, and specular IS
     * "Highlights"; that anchors the label at 0x00466b38 to the byte read at
     * 0x0042fd15, i.e. each label is pushed BEFORE the byte it describes is
     * read.  The other three then follow by position, each label's push
     * followed by exactly one option read, 1:1 and in order:
     *   "Reflection" 0x00466b4c @0x0042eacf -> 0x0042edd6 reads +0x1f609
     *   "Shadows"    0x00466b44 @0x0042ee90 -> 0x0042f3a8 reads +0x1f608
     *   "Highlights" 0x00466b38 @0x0042f9d9 -> 0x0042fd15 reads +0x1f60a
     *   "Particles"  0x00466b2c @0x004301a8 -> 0x00430399 reads +0x1f60b
     * Corroborated twice: the reverse pairing leaves "Reflection" with no
     * read at all, and Reflection is the one option HandleKeypress toggles
     * 0/1 rather than stepping 0..2 -- which is byte [1], +0x1f609. */
    unsigned char &videoShadows()                      { return videoOptions_[0]; }
    unsigned char &videoReflection()                   { return videoOptions_[1]; }
    unsigned char &videoHighlights()                   { return videoOptions_[2]; }
    unsigned char &videoParticles()                    { return videoOptions_[3]; }
    unsigned int   field20a4cBits() const              { unsigned int b; memcpy(&b, &field_20a4c_, 4); return b; }
    void           setField20a48Bits(unsigned int b)   { memcpy(field_20a48_, &b, 4); }

private:
    Config() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(Config);

    const void    *vtable_;                          /* +0x00000  0x45d414 */
    unsigned char  gap_00004[0x1f404 - 0x00004];
    unsigned short field_1f404_;                     /* +0x1f404  ctor zeroes */
    unsigned char  gap_1f406[0x1f604 - 0x1f406];
    /* ── the persisted blob ── */
    unsigned int   field_1f604_;                     /* +0x1f604  default 1 */
    /* Shadows, Reflection, Highlights, Particles -- see the accessors. */
    unsigned char  videoOptions_[4];                 /* +0x1f608  defaults 2,1,2,2 */
    float          cameraDistanceSetting_;           /* +0x1f60c  5.0 */
    unsigned char  gap_1f610[0x1f624 - 0x1f610];
    unsigned int   field_1f624_;                     /* +0x1f624  default 1 */
    int            musicOn_;                         /* +0x1f628  Game 0x2aa156 */
    unsigned char  cdVolume_;                        /* +0x1f62c  Game 0x2aa15a */
    unsigned char  gap_1f62d[0x1f631 - 0x1f62d];
    unsigned int   cdMixerVolume_;                   /* +0x1f631  Game 0x2aa15f */
    unsigned char  gap_1f635[0x20a36 - 0x1f635];
    int            sound3D_;                         /* +0x20a36  Game 0x2ab564 */
    unsigned char  waveVolume_;                      /* +0x20a3a  Game 0x2ab568 */
    unsigned char  gap_20a3b[0x20a3f - 0x20a3b];
    unsigned int   waveOutVolume_;                   /* +0x20a3f  Game 0x2ab56d */
    unsigned char  cameraTurnsWithPlayer_;           /* +0x20a43  Game 0x2ab571 */
    unsigned int   field_20a44_;                     /* +0x20a44  default 0 */
    unsigned char  field_20a48_[4];                  /* +0x20a48  gets +0x20a4c */
    float          field_20a4c_;                     /* +0x20a4c  default 50.0 */
    unsigned short joyDeadzone_;                     /* +0x20a50  Game 0x2ab57e */
};

KAROO_LAYOUT_CHECKS(Config)
{
    KAROO_LAYOUT_AT(field_1f404_,           0x1f404);
    KAROO_LAYOUT_AT(field_1f604_,           0x1f604);
    KAROO_LAYOUT_AT(videoOptions_,          0x1f608);
    KAROO_LAYOUT_AT(cameraDistanceSetting_, 0x1f60c);
    KAROO_LAYOUT_AT(field_1f624_,           0x1f624);
    KAROO_LAYOUT_AT(musicOn_,               0x1f628);
    KAROO_LAYOUT_AT(cdVolume_,              0x1f62c);
    KAROO_LAYOUT_AT(cdMixerVolume_,         0x1f631);
    KAROO_LAYOUT_AT(sound3D_,               0x20a36);
    KAROO_LAYOUT_AT(waveVolume_,            0x20a3a);
    KAROO_LAYOUT_AT(waveOutVolume_,         0x20a3f);
    KAROO_LAYOUT_AT(cameraTurnsWithPlayer_, 0x20a43);
    KAROO_LAYOUT_AT(field_20a44_,           0x20a44);
    KAROO_LAYOUT_AT(field_20a48_,           0x20a48);
    KAROO_LAYOUT_AT(field_20a4c_,           0x20a4c);
    KAROO_LAYOUT_AT(joyDeadzone_,           0x20a50);
    /* The blob is the object's tail. */
    KAROO_LAYOUT_SIZE(Config::PERSISTED_OFFSET + Config::PERSISTED_SIZE);
}

/* The exports patch.py binds (config.cpp). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Config_LoadValues(Config *self, const char *path);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Config_Save(Config *self, const char *path);
