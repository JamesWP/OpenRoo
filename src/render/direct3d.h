#pragma once
#define DIRECTDRAW_VERSION 0x0100
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>

/* LinkedList / LinkedListNode live in linkedlist.h, with the callbacks.
 * Direct3D uses one to hold the enumerated display modes. */
#include "linkedlist.h"

/* DisplayModeNode — 12 bytes, the pValue of each modeList node. */
struct DisplayModeNode {
    DWORD dwWidth, dwHeight, dwBitDepth;
};

/* Direct3D — the game's D3D wrapper object, reached through the global
 * g_pDirect3D @ 0x004e04ac.  Only the fields the replaced functions touch are
 * named; the rest is padding to the confirmed 568-byte size (Ghidra struct
 * "Direct3D", cross-checked against HOOKS.md § Direct3D struct layout). */
struct Direct3D {
    void                **vtable;      // +0x00
    IDirect3D3           *pD3D;        // +0x04
    IDirect3DViewport3   *pViewport;   // +0x08
    IDirect3DDevice3     *pDevice;     // +0x0c
    DWORD                 dwModeFilterFlags; // +0x10
    DWORD                 zbufFmt[8];  // +0x14..+0x30  DDPIXELFORMAT by value
    /* NOTE: these two names are backwards, and CreateD3DDevice is what proves
     * it — +0x34 receives the CreateSurface'd DDSCAPS_ZBUFFER surface and
     * +0x3c receives the GetAttachedSurface(DDSCAPS_BACKBUFFER) result.  That
     * is why FlipPrimaryFrame Blts to "pZBuffer" and why ReleaseResources
     * releases "pBackBuffer" but not "pZBuffer".  Kept as-is because shipped
     * code already reads against these names; see RENDER_PLAN.md 2026-09-03. */
    IDirectDrawSurface4  *pBackBuffer; // +0x34  really the Z-BUFFER
    IDirectDrawSurface4  *pPrimary;    // +0x38
    IDirectDrawSurface4  *pZBuffer;    // +0x3c  really the BACK BUFFER
    LinkedList            modeList;    // +0x40 DisplayModeNode* list (16 bytes)
    DisplayModeNode      *pSelectedMode; // +0x50
    char                  pLastError[100]; // +0x54
    IDirectDraw4         *pDD4;       // +0xb8
    HWND                  hWnd;       // +0xbc
    BYTE                  padc0[0x178];// +0xc0.. remainder of the 568-byte object
};

static_assert(offsetof(Direct3D, pDevice)     == 0x0c, "Direct3D layout mismatch");
static_assert(offsetof(Direct3D, pBackBuffer) == 0x34, "Direct3D layout mismatch");
static_assert(offsetof(Direct3D, pPrimary)    == 0x38, "Direct3D layout mismatch");
static_assert(offsetof(Direct3D, pZBuffer)      == 0x3c, "Direct3D layout mismatch");
static_assert(offsetof(Direct3D, modeList)      == 0x40, "Direct3D layout mismatch");
static_assert(offsetof(Direct3D, pSelectedMode) == 0x50, "Direct3D layout mismatch");
static_assert(offsetof(Direct3D, pDD4)          == 0xb8, "Direct3D layout mismatch");
static_assert(offsetof(Direct3D, hWnd)          == 0xbc, "Direct3D layout mismatch");
static_assert(sizeof(Direct3D)                == 568,  "Direct3D size mismatch");

/* LoadedImage — base (24-byte) variant.  FlipPrimaryFrame reads only
 * pTextureSurface; see HOOKS.md § LoadedImage struct — two sizes. */
struct LoadedImage {
    void                *unknown00;       // +0x00
    IDirectDrawSurface4 *pTextureSurface; // +0x04
    IDirectDrawSurface4 *pTexturePalette; // +0x08
    char                *ImageName;       // +0x0c
    int                  loadStatus;      // +0x10
    int                  loadedState;     // +0x14
};

static_assert(offsetof(LoadedImage, pTextureSurface) == 0x04, "LoadedImage layout mismatch");
static_assert(sizeof(LoadedImage) == 0x18, "LoadedImage base size mismatch");

#define g_pDirect3D (*(Direct3D **)0x004e04ac)

/* Exports of direct3d.cpp other files call (COHESION_PLAN.md template 10). */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Direct3D_ReleaseResources(Direct3D *self);
