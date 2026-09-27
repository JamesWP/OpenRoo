/* Direct3D: the game's Direct3D / DirectDraw wrapper. Owns the D3D and
 * DirectDraw device interfaces, the primary/back-buffer surfaces, the
 * enumerated display modes and the selected one. One instance exists, at
 * g_pDirect3D; device creation is in createdevice.h/.cpp, everything else
 * (present, teardown, lifecycle) in direct3d.h/.cpp. LoadedImage and
 * DisplayModeNode are the two payload types other code hands it. */

#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>

/* Direct3D keeps the enumerated display modes in a LinkedList of
 * DisplayModeNode. */
#include "linkedlist.h"

/* One DisplayModeNode per enumerated display mode. */
struct DisplayModeNode {
    DWORD dwWidth, dwHeight, dwBitDepth;
};

/* The game's Direct3D wrapper: the D3D and DirectDraw interfaces, the
 * primary/back-buffer/z-buffer surfaces, and the display mode list. Only the
 * fields read or written elsewhere are named; the rest is unused padding to
 * the object's fixed size. */
struct Direct3D {
    void                **vtable;             // +0x00
    IDirect3D3           *pD3D;               // +0x04
    IDirect3DViewport3   *pViewport;          // +0x08
    IDirect3DDevice3     *pDevice;            // +0x0c
    DWORD                 dwModeFilterFlags;  // +0x10
    DWORD                 zbufFmt[8];         // +0x14..+0x30  DDPIXELFORMAT by value
    // pBackBuffer and pZBuffer name the surfaces the wrong way round:
    // pBackBuffer holds the z-buffer surface and pZBuffer holds the back
    // buffer, as CreateD3DDevice fills them.
    IDirectDrawSurface4  *pBackBuffer;      // +0x34  really the z-buffer
    IDirectDrawSurface4  *pPrimary;         // +0x38
    IDirectDrawSurface4  *pZBuffer;         // +0x3c  really the back buffer
    LinkedList            modeList;         // +0x40  DisplayModeNode* list
    DisplayModeNode      *pSelectedMode;    // +0x50
    char                  pLastError[100];  // +0x54
    IDirectDraw4         *pDD4;             // +0xb8
    HWND                  hWnd;             // +0xbc
    BYTE                  padc0[0x178];     // +0xc0.. remainder of the object
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

/* LoadedImage: the smaller (24-byte) variant; only pTextureSurface is read
 * here. */
struct LoadedImage {
    void                *unknown00;        // +0x00
    IDirectDrawSurface4 *pTextureSurface;  // +0x04
    IDirectDrawSurface4 *pTexturePalette;  // +0x08
    char                *ImageName;        // +0x0c
    int                  loadStatus;       // +0x10
    int                  loadedState;      // +0x14
};

static_assert(offsetof(LoadedImage, pTextureSurface) == 0x04, "LoadedImage layout mismatch");
static_assert(sizeof(LoadedImage) == 0x18, "LoadedImage base size mismatch");

extern Direct3D* g_pDirect3D;

/* Presents img full-screen onto the primary surface. */
extern "C" __declspec(dllexport) void __cdecl
Direct3D_FlipPrimaryFrame(LoadedImage *img);

/* Exported so other translation units can call it directly. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Direct3D_ReleaseResources(Direct3D *self);

/* Construction, destruction and the scalar deleting destructor. */
extern "C" __declspec(dllexport) Direct3D *__attribute__((thiscall))
Direct3D_Construct(Direct3D *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Direct3D_Destruct(Direct3D *self);
extern "C" __declspec(dllexport) Direct3D *__attribute__((thiscall))
Direct3D_ScalarDestructor(Direct3D *self, unsigned char flags);

/* CreateD3DDevice lives in its own translation unit; its declaration comes
 * with this one. */
#include "createdevice.h"
