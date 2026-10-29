#include "soundevents.h"
#include "soundvoice.h"

SoundEvents::SoundEvents(SoundLibrary *library) : library_(library) {}

SoundEvents::~SoundEvents()
{
    clear();
}

bool SoundEvents::load(Sound &s)
{
    SoundVoice *v = library_->acquireVoice(s.name.c_str(), s.positional);
    if (v == nullptr)
        return false;
    s.voices.push_back({ v });
    return true;
}

void SoundEvents::release(Sound &s)
{
    for (Voice &v : s.voices) {
        v.voice->stop();
        library_->releaseVoice(v.voice, true);
    }
    s.voices.clear();
}

void SoundEvents::define(SoundSource source, Sfx sfx, unsigned char variant, const char *name,
                         bool positional, unsigned voices)
{
    undefine(source, sfx, variant);
    if (!library_->active())
        return;
    Sound &s = sounds_[{ source, sfx, variant }];
    s.name = name;
    s.positional = positional;
    for (unsigned i = 0; i < voices && load(s); i++) {
    }
}

void SoundEvents::undefine(SoundSource source, Sfx sfx, unsigned char variant)
{
    auto it = sounds_.find({ source, sfx, variant });
    if (it == sounds_.end())
        return;
    release(it->second);
    sounds_.erase(it);
}

void SoundEvents::clear()
{
    for (auto &entry : sounds_)
        release(entry.second);
    sounds_.clear();
}

SoundEvents::Sound *SoundEvents::sound(const SoundKey &key)
{
    auto it = sounds_.find({ key.source, key.sfx, key.variant });
    return it == sounds_.end() || it->second.voices.empty() ? nullptr : &it->second;
}

SoundEvents::Voice *SoundEvents::owned(const SoundKey &key)
{
    Sound *s = sound(key);
    if (s == nullptr)
        return nullptr;
    for (Voice &v : s->voices)
        if (v.owned && v.owner.id == key.id)
            return &v;
    return nullptr;
}

SoundEvents::Voice *SoundEvents::take(Sound &s, const SoundKey &key)
{
    for (Voice &v : s.voices)
        if (v.owned && v.owner.id == key.id)
            return &v;
    for (size_t n = 0; n < s.voices.size(); n++) {
        Voice &v = s.voices[(s.next + n) % s.voices.size()];
        if (v.held)
            continue;
        s.next = (s.next + n + 1) % s.voices.size();
        v.owner = key;
        v.owned = true;
        return &v;
    }
    if (!load(s))
        return nullptr;
    Voice &v = s.voices.back();
    v.owner = key;
    v.owned = true;
    return &v;
}

void SoundEvents::soundPlay(const SoundKey &key, float x, float y, float z)
{
    Sound *s = sound(key);
    if (Voice *v = s ? take(*s, key) : nullptr) {
        v->voice->setPosition(x, y, z, true);
        v->voice->play(false);
    }
}

void SoundEvents::soundPlay(const SoundKey &key)
{
    Sound *s = sound(key);
    if (Voice *v = s ? take(*s, key) : nullptr)
        v->voice->play(false);
}

void SoundEvents::soundStart(const SoundKey &key, float x, float y, float z)
{
    Sound *s = sound(key);
    if (Voice *v = s ? take(*s, key) : nullptr) {
        v->held = true;
        v->voice->setPosition(x, y, z, true);
        v->voice->play(true);
    }
}

void SoundEvents::soundMove(const SoundKey &key, float x, float y, float z)
{
    if (Voice *v = owned(key))
        v->voice->setPosition(x, y, z, true);
}

void SoundEvents::soundStop(const SoundKey &key)
{
    if (Voice *v = owned(key)) {
        v->voice->stop();
        v->owned = false;
        v->held = false;
    }
}
