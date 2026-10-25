#include "worlds.h"
#include <algorithm>
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
    const BotView &b = base().view();
    blocks_  = std::min<size_t>(base().blocks().size(), 63);
    bridges_ = std::min<size_t>(b.bridges.size(), 63 - blocks_);
    on_.assign(b.cells.size(), 0);
    for (size_t i = 0; i < blocks_; i++)
        on_[(size_t)block(i).v * b.extentU + block(i).u] |= (Changes)1 << i;
    for (size_t i = 0; i < bridges_; i++)
        for (const Step &d : b.bridges[i].deck)
            on_[(size_t)d.v * b.extentU + d.u] |= bridgeBit(i);
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
