#pragma once
#include <windows.h>
#include <stddef.h>

/* FaktMovie object layout (from disassembly of 0x0044f430–0x00450420).
   Only the fields referenced by our hooks are named; the rest is padding. */
struct FaktMovie {
    void  *vtable;      // +0x000
    void  *log_obj;     // +0x004
    DWORD  field_0x8;   // +0x008
    BYTE   _pad[0x104]; // +0x00c..+0x10f  (the ctor's 0x41-dword REP STOSD)
    DWORD  state;       // +0x110  3=playing (Movie_Notify acts),
                        //         1=finished (WndProc zeros movie-active flag)
    DWORD  _tail[9];    // +0x114..+0x134
    DWORD  notify_msg;  // +0x138  ctor's one non-zero init: 0xfd

    void construct();   // 0x0044f3e0 — field init only, no vtable
    void destruct();    // 0x0044f3d0 — restore vtable, then teardown()

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
static_assert(offsetof(FaktMovie, notify_msg) == 0x138, "FaktMovie layout mismatch");

/* The class vtable, still the game's data at 0x0045f200; slot 0 (the scalar
   deleting destructor) is redirected to us by patch.py's VTABLE_PATCHES. */
#define FAKTMOVIE_VTABLE ((void *)0x0045f200)
