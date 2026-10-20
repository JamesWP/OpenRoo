/* Terrain: the level's ground as the nav model sees it (nav.h), possibly
 * with some bombable blocks spent, and the quickest routes over it.
 *
 * A Terrain is a copy of the view's cells, so it stands while the cells it
 * was made from do.  changed() makes the Terrain of the same level after a
 * bomb has blown the given blocks (each is open ground at its height) and the
 * given bridges have been switched (an extended deck is void, a retracted
 * one ground).  The planner (docs/BOT_DESIGN.md, section 3.4) weighs such
 * what-if terrains; the facts that differ are which blocks are spent and
 * which bridges switched.
 *
 * The routes from a cell are found when first asked for and kept. */
#pragma once

#include <map>
#include <memory>
#include <vector>
#include "botview.h"
#include "glide.h"
#include "nav.h"

namespace bot {

class Terrain : public GlideSource {
public:
    /* With throughSwitches its routes pass over switches and the one-shot
     * falling tiles (nav.h), and with glides they glide (Paths): a bound. */
    explicit Terrain(const BotView &view, bool throughSwitches = false, bool glides = false);
    Terrain(const Terrain &) = delete;
    Terrain &operator=(const Terrain &) = delete;

    /* The same level with these blocks spent, as the blast spends them, and
     * these bridges (indices into view().bridges) switched, their moves
     * done. */
    std::unique_ptr<Terrain> changed(const std::vector<Step> &blocks,
                                     const std::vector<size_t> &bridges,
                                     bool throughSwitches = false, bool glides = false) const;

    const BotView  &view() const { return view_; }
    const NavModel &nav() const { return nav_; }

    /* The quickest routes from rest on (u, v) facing f. */
    const Paths &from(int u, int v, int facing) const;

    /* The landings of a glide launched from rest on (u, v) facing f (glide.h). */
    const std::vector<Landing> &glides(int u, int v, int facing) const override;

    /* The blocks not yet spent, in grid order (v, then u). */
    const std::vector<Step> &blocks() const { return blocks_; }

private:
    BotView view_;
    NavModel nav_;
    std::vector<Step> blocks_;
    bool throughSwitches_;
    bool glides_on_;
    mutable std::map<int, std::unique_ptr<Paths>> from_;
    mutable std::map<int, std::vector<Landing>> glides_;
};

}  // namespace bot
