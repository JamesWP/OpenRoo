#include "worlds.h"

namespace bot {

void Worlds::update(const BotView &view)
{
    const unsigned long long h = navHash(view);
    if (!worlds_.empty() && h == hash_)
        return;
    hash_ = h;
    facing_ = view.player.facing;
    worlds_.clear();
    worlds_[0] = std::make_unique<Terrain>(view);
}

size_t Worlds::blockCount() const
{
    const size_t n = base().blocks().size();
    return n < 64 ? n : 64;
}

const Terrain &Worlds::get(Spent s)
{
    auto it = worlds_.find(s);
    if (it != worlds_.end())
        return *it->second;
    std::vector<Step> blocks;
    for (size_t i = 0; i < blockCount(); i++)
        if (s & ((Spent)1 << i))
            blocks.push_back(block(i));
    return *(worlds_[s] = base().spent(blocks));
}

}  // namespace bot
