#include "soundevents.h"
#include "soundvoice.h"
#include "gameaudio.h"
#include "soundmanager.h"

SoundEvents::SoundEvents(GameAudio *audio) : audio_(audio), library_(audio->soundManager()) {}

void SoundEvents::attach(const SoundKey &key, SoundVoice *voice)
{
    voices_[key] = voice;
}

SoundVoice *SoundEvents::voice(const SoundKey &key) const
{
    auto it = voices_.find(key);
    return it == voices_.end() ? nullptr : it->second;
}

void SoundEvents::soundPlay(const SoundKey &key, float x, float y, float z)
{
    if (SoundVoice *v = voice(key)) {
        v->setPosition(x, y, z, true);
        v->play(false);
    }
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

void SoundEvents::acquire(const SoundKey &key, ThemeSound sound)
{
    const SoundAssetName *asset = audio_->themeSound(sound);
    if (asset->enabled != 0)
        voices_[key] = library_->acquireVoice(asset->name, true);
}

/* A bomb's sounds, acquired as it is dropped. */
void SoundEvents::entityCreated(SoundSource source, unsigned id)
{
    if (source != SoundSource::Bomb || !library_->active())
        return;
    acquire({source, id, Sfx::JumpPad}, ThemeSound::MoveJumpPad);
    if (audio_->themeSound(ThemeSound::Teleporter)->enabled != 0) {
        acquire({source, id, Sfx::Teleport}, ThemeSound::Teleporter);
        acquire({source, id, Sfx::Teleported}, ThemeSound::Teleporter);
    }
    acquire({source, id, Sfx::BombBlast}, ThemeSound::ExplosionBomb);
    acquire({source, id, Sfx::Fall}, ThemeSound::FallCatcher);
    acquire({source, id, Sfx::BombRoll}, ThemeSound::BombTick);
}

void SoundEvents::entityDestroyed(SoundSource source, unsigned id)
{
    auto it = voices_.lower_bound({source, id, Sfx{}});
    while (it != voices_.end() && it->first.source == source && it->first.id == id) {
        if (library_->active() && it->second != nullptr)
            // A bomb's sounds stay loaded for the next.
            library_->releaseVoice(it->second, source != SoundSource::Bomb);
        it = voices_.erase(it);
    }
}
