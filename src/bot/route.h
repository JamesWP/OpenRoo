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

}  // namespace bot
