#include <windows.h>
#include <string.h>
#include "movie.h"
#include "log.h"
#include <stdlib.h>

/* FaktMovie::FaktMovie 0x0044f3e0 — field init, called by the ctor proper and
   (dead now) from the teardown path.  It zeroes +0x4 through +0x134 and sets
   +0x138 to 0xfd; the vtable at +0 is the caller's job.  The original leaves
   EAX zero on return, so the "constructor" hands back a NULL `this` — nothing
   reads it, and the two callers use their own register.  Bug preserved by
   returning void. */
void FaktMovie::construct()
{
    memset((char *)this + 4, 0, 0x134);
    notify_msg = 0xfd;
}

/* FaktMovie::~FaktMovie 0x0044f3d0 — restore the vtable, tail-jump to the COM
   teardown at 0x0044f430 (our teardown()). */
void FaktMovie::destruct()
{
    vtable = FAKTMOVIE_VTABLE;
    teardown();
}

void FaktMovie::teardown()
{
    log_write("FaktMovie::teardown(this=%p)\n", this);
}

void FaktMovie::setup(void *log_obj_arg)
{
    log_write("FaktMovie::setup(this=%p, log_obj=%p)\n", this, log_obj_arg);
}

int FaktMovie::loadVideo(void * /*arg1*/, void * /*arg2*/, void * /*arg3*/, const char *path)
{
    log_write("FaktMovie::loadVideo(this=%p, path=\"%s\")\n",
              this, path ? path : "(null)");
    return 0;
}

void FaktMovie::notify(DWORD a, DWORD b, DWORD c)
{
    log_write("FaktMovie::notify(this=%p, a=0x%lX, b=0x%lX, c=0x%lX) — state→1\n",
              this, a, b, c);
    state = 1;
}

void FaktMovie::play()
{
    HWND hwnd = *(HWND *)0x004dc698;  /* CdAudioGlobal(0x4dc640) + notify_hwnd(+0x58) */
    state = 3;
    log_write("FaktMovie::play(this=%p) — posting 0x464 to HWND %p\n", this, hwnd);
    PostMessageA(hwnd, 0x464, 0, 0);
}

void FaktMovie::pause()
{
    log_write("FaktMovie::pause(this=%p)\n", this);
}

void FaktMovie::stop()
{
    log_write("FaktMovie::stop(this=%p)\n", this);
}

void FaktMovie::setWindow(void *surface)
{
    log_write("FaktMovie::setWindow(this=%p, surface=%p)\n", this, surface);
}

/* ─── Exports — thin thiscall wrappers so patch.py import names resolve ─── */
extern "C" {

/* FaktMovie::FaktMovie 0x0044f390 — the constructor proper: install the
   vtable, then the field init.  Reached only through the static-initialiser
   thunk at 0x00425600, which builds the one global FaktMovie at 0x0046c5d8. */
__declspec(dllexport) FaktMovie * __attribute__((thiscall))
Movie_Construct(FaktMovie *self)
{
    self->vtable = FAKTMOVIE_VTABLE;
    self->construct();
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
Movie_Destruct(FaktMovie *self) { self->destruct(); }

/* FaktMovie::ScalarDeletingDtor 0x0044f3b0 — vtable slot 0.  The only
   FaktMovie is the static global, so the free branch never runs; it is kept
   faithful anyway, and the block would have come from the game heap. */
__declspec(dllexport) FaktMovie * __attribute__((thiscall))
Movie_ScalarDeletingDtor(FaktMovie *self, unsigned int flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
Movie_Teardown(FaktMovie *self) { self->teardown(); }

__declspec(dllexport) int __attribute__((thiscall))
Movie_Setup(FaktMovie *self, void *log_obj)
{
    self->setup(log_obj);
    return 1;
}

__declspec(dllexport) int __attribute__((thiscall))
Movie_LoadVideo(FaktMovie *self, void *arg1, void *arg2, void *arg3, const char *path)
{
    return self->loadVideo(arg1, arg2, arg3, path);
}

__declspec(dllexport) int __attribute__((thiscall))
Movie_Notify(FaktMovie *self, DWORD a, DWORD b, DWORD c)
{
    self->notify(a, b, c);
    return 0;
}

__declspec(dllexport) int __attribute__((thiscall))
Movie_Play(FaktMovie *self) { self->play(); return 0; }

__declspec(dllexport) int __attribute__((thiscall))
Movie_Pause(FaktMovie *self) { self->pause(); return 0; }

__declspec(dllexport) int __attribute__((thiscall))
Movie_Stop(FaktMovie *self) { self->stop(); return 0; }

__declspec(dllexport) void __attribute__((thiscall))
Movie_SetWindow(FaktMovie *self, void *surface) { self->setWindow(surface); }

} // extern "C"
