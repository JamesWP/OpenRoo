/* SceneSpotLight: the scene's single spotlight, wrapping a Direct3D
 * IDirect3DLight and its D3DLIGHT2 description.  There is one instance,
 * g_light, constructed at startup and torn down at exit by staticinit.cpp;
 * renderstate.cpp creates and configures the underlying COM light once, at
 * startup.  The struct's layout is fixed by the static_asserts
 * below and must not be reordered. */

#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>
struct RenderDevice;

/* One spotlight: the D3DLIGHT2 description sent to Direct3D, the COM light
 * object it produced, and a flag set whenever the light is released. */
struct SceneSpotLight {
    void           *pVtable;      // +0x00
    D3DLIGHT2       light;        // +0x04 (80 bytes)
    IDirect3DLight *pLight;       // +0x54
    BYTE            bNotInScene;  // +0x58
};

/* Constructed by staticinit.cpp; configured by renderstate.cpp. */
extern SceneSpotLight g_light;

/* D3DLIGHT2 and pLight sit inline and by pointer respectively; the offsets
 * below are the layout contract. */
static_assert(sizeof(D3DLIGHT2) == 80, "D3DLIGHT2 size mismatch");
static_assert(offsetof(SceneSpotLight, pLight)      == 0x54, "SceneSpotLight layout");
static_assert(offsetof(SceneSpotLight, bNotInScene) == 0x58, "SceneSpotLight layout");

/* The struct's C++ size pads past 89 bytes because of the pointer members;
 * only the field offsets above are load-bearing. */
static_assert(offsetof(SceneSpotLight, bNotInScene) + 1 == 89,
              "SceneSpotLight fields do not tile to the game's 89 bytes");

/* The values the constructor writes into the light description, by field. */
static_assert(4 + offsetof(D3DLIGHT2, dvRange)        == 0x34, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dvFalloff)      == 0x38, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dvAttenuation0) == 0x3c, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dvAttenuation2) == 0x44, "SceneSpotLight ctor");
static_assert(4 + offsetof(D3DLIGHT2, dwFlags)        == 0x50, "SceneSpotLight ctor");

/* Releases any existing light, then creates a new one via
 * Direct3D::pD3D->CreateLight.  Returns true on success. */
extern "C" __declspec(dllexport) bool __attribute__((thiscall))
SceneLight_Create(SceneSpotLight *self, RenderDevice *d3d);

/* Constructor and destructor body for the one global instance. */
extern "C" __declspec(dllexport) SceneSpotLight *__attribute__((thiscall)) SceneLight_Construct(SceneSpotLight *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall)) SceneLight_DtorBody(SceneSpotLight *self);
