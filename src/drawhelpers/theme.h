/* The theme asset block: everything a theme file (themes\<name>.thm) defines,
 * in one ThemeAssetBlock, which the Renderer owns (render/renderer.h); themedraw.h
 * draws its looks.  For each of 38 object types (the player, foes, tiles,
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
#include <string>
#include <vector>
 
#include "texture.h"
#include "uvanimator.h"
#include "ani.h"
#include "animatedmesh.h"
#include "particles.h"
#include "explodedebris.h"
#include "sky.h"

class RenderDevice;
class TextureManager;
class ModelManager;

/* A theme record's draw state for one sub-object: blend, texture address mode,
 * visibility condition and an animated effect, as the theme file's depth-2 and
 * depth-3 keywords set them. */
enum SceneSubObjectEffect : uint32_t {
    SUBOBJ_EFFECT_NONE        = 0,
    SUBOBJ_EFFECT_FLASH       = 1,  // 3 params
    SUBOBJ_EFFECT_PULSE       = 2,  // 2 params
    SUBOBJ_EFFECT_TURN        = 3,  // 1 param
    SUBOBJ_EFFECT_WOBBLE      = 4,  // 3 params
    SUBOBJ_EFFECT_ENVIRONMENT = 5,  // none
    SUBOBJ_EFFECT_SCROLL      = 6,  // 2 params
};

/* dwVisibilityGate is the theme's `condition`: active 1, inactive 2, dead 3,
 * alive 4, paraglide 5, protection 6. */
struct SceneSubObject {
    uint32_t  dwVisibilityGate;
    Texture *pTexture;
    uint32_t  dwBlendSrc;       // the SRCBLEND value
    uint32_t  dwBlendDst;       // the DESTBLEND value
    uint32_t  dwTexAddress;     // TEXTUREADDRESSU/V; 0 means 3
    SceneSubObjectEffect effect;
    float  flEffectParams[3];
};

/* A theme file's `sound <event> <wave>` line, inside `environment { }`. */
struct ThemeSoundLine {
    std::string event;
    std::string wave;
};

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
    float pos[3]{};
    int   msLeft{};  // 1000 at spawn
    uint8_t  active{};
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

    /* Once a frame: true the first time it is asked at this frame clock, so a
     * look drawn at several instances advances its debris (or ticks its
     * particle systems) once, not once per instance. */
    bool particlesDue(double now) { return due(&particlesAt_, now); }

private:
    friend class ThemeParser;          // theme.cpp fills the records
    friend class ThemeObjectTypeSlot;  // and releases them
    ThemeObjectKind kind_;
    AnimatedMesh   *pMesh_{};
    UVAnimator uvAnimator_;
    ExplodeDebris   explode_;  // set up only by `explode`, which needs the mesh first
    uint32_t        bExplode_{};
    float        flExplodeDir_[3]{};  // (t4, t5, t6) rotated -90 degrees about X

    AnimTable    animTable_;
    float        flBillboardScale_{};
    ParticleSystem *pParticleSystems_[16]{};
    // Sixteen bursts of this record's particle systems, one per system,
    // spawned and aged by the frame renderer.
    FxBurst      bursts_[16];

    uint32_t  dwInstanceCount_{};
    uint32_t  dwMovableType_{};
    float  flPosX_{};
    float  flPosY_{};
    float  flPosZ_{};
    float  flScaleX_{};
    float  flScaleY_{};
    float  flScaleZ_{};
    float  flRotRateX_{};
    float  flRotRateY_{};
    float  flRotRateZ_{};
    uint32_t  dwSubObjectCount_{};
    SceneSubObject pSubObjects_[8];

    uint32_t  bLit_{};
    uint32_t  bNoMoveStates_{};
    uint32_t  bNoZWrite_{};
    uint32_t  bNoShadow_{};
    uint32_t  bSpecular_{};
    uint32_t  bRandomYAngle_{};
    uint32_t  bOscillateRandom_{};
    float  flOscillationAmplitude_{};
    float  flOscillationFrequency_{};
    float  flOscillationPhase_{};
    float  flPump_[4]{};
    double particlesAt_ = -1.0;  // the frame clock of the last particlesDue

    static bool due(double *at, double now)
    {
        if (*at == now)
            return false;
        *at = now;
        return true;
    }

     
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
    uint32_t            dwInstanceCount_{};  // last record index + 1
    ThemeLevelObject records_[8];

     
};

 
/* The looks: one slot per kind of thing in the level, named for what the
 * game calls it.  The theme file's keyword for each is beside it.  Looks
 * named _DYING or _EXPLODING replace the plain one while it dies; the
 * PLAYER_ looks are overlays on the player while an effect lasts; _PICKUP,
 * _DUST and _TRAIL looks are bursts and trails (themedraw.h). */
