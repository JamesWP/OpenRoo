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
 

    void  *vtable;
    void  *log_obj;  // the logger given to Setup
    DWORD  state;  // 3 playing (Notify acts), 1 finished (the window procedure clears its flag)
    // The overlay colour key, set by WinMain for a player that would use it.
    DWORD  useColorKey;  // WinMain sets 1
    DWORD  colorKey[4];  // COLORKEY {CK_RGB, palette index 0, low 0, high 0}
    DWORD  notify_msg;   // the constructor's one non-zero value, 0xfd
};

 
extern void *const g_faktMovieVtable[1];
#define FAKTMOVIE_VTABLE ((void *)g_faktMovieVtable)

extern FaktMovie g_movie;
