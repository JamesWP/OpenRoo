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
class __attribute__((packed)) CFaktSound {
public:
    /* Turns the 3D listener on or off.  Returns 0 on failure, after releasing
     * everything; the sound manager gives up when it does. */
    int create3DListener(int enable);

    /* Lifecycle and start-up.  Initialize creates the device at priority level
     * and the primary buffer in the given format, then starts it playing;
     * returns 0 on failure.  The 3D form adds the listener. */
    void blankFields();
    void clearState();
    void releaseComRefs();
    int  initialize(HWND window, UINT bufferflags, short channels,
                    int samplespersec, USHORT bitspersample, void *logger);
    int  initializeWith3DAudio(HWND window, UINT bufferflags, short channels,
                               int samplespersec, USHORT bitspersample,
                               void *logger);

    /* The 3D listener: position, orientation and the deferred commit.  The
     * renderer moves the listener with the camera each frame. */
    void commitSettings();
    void setPosition(vec3d *pos, DWORD dwApply);
    void setOrientation(vec3d *front, vec3d *top, DWORD dwApply);

    /* WinMain sets a rolloff of 0.3. */
    void apply3DRolloffParams(float rolloff_factor, DWORD dwApply);

    void                   *vtable() const { return vtable_; }
    void                   *logger() const { return logger_; }
    IDirectSound           *directsound() const { return directsound_; }
    IDirectSoundBuffer     *soundbuffer() const { return soundbuffer_; }

    /* Vtable slot 0.  Always embedded: the free flag is ignored. */
    static CFaktSound * 
    scalarDeletingDtor(CFaktSound *self, DWORD free_memory);

private:
    static void checkLayout();

    void                   *vtable_;
    DWORD                   logger_initialized_;  // always 0: the logger is never owned
    void                   *logger_;              // the caller's logger; not used here
    IDirectSound           *directsound_;
    IDirectSoundBuffer     *soundbuffer_;            // the primary buffer
    DSCAPS                  caps_check_;             // filled by Initialize; unread
    IDirectSound3DListener *directsound3dlistener_;  // NULL when 3D sound is off
};
#pragma pack(pop)

inline void CFaktSound::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(CFaktSound, vtable_)                == 0x00, "vtable offset");
    static_assert(offsetof(CFaktSound, logger_initialized_)     == 0x04, "logger_initialized offset");
    static_assert(offsetof(CFaktSound, logger_)                == 0x08, "logger offset");
    static_assert(offsetof(CFaktSound, directsound_)           == 0x0C, "directsound offset");
    static_assert(offsetof(CFaktSound, soundbuffer_)           == 0x10, "soundbuffer offset");
    static_assert(offsetof(CFaktSound, caps_check_)            == 0x14, "caps_check offset");
    static_assert(offsetof(CFaktSound, directsound3dlistener_) == 0x74, "directsound3dlistener offset");
#pragma GCC diagnostic pop
}

static_assert(sizeof(CFaktSound)                          == 0x78, "CFaktSound size");
static_assert(sizeof(DSCAPS)                              == 0x60, "DSCAPS size");

/* The one-slot vtable: the deleting destructor. */
extern const void *const CFAKTSOUND_VTABLE;

