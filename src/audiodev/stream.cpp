#define DIRECTSOUND_VERSION 0x0800
#include "dsound_internal.h"

namespace audiodev {

/* The whole file in one buffer, played once.  A watcher thread polls it and
 * marks the stream done when playback ends. */
struct StreamState {
    std::string          filename;
    IDirectSoundBuffer  *soundbuffer;
    volatile DWORD       done;  // 0 playing, 1 finished or idle
    HANDLE               watcher;
    HANDLE               stopEvent;
};

/* Polls the buffer every 50 ms and sets done once it stops playing or the
 * stop event is signalled. */
static DWORD WINAPI watcher_proc(LPVOID param)
{
    StreamState *s = static_cast<StreamState*>(param);
    for (;;) {
        DWORD ret = WaitForSingleObject(s->stopEvent, 50);
        if (ret == WAIT_OBJECT_0) break;
        if (!s->soundbuffer) { s->done = 1; break; }
        DWORD status = 0;
        HRESULT hr = s->soundbuffer->GetStatus(&status);
        if (FAILED(hr) || !(status & DSBSTATUS_PLAYING)) {
            s->done = 1;
            break;
        }
    }
    return 0;
}

Stream::Stream() : state_(new StreamState())
{
    state_->done = 1;
}

Stream::~Stream()
{
    release();
    delete state_;
}

bool Stream::done() const
{
    return state_->done != 0;
}

bool Stream::prepare(Device &dev, const char *path)
{
    AD_LOG("audiodev: Stream::prepare('%s')\n", path ? path : "<null>");

    release();

    if (!path || !dev.isUp())
        return false;

    state_->filename = path;

    Wav wav;
    if (!loadWav(path, &wav)) {
        release();
        return false;
    }

    state_->soundbuffer = createWavBuffer(
        dev.state()->directsound,
        DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS, wav);
    if (!state_->soundbuffer) {
        release();
        return false;
    }
    return true;
}

void Stream::play()
{
    if (!state_->soundbuffer) return;

    if (state_->watcher) stop();

    if (!state_->stopEvent)
        state_->stopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    else
        ResetEvent(state_->stopEvent);

    state_->soundbuffer->SetCurrentPosition(0);
    state_->done = 0;
    state_->soundbuffer->Play(0, 0, 0);

    DWORD tid;
    state_->watcher = CreateThread(NULL, 0, watcher_proc, state_, 0, &tid);
}

void Stream::stop()
{
    if (state_->watcher) {
        if (state_->soundbuffer) state_->soundbuffer->Stop();
        if (state_->stopEvent)   SetEvent(state_->stopEvent);
        WaitForSingleObject(state_->watcher, 5000);
        CloseHandle(state_->watcher);
        state_->watcher = NULL;
    }
    state_->done = 1;
}

void Stream::release()
{
    stop();

    if (state_->soundbuffer) {
        state_->soundbuffer->Release();
        state_->soundbuffer = NULL;
    }
    if (state_->stopEvent) {
        CloseHandle(state_->stopEvent);
        state_->stopEvent = NULL;
    }
    state_->filename.clear();
    state_->done = 1;
}

}  // namespace audiodev
