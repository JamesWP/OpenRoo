#include "goto.h"
#include "movegraph.h"
#include "world.h"

namespace simbot {

namespace {

const int MAX_LEAD = 600;
/* Waits in a row for a foe to pass, where nothing else moves: 2 s. */
const int FOE_WAITS = 8;
/* Timed waits in a row (schedule.h): two lifts' cycles. */
const int TIMED_WAITS = 4;

}  // namespace

Attempt::Status GoTo::act(const World &live, Work &work, unsigned *presses)
{
    if (!route_) {
        if (!atRest(live))
            return Thinking;
        if (placeOf(live).cell() == target_)
            return Done;
        World ahead(live);
        for (int i = 0; i < lead_; i++)
            tick(ahead, 0, &work);
        route_ = std::make_unique<Route>(ahead, target_, *graph_, 6000,
                                         waits_ && !ahead.foes().empty() ? FOE_WAITS : 0,
                                         waits_ ? TIMED_WAITS : 0);
        at_ = 0;
    }
    const Route::Status status = route_->run(work);
    if (status == Route::NoWay) {
        route_.reset();
        if (waits_)
            return Impossible;
        waits_ = true;
        return Thinking;
    }
    if (status == Route::Searching)
        return Thinking;
    const auto &p = route_->presses();
    const auto &e = route_->expects();
    if (at_ < e.size() && at_ == 0 && live.now() < e[0].now) {
        *presses = 0;   // standing till the game comes to where the Route starts
        return Pressing;
    }
    if (at_ >= p.size()) {
        route_.reset();
        return placeOf(live).cell() == target_ ? Done : Thinking;
    }
    if (expectOf(live) == e[at_]) {
        *presses = p[at_++];
        return Pressing;
    }
    if (at_ == 0 && lead_ < MAX_LEAD)
        lead_ *= 2;   // the search outran its lead
    route_.reset();
    return Thinking;
}

}  // namespace simbot
