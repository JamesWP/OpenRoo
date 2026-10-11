#!/usr/bin/env python3
"""Draws the src/ group hierarchy as an SVG (or Graphviz DOT).

The groups and their dependencies come from the same places the build and
tools/check_layers.py read them: each src/<group>/CMakeLists.txt's
target_link_libraries, and src/layers.txt for the order.  Every group sits on
a row by its depth (one above the deepest group it links), so the foundation
is at the bottom and app at the top, and groups that don't depend on each
other share a row.

By default only the links that aren't implied by others are drawn (the
transitive reduction): if core links sim and sim links game, core's own link
to game is left out.  --all draws every link.  PUBLIC links (the group's
headers use the dependency) are solid, PRIVATE ones (only its .cpp files do)
dashed.  Hovering a group or a link shows its details.

  group_graph.py [--all] [--dot] [-o FILE] [<source dir>]

Writes groups.svg (or groups.dot with --dot) unless -o says otherwise; -o -
writes to stdout.
"""
import argparse
import html
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_layers import load_layers, load_links, scan  # noqa: E402


def load(root):
    src = os.path.join(root, 'src')
    order = load_layers(os.path.join(src, 'layers.txt'))
    groups, edges = scan(src)
    groups = [g for g in groups if g in order]
    links = load_links(src, set(groups))
    includes = {}  # (group, dep) -> number of includes
    for _f, g, _h, d, _is_header in edges:
        includes[(g, d)] = includes.get((g, d), 0) + 1
    size = {}
    for g in groups:
        files = [f for f in os.listdir(os.path.join(src, g)) if f.endswith(('.cpp', '.h'))]
        lines = sum(sum(1 for _ in open(os.path.join(src, g, f), errors='replace')) for f in files)
        size[g] = (len(files), lines)
    return groups, order, links, includes, size


def depths(groups, links):
    depth = {}

    def visit(g):
        if g not in depth:
            depth[g] = 0
            depth[g] = 1 + max((visit(d) for d in links[g]), default=-1)
        return depth[g]
    for g in groups:
        visit(g)
    return depth


def reduction(groups, links):
    """The links not implied by a path through another link."""
    reach = {}

    def below(g):
        if g not in reach:
            reach[g] = set()
            for d in links[g]:
                reach[g] |= {d} | below(d)
        return reach[g]
    kept = {}
    for g in groups:
        implied = set()
        for d in links[g]:
            implied |= below(d)
        kept[g] = {d: k for d, k in links[g].items() if d not in implied}
    return kept


def layout(groups, order, depth, drawn):
    """Rows by depth; within a row, a few barycentre sweeps against the rows
    around it, starting from layers.txt order.  A link that skips rows gets a
    waypoint on each row it crosses, placed like a (narrow) node, so it is
    routed between the groups on those rows rather than through them.

    Returns the rows (groups and waypoints), each position, and each link's
    route: the waypoints between its two ends."""
    rows = {}
    for g in sorted(groups, key=lambda g: order[g]):
        rows.setdefault(depth[g], []).append(g)
    neighbours = {g: set() for g in groups}
    routes = {}
    for g in groups:
        for d in drawn[g]:
            chain = [g]
            for r in range(depth[g] - 1, depth[d], -1):
                w = ('via', g, d, r)
                rows.setdefault(r, []).append(w)
                neighbours[w] = set()
                chain.append(w)
            chain.append(d)
            for a, b in zip(chain, chain[1:]):
                neighbours[a].add(b)
                neighbours[b].add(a)
            routes[(g, d)] = chain[1:-1]
    pos = {}

    def place():
        for r in rows.values():
            for i, g in enumerate(r):
                pos[g] = i - (len(r) - 1) / 2.0
    place()
    top = max(rows)
    for sweep in range(12):
        for r in (range(top, -1, -1) if sweep % 2 == 0 else range(0, top + 1)):
            if r not in rows:
                continue
            def key(g):
                ns = [pos[n] for n in neighbours[g]]
                return sum(ns) / len(ns) if ns else pos[g]
            rows[r].sort(key=key)
            place()
    return rows, pos, routes


