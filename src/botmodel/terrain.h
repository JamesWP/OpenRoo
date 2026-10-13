/* Terrain: the level's ground as the nav model sees it (nav.h), possibly
 * with some bombable blocks spent, and the quickest routes over it.
 *
 * A Terrain is a copy of the view's cells, so it stands while the cells it
 * was made from do.  spent() makes the Terrain of the same level after a bomb
 * has blown the given blocks: each is open ground at its height.  The planner
 * (docs/BOT_DESIGN.md, section 3.4) weighs such what-if terrains; the facts
 * that differ are which blocks are spent.
 *
 * The routes from a cell are found when first asked for and kept. */
#pragma once

#include <map>
#include <memory>
#include <vector>
#include "botview.h"
#include "nav.h"

namespace bot {

class Terrain {
public:
    explicit Terrain(const BotView &view);
    Terrain(const Terrain &) = delete;
    Terrain &operator=(const Terrain &) = delete;

    /* The same level with these blocks spent, as the blast spends them. */
    std::unique_ptr<Terrain> spent(const std::vector<Step> &blocks) const;

    const BotView  &view() const { return view_; }
    const NavModel &nav() const { return nav_; }

    /* The quickest routes from rest on (u, v) facing f. */
    const Paths &from(int u, int v, int facing) const;

    /* The blocks not yet spent, in grid order (v, then u). */
    const std::vector<Step> &blocks() const { return blocks_; }

private:
    BotView view_;
    NavModel nav_;
    std::vector<Step> blocks_;
    mutable std::map<int, std::unique_ptr<Paths>> from_;
};

}  // namespace bot
