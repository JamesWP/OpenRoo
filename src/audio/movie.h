#pragma once
#include <windows.h>
#include <stddef.h>

/* FaktMovie object layout (from disassembly of 0x0044f430–0x00450420).
   Only the fields referenced by our hooks are named; the rest is padding. */
struct FaktMovie {
    void  *vtable;      // +0x000
    void  *log_obj;     // +0x004
    BYTE   _pad[0x108]; // +0x008..+0x10f
    DWORD  state;       // +0x110  3=playing (Movie_Notify acts),
                        //         1=finished (WndProc zeros movie-active flag)

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
