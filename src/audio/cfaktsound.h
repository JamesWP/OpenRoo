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
struct CFaktSound {
    void                   *vtable;
    DWORD                   logger_initialized;  // always 0: the logger is never owned
    void                   *logger;              // the caller's logger; not used here
    IDirectSound           *directsound;
    IDirectSoundBuffer     *soundbuffer;            // the primary buffer
    DSCAPS                  caps_check;             // filled by Initialize; unread
    IDirectSound3DListener *directsound3dlistener;  // NULL when 3D sound is off
};

/* Turns the 3D listener on or off.  Returns 0 on failure, after releasing
 * everything; the sound manager gives up when it does. */
int CFaktSound_Create3DListener(CFaktSound *self, int enable);

/* Lifecycle and start-up.  Initialize creates the device at priority level and
 * the primary buffer in the given format, then starts it playing; returns 0 on
 * failure.  The 3D form adds the listener. */
void CFaktSound_BlankFields(CFaktSound *self);
void CFaktSound_ClearState(CFaktSound *self);
void CFaktSound_ReleaseComRefs(CFaktSound *self);
int CFaktSound_Initialize(CFaktSound *self, HWND window, UINT bufferflags,
                          short channels, int samplespersec,
                          USHORT bitspersample, void *logger);
int CFaktSound_InitializeWith3DAudio(CFaktSound *self, HWND window,
                                     UINT bufferflags, short channels,
                                     int samplespersec, USHORT bitspersample,
                                     void *logger);

/* The one-slot vtable: the deleting destructor. */
extern const void *const CFAKTSOUND_VTABLE;

/* The 3D listener: position, orientation and the deferred commit.  The
 * renderer moves the listener with the camera each frame. */
void CFaktSound_CommitSettings(CFaktSound *self);
void CFaktSound_SetPosition(CFaktSound *self, vec3d *pos, DWORD dwApply);
void
CFaktSound_SetOrientation(CFaktSound *self, vec3d *front, vec3d *top, DWORD dwApply);

/* WinMain sets a rolloff of 0.3. */
void
CFaktSound_Apply3DRolloffParams(CFaktSound *self, float rolloff_factor, DWORD dwApply);
