#include "route.h"
#include <algorithm>
#include <limits>

namespace bot {

namespace {

const double INF = std::numeric_limits<double>::infinity();
const int EXACT_MAX = 14;  // points, for the subset DP

double d_of(const std::vector<std::vector<double>> &dist, int a, int b)
{
    const double d = dist[a][b];
    return d < 0 ? INF : d;
}

/* The tour's cost: start, the points in order, end. */
double tour_cost(const std::vector<std::vector<double>> &dist, const std::vector<int> &t, int end)
{
    double c = 0;
    int at = 0;
    for (int p : t) {
        c += d_of(dist, at, p);
        at = p;
    }
    return c + d_of(dist, at, end);
}

std::vector<int> exact(const std::vector<std::vector<double>> &dist, int n, int k, double *cost)
{
    const int end = n + 1;
    const size_t full = (size_t)1 << n;
    std::vector<double> dp(full * n, INF);
    std::vector<signed char> from(full * n, -1);
    for (int i = 0; i < n; i++)
        dp[((size_t)1 << i) * n + i] = d_of(dist, 0, i + 1);
    double best = k == 0 ? d_of(dist, 0, end) : INF;
    size_t bestMask = 0;
    int bestLast = -1;
    for (size_t m = 1; m < full; m++) {
        const int pop = __builtin_popcountll(m);
        if (pop > k)
            continue;
        for (int i = 0; i < n; i++) {
            const double c = dp[m * n + i];
            if (c == INF)
                continue;
            if (pop == k) {
                const double total = c + d_of(dist, i + 1, end);
                if (total < best) {
                    best = total;
                    bestMask = m;
                    bestLast = i;
                }
                continue;
            }
            for (int j = 0; j < n; j++) {
                if (m & ((size_t)1 << j))
                    continue;
                const size_t m2 = m | ((size_t)1 << j);
                const double c2 = c + d_of(dist, i + 1, j + 1);
                if (c2 < dp[m2 * n + j]) {
                    dp[m2 * n + j]   = c2;
                    from[m2 * n + j] = (signed char)i;
                }
            }
        }
    }
    *cost = best == INF ? -1 : best;
    std::vector<int> order;
    if (best == INF)
        return order;
    for (size_t m = bestMask; bestLast >= 0;) {
        order.push_back(bestLast + 1);
        const int prev = from[m * n + bestLast];
        m &= ~((size_t)1 << bestLast);
        bestLast = prev;
    }
    std::reverse(order.begin(), order.end());
    return order;
}

/* The cheapest place to insert p into t, and what it adds. */
double best_insert(const std::vector<std::vector<double>> &dist, const std::vector<int> &t,
                   int end, int p, size_t *at)
{
    double best = INF;
    for (size_t i = 0; i <= t.size(); i++) {
        const int a = i == 0 ? 0 : t[i - 1];
        const int b = i == t.size() ? end : t[i];
        const double add = d_of(dist, a, p) + d_of(dist, p, b) - d_of(dist, a, b);
        if (add < best) {
            best = add;
            *at = i;
        }
    }
    return best;
}

std::vector<int> heuristic(const std::vector<std::vector<double>> &dist, int n, int k, double *cost)
{
    const int end = n + 1;
    std::vector<int> tour;
    std::vector<char> chosen(n + 2, 0);
    for (int added = 0; added < k; added++) {
        double best = INF;
        int bestP = -1;
        size_t bestAt = 0;
        for (int p = 1; p <= n; p++) {
            if (chosen[p])
                continue;
            size_t at;
            const double add = best_insert(dist, tour, end, p, &at);
            if (add < best) {
                best = add;
                bestP = p;
                bestAt = at;
            }
        }
        if (bestP < 0 || best == INF) {
            *cost = -1;
            return {};
        }
        tour.insert(tour.begin() + (long)bestAt, bestP);
        chosen[bestP] = 1;
    }

    double c = tour_cost(dist, tour, end);
    for (bool improved = true; improved;) {
        improved = false;
        // 2-opt: reverse a stretch (the distances need not be symmetric, so
        // each reversal is costed in full).
        for (size_t i = 0; i + 1 < tour.size(); i++)
            for (size_t j = i + 1; j < tour.size(); j++) {
                std::reverse(tour.begin() + (long)i, tour.begin() + (long)j + 1);
                const double c2 = tour_cost(dist, tour, end);
                if (c2 + 1e-9 < c) {
                    c = c2;
                    improved = true;
                } else {
                    std::reverse(tour.begin() + (long)i, tour.begin() + (long)j + 1);
                }
            }
        // Relocate one point to its best place.
        for (size_t i = 0; i < tour.size(); i++) {
            const int p = tour[i];
            std::vector<int> rest = tour;
            rest.erase(rest.begin() + (long)i);
            size_t at;
            best_insert(dist, rest, end, p, &at);
            rest.insert(rest.begin() + (long)at, p);
            const double c2 = tour_cost(dist, rest, end);
            if (c2 + 1e-9 < c) {
                tour = rest;
                c = c2;
                improved = true;
            }
        }
        // Swap a chosen point for an unchosen one, at its best place.
        for (size_t i = 0; i < tour.size(); i++)
            for (int q = 1; q <= n; q++) {
                if (chosen[q])
                    continue;
                std::vector<int> rest = tour;
                rest.erase(rest.begin() + (long)i);
                size_t at;
                best_insert(dist, rest, end, q, &at);
                rest.insert(rest.begin() + (long)at, q);
                const double c2 = tour_cost(dist, rest, end);
                if (c2 + 1e-9 < c) {
                    chosen[tour[i]] = 0;
                    chosen[q] = 1;
                    tour = rest;
                    c = c2;
                    improved = true;
                    break;
                }
            }
    }
    *cost = c == INF ? -1 : c;
    return c == INF ? std::vector<int>{} : tour;
}

}  // namespace

std::vector<int> planRoute(const std::vector<std::vector<double>> &dist, int k, double *cost)
{
    const int n = (int)dist.size() - 2;
    if (k > n) {
        *cost = -1;
        return {};
    }
    if (n <= EXACT_MAX)
        return exact(dist, n, k, cost);
    return heuristic(dist, n, k, cost);
}

}  // namespace bot
