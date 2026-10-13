#include "terrain.h"
#include "tile.h"

namespace bot {

Terrain::Terrain(const BotView &view) : view_(view), nav_(view_)
{
    for (int v = 0; v < view_.extentV; v++)
        for (int u = 0; u < view_.extentU; u++) {
            const Cell &c = view_.cell(u, v);
            if (c.kind == TILE_BOMBABLE && !c.busy)
                blocks_.push_back({u, v});
        }
}

std::unique_ptr<Terrain> Terrain::spent(const std::vector<Step> &blocks) const
{
    BotView next = view_;
    for (const Step &b : blocks)
        next.cells[(size_t)b.v * next.extentU + b.u].busy = true;
    return std::make_unique<Terrain>(next);
}

const Paths &Terrain::from(int u, int v, int facing) const
{
    const int key = ((v * view_.extentU) + u) * 4 + (facing - 1);
    std::unique_ptr<Paths> &p = from_[key];
    if (!p)
        p = std::make_unique<Paths>(nav_, u, v, facing);
    return *p;
}

}  // namespace bot
