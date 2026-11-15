#include "facts.h"
#include "moves.h"
#include "survey.h"
#include "tile.h"
#include "world.h"

namespace simbot {

Facts factsOf(const World &w, const Survey &survey)
{
    Facts f;
    f.at   = survey.graph().find(placeOf(w));
    f.gems = w.player()->gemsCollected();
    f.done = w.completed();
    for (size_t i = 0; i < survey.gems().size(); i++) {
        const Cell c = survey.gems()[i];
        if (w.map()->tile(c.u, c.v)->contents() != CONTENTS_CRYSTAL)
            f.taken.add(i);
    }
    for (size_t i = 0; i < survey.pickups().size(); i++) {
        const Cell c = survey.pickups()[i];
        if (w.map()->tile(c.u, c.v)->contents() != CONTENTS_GRANT_09)
            f.pickups.add(i);
    }
    f.bombs = w.player()->bombsCarried();
    for (size_t i = 0; i < survey.gliders().size(); i++) {
        const Cell c = survey.gliders()[i];
        if (w.map()->tile(c.u, c.v)->contents() != CONTENTS_PARAGLIDER)
            f.gliders.add(i);
    }
    f.glides = w.player()->glides();
    for (size_t i = 0; i < survey.foes().size(); i++)
        if (!foeDead(w, survey.foes()[i]))
            f.alive |= (uint64_t)1 << i;
    return f;
}

Requirement foeDead(const Survey &survey, unsigned id)
{
    const std::vector<unsigned> &foes = survey.foes();
    for (size_t i = 0; i < foes.size(); i++)
        if (foes[i] == id) {
            const uint64_t bit = (uint64_t)1 << i;
            return {"dead f" + std::to_string(i), [bit](const Facts &f) { return !(f.alive & bit); }};
        }
    return {};
}

}  // namespace simbot
