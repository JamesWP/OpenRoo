#pragma once
#include <windows.h>
#include <stddef.h>

/* The intro movie.  No video is played: loading always fails, so WinMain goes
 * straight to the game.  The object keeps the game's shape so the window
 * procedure's movie handling works unchanged.  One global instance. */
class FaktMovie {
public:
    FaktMovie *init();  // installs the vtable, then construct(); returns this
    void construct();   // fields only, not the vtable
    void destruct();    // restores the vtable, then teardown()
    /* The vtable slot: destruct, and free on bit 0 of flags. */
    static FaktMovie * 
    scalarDeletingDtor(FaktMovie *self, unsigned int flags);

    int  setup(void *log_obj_arg);  // always 1
    int  loadVideo(void *arg1, void *arg2, void *arg3, const char *path);
    void notify(DWORD a, DWORD b, DWORD c);
    void play();
    void pause();
    void stop();
    void setWindow(void *surface);
    void teardown();

    DWORD movieState() const { return state; }
    // WinMain sets the overlay colour key for a player that would use it.
    void setColorKey(DWORD type, DWORD low, DWORD high)
    {
        useColorKey = 1;
        colorKey[0] = type;
        colorKey[2] = low;
        colorKey[3] = high;
    }

private:
    static void checkLayout();

    void  *vtable;
    void  *log_obj;  // the logger given to Setup
    DWORD  field_0x8;
    BYTE   _pad[0x104];  // zeroed by the constructor
    DWORD  state;  // 3 playing (Notify acts), 1 finished (the window procedure clears its flag)
    DWORD  _tail[4];
    // The overlay colour key, set by WinMain for a player that would use it.
    DWORD  useColorKey;  // WinMain sets 1
    DWORD  colorKey[4];  // COLORKEY {CK_RGB, palette index 0, low 0, high 0}
    DWORD  notify_msg;   // the constructor's one non-zero value, 0xfd
};

inline void FaktMovie::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(FaktMovie, state) == 0x110, "FaktMovie layout mismatch");
    static_assert(offsetof(FaktMovie, useColorKey) == 0x124, "FaktMovie layout mismatch");
    static_assert(offsetof(FaktMovie, colorKey) == 0x128, "FaktMovie layout mismatch");
    static_assert(offsetof(FaktMovie, notify_msg) == 0x138, "FaktMovie layout mismatch");
#pragma GCC diagnostic pop
}

extern void *const g_faktMovieVtable[1];
#define FAKTMOVIE_VTABLE ((void *)g_faktMovieVtable)

extern FaktMovie g_movie;
