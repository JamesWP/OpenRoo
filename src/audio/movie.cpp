#include <windows.h>
#include <string.h>
#include "movie.h"
#include "gameglobals.h"
#include "cdm.h"
#include "log.h"
#include <stdlib.h>
FaktMovie g_movie;

/* Zeroes everything after the vtable and sets the notify message.  PRESERVED:
 * the game's version returns NULL, which nobody reads; this one returns
 * nothing. */
void FaktMovie::construct()
{
    memset((char *)this + 4, 0, 0x134);
    notify_msg = 0xfd;
}

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

int FaktMovie::loadVideo(void * , void * , void * , const char *path)
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
    HWND hwnd = g_cdAudio.windowhandle;
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

extern "C" {

/* Installs the vtable, then the fields. */
__declspec(dllexport) FaktMovie * __attribute__((thiscall))
Movie_Construct(FaktMovie *self)
{
    self->vtable = FAKTMOVIE_VTABLE;
    self->construct();
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
Movie_Destruct(FaktMovie *self) { self->destruct(); }

/* Frees on bit 0; the one movie is a global, so it never does. */
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

}

void *const g_faktMovieVtable[1] = { (void *)&Movie_ScalarDeletingDtor };
