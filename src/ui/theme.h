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

#include <stdint.h>
#include <stddef.h>
 
#include "texture.h"
#include "uvanimator.h"
#include "ani.h"
#include "animatedmesh.h"
#include "levelobject.h"
#include "particles.h"
#include "explodedebris.h"
#include "sky.h"

class Game;
class RenderDevice;

enum ThemeObjectKind : uint32_t {
    THEME_KIND_NONE           = 0,
    THEME_KIND_MODEL          = 1,
    THEME_KIND_FIELD          = 2,
    THEME_KIND_BILLBOARD      = 3,
    THEME_KIND_PARTICLESYSTEM = 4,
};

/* A short-lived particle burst: a position, the milliseconds left and whether
 * it is live. */
struct FxBurst {
    float pos[3];
    int   msLeft;  // 1000 at spawn
    uint8_t  active;
};

/* One `model`, `field`, `billboard` or `particlesystem` entry. */
class ThemeLevelObject {
public:
     

    // The UV animator and explode-debris members build and tear themselves down.
    ThemeLevelObject() = default;

    ThemeObjectKind kind() const { return kind_; }
    AnimatedMesh   *mesh() const { return pMesh_; }
    UVAnimator &uvAnimator()             { return uvAnimator_; }
    const UVAnimator &uvAnimator() const { return uvAnimator_; }
    ExplodeDebris &explodeDebris()       { return explode_; }
    const ExplodeDebris &explodeDebris() const { return explode_; }
    AnimTable     &animTable()           { return animTable_; }
    const AnimTable &animTable() const   { return animTable_; }
    uint32_t explodes() const { return bExplode_; }
 
 
    float *explodeDir() { return flExplodeDir_; }
    const float *explodeDir() const { return flExplodeDir_; }
 
    float billboardScale() const { return flBillboardScale_; }
 
 
    ParticleSystem **particleSystems() { return pParticleSystems_; }
    ParticleSystem *const *particleSystems() const { return pParticleSystems_; }
 
 
 
    FxBurst *bursts() { return bursts_; }
    const FxBurst *bursts() const { return bursts_; }
 
    uint32_t instanceCount() const { return dwInstanceCount_; }
    uint32_t movableType() const { return dwMovableType_; }
    float posX() const { return flPosX_; }
    float posY() const { return flPosY_; }
    float posZ() const { return flPosZ_; }
    float scaleX() const { return flScaleX_; }
    float scaleY() const { return flScaleY_; }
    float scaleZ() const { return flScaleZ_; }
    float rotRateX() const { return flRotRateX_; }
    float rotRateY() const { return flRotRateY_; }
    float rotRateZ() const { return flRotRateZ_; }
    uint32_t subObjectCount() const { return dwSubObjectCount_; }
 
 
    SceneSubObject *subObjects() { return pSubObjects_; }
    const SceneSubObject *subObjects() const { return pSubObjects_; }
 
    uint32_t lit() const { return bLit_; }
    uint32_t noMoveStates() const { return bNoMoveStates_; }
    uint32_t noZWrite() const { return bNoZWrite_; }
    uint32_t noShadow() const { return bNoShadow_; }
    uint32_t specular() const { return bSpecular_; }
    uint32_t randomYAngle() const { return bRandomYAngle_; }
    uint32_t oscillateRandom() const { return bOscillateRandom_; }
    float oscillationAmplitude() const { return flOscillationAmplitude_; }
    float oscillationFrequency() const { return flOscillationFrequency_; }
    float oscillationPhase() const { return flOscillationPhase_; }
 
 
    float *pump() { return flPump_; }
    const float *pump() const { return flPump_; }
 

private:
    friend class ThemeParser;          // theme.cpp fills the records
    friend class ThemeObjectTypeSlot;  // and releases them
    ThemeObjectKind kind_;
    AnimatedMesh   *pMesh_;
    UVAnimator uvAnimator_;
    ExplodeDebris   explode_;  // set up only by `explode`, which needs the mesh first
    uint32_t        bExplode_;
    float        flExplodeDir_[3];  // (t4, t5, t6) rotated -90 degrees about X

    AnimTable    animTable_;
    float        flBillboardScale_;
    ParticleSystem *pParticleSystems_[16];
    // Sixteen bursts of this record's particle systems, one per system,
    // spawned and aged by the frame renderer.
    FxBurst      bursts_[16];

    uint32_t  dwInstanceCount_;
    uint32_t  dwMovableType_;
    float  flPosX_;
    float  flPosY_;
    float  flPosZ_;
    float  flScaleX_;
    float  flScaleY_;
    float  flScaleZ_;
    float  flRotRateX_;
    float  flRotRateY_;
    float  flRotRateZ_;
    uint32_t  dwSubObjectCount_;
    SceneSubObject pSubObjects_[8];

    uint32_t  bLit_;
    uint32_t  bNoMoveStates_;
    uint32_t  bNoZWrite_;
    uint32_t  bNoShadow_;
    uint32_t  bSpecular_;
    uint32_t  bRandomYAngle_;
    uint32_t  bOscillateRandom_;
    float  flOscillationAmplitude_;
    float  flOscillationFrequency_;
    float  flOscillationPhase_;
    float  flPump_[4];

     
};

