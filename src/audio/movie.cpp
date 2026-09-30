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
FaktMovie::FaktMovie()
    : log_obj(NULL), state(0), useColorKey(0), notify_msg(0xfd)
{
    memset(colorKey, 0, sizeof(colorKey));
}

FaktMovie::~FaktMovie()
{
    teardown();
}

void FaktMovie::teardown()
{
    log_write("FaktMovie::teardown(this=%p)\n", this);
}

int FaktMovie::setup(void *log_obj_arg)
{
    log_write("FaktMovie::setup(this=%p, log_obj=%p)\n", this, log_obj_arg);
    return 1;
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
    HWND hwnd = g_cdAudio.windowHandle();
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

