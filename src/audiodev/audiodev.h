/* The platform sound layer: the only code that may touch the system audio
 * APIs (SDL_mixer).  Everything here is opaque and
 * free of platform headers; the game's sound logic (src/audio) is written
 * against this file alone, so a port replaces the .cpp files beside it.
 *
 *   Device  the output device and, optionally, the 3D listener
 *   Buffer  one whole .wav file loaded for repeated playback, in 2D or 3D
 *   Stream  one .wav file played once, with a completion flag
 *   Music   one looping/one-shot background track
 *   free functions: master volume, one-off system sounds, logging */
#pragma once

#include <string>

namespace audiodev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

/* Turns the file names the game passes in into ones the system opens (the
 * game's own are not the host's).  The names given to the layer stay as they
 * are everywhere else, so they remain valid lookup keys.  May stay unset. */
typedef std::string (*PathFn)(const char *path);
void setPathResolver(PathFn fn);

/* Whether this run is silent: KAROO_HEADLESS is set.  Nothing then touches the
 * system audio: the device comes up, sounds load and play, but no SDL or
 * SDL_mixer call is made and nothing is heard.  Streams finish at once. */
bool silent();

/* The two volumes, each 0 (silent) to 1: the music, and every other sound.
 * They apply to this program's own output.  A fire-and-forget sound for the
 * launcher dialogs plays at the effects volume. */
void setEffectsVolume(float gain);
void setMusicVolume(float gain);
void playSystemSound(const char *path, bool async);

struct DeviceConfig {
    void *window;        // the native window handle that owns the sound
    bool  enable3D;      // create the 3D listener too
    int   channels;
    int   sampleRate;
    int   bitsPerSample;
};

struct DeviceState;  // the platform's half; defined in the .cpp files

class Device {
public:
    Device();
    ~Device();
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    /* Brings the device up (tearing down any earlier one).  False on failure,
     * with nothing left allocated. */
    bool create(const DeviceConfig &config);
    void destroy();
    bool isUp() const;

    /* Turns the 3D listener on or off.  False if the device is not up. */
    bool set3DEnabled(bool enable);

    /* The listener follows the camera: position, orientation and rolloff take
     * effect immediately or wait for commit(). */
    void setListenerPosition(const float pos[3], bool immediate);
    void setListenerOrientation(const float front[3], const float top[3],
                                bool immediate);
    void setListenerRolloff(float rolloff, bool immediate);
    void commit();

    DeviceState *state() const { return state_; }

private:
    DeviceState *state_;
};

struct BufferState;

/* One sound.  It remembers its file so it can be reloaded when the 2D/3D mode
 * switches. */
class Buffer {
public:
    Buffer();
    ~Buffer();
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;

    /* Loads a .wav file; a 3D buffer can be positioned.  False on failure. */
    bool load(Device &dev, const char *path, bool want3D);

    /* Becomes a second voice of src, sharing its sample data.  False if the
     * platform refuses, with this left empty. */
    bool duplicate(Device &dev, const Buffer &src);

    /* Reloads the buffer's own file if its 2D/3D mode differs from want3D. */
    bool reload3D(Device &dev, bool want3D);

    /* Releases the sound and forgets the file. */
    void reset();

    bool        isLoaded() const;
    bool        is3D() const;
    const char *filename() const;

    /* Switches a 3D buffer's spatialisation on or off; false if it is 2D. */
    bool set3DEnabled(bool enable);
    void setPosition(float x, float y, float z, bool immediate = true);

    /* Starts from the beginning, restoring the sound first if the platform
     * lost it.  Retriggering a playing buffer restarts it. */
    void play(bool loop);
    void stop();

private:
    BufferState *state_;
};

struct StreamState;

/* One .wav file played once.  Done is true when nothing is playing. */
class Stream {
public:
    Stream();
    ~Stream();
    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;

    /* Releases any earlier file, then loads path.  False on failure. */
    bool prepare(Device &dev, const char *path);
    void play();
    void stop();
    void release();
    bool done() const;

private:
    StreamState *state_;
};

struct MusicState;

/* Background music.  Tracks play in the background; a repeating track starts
 * again when it ends. */
class Music {
public:
    Music();
    ~Music();
    Music(const Music &) = delete;
    Music &operator=(const Music &) = delete;

    /* Unused: nothing needs the window any more. */
    void setWindow(void *window);

    /* Plays path, replacing whatever was playing. */
    void play(const char *path, bool repeat);
    void stop();

    /* Offered every window message; always false now that the track restarts
     * itself. */
    bool handleWindowMessage(unsigned msg, unsigned long wParam, long lParam);

private:
    MusicState *state_;
};

}  // namespace audiodev
