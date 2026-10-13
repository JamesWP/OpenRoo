#include "distances.h"
#include "nav.h"
#include "tile.h"

namespace bot {

void Distances::update(const BotView &view)
{
    const unsigned long long t = navHash(view);
    if (done_ && t == terrain_)
        return;
    done_ = true;
    terrain_ = t;
    points_.clear();
    for (const Point::Kind kind : {Point::Gem, Point::Bombs})
        for (int v = 0; v < view.extentV; v++)
            for (int u = 0; u < view.extentU; u++) {
                const int c = view.cell(u, v).contents;
                if (c == (kind == Point::Gem ? CONTENTS_CRYSTAL : CONTENTS_GRANT_09))
                    points_.push_back({kind, {u, v}});
            }
    points_.push_back({Point::Exit, {view.exitU, view.exitV}});

    const NavModel nav(view);
    const size_t n = points_.size();
    between_.assign(n, std::vector<double>(n, -1));
    for (size_t i = 0; i < n; i++) {
        const Paths from(nav, points_[i].cell.u, points_[i].cell.v, view.player.facing);
        for (size_t j = 0; j < n; j++)
            between_[i][j] = i == j ? 0 : from.cost(points_[j].cell.u, points_[j].cell.v);
    }
}

int Distances::find(Step c) const
{
    for (size_t i = 0; i < points_.size(); i++)
        if (points_[i].cell.u == c.u && points_[i].cell.v == c.v)
            return (int)i;
    return -1;
}

}  // namespace bot
