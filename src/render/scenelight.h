#pragma once
#define DIRECTDRAW_VERSION 0x0100
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>

/* SceneSpotLight — the scene's spot light wrapper.  Layout from the Ghidra
 * struct (89 bytes), which tiles exactly: vtable, an inline D3DLIGHT2, the
 * COM pointer, and the "not currently in the viewport" flag. */
struct SceneSpotLight {
    void           *pVtable;      // +0x00
    D3DLIGHT2       light;        // +0x04 (80 bytes)
    IDirect3DLight *pLight;       // +0x54
    BYTE            bNotInScene;  // +0x58
};

static_assert(sizeof(D3DLIGHT2) == 80, "D3DLIGHT2 size mismatch");
static_assert(offsetof(SceneSpotLight, pLight)      == 0x54, "SceneSpotLight layout");
static_assert(offsetof(SceneSpotLight, bNotInScene) == 0x58, "SceneSpotLight layout");

/* The game's object is 89 bytes (Ghidra, alignment 1); C++ pads this
 * declaration's tail to 92 because of the pointer members.  That is harmless
 * — we never allocate one, only view the game's memory through it — so the
 * offsets above are the assertions that matter, and the size is checked only
 * as "the fields reach exactly 89 bytes before padding". */
static_assert(offsetof(SceneSpotLight, bNotInScene) + 1 == 89,
              "SceneSpotLight fields do not tile to the game's 89 bytes");

/* The fields the constructor (0x425380) writes, by their object offset. */
static_assert(4 + offsetof(D3DLIGHT2, dvRange)        == 0x34, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dvFalloff)      == 0x38, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dvAttenuation0) == 0x3c, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dvAttenuation2) == 0x44, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dwFlags)        == 0x50, "SceneSpotLight ctor");