class ThemeObjectTypeSlot {
public:
     

    /* Releases one type's records. */
    void release();

    /* The records build themselves; the destructor releases them. */
    ThemeObjectTypeSlot();
    ~ThemeObjectTypeSlot();
    ThemeObjectTypeSlot(const ThemeObjectTypeSlot &) = delete;
    ThemeObjectTypeSlot &operator=(const ThemeObjectTypeSlot &) = delete;

    uint32_t instanceCount() const { return dwInstanceCount_; }
 
 
    ThemeLevelObject *records() { return records_; }
    const ThemeLevelObject *records() const { return records_; }
 

private:
    friend class ThemeParser;  // theme.cpp fills the slots
    uint32_t            dwInstanceCount_;  // last record index + 1
    ThemeLevelObject records_[8];

     
};

 
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

struct ThemeTextColorPair {
    uint32_t color1;
    uint32_t color2;
};

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

class ThemeAssetBlock {
public:
     

    /* Releases the block, parses the theme file at path into it, and builds its
     * meshes, textures and sounds. */
    bool load(Game *game, RenderDevice *d3d, char *path);

    /* Releases everything the block holds.  Also called at shutdown. */
    void release();

    /* The 38 slots and the sky build themselves, and are destroyed in
     * reverse; the plain data between them is left alone. */
    ThemeAssetBlock();
    ~ThemeAssetBlock();
    ThemeAssetBlock(const ThemeAssetBlock &) = delete;
    ThemeAssetBlock &operator=(const ThemeAssetBlock &) = delete;

    const char *themeName() const { return themeName_; }
    ThemeObjectTypeSlot *slot(int type)       { return &slots_[type]; }
    const ThemeObjectTypeSlot *slot(int type) const { return &slots_[type]; }
    uint32_t unknown100() const { return dwUnknown100_; }
    Texture        *image(int slot) const { return images_[slot]; }
    const ThemeTextColorPair &textColor(int which) const { return textColors_[which]; }
    uint8_t           fogEnabled() const { return bFogEnabled_; }
    SkyBackground &sky()              { return sky_; }
    const SkyBackground &sky() const  { return sky_; }
    float          sideHeight() const { return flSideHeight_; }

private:
    friend class ThemeParser;  // theme.cpp fills the block

    char                 themeName_[0x100];  // copied from the path after the file is read
    uint32_t                dwUnknown100_;      // never written
    ThemeObjectTypeSlot  slots_[THEME_OBJ_COUNT];

    // Written only inside `environment { }`.
    Texture        *images_[THEME_IMG_COUNT];
    ThemeTextColorPair   textColors_[THEME_COLOR_COUNT];
    uint8_t                 bFogEnabled_;
    SkyBackground        sky_;  // built from the face names; drawn by sky.cpp
    float                flSideHeight_;

    bool themeLoad(Game *game, RenderDevice *d3d, char *path);

     
};

/* The loader and its helpers. */

/* A sound asset's file name and, immediately after it, its enabled flag.
 * The spawn's unbounded strcpy of `name` relies on the flag to stop it. */
struct SoundAssetName {
    char name[256];
    int  enabled;
    uint32_t unknown104;   /* add()'s arg4; the .thm path passes 1 */
    uint32_t unknown108;   /* add()'s arg3; the .thm path passes 1 */
};

/* The theme sound table ("TSM" in its log line),.  A .thm
 * `Sound <event> <wave>` line fills entries[id] through ThemeSoundTable::add
 * (theme.cpp); the id is RegisterThemeSound's event number, so e.g. entry 0
 * is movecatcher and entry 70 explosionbomb.  The event table's largest id is
 * 0x47, but the table holds 100 entries: ReleaseAll clears exactly
 * 100, where switchMax_ begins.  Lifecycle in
 * theme.cpp; the vptr is at +0. */
#define THEME_SOUND_COUNT 100
class ThemeSoundTable {
public:
    /* Adds (or replaces) the wave for a theme sound id. */
    int add(unsigned int id, const char *waveName, uint32_t arg3, uint32_t arg4);

    /* An empty table: every entry cleared (see releaseAll). */
    ThemeSoundTable();
    virtual ~ThemeSoundTable();
    ThemeSoundTable(const ThemeSoundTable &) = delete;
    ThemeSoundTable &operator=(const ThemeSoundTable &) = delete;

    int releaseAll();

    /* The entry for theme event id. */
    const SoundAssetName *entry(int id) const { return &entries_[id]; }

private:
    uint32_t          unknown4_;     /* +4  never written */
    uint16_t           unknown8_;     /* +8  zeroed by the ctor, never read */
    SoundAssetName entries_[THEME_SOUND_COUNT];
};

extern ThemeAssetBlock g_themeBlock;

/* The `sound` keyword: event name to id, then ThemeSoundTable::add. */
  bool  
Theme_RegisterSound(Game *game, char *eventName, const char *waveName);

