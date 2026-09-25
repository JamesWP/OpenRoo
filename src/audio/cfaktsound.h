#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>

struct vec3d {
    float x, y, z;
};

/*
 * CFaktSound — DirectSound device manager, 120 bytes (0x78).
 * Always embedded inside SoundManager at SoundManager+0x14.
 * Never heap-allocated standalone; ScalarDeletingDtor is always called with free_memory=0.
 *
 * layout verified from HOOKS.md and Ghidra decompile
 */
#pragma pack(push, 1)
struct CFaktSound {
    void                   *vtable;                   // +0x00
    DWORD                   logger_initialized;        // +0x04  1 if we own the logger, 0 otherwise
    void                   *logger;                   // +0x08  opaque Logger*; never dereferenced here
    IDirectSound           *directsound;              // +0x0C
    IDirectSoundBuffer     *soundbuffer;              // +0x10  primary buffer
    DSCAPS                  caps_check;               // +0x14  96 bytes = 0x60
    IDirectSound3DListener *directsound3dlistener;    // +0x74
};
#pragma pack(pop)

static_assert(offsetof(CFaktSound, vtable)                == 0x00, "vtable offset");
static_assert(offsetof(CFaktSound, logger_initialized)     == 0x04, "logger_initialized offset");
static_assert(offsetof(CFaktSound, logger)                == 0x08, "logger offset");
static_assert(offsetof(CFaktSound, directsound)           == 0x0C, "directsound offset");
static_assert(offsetof(CFaktSound, soundbuffer)           == 0x10, "soundbuffer offset");
static_assert(offsetof(CFaktSound, caps_check)            == 0x14, "caps_check offset");
static_assert(offsetof(CFaktSound, directsound3dlistener) == 0x74, "directsound3dlistener offset");
static_assert(sizeof(CFaktSound)                          == 0x78, "CFaktSound size");
static_assert(sizeof(DSCAPS)                              == 0x60, "DSCAPS size");

/* Turn the 3D listener on or off.  Returns 0 on failure; SoundManager::setup
 * gives up when it does.  Declared here, by the owning header, rather than
 * redeclared at the call site (COHESION_PLAN template 10). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CFaktSound_Create3DListener(CFaktSound *self, int enable);

/* The lifecycle and startup the SoundManager's own lifecycle
 * (soundmanager.cpp) is written on. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_BlankFields(CFaktSound *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_ClearState(CFaktSound *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_ReleaseComRefs(CFaktSound *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CFaktSound_Initialize(CFaktSound *self, HWND window, UINT bufferflags,
                      short channels, int samplespersec,
                      USHORT bitspersample, void *logger);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CFaktSound_InitializeWith3DAudio(CFaktSound *self, HWND window,
                                 UINT bufferflags, short channels,
                                 int samplespersec, USHORT bitspersample,
                                 void *logger);

/* Original vtable at 0x45efa8 — slot 0: ScalarDeletingDtor @ 0x444fb0. */
static const void *const CFAKTSOUND_VTABLE = reinterpret_cast<const void*>(0x45efa8);

/* The 3D listener: position, orientation, then the deferred commit
 * (cfaktsound.cpp).  RenderGameFrame moves the listener with the camera. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_CommitSettings(CFaktSound *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_SetPosition(CFaktSound *self, vec3d *pos, DWORD dwApply);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_SetOrientation(CFaktSound *self, vec3d *front, vec3d *top, DWORD dwApply);
