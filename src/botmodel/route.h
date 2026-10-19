/* The route optimiser: which k of n points to visit, and in what order, on
 * the way from a start to an end, for the least total time.  The gem quota
 * is often below the gems on the map, so choosing which to skip matters as
 * much as the order (a k-of-n, prize-collecting tour).
 *
 * Exact over subsets for small n; beyond that a cheapest-insertion tour
 * improved by 2-opt, relocation and swapping a chosen point for an unchosen
 * one, until none of them helps. */
#pragma once

#include <vector>

namespace bot {

/* dist is (n + 2) square: index 0 the start, 1..n the points, n + 1 the end;
 * a negative entry is unreachable.  Returns the chosen points (1..n) in
 * visiting order, or empty with *cost < 0 if no route of k exists. */
std::vector<int> planRoute(const std::vector<std::vector<double>> &dist, int k,
                           double *cost);

/* The same against the clock, with time bonuses on the way: dist is
 * (n + b + 2) square, 1..n the points and n + 1..n + b the bonuses.  The
 * route visits k of the points and any of the bonuses, and comes to each of
 * them, and to the end, within left ms and bonusMs more for each bonus
 * visited before.  Returns the points and bonuses in visiting order, or
 * empty with *cost < 0 if there is no such route.  Exact, and only for
 * n + b up to TIMED_MAX: with more there is none. */
const int TIMED_MAX = 16;
std::vector<int> planTimedRoute(const std::vector<std::vector<double>> &dist, int n, int k,
                                double left, double bonusMs, double *cost);

}  // namespace bot
