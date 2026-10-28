#include "soundevents.h"
#include "soundvoice.h"

void SoundEvents::attach(const SoundKey &key, SoundVoice *voice)
{
    voices_[key] = voice;
}

SoundVoice *SoundEvents::voice(const SoundKey &key) const
{
    auto it = voices_.find(key);
    return it == voices_.end() ? nullptr : it->second;
}

void SoundEvents::soundStart(const SoundKey &key, float x, float y, float z)
{
    if (SoundVoice *v = voice(key)) {
        v->setPosition(x, y, z, true);
        v->play(true);
    }
}

void SoundEvents::soundMove(const SoundKey &key, float x, float y, float z)
{
    if (SoundVoice *v = voice(key))
        v->setPosition(x, y, z, true);
}

void SoundEvents::soundStop(const SoundKey &key)
{
    if (SoundVoice *v = voice(key))
        v->stop();
}

void SoundEvents::entityDestroyed(SoundSource source, unsigned id)
{
    auto it = voices_.lower_bound({source, id, Sfx{}});
    while (it != voices_.end() && it->first.source == source && it->first.id == id) {
        if (library_->active() && it->second != nullptr)
            library_->releaseVoice(it->second, true);
        it = voices_.erase(it);
    }
}
