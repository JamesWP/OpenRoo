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

/* FaktMovie's vtable: ONE slot at 0x0045f200, holding the scalar deleting
   destructor 0x0044f3b0, which is that function's only reference anywhere
   (xref.py finds no CALL and no JMP).  patch.py redirects the slot at file
   offset 0x5F200.  The class is otherwise built and torn down only by the two
   static-initialiser thunks at 0x00425600 / 0x00425620 ("mov ecx,0x46c5d8;
   jmp"), for the single global FaktMovie at 0x0046c5d8 — E9 sites, so they
   are JMP_PATCHES rather than CALL_PATCHES. */
#define FAKTMOVIE_VTABLE ((void *)0x0045f200)

/* The single global FaktMovie. */
static FaktMovie *const GG_MOVIE = (FaktMovie *)0x0046c5d8;

/* The exports WinMain drives (movie.cpp). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Setup(FaktMovie *self, void *log_obj);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_LoadVideo(FaktMovie *self, void *arg1, void *arg2, void *arg3, const char *path);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Play(FaktMovie *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Movie_SetWindow(FaktMovie *self, void *surface);
/* The exports the WndProc drives. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Movie_Teardown(FaktMovie *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Notify(FaktMovie *self, DWORD a, DWORD b, DWORD c);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Pause(FaktMovie *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Movie_Stop(FaktMovie *self);
