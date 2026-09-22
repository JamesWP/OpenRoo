#pragma once

#include <windows.h>
#include <stddef.h>
#include "layout.h"
#include "texture.h"
#include "wrapperobject.h"
#include "ani.h"
#include "faktmesh.h"
#include "levelobject.h"
#include "particles.h"

bool theme_diag_on_open(const char *path);
void theme_diag_on_close();

class __attribute__((packed)) ThemeLevelObject {
public:
    static const int ORIGIN = 0;

    BYTE         gap_000_pumpOverflowTarget[8];
    DWORD        dwKind;
    CFaktMesh   *pMesh;
    WrapperObject wrapper;
    BYTE         gap_01d[0xc9 - 0x10 - sizeof(WrapperObject)];

    AnimTable    animTable;
    float        flBillboardScale;
    ParticleSystem *pParticleSystems[16];
    BYTE         gap_28d[0x39d - 0x28d];

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
    float  flPump[2];
private:
    KAROO_LAYOUT_REGISTER(ThemeLevelObject);
};

KAROO_LAYOUT_CHECKS(ThemeLevelObject)
{
    KAROO_LAYOUT_AT(dwKind,           0x008);
    KAROO_LAYOUT_AT(pMesh,            0x00c);
    KAROO_LAYOUT_AT(wrapper,          0x010);
    KAROO_LAYOUT_AT(animTable,        0x0c9);
    KAROO_LAYOUT_AT(flBillboardScale, 0x249);
    KAROO_LAYOUT_AT(pParticleSystems, 0x24d);
    KAROO_LAYOUT_AT(dwInstanceCount,  0x39d);
    KAROO_LAYOUT_AT(dwMovableType,    0x3a1);
    KAROO_LAYOUT_AT(flPosX,           0x3a5);
    KAROO_LAYOUT_AT(flScaleX,         0x3b1);
    KAROO_LAYOUT_AT(flRotRateX,       0x3bd);
    KAROO_LAYOUT_AT(dwSubObjectCount, 0x3c9);
    KAROO_LAYOUT_AT(pSubObjects,      0x3cd);
    KAROO_LAYOUT_AT(bLit,             0x5ad);
    KAROO_LAYOUT_AT(bNoMoveStates,    0x5b1);
    KAROO_LAYOUT_AT(bNoZWrite,        0x5b5);
    KAROO_LAYOUT_AT(bNoShadow,        0x5b9);
    KAROO_LAYOUT_AT(bSpecular,        0x5bd);
    KAROO_LAYOUT_AT(bRandomYAngle,    0x5c1);
    KAROO_LAYOUT_AT(bOscillateRandom, 0x5c5);
    KAROO_LAYOUT_AT(flOscillationAmplitude, 0x5c9);
    KAROO_LAYOUT_AT(flOscillationFrequency, 0x5cd);
    KAROO_LAYOUT_AT(flOscillationPhase,     0x5d1);
    KAROO_LAYOUT_AT(flPump,           0x5d5);
    KAROO_LAYOUT_SIZE(0x5dd);
}

class __attribute__((packed)) ThemeObjectTypeSlot {
public:
    static const int ORIGIN = 0;

    DWORD            dwUnknownHeader0_prevSlotPumpOverflowTarget;
    DWORD            dwInstanceCount;
    ThemeLevelObject records[8];
private:
    KAROO_LAYOUT_REGISTER(ThemeObjectTypeSlot);
};

KAROO_LAYOUT_CHECKS(ThemeObjectTypeSlot)
{
    KAROO_LAYOUT_AT(dwInstanceCount, 0x4);
    KAROO_LAYOUT_AT(records,         0x8);
    KAROO_LAYOUT_SIZE(0x2ef0);
}

class __attribute__((packed)) SkyCube {
public:
    static const int ORIGIN = 0;

    BYTE          header[8];
    SceneTexture  faces[6];
private:
    KAROO_LAYOUT_REGISTER(SkyCube);
};

KAROO_LAYOUT_CHECKS(SkyCube)
{
    KAROO_LAYOUT_AT(faces, 0x8);
    KAROO_LAYOUT_SIZE(8 + 6 * sizeof(SceneTexture));
}

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

    char                 themeName[0x100];
    DWORD                dwUnknown100;
    ThemeObjectTypeSlot  slots[THEME_OBJ_COUNT];

    SceneTexture        *images[THEME_IMG_COUNT];
    ThemeTextColorPair   textColors[THEME_COLOR_COUNT];
    BYTE                 bFogEnabled;
    SkyCube              sky;
    unsigned char        gap_6fa4d[0x6fd8d - 0x6f99d - sizeof(SkyCube)];
    float                flSideHeight;
private:
    KAROO_LAYOUT_REGISTER(ThemeAssetBlock);
};

KAROO_LAYOUT_CHECKS(ThemeAssetBlock)
{
    KAROO_LAYOUT_AT(slots, 0x00104);
    KAROO_LAYOUT_AT(slots[THEME_OBJ_JOHN],      0x00104);
    KAROO_LAYOUT_AT(slots[THEME_OBJ_EXIT],      0x20554);
    KAROO_LAYOUT_AT(slots[THEME_OBJ_EXPLOSION], 0x43894);
    KAROO_LAYOUT_AT(slots[THEME_OBJ_BRIDGE],    0x6c9b4);

    KAROO_LAYOUT_AT(images, 0x6f8a4);
    KAROO_LAYOUT_AT(images[THEME_IMG_HUD],            0x6f8a4);
    KAROO_LAYOUT_AT(images[THEME_IMG_MENU],           0x6f8a8);
    KAROO_LAYOUT_AT(images[THEME_IMG_EDGE],           0x6f8ac);
    KAROO_LAYOUT_AT(images[THEME_IMG_RADAR],          0x6f8b0);
    KAROO_LAYOUT_AT(images[THEME_IMG_POINTER],        0x6f8b4);
    KAROO_LAYOUT_AT(images[THEME_IMG_FREEZE],         0x6f8b8);
    KAROO_LAYOUT_AT(images[THEME_IMG_INVERSECONTROL], 0x6f8bc);
    KAROO_LAYOUT_AT(images[THEME_IMG_PROTECTION],     0x6f8c0);
    KAROO_LAYOUT_AT(images[THEME_IMG_SLOWDOWN],       0x6f8c4);
    KAROO_LAYOUT_AT(images[THEME_IMG_SPEED],          0x6f8c8);

    KAROO_LAYOUT_AT(textColors, 0x6f8cc);
    KAROO_LAYOUT_AT(textColors[THEME_COLOR_HUD],            0x6f8cc);
    KAROO_LAYOUT_AT(textColors[THEME_COLOR_MENUNEWGAME],    0x6f8d4);
    KAROO_LAYOUT_AT(textColors[THEME_COLOR_MENULOADGAME],   0x6f8dc);
    KAROO_LAYOUT_AT(textColors[THEME_COLOR_MENUSUMMARYSAVE], 0x6f994);

    KAROO_LAYOUT_AT(bFogEnabled, 0x6f99c);
    KAROO_LAYOUT_AT(sky, 0x6f99d);
    KAROO_LAYOUT_AT(flSideHeight, 0x6fd8d);
    KAROO_LAYOUT_SIZE(0x6fd91);
}
