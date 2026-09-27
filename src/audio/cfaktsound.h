/* The DirectSound device: the device itself, its primary buffer (kept
 * playing), and the 3D listener that follows the camera.  One instance, a
 * sub-object of the sound manager. */
#pragma once
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include <stddef.h>

struct vec3d {
    float x, y, z;
};

/* The device state.  Embedded, never allocated on its own, so its deleting
 * destructor never frees. */
#pragma pack(push, 1)
struct CFaktSound {
    void                   *vtable;
    DWORD                   logger_initialized;  // always 0: the logger is never owned
    void                   *logger;              // the caller's logger; not used here
    IDirectSound           *directsound;
    IDirectSoundBuffer     *soundbuffer;            // the primary buffer
    DSCAPS                  caps_check;             // filled by Initialize; unread
    IDirectSound3DListener *directsound3dlistener;  // NULL when 3D sound is off
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

/* Turns the 3D listener on or off.  Returns 0 on failure, after releasing
 * everything; the sound manager gives up when it does. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CFaktSound_Create3DListener(CFaktSound *self, int enable);

/* Lifecycle and start-up.  Initialize creates the device at priority level and
 * the primary buffer in the given format, then starts it playing; returns 0 on
 * failure.  The 3D form adds the listener. */
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

/* The one-slot vtable: the deleting destructor. */
extern const void *const CFAKTSOUND_VTABLE;

/* The 3D listener: position, orientation and the deferred commit.  The
 * renderer moves the listener with the camera each frame. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_CommitSettings(CFaktSound *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_SetPosition(CFaktSound *self, vec3d *pos, DWORD dwApply);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_SetOrientation(CFaktSound *self, vec3d *front, vec3d *top, DWORD dwApply);

/* WinMain sets a rolloff of 0.3. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CFaktSound_Apply3DRolloffParams(CFaktSound *self, float rolloff_factor, DWORD dwApply);
