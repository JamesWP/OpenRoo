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
#include "texture.h"
#include "wrapperobject.h"
#include "ani.h"
#include "faktmesh.h"
#include "levelobject.h"
#include "particles.h"
#include "explodedebris.h"
#include "sky.h"

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

    ThemeObjectKind kind;
    CFaktMesh   *pMesh;
    WrapperObject wrapper;
    ExplodeDebris   explode;  // set up only by `explode`, which needs the mesh first
    DWORD        bExplode;
    float        flExplodeDir[3];  // (t4, t5, t6) rotated -90 degrees about X

    AnimTable    animTable;
    float        flBillboardScale;
    ParticleSystem *pParticleSystems[16];
    // Sixteen bursts of this record's particle systems, one per system,
    // spawned and aged by the frame renderer.
    FxBurst      bursts[16];

    DWORD  dwInstanceCount;
    DWORD  dwMovableType;
    float  flPosX;
    float  flPosY;
    float  flPosZ;
    float  flScaleX;
    float  flScaleY;
    float  flScaleZ;
    float  flRotRateX;
    float  flRotRateY;
    float  flRotRateZ;
    DWORD  dwSubObjectCount;
    SceneSubObject pSubObjects[8];

    DWORD  bLit;
    DWORD  bNoMoveStates;
    DWORD  bNoZWrite;
    DWORD  bNoShadow;
    DWORD  bSpecular;
    DWORD  bRandomYAngle;
    DWORD  bOscillateRandom;
    float  flOscillationAmplitude;
    float  flOscillationFrequency;
    float  flOscillationPhase;
    float  flPump[4];
private:
};

class __attribute__((packed)) ThemeObjectTypeSlot {
public:

    void            *pVtable;          // our one-slot table
    DWORD            dwInstanceCount;  // last record index + 1
    ThemeLevelObject records[8];
private:
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

    char                 themeName[0x100];  // copied from the path after the file is read
    DWORD                dwUnknown100;      // never written
    ThemeObjectTypeSlot  slots[THEME_OBJ_COUNT];

    // Written only inside `environment { }`.
    SceneTexture        *images[THEME_IMG_COUNT];
    ThemeTextColorPair   textColors[THEME_COLOR_COUNT];
    BYTE                 bFogEnabled;
    SkyBackground        sky;  // built from the face names; drawn by sky.cpp
    float                flSideHeight;
private:
};

/* The loader and its helpers. */
class Game;
class RenderDevice;
struct GameLogger;
struct ThemeSoundTable;

extern ThemeAssetBlock g_themeBlock;

/* Releases the block, parses the theme file at path into it, and builds its
 * meshes, textures and sounds. */
bool Theme_Load(Game *game, RenderDevice *d3d, ThemeAssetBlock *block,
                char *path, GameLogger *logger);

/* Releases everything the block holds.  Also called at shutdown. */
void Theme_ReleaseBlock(ThemeAssetBlock *block);

/* Releases one type's records. */
void __attribute__((fastcall))
Theme_ReleaseSlot(ThemeObjectTypeSlot *slot);

/* The lifecycles of a type slot and its records. */
ThemeObjectTypeSlot *Theme_SlotConstruct(ThemeObjectTypeSlot *self);
void Theme_SlotDestruct(ThemeObjectTypeSlot *self);
ThemeObjectTypeSlot *
Theme_SlotScalarDtor(ThemeObjectTypeSlot *self, unsigned int flags);
ThemeLevelObject *Theme_RecordConstruct(ThemeLevelObject *self);
void Theme_RecordDestruct(ThemeLevelObject *self);

/* The block's aggregate construction and destruction, for g_themeBlock. */
ThemeAssetBlock *Theme_BlockConstruct(ThemeAssetBlock *self);
void Theme_BlockDestruct(ThemeAssetBlock *self);

/* The `sound` keyword: event name to id, then ThemeSound_Add. */
bool Theme_RegisterSound(Game *game, char *eventName, const char *waveName);

/* Adds (or replaces) the wave for a theme sound id. */
int ThemeSound_Add(ThemeSoundTable *self, unsigned int id, const char *waveName,
                   DWORD arg3, DWORD arg4);

/* The theme sound table's lifecycle; it is a Game member. */
ThemeSoundTable *ThemeSound_Construct(ThemeSoundTable *self);
void ThemeSound_Destruct(ThemeSoundTable *self);
ThemeSoundTable *
ThemeSound_ScalarDestructor(ThemeSoundTable *self, unsigned char flags);
int ThemeSound_ReleaseAll(ThemeSoundTable *self);
