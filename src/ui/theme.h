/* The theme asset block: everything a theme file (themes\<name>.thm) defines,
 * in one global.  For each of 38 object types (the player, foes, tiles,
 * pickups...) up to eight records of meshes, animations, billboards and
 * particle systems; then the HUD and menu images, the text colours, fog, the
 * sky and the side height.  Loading a theme releases the old one first.
 *
 * PRESERVED: inside `environment { }`, the depth-3 `environment` and
 * `textureadress` keywords skip the check for a missing record every other
 * handler makes, and would write near address 0.  No shipped theme does it. */
#pragma once

#include <windows.h>
#include <stddef.h>
#include "layout.h"
#include "scenetexture.h"
#include "wrapperobject.h"
#include "ani.h"
#include "faktmesh.h"
#include "levelobject.h"
#include "particles.h"
#include "explodedebris.h"
#include "sky.h"

class Game;
class RenderDevice;
class GameLogger;

enum ThemeObjectKind : DWORD {
    THEME_KIND_NONE           = 0,
    THEME_KIND_MODEL          = 1,
    THEME_KIND_FIELD          = 2,
    THEME_KIND_BILLBOARD      = 3,
    THEME_KIND_PARTICLESYSTEM = 4,
};
static_assert(sizeof(ThemeObjectKind) == 4, "ThemeObjectKind must stay DWORD-sized");

/* A short-lived particle burst: a position, the milliseconds left and whether
 * it is live. */
struct __attribute__((packed)) FxBurst {
    float pos[3];
    int   msLeft;  // 1000 at spawn
    BYTE  active;
};
static_assert(sizeof(FxBurst) == 0x11, "FxBurst stride");

/* One `model`, `field`, `billboard` or `particlesystem` entry. */
class __attribute__((packed)) ThemeLevelObject {
public:
    static const int ORIGIN = 0;

    ThemeLevelObject *construct();

    void destruct();

