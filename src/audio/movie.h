#pragma once
#include <windows.h>
#include <stddef.h>

/* The intro movie.  No video is played: loading always fails, so WinMain goes
 * straight to the game.  The object keeps the game's shape so the window
 * procedure's movie handling works unchanged.  One global instance. */
struct FaktMovie {
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

    void construct();  // fields only, not the vtable
    void destruct();   // restores the vtable, then teardown()

    void setup(void *log_obj_arg);
    int  loadVideo(void *arg1, void *arg2, void *arg3, const char *path);
    void notify(DWORD a, DWORD b, DWORD c);
    void play();
    void pause();
    void stop();
    void setWindow(void *surface);
    void teardown();
};

static_assert(offsetof(FaktMovie, state) == 0x110, "FaktMovie layout mismatch");
static_assert(offsetof(FaktMovie, useColorKey) == 0x124, "FaktMovie layout mismatch");
static_assert(offsetof(FaktMovie, colorKey) == 0x128, "FaktMovie layout mismatch");
static_assert(offsetof(FaktMovie, notify_msg) == 0x138, "FaktMovie layout mismatch");

/* The one-slot vtable: the deleting destructor. */
extern void *const g_faktMovieVtable[1];
#define FAKTMOVIE_VTABLE ((void *)g_faktMovieVtable)

extern FaktMovie g_movie;

/* Called by WinMain.  Loading returns 0: no movie. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Setup(FaktMovie *self, void *log_obj);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_LoadVideo(FaktMovie *self, void *arg1, void *arg2, void *arg3, const char *path);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Play(FaktMovie *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Movie_SetWindow(FaktMovie *self, void *surface);

/* Called by the window procedure. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Movie_Teardown(FaktMovie *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Notify(FaktMovie *self, DWORD a, DWORD b, DWORD c);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Pause(FaktMovie *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Stop(FaktMovie *self);

/* Construction and destruction of the global instance, driven by
 * staticinit.cpp. */
extern "C" __declspec(dllexport) FaktMovie *__attribute__((thiscall)) Movie_Construct(FaktMovie *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall)) Movie_Destruct(FaktMovie *self);
