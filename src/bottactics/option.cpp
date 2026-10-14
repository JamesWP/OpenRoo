#include "option.h"

namespace bot {

std::vector<Timed> Option::ahead(double elapsedMs, Step here) const
{
    std::vector<Timed> rest{{0, here}};
    for (const Timed &m : schedule)
        if (m.t > elapsedMs)
            rest.push_back({m.t - elapsedMs, m.cell});
    return rest;
}

}  // namespace bot