def svg(groups, links, drawn, includes, size, depth, rows, routes, show_all):
    char_w, node_h, row_h, gap = 7.4, 38, 96, 22
    width_of = {g: max(len(g) * 9.5, len('%d files' % size[g][0]) * char_w) + 26 for g in groups}
    for rs in rows.values():
        for w in rs:
            if isinstance(w, tuple):
                width_of[w] = 4  # a waypoint: just room for a line
    row_w = {r: sum(width_of[g] for g in rs) + gap * (len(rs) - 1) for r, rs in rows.items()}
    page_w = max(row_w.values()) + 80
    top = max(rows)
    page_h = (top + 1) * row_h + 70
    x, y = {}, {}
    for r, rs in rows.items():
        cur = (page_w - row_w[r]) / 2
        for g in rs:
            x[g] = cur + width_of[g] / 2
            y[g] = 40 + (top - r) * row_h + node_h / 2
            cur += width_of[g] + gap

    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
           'viewBox="0 0 %d %d" font-family="system-ui, sans-serif">' % (page_w, page_h, page_w, page_h),
           '<style>'
           '.bg{fill:#ffffff}.node rect{fill:#f3f5f8;stroke:#5b6b80;stroke-width:1.2}'
           '.node text{fill:#1d2733}.node .sub{fill:#66727f;font-size:10px}'
           '.pub{stroke:#38506b;fill:none;stroke-width:1.3}'
           '.priv{stroke:#8a99aa;fill:none;stroke-width:1.1;stroke-dasharray:5 4}'
           '.edge:hover path{stroke:#d0542b;stroke-width:2.4}'
           '.node:hover rect{stroke:#d0542b;stroke-width:2}'
           '.legend text{fill:#1d2733;font-size:11px}'
           '@media (prefers-color-scheme: dark){'
           '.bg{fill:#14181d}.node rect{fill:#222a33;stroke:#8fa3ba}.node text{fill:#e6ebf0}'
           '.node .sub{fill:#9aa6b2}.pub{stroke:#b4c4d6}.priv{stroke:#6d7c8c}'
           '.legend text{fill:#e6ebf0}}'
           '</style>',
           '<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
           'markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" '
           'fill="#7d8c9c"/></marker></defs>',
           '<rect class="bg" width="100%" height="100%"/>']

    for g in sorted(groups, key=lambda g: -depth[g]):
        for d, kind in sorted(drawn[g].items()):
            # Through each waypoint at its row's mid-height, one vertical-
            # tangent curve per row crossed.
            pts = [(x[g], y[g] + node_h / 2)]
            pts += [(x[w], y[w]) for w in routes[(g, d)]]
            pts.append((x[d], y[d] - node_h / 2 - 2))
            path = 'M%.1f,%.1f' % pts[0]
            for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                bend = (y2 - y1) * 0.5
                path += ' C%.1f,%.1f %.1f,%.1f %.1f,%.1f' % (x1, y1 + bend, x2, y2 - bend, x2, y2)
            tip = '%s → %s (%s, %d include%s)' % (
                g, d, kind, includes.get((g, d), 0), '' if includes.get((g, d), 0) == 1 else 's')
            out.append('<g class="edge"><title>%s</title><path class="%s" marker-end="url(#arrow)" '
                       'd="%s"/></g>' % (
                           html.escape(tip), 'pub' if kind in ('PUBLIC', 'INTERFACE') else 'priv', path))

    for g in groups:
        files, lines = size[g]
        pub = sorted(d for d, k in links[g].items() if k != 'PRIVATE')
        priv = sorted(d for d, k in links[g].items() if k == 'PRIVATE')
        tip = '%s: %d files, %d lines\nPUBLIC: %s\nPRIVATE: %s' % (
            g, files, lines, ' '.join(pub) or '-', ' '.join(priv) or '-')
        w = width_of[g]
        out.append('<g class="node"><title>%s</title>'
                   '<rect x="%.1f" y="%.1f" width="%.1f" height="%d" rx="6"/>'
                   '<text x="%.1f" y="%.1f" text-anchor="middle" font-size="13" font-weight="600">%s</text>'
                   '<text class="sub" x="%.1f" y="%.1f" text-anchor="middle">%d file%s</text></g>' % (
                       html.escape(tip), x[g] - w / 2, y[g] - node_h / 2, w, node_h,
                       x[g], y[g] - 2, html.escape(g), x[g], y[g] + 12, files, '' if files == 1 else 's'))

    ly = page_h - 22
    out.append('<g class="legend">'
               '<path class="pub" d="M24,%d h34" marker-end="url(#arrow)"/><text x="64" y="%d">PUBLIC</text>'
               '<path class="priv" d="M130,%d h34" marker-end="url(#arrow)"/><text x="170" y="%d">PRIVATE</text>'
               '<text x="250" y="%d">%s</text></g>' % (
                   ly, ly + 4, ly, ly + 4, ly + 4,
                   'every link' if show_all else 'links implied by others are hidden (--all shows them)'))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def dot(groups, drawn, depth):
    out = ['digraph groups {', '  rankdir=TB;', '  node [shape=box, style=rounded, fontname="sans-serif"];']
    for r in sorted(set(depth.values())):
        out.append('  { rank=same; %s }' % ' '.join('"%s";' % g for g in groups if depth[g] == r))
    for g in groups:
        for d, kind in sorted(drawn[g].items()):
            out.append('  "%s" -> "%s"%s;' % (g, d, '' if kind != 'PRIVATE' else ' [style=dashed]'))
    out.append('}')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('root', nargs='?', default='.', help='the repository root (default .)')
    ap.add_argument('--all', action='store_true', help='draw every link, not just the reduction')
    ap.add_argument('--dot', action='store_true', help='write Graphviz DOT instead of SVG')
    ap.add_argument('-o', '--output', help='output file (default groups.svg / groups.dot; - for stdout)')
    args = ap.parse_args()

    groups, order, links, includes, size = load(args.root)
    depth = depths(groups, links)
    drawn = links if args.all else reduction(groups, links)
    if args.dot:
        text = dot(groups, drawn, depth)
    else:
        rows, _pos, routes = layout(groups, order, depth, drawn)
        text = svg(groups, links, drawn, includes, size, depth, rows, routes, args.all)

    path = args.output or ('groups.dot' if args.dot else 'groups.svg')
    if path == '-':
        sys.stdout.write(text)
    else:
        with open(path, 'w') as f:
            f.write(text)
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
