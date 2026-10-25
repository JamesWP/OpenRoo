#include "route.h"
#include <algorithm>
#include <bitset>
#include <limits>
#include <unordered_map>

namespace bot {

namespace {

const double INF = std::numeric_limits<double>::infinity();
const int EXACT_MAX = 14;  // points, for the subset DP
const size_t KEPT_ROUTES = 256;

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
        const int pop = (int)std::bitset<64>(m).count();
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

/* A tour of k points, when one exists, for when the insertions find none
 * (distances that are not symmetric: a drop cannot be climbed back).  Each
 * next point is the nearest one from which the end, and as many more points
 * as are still to visit, can be reached; with every point to visit, all of
 * them must be. */
std::vector<int> feasible_tour(const std::vector<std::vector<double>> &dist, int n, int k)
{
    const int m = n + 2;
    std::vector<std::vector<char>> reach(m, std::vector<char>(m, 0));
    for (int i = 0; i < m; i++)
        for (int j = 0; j < m; j++)
            reach[i][j] = i == j || dist[i][j] >= 0;
    for (int w = 0; w < m; w++)
        for (int i = 0; i < m; i++)
            for (int j = 0; j < m; j++)
                reach[i][j] = reach[i][j] || (reach[i][w] && reach[w][j]);
    std::vector<int> tour;
    std::vector<char> done(m, 0);
    int at = 0;
    for (int added = 0; added < k; added++) {
        int best = -1;
        for (int c = 1; c <= n; c++) {
            if (done[c] || dist[at][c] < 0 || !reach[c][n + 1])
                continue;
            int ahead = 0;   // the points still to visit that c can reach, itself too
            bool all = true;
            for (int u = 1; u <= n; u++) {
                if (done[u])
                    continue;
                ahead += reach[c][u];
                all = all && reach[c][u];
            }
            if (k == n ? !all : ahead < k - added)
                continue;
            if (best < 0 || dist[at][c] < dist[at][best])
                best = c;
        }
        if (best < 0)
            return {};
        done[best] = 1;
        tour.push_back(best);
        at = best;
    }
    return tour;
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
            tour.clear();
            break;
        }
        tour.insert(tour.begin() + (long)bestAt, bestP);
        chosen[bestP] = 1;
    }
    if ((int)tour.size() < k || tour_cost(dist, tour, end) == INF) {
        tour = feasible_tour(dist, n, k);
        if (tour.empty()) {
            *cost = -1;
            return {};
        }
        std::fill(chosen.begin(), chosen.end(), 0);
        for (int p : tour)
            chosen[p] = 1;
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

std::vector<int> planTimedRoute(const std::vector<std::vector<double>> &dist, int n, int k,
                                double left, double bonusMs, double *cost)
{
    const int m = (int)dist.size() - 2, end = m + 1;
    *cost = -1;
    if (k > n || m > TIMED_MAX)
        return {};
    const size_t full = (size_t)1 << m, gemMask = ((size_t)1 << n) - 1;
    // dp: the earliest arrival at the last point, having visited the set.
    std::vector<double> dp(full * m, INF);
    std::vector<signed char> from(full * m, -1);
    for (int i = 0; i < m; i++)
        if (d_of(dist, 0, i + 1) <= left)
            dp[((size_t)1 << i) * m + i] = d_of(dist, 0, i + 1);
    double best = k == 0 && d_of(dist, 0, end) <= left ? d_of(dist, 0, end) : INF;
    size_t bestMask = 0;
    int bestLast = -1;
    for (size_t s = 1; s < full; s++) {
        const int gems    = (int)std::bitset<64>(s & gemMask).count();
        const int bonuses = (int)std::bitset<64>(s >> n).count();
        if (gems > k)
            continue;
        const double by = left + bonusMs * bonuses;   // the deadline, from here on
        for (int i = 0; i < m; i++) {
            const double c = dp[s * m + i];
            if (c == INF)
                continue;
            if (gems == k) {
                const double total = c + d_of(dist, i + 1, end);
                if (total <= by && total < best) {
                    best = total;
                    bestMask = s;
                    bestLast = i;
                }
            }
            for (int j = gems == k ? n : 0; j < m; j++) {
                if (s & ((size_t)1 << j))
                    continue;
                const size_t s2 = s | ((size_t)1 << j);
                const double c2 = c + d_of(dist, i + 1, j + 1);
                if (c2 <= by && c2 < dp[s2 * m + j]) {
                    dp[s2 * m + j]   = c2;
                    from[s2 * m + j] = (signed char)i;
                }
            }
        }
    }
    if (best == INF)
        return {};
    *cost = best;
    std::vector<int> order;
    for (size_t s = bestMask; bestLast >= 0;) {
        order.push_back(bestLast + 1);
        const int prev = from[s * m + bestLast];
        s &= ~((size_t)1 << bestLast);
        bestLast = prev;
    }
    std::reverse(order.begin(), order.end());
    return order;
}

std::vector<int> planRoute(const std::vector<std::vector<double>> &dist, int k, double *cost)
{
    const int n = (int)dist.size() - 2;
    if (k > n) {
        *cost = -1;
        return {};
    }
    // The same question comes again and again, decision after decision (the
    // board's routes are kept for one), and the answer is slow to find: the
    // answers are kept, up to KEPT_ROUTES of them.
    struct Answer {
        std::vector<std::vector<double>> dist;
        int k;
        std::vector<int> order;
        double cost;
    };
    static std::unordered_multimap<size_t, Answer> kept;
    size_t h = std::hash<int>()(k);
    for (const std::vector<double> &row : dist)
        for (const double d : row)
            h = h * 1099511628211ull ^ std::hash<double>()(d);
    const auto [first, last] = kept.equal_range(h);
    for (auto it = first; it != last; ++it)
        if (it->second.k == k && it->second.dist == dist) {
            *cost = it->second.cost;
            return it->second.order;
        }
    if (kept.size() >= KEPT_ROUTES)
        kept.clear();
    Answer a{dist, k, n <= EXACT_MAX ? exact(dist, n, k, cost) : heuristic(dist, n, k, cost), 0};
    a.cost = *cost;
    kept.emplace(h, a);
    return a.order;
}

}  // namespace bot
