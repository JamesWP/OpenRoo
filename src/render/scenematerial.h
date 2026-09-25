#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>

/* SceneMaterial — the scene's material wrapper (96 bytes, Ghidra struct).
 *
 * NOTE: HOOKS.md § Scene setup functions has hMaterial and mat the wrong way
 * round ("D3DMATERIAL at this+0x08, handle at this+0x0C").  The handle is at
 * +0x08 and the 80-byte D3DMATERIAL at +0x0C, which is the only arrangement
 * that tiles into 96 bytes with the trailing heap pointer. */
struct SceneMaterial {
    void                *pVtable;    // +0x00
    IDirect3DMaterial3  *pMaterial;  // +0x04
    D3DMATERIALHANDLE    hMaterial;  // +0x08
    D3DMATERIAL          mat;        // +0x0c (80 bytes)
    void                *pHeapData;  // +0x5c  freed by FactAlloc::Free2
};

static_assert(sizeof(D3DMATERIAL) == 80, "D3DMATERIAL size mismatch");
static_assert(offsetof(SceneMaterial, pMaterial) == 0x04, "SceneMaterial layout");
static_assert(offsetof(SceneMaterial, hMaterial) == 0x08, "SceneMaterial layout");
static_assert(offsetof(SceneMaterial, mat)       == 0x0c, "SceneMaterial layout");
static_assert(offsetof(SceneMaterial, pHeapData) == 0x5c, "SceneMaterial layout");
static_assert(sizeof(SceneMaterial) == 96, "SceneMaterial size mismatch");

/* The fields the constructor (0x42d690) writes, by their object offset. */
static_assert(0x0c + offsetof(D3DMATERIAL, diffuse)    == 0x10, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, ambient)    == 0x20, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, specular)   == 0x30, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, power)      == 0x50, "SceneMaterial ctor");
static_assert(0x0c + offsetof(D3DMATERIAL, dwRampSize) == 0x58, "SceneMaterial ctor");
