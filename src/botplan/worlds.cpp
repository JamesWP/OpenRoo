#include "worlds.h"
#include "tile.h"

namespace bot {

namespace {

/* Whether the player has, or could pick up, a glide. */
bool glidesToBe(const BotView &view)
{
    if (view.player.glides > 0)
        return true;
    for (const Cell &c : view.cells)
        if (c.contents == CONTENTS_PARAGLIDER || c.hidden == CONTENTS_PARAGLIDER)
            return true;
    return false;
}

}  // namespace

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
    return n < 63 ? n : 63;
}

size_t Worlds::bridgeCount() const
{
    const size_t n = base().view().bridges.size(), room = 63 - blockCount();
    return n < room ? n : room;
}

const Terrain &Worlds::get(Changes s)
{
    auto it = worlds_.find(s);
    if (it != worlds_.end())
        return *it->second;
    std::vector<Step> blocks;
    for (size_t i = 0; i < blockCount(); i++)
        if (s & ((Changes)1 << i))
            blocks.push_back(block(i));
    std::vector<size_t> bridges;
    for (size_t i = 0; i < bridgeCount(); i++)
        if (s & bridgeBit(i))
            bridges.push_back(i);
    const bool open = (s & OPEN) != 0;
    return *(worlds_[s] = base().changed(blocks, bridges, open, open && glidesToBe(base().view())));
}

}  // namespace bot