enum ThemeObjectType {
    THEME_OBJ_PLAYER,               // john
    THEME_OBJ_FOE_CATCHER,          // catcher
    THEME_OBJ_FOE_CATCHER_DYING,    // catcherfx
    THEME_OBJ_FOE_THROWER,          // thrower
    THEME_OBJ_FOE_THROWER_DYING,    // throwerfx
    THEME_OBJ_FLOOR,                // plate
    THEME_OBJ_WALL,                 // side
    THEME_OBJ_PLATFORM,             // platform
    THEME_OBJ_ITEM_PARAGLIDER,      // paraglide
    THEME_OBJ_PLAYER_GLIDING,       // paraglidefx
    THEME_OBJ_LIFT,                 // elevator
    THEME_OBJ_EXIT,                 // exit
    THEME_OBJ_STICKY,               // glue
    THEME_OBJ_FALLING,              // destructfield
    THEME_OBJ_FALLING_DUST,         // destructfieldfx
    THEME_OBJ_JUMP_PAD,             // jumppad
    THEME_OBJ_SLIDE,                // slide
    THEME_OBJ_STAIRS,               // stair
    THEME_OBJ_TELEPORTER,           // teleporter
    THEME_OBJ_ITEM_CRYSTAL,         // crystal
    THEME_OBJ_CRYSTAL_PICKUP,       // crystalfx
    THEME_OBJ_ITEM_BOMBS,           // ammunition
    THEME_OBJ_BOMB,                 // bomb
    THEME_OBJ_BOMB_EXPLODING,       // explosion
    THEME_OBJ_ITEM_RANDOM,          // surprise
    THEME_OBJ_ITEM_FREEZE,          // freeze
    THEME_OBJ_ITEM_SPEED_UP,        // speed
    THEME_OBJ_PLAYER_SPEED_TRAIL,   // speedfx
    THEME_OBJ_ITEM_PICKUP,          // collfx
    THEME_OBJ_ITEM_EXTRA_LIFE,      // life
    THEME_OBJ_SWITCH,               // switch
    THEME_OBJ_ITEM_TIME_BONUS,      // time
    THEME_OBJ_ICE,                  // ice
    THEME_OBJ_BOMBABLE,             // obstacle
    THEME_OBJ_BOMBABLE_EXPLODING,   // obstaclefx
    THEME_OBJ_ITEM_PROTECTION,      // protection
    THEME_OBJ_PLAYER_PROTECTED,     // protectionfx
    THEME_OBJ_BRIDGE,               // bridge
    THEME_OBJ_COUNT
};

enum ThemeImageSlot {
    THEME_IMG_HUD, THEME_IMG_MENU, THEME_IMG_EDGE, THEME_IMG_RADAR, THEME_IMG_POINTER,
    THEME_IMG_FREEZE, THEME_IMG_INVERSECONTROL, THEME_IMG_PROTECTION, THEME_IMG_SLOWDOWN,
    THEME_IMG_SPEED,
    THEME_IMG_COUNT
};

struct ThemeTextColorPair {
    uint32_t color1{};
    uint32_t color2{};
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
     * meshes and textures.  The theme's sounds are not the renderer's: its
     * `sound` lines are handed back in sounds, in file order, for the caller to
     * give the theme sound table (ThemeSoundTable::addEvent). */
    bool load(RenderDevice *d3d, TextureManager *textures, ModelManager *models,
              char *path, std::vector<ThemeSoundLine> &sounds);

    /* Releases everything the block holds.  Also called at shutdown. */
    void release(TextureManager *textures, ModelManager *models);

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

    char                 themeName_[0x100]{};  // copied from the path after the file is read
    uint32_t                dwUnknown100_{};      // never written
    ThemeObjectTypeSlot  slots_[THEME_OBJ_COUNT];

    // Written only inside `environment { }`.
    Texture        *images_[THEME_IMG_COUNT]{};
    ThemeTextColorPair   textColors_[THEME_COLOR_COUNT];
    uint8_t                 bFogEnabled_{};
    SkyBackground        sky_;  // built from the face names; drawn by sky.cpp
    float                flSideHeight_{};

    bool themeLoad(RenderDevice *d3d, TextureManager *textures, ModelManager *models,
                   char *path, std::vector<ThemeSoundLine> &sounds);

     
};

/* The loader and its helpers. */



