/* SceneMaterial: the single Direct3D material used for the whole scene.
 * staticinit.cpp constructs the one global instance; renderstate.cpp creates
 * its Direct3D material once, at startup. */

#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>

struct SceneMaterial {
    void                *pVtable;
    IDirect3DMaterial3  *pMaterial;  // the created Direct3DMaterial3, null before creation
    D3DMATERIALHANDLE    hMaterial;
    D3DMATERIAL          mat;
    void                *pHeapData;  // heap block freed by SceneMaterial_Release
};

/* The one global instance; renderstate.cpp creates it against the device. */
extern SceneMaterial g_material;

static_assert(sizeof(D3DMATERIAL) == 80, "D3DMATERIAL size mismatch");
static_assert(offsetof(SceneMaterial, pMaterial) == 0x04, "SceneMaterial layout");
static_assert(offsetof(SceneMaterial, hMaterial) == 0x08, "SceneMaterial layout");
static_assert(offsetof(SceneMaterial, mat)       == 0x0c, "SceneMaterial layout");
static_assert(offsetof(SceneMaterial, pHeapData) == 0x5c, "SceneMaterial layout");
static_assert(sizeof(SceneMaterial) == 96, "SceneMaterial size mismatch");

static_assert(0x0c + offsetof(D3DMATERIAL, diffuse)    == 0x10, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, ambient)    == 0x20, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, specular)   == 0x30, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, power)      == 0x50, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, dwRampSize) == 0x58, "SceneMaterial ctor");

/* CreateMaterial + GetHandle for the current device; releases any prior
 * material first. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
SceneMaterial_Create(SceneMaterial *self, IDirect3D3 *pD3D,
                     IDirect3DDevice3 *pDevice);

/* Constructor and destructor body, run by staticinit.cpp. */
extern "C" __declspec(dllexport) SceneMaterial *__attribute__((thiscall)) SceneMaterial_Construct(SceneMaterial *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall)) SceneMaterial_DtorBody(SceneMaterial *self);
