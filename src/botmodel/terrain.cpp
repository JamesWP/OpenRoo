#include "terrain.h"
#include "tile.h"

namespace bot {

Terrain::Terrain(const BotView &view, bool throughSwitches, bool glides)
    : view_(view), nav_(view_, throughSwitches), throughSwitches_(throughSwitches), glides_on_(glides)
{
    // A bridge on its way back is gone already: its deck goes from under
    // whoever is on it (one going out is there as far as it is stamped).
    for (const BridgeView &b : view_.bridges)
        if (b.moving && !b.extended)
            for (const Step &d : b.deck) {
                Cell &c = view_.cells[(size_t)d.v * view_.extentU + d.u];
                c.kind = TILE_EMPTY;
                c.height = 0;
                c.busy = false;
            }
    for (int v = 0; v < view_.extentV; v++)
        for (int u = 0; u < view_.extentU; u++) {
            const Cell &c = view_.cell(u, v);
            if (c.kind == TILE_BOMBABLE && !c.busy)
                blocks_.push_back({u, v});
        }
}

std::unique_ptr<Terrain> Terrain::changed(const std::vector<Step> &blocks,
                                          const std::vector<size_t> &bridges,
                                          bool throughSwitches, bool glides) const
{
    BotView next = view_;
    for (const Step &b : blocks)
        next.cells[(size_t)b.v * next.extentU + b.u].busy = true;
    for (const size_t i : bridges) {
        BridgeView &br = next.bridges[i];
        br.extended = !br.extended;
        br.moving = false;
        // As the bridge stamps and clears its deck (bridgeobject.cpp).
        for (const Step &d : br.deck) {
            Cell &c = next.cells[(size_t)d.v * next.extentU + d.u];
            c.kind   = br.extended ? TILE_BRIDGE_DECK : TILE_EMPTY;
            c.height = br.extended ? br.height : 0;
            c.busy   = br.extended;
        }
    }
    return std::make_unique<Terrain>(next, throughSwitches, glides);
}

const std::vector<Landing> &Terrain::glides(int u, int v, int facing) const
{
    const int key = ((v * view_.extentU) + u) * 4 + (facing - 1);
    auto it = glides_.find(key);
    if (it != glides_.end())
        return it->second;
    return glides_[key] = glideLandings(view_, u, v, facing);
}

const Paths &Terrain::from(int u, int v, int facing) const
{
    const int key = ((v * view_.extentU) + u) * 4 + (facing - 1);
    std::unique_ptr<Paths> &p = from_[key];
    if (!p)
        p = std::make_unique<Paths>(nav_, u, v, facing, 0, throughSwitches_,
                                    glides_on_ ? this : nullptr);
    return *p;
}

}  // namespace bot