    ThemeObjectKind kind() const { return kind_; }
    CFaktMesh   *mesh() const { return pMesh_; }
    WrapperObject &wrapper()             { return wrapper_; }
    const WrapperObject &wrapper() const { return wrapper_; }
    ExplodeDebris &explodeDebris()       { return explode_; }
    const ExplodeDebris &explodeDebris() const { return explode_; }
    AnimTable     &animTable()           { return animTable_; }
    const AnimTable &animTable() const   { return animTable_; }
    DWORD explodes() const { return bExplode_; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    float *explodeDir() { return flExplodeDir_; }
    const float *explodeDir() const { return flExplodeDir_; }
#pragma GCC diagnostic pop
    float billboardScale() const { return flBillboardScale_; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    ParticleSystem **particleSystems() { return pParticleSystems_; }
    ParticleSystem *const *particleSystems() const { return pParticleSystems_; }
#pragma GCC diagnostic pop
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    FxBurst *bursts() { return bursts_; }
    const FxBurst *bursts() const { return bursts_; }
#pragma GCC diagnostic pop
    DWORD instanceCount() const { return dwInstanceCount_; }
    DWORD movableType() const { return dwMovableType_; }
    float posX() const { return flPosX_; }
    float posY() const { return flPosY_; }
    float posZ() const { return flPosZ_; }
    float scaleX() const { return flScaleX_; }
    float scaleY() const { return flScaleY_; }
    float scaleZ() const { return flScaleZ_; }
    float rotRateX() const { return flRotRateX_; }
    float rotRateY() const { return flRotRateY_; }
    float rotRateZ() const { return flRotRateZ_; }
    DWORD subObjectCount() const { return dwSubObjectCount_; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    SceneSubObject *subObjects() { return pSubObjects_; }
    const SceneSubObject *subObjects() const { return pSubObjects_; }
#pragma GCC diagnostic pop
    DWORD lit() const { return bLit_; }
    DWORD noMoveStates() const { return bNoMoveStates_; }
    DWORD noZWrite() const { return bNoZWrite_; }
    DWORD noShadow() const { return bNoShadow_; }
    DWORD specular() const { return bSpecular_; }
    DWORD randomYAngle() const { return bRandomYAngle_; }
    DWORD oscillateRandom() const { return bOscillateRandom_; }
    float oscillationAmplitude() const { return flOscillationAmplitude_; }
    float oscillationFrequency() const { return flOscillationFrequency_; }
    float oscillationPhase() const { return flOscillationPhase_; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    float *pump() { return flPump_; }
    const float *pump() const { return flPump_; }
#pragma GCC diagnostic pop

private:
    friend class ThemeParser;          // theme.cpp fills the records
    friend class ThemeObjectTypeSlot;  // and releases them
    ThemeObjectKind kind_;
    CFaktMesh   *pMesh_;
    WrapperObject wrapper_;
    ExplodeDebris   explode_;  // set up only by `explode`, which needs the mesh first
    DWORD        bExplode_;
    float        flExplodeDir_[3];  // (t4, t5, t6) rotated -90 degrees about X

    AnimTable    animTable_;
    float        flBillboardScale_;
    ParticleSystem *pParticleSystems_[16];
    // Sixteen bursts of this record's particle systems, one per system,
    // spawned and aged by the frame renderer.
    FxBurst      bursts_[16];

    DWORD  dwInstanceCount_;
    DWORD  dwMovableType_;
    float  flPosX_;
    float  flPosY_;
    float  flPosZ_;
    float  flScaleX_;
    float  flScaleY_;
    float  flScaleZ_;
    float  flRotRateX_;
    float  flRotRateY_;
    float  flRotRateZ_;
    DWORD  dwSubObjectCount_;
    SceneSubObject pSubObjects_[8];

    DWORD  bLit_;
    DWORD  bNoMoveStates_;
    DWORD  bNoZWrite_;
    DWORD  bNoShadow_;
    DWORD  bSpecular_;
    DWORD  bRandomYAngle_;
    DWORD  bOscillateRandom_;
    float  flOscillationAmplitude_;
    float  flOscillationFrequency_;
    float  flOscillationPhase_;
    float  flPump_[4];

    KAROO_LAYOUT_REGISTER(ThemeLevelObject);
};

KAROO_LAYOUT_CHECKS(ThemeLevelObject)
{
    KAROO_LAYOUT_AT(kind_,             0x000);
    KAROO_LAYOUT_AT(pMesh_,            0x004);
    KAROO_LAYOUT_AT(wrapper_,          0x008);
    KAROO_LAYOUT_AT(explode_,          0x015);
    KAROO_LAYOUT_AT(bExplode_,         0x0b1);
    KAROO_LAYOUT_AT(flExplodeDir_,     0x0b5);
    KAROO_LAYOUT_AT(animTable_,        0x0c1);
    KAROO_LAYOUT_AT(flBillboardScale_, 0x241);
    KAROO_LAYOUT_AT(pParticleSystems_, 0x245);
    KAROO_LAYOUT_AT(bursts_,           0x285);
    KAROO_LAYOUT_AT(dwInstanceCount_,  0x395);
    KAROO_LAYOUT_AT(dwMovableType_,    0x399);
    KAROO_LAYOUT_AT(flPosX_,           0x39d);
    KAROO_LAYOUT_AT(flScaleX_,         0x3a9);
    KAROO_LAYOUT_AT(flRotRateX_,       0x3b5);
    KAROO_LAYOUT_AT(dwSubObjectCount_, 0x3c1);
    KAROO_LAYOUT_AT(pSubObjects_,      0x3c5);
    KAROO_LAYOUT_AT(bLit_,             0x5a5);
    KAROO_LAYOUT_AT(bNoMoveStates_,    0x5a9);
    KAROO_LAYOUT_AT(bNoZWrite_,        0x5ad);
    KAROO_LAYOUT_AT(bNoShadow_,        0x5b1);
    KAROO_LAYOUT_AT(bSpecular_,        0x5b5);
    KAROO_LAYOUT_AT(bRandomYAngle_,    0x5b9);
    KAROO_LAYOUT_AT(bOscillateRandom_, 0x5bd);
    KAROO_LAYOUT_AT(flOscillationAmplitude_, 0x5c1);
    KAROO_LAYOUT_AT(flOscillationFrequency_, 0x5c5);
    KAROO_LAYOUT_AT(flOscillationPhase_,     0x5c9);
    KAROO_LAYOUT_AT(flPump_,           0x5cd);
    KAROO_LAYOUT_SIZE(0x5dd);
}

class __attribute__((packed)) ThemeObjectTypeSlot {
public:
    static const int ORIGIN = 0;

    /* Releases one type's records. */
    void release();

    /* The lifecycles of a type slot and its records. */
    ThemeObjectTypeSlot *construct();

    void destruct();

    static ThemeObjectTypeSlot * 
    scalarDtor(ThemeObjectTypeSlot *self, unsigned int flags);

    void            *vtable() const { return pVtable_; }
    DWORD instanceCount() const { return dwInstanceCount_; }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    ThemeLevelObject *records() { return records_; }
    const ThemeLevelObject *records() const { return records_; }
#pragma GCC diagnostic pop

private:
    friend class ThemeParser;  // theme.cpp fills the slots
    void            *pVtable_;          // our one-slot table
    DWORD            dwInstanceCount_;  // last record index + 1
    ThemeLevelObject records_[8];

    KAROO_LAYOUT_REGISTER(ThemeObjectTypeSlot);
};

KAROO_LAYOUT_CHECKS(ThemeObjectTypeSlot)
{
    KAROO_LAYOUT_AT(dwInstanceCount_, 0x4);
    KAROO_LAYOUT_AT(records_,         0x8);
    KAROO_LAYOUT_SIZE(0x2ef0);
}

/* Slot order is the theme file's keyword order. */
enum ThemeObjectType {
    THEME_OBJ_JOHN, THEME_OBJ_CATCHER, THEME_OBJ_CATCHERFX, THEME_OBJ_THROWER,
    THEME_OBJ_THROWERFX, THEME_OBJ_PLATE, THEME_OBJ_SIDE, THEME_OBJ_PLATFORM,
    THEME_OBJ_PARAGLIDE, THEME_OBJ_PARAGLIDEFX, THEME_OBJ_ELEVATOR, THEME_OBJ_EXIT,
    THEME_OBJ_GLUE, THEME_OBJ_DESTRUCTFIELD, THEME_OBJ_DESTRUCTFIELDFX, THEME_OBJ_JUMPPAD,
    THEME_OBJ_SLIDE, THEME_OBJ_STAIR, THEME_OBJ_TELEPORTER, THEME_OBJ_CRYSTAL,
    THEME_OBJ_CRYSTALFX, THEME_OBJ_AMMUNITION, THEME_OBJ_BOMB, THEME_OBJ_EXPLOSION,
    THEME_OBJ_SURPRISE, THEME_OBJ_FREEZE, THEME_OBJ_SPEED, THEME_OBJ_SPEEDFX,
    THEME_OBJ_COLLFX, THEME_OBJ_LIFE, THEME_OBJ_SWITCH, THEME_OBJ_TIME,
    THEME_OBJ_ICE, THEME_OBJ_OBSTACLE, THEME_OBJ_OBSTACLEFX, THEME_OBJ_PROTECTION,
    THEME_OBJ_PROTECTIONFX, THEME_OBJ_BRIDGE,
    // PRESERVED: the release covers only 37 slots; EXPLOSION's is never
    // released or cleared between theme loads.
    THEME_OBJ_COUNT
};

enum ThemeImageSlot {
    THEME_IMG_HUD, THEME_IMG_MENU, THEME_IMG_EDGE, THEME_IMG_RADAR, THEME_IMG_POINTER,
    THEME_IMG_FREEZE, THEME_IMG_INVERSECONTROL, THEME_IMG_PROTECTION, THEME_IMG_SLOWDOWN,
    THEME_IMG_SPEED,
    THEME_IMG_COUNT
};

struct __attribute__((packed)) ThemeTextColorPair {
    DWORD color1;
    DWORD color2;
};
static_assert(sizeof(ThemeTextColorPair) == 8, "ThemeTextColorPair stride mismatch");

enum ThemeTextColorSlot {
    THEME_COLOR_HUD, THEME_COLOR_MENUNEWGAME, THEME_COLOR_MENULOADGAME,
    THEME_COLOR_MENUHIGHSCORES, THEME_COLOR_MENUOPTIONS, THEME_COLOR_MENUCREDITS,
    THEME_COLOR_MENUQUIT, THEME_COLOR_MENULOADGAMEENTRIES, THEME_COLOR_MENUSAVEGAMEENTRIES,
    THEME_COLOR_MENUHIGHSCORESENTRIES, THEME_COLOR_MENUOPTIONSCONTROL,
    THEME_COLOR_MENUOPTIONSVIDEO, THEME_COLOR_MENUOPTIONSAUDIO, THEME_COLOR_MENUCONTROLENTRIES,
    THEME_COLOR_MENUCONTROLCAMERA, THEME_COLOR_MENUVIDEOREFLECTION,
    THEME_COLOR_MENUVIDEOSHADOW, THEME_COLOR_MENUVIDEOHIGHLIGHT, THEME_COLOR_MENUVIDEOPARTICLE,
    THEME_COLOR_MENUAUDIO3DSOUND, THEME_COLOR_MENUAUDIOSOUNDVOL, THEME_COLOR_MENUAUDIOCDMUSIC,
    THEME_COLOR_MENUAUDIOCDVOL, THEME_COLOR_MENUSUMMARYENTRIES, THEME_COLOR_MENUSUMMARYNEXT,
    THEME_COLOR_MENUSUMMARYSAVE,
    THEME_COLOR_COUNT
};

class __attribute__((packed)) ThemeAssetBlock {
public:
    static const int ORIGIN = 0;

    /* Releases the block, parses the theme file at path into it, and builds its
     * meshes, textures and sounds. */
    bool load(Game *game, RenderDevice *d3d, char *path, GameLogger *logger);

    /* Releases everything the block holds.  Also called at shutdown. */
    void release();

    /* The block's aggregate construction and destruction, for g_themeBlock. */
    ThemeAssetBlock *construct();

    void destruct();

    const char *themeName() const { return themeName_; }
    ThemeObjectTypeSlot *slot(int type)       { return &slots_[type]; }
    const ThemeObjectTypeSlot *slot(int type) const { return &slots_[type]; }
    DWORD unknown100() const { return dwUnknown100_; }
    SceneTexture        *image(int slot) const { return images_[slot]; }
    const ThemeTextColorPair &textColor(int which) const { return textColors_[which]; }
    BYTE           fogEnabled() const { return bFogEnabled_; }
    SkyBackground &sky()              { return sky_; }
    const SkyBackground &sky() const  { return sky_; }
    float          sideHeight() const { return flSideHeight_; }

private:
    friend class ThemeParser;  // theme.cpp fills the block

    char                 themeName_[0x100];  // copied from the path after the file is read
    DWORD                dwUnknown100_;      // never written
    ThemeObjectTypeSlot  slots_[THEME_OBJ_COUNT];

    // Written only inside `environment { }`.
    SceneTexture        *images_[THEME_IMG_COUNT];
    ThemeTextColorPair   textColors_[THEME_COLOR_COUNT];
    BYTE                 bFogEnabled_;
    SkyBackground        sky_;  // built from the face names; drawn by sky.cpp
    float                flSideHeight_;

    bool themeLoad(Game *game, RenderDevice *d3d, char *path,
                   GameLogger *logger);

    KAROO_LAYOUT_REGISTER(ThemeAssetBlock);
};

KAROO_LAYOUT_CHECKS(ThemeAssetBlock)
{
    KAROO_LAYOUT_AT(slots_, 0x00104);
    KAROO_LAYOUT_AT(slots_[THEME_OBJ_JOHN],      0x00104);
    KAROO_LAYOUT_AT(slots_[THEME_OBJ_EXIT],      0x20554);
    KAROO_LAYOUT_AT(slots_[THEME_OBJ_EXPLOSION], 0x43894);
    KAROO_LAYOUT_AT(slots_[THEME_OBJ_BRIDGE],    0x6c9b4);

    KAROO_LAYOUT_AT(images_, 0x6f8a4);
    KAROO_LAYOUT_AT(images_[THEME_IMG_HUD],            0x6f8a4);
    KAROO_LAYOUT_AT(images_[THEME_IMG_MENU],           0x6f8a8);
    KAROO_LAYOUT_AT(images_[THEME_IMG_EDGE],           0x6f8ac);
    KAROO_LAYOUT_AT(images_[THEME_IMG_RADAR],          0x6f8b0);
    KAROO_LAYOUT_AT(images_[THEME_IMG_POINTER],        0x6f8b4);
    KAROO_LAYOUT_AT(images_[THEME_IMG_FREEZE],         0x6f8b8);
    KAROO_LAYOUT_AT(images_[THEME_IMG_INVERSECONTROL], 0x6f8bc);
    KAROO_LAYOUT_AT(images_[THEME_IMG_PROTECTION],     0x6f8c0);
    KAROO_LAYOUT_AT(images_[THEME_IMG_SLOWDOWN],       0x6f8c4);
    KAROO_LAYOUT_AT(images_[THEME_IMG_SPEED],          0x6f8c8);

    KAROO_LAYOUT_AT(textColors_, 0x6f8cc);
    KAROO_LAYOUT_AT(textColors_[THEME_COLOR_HUD],            0x6f8cc);
    KAROO_LAYOUT_AT(textColors_[THEME_COLOR_MENUNEWGAME],    0x6f8d4);
    KAROO_LAYOUT_AT(textColors_[THEME_COLOR_MENULOADGAME],   0x6f8dc);
    KAROO_LAYOUT_AT(textColors_[THEME_COLOR_MENUSUMMARYSAVE], 0x6f994);

    KAROO_LAYOUT_AT(bFogEnabled_, 0x6f99c);
    KAROO_LAYOUT_AT(sky_, 0x6f99d);
    KAROO_LAYOUT_AT(flSideHeight_, 0x6fd8d);
    KAROO_LAYOUT_SIZE(0x6fd91);
}

/* The loader and its helpers. */

/* A sound asset's file name and, immediately after it, its enabled flag.
 * The spawn's unbounded strcpy of `name` relies on the flag to stop it. */
struct __attribute__((packed)) SoundAssetName {
    char name[256];
    int  enabled;
    DWORD unknown104;   /* add()'s arg4; the .thm path passes 1 */
    DWORD unknown108;   /* add()'s arg3; the .thm path passes 1 */
};
static_assert(sizeof(SoundAssetName) == 0x10c, "SoundAssetName stride");

/* The theme sound table ("TSM" in its log line), Game+0x42258.  A .thm
 * `Sound <event> <wave>` line fills entries[id] through ThemeSoundTable::add
 * (theme.cpp); the id is RegisterThemeSound's event number, so e.g. entry 0
 * is movecatcher and entry 70 explosionbomb.  The event table's largest id is
 * 0x47, but the table holds 100 entries: ReleaseAll clears exactly
 * 100, ending at Game+0x48b12 where switchMax_ begins.  Lifecycle in
 * theme.cpp; the vtable is ours, one slot. */
#define THEME_SOUND_COUNT 100
class __attribute__((packed)) ThemeSoundTable {
public:
    /* Adds (or replaces) the wave for a theme sound id. */
    int add(unsigned int id, const char *waveName, DWORD arg3, DWORD arg4);

    /* The theme sound table's lifecycle; it is a Game member. */
    ThemeSoundTable *construct();

    void destruct();
    static ThemeSoundTable * 
    scalarDestructor(ThemeSoundTable *self, unsigned char flags);

    int releaseAll();

    void          *vtable() const { return vtable_; }

    /* The entry for theme event id. */
    const SoundAssetName *entry(int id) const { return &entries_[id]; }

private:
    void          *vtable_;       /* +0 */
    DWORD          unknown4_;     /* +4  never written */
    WORD           unknown8_;     /* +8  zeroed by the ctor, never read */
    SoundAssetName entries_[THEME_SOUND_COUNT];
};
static_assert(sizeof(ThemeSoundTable) == 10 + 100 * 0x10c, "ThemeSoundTable size");

extern ThemeAssetBlock g_themeBlock;

/* The `sound` keyword: event name to id, then ThemeSoundTable::add. */
  bool  
Theme_RegisterSound(Game *game, char *eventName, const char *waveName);

