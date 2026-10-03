#define DIRECTSOUND_VERSION 0x0800
#define INITGUID
#include "dsound_internal.h"

namespace audiodev {

Buffer::Buffer() : state_(new BufferState())
{
}

Buffer::~Buffer()
{
    reset();
    delete state_;
}

void Buffer::reset()
{
    if (state_->threeD) {
        state_->threeD->Release();
        state_->threeD = NULL;
    }
    if (state_->soundbuffer) {
        state_->soundbuffer->Release();
        state_->soundbuffer = NULL;
    }
    state_->filename.clear();
}

bool Buffer::isLoaded() const        { return state_->soundbuffer != NULL; }
bool Buffer::is3D() const            { return state_->threeD != NULL; }
const char *Buffer::filename() const { return state_->filename.c_str(); }

static bool query_3d(BufferState *s)
{
    GUID iid3D = IID_IDirectSound3DBuffer;
    HRESULT hr = s->soundbuffer->QueryInterface(iid3D, (void**)&s->threeD);
    if (FAILED(hr)) {
        AD_LOG("audiodev: QueryInterface 3DBuffer failed hr=0x%lx\n",
               (unsigned long)hr);
        return false;
    }
    return true;
}

bool Buffer::load(Device &dev, const char *path, bool want3D)
{
    reset();

    if (!dev.isUp() || !path) {
        AD_LOG("audiodev: load: no device or path\n");
        return false;
    }

    state_->filename = path;

    Wav wav;
    if (!loadWav(path, &wav)) {
        AD_LOG("audiodev: load: can't parse '%s'\n", path);
        reset();
        return false;
    }

    DWORD flags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS
                | DSBCAPS_STATIC;
    if (want3D)
        flags |= DSBCAPS_CTRL3D;

    state_->soundbuffer = createWavBuffer(dev.state()->directsound, flags, wav);
    if (!state_->soundbuffer) {
        AD_LOG("audiodev: load: can't create a buffer for '%s'\n", path);
        reset();
        return false;
    }

    if (want3D && !query_3d(state_)) {
        reset();
        return false;
    }
    return true;
}

bool Buffer::duplicate(Device &dev, const Buffer &src)
{
    reset();

    if (!src.state_->soundbuffer) return false;

    HRESULT hr = dev.state()->directsound->DuplicateSoundBuffer(
                     src.state_->soundbuffer, &state_->soundbuffer);
    if (FAILED(hr)) {
        AD_LOG("audiodev: DuplicateSoundBuffer failed hr=0x%lx\n",
               (unsigned long)hr);
        state_->soundbuffer = NULL;
        return false;
    }

    state_->filename = src.state_->filename;
    if (src.state_->threeD && !query_3d(state_)) {
        reset();
        return false;
    }
    return true;
}

bool Buffer::reload3D(Device &dev, bool want3D)
{
    if (!state_->soundbuffer) return false;
    if (want3D == is3D()) return true;

    std::string path = state_->filename;
    reset();
    return load(dev, path.c_str(), want3D);
}

bool Buffer::set3DEnabled(bool enable)
{
    if (!state_->threeD) return false;
    // PRESERVED: the apply argument is 0, which is neither DS3D_IMMEDIATE nor
    // DS3D_DEFERRED.
    state_->threeD->SetMode(enable ? DS3DMODE_NORMAL : DS3DMODE_DISABLE, 0);
    return true;
}

void Buffer::setPosition(float x, float y, float z, bool immediate)
{
    if (state_->threeD)
        state_->threeD->SetPosition(x, y, z,
                                    immediate ? DS3D_IMMEDIATE : DS3D_DEFERRED);
}

/* Restores a lost buffer and refills it from the file. */
static bool restore(BufferState *s)
{
    HRESULT hr = s->soundbuffer->Restore();
    if (FAILED(hr)) {
        AD_LOG("audiodev: Restore failed hr=0x%lx\n", (unsigned long)hr);
        return false;
    }
    Wav wav;
    if (!loadWav(s->filename.c_str(), &wav))
        return false;
    bool ok = fillWavBuffer(s->soundbuffer, wav);
    return ok;
}

void Buffer::play(bool loop)
{
    if (!state_->soundbuffer) return;

    DWORD flags = loop ? DSBPLAY_LOOPING : 0;
    HRESULT hr = state_->soundbuffer->Play(0, 0, flags);
    // A lost buffer is restored, refilled and played again once.
    if (hr == DSERR_BUFFERLOST) {
        AD_LOG("audiodev: buffer lost, restoring '%s'\n",
               state_->filename.c_str());
        if (!restore(state_)) return;
        hr = state_->soundbuffer->Play(0, 0, flags);
    }
    if (FAILED(hr))
        AD_LOG("audiodev: Play failed hr=0x%lx\n", (unsigned long)hr);
}

void Buffer::stop()
{
    if (state_->soundbuffer)
        state_->soundbuffer->Stop();
}

}  // namespace audiodev
