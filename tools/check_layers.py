#!/usr/bin/env python3
"""Checks that the src/ groups form the hierarchy declared in src/layers.txt
and the direct dependencies declared in src/deps.txt.

Every quoted #include of a project header is a dependency of the including
group on the group that owns the header.  Such a dependency must be allowed:
the owner is the same group, or the owner sits on a lower line of layers.txt,
and the owner is one of the including group's deps in deps.txt.  Also an error:
a group missing from layers.txt, or listed there without a directory; a group
missing from deps.txt, or listed there without a directory; a declared dep that
is not on a lower line of layers.txt; and a declared dep that no include uses.

  check_layers.py <source dir>
"""
import os
import re
import sys

INCLUDE = re.compile(r'\s*#\s*include\s+"([^"]+)"')


def load_layers(path):
    rank = {}
    layer = 0
    for line in open(path):
        line = line.split('#', 1)[0].split()
        if not line:
            continue
        for g in line:
            rank[g] = layer
        layer += 1
    return rank


def load_deps(path):
    """Returns {group: [dep, ...]} and the lines that are not 'group: deps'."""
    deps = {}
    bad = []
    for n, line in enumerate(open(path), 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        if ':' not in line:
            bad.append('src/deps.txt:%d: expected "group: dep dep", got "%s"' % (n, line))
            continue
        g, rest = line.split(':', 1)
        g = g.strip()
        if g in deps:
            bad.append('src/deps.txt:%d: %s is listed twice' % (n, g))
            continue
        deps[g] = rest.split()
    return deps, bad


def scan(src):
    groups = sorted(g for g in os.listdir(src) if os.path.isdir(os.path.join(src, g)))
    owner = {}
    for g in groups:
        for f in os.listdir(os.path.join(src, g)):
            if f.endswith('.h'):
                owner[f] = g
    edges = []  # (file, group, header, owning group)
    for g in groups:
        for f in sorted(os.listdir(os.path.join(src, g))):
            if not f.endswith(('.cpp', '.h')):
                continue
            with open(os.path.join(src, g, f), errors='replace') as fh:
                for line in fh:
                    m = INCLUDE.match(line)
                    if not m:
                        continue
                    h = os.path.basename(m.group(1))
                    if h in owner and owner[h] != g:
                        edges.append((g + '/' + f, g, owner[h] + '/' + h, owner[h]))
    return groups, edges


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    src = os.path.join(root, 'src')
    rank = load_layers(os.path.join(src, 'layers.txt'))
    deps, errors = load_deps(os.path.join(src, 'deps.txt'))
    groups, edges = scan(src)

    membership = []
    for g in groups:
        if g not in rank:
            membership.append('src/%s: group is not in src/layers.txt' % g)
    for g in rank:
        if g not in groups:
            membership.append('src/layers.txt: %s is not a group under src/' % g)
    for g in groups:
        if g not in deps:
            errors.append('src/%s: group is not in src/deps.txt' % g)
    for g in deps:
        if g not in groups:
            errors.append('src/deps.txt: %s is not a group under src/' % g)
    for g, ds in sorted(deps.items()):
        for d in ds:
            if d not in rank:
                errors.append('src/deps.txt: %s declares %s, which is not a group '
                              'in src/layers.txt' % (g, d))
            elif g in rank and rank[d] >= rank[g]:
                errors.append('src/deps.txt: %s declares %s, which sits on the same '
                              'or a higher layer than %s (src/layers.txt)' % (g, d, g))
    errors = membership + errors
    if not membership:
        used = set()
        for f, g, h, o in edges:
            used.add((g, o))
            if rank[o] >= rank[g]:
                errors.append('src/%s includes %s: %s sits on the same or a higher '
                              'layer than %s (src/layers.txt)' % (f, h, o, g))
            elif o not in deps.get(g, []):
                errors.append("src/%s includes %s: %s is not in %s's deps (src/deps.txt)"
                              % (f, h, o, g))
        for g, ds in sorted(deps.items()):
            for d in ds:
                if (g, d) not in used and g in groups and d in groups:
                    errors.append('src/deps.txt: %s declares %s but includes nothing from it'
                                  % (g, d))
    if errors:
        print('\n'.join(errors))
        return 1
    return 0


sys.exit(main())
