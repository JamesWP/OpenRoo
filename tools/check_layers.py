#!/usr/bin/env python3
"""Checks that the src/ groups form the hierarchy declared in src/layers.txt.

Every quoted #include of a project header is a dependency of the including
group on the group that owns the header.  A dependency is allowed when the
owner is the same group or sits on a lower line of layers.txt; anything else
(an include of a group on the same line or above) is an error, as is a group
missing from layers.txt or listed there without a directory.

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
    groups, edges = scan(src)

    errors = []
    for g in groups:
        if g not in rank:
            errors.append('src/%s: group is not in src/layers.txt' % g)
    for g in rank:
        if g not in groups:
            errors.append('src/layers.txt: %s is not a group under src/' % g)
    if not errors:
        for f, g, h, o in edges:
            if rank[o] >= rank[g]:
                errors.append('src/%s includes %s: %s sits on the same or a higher '
                              'layer than %s (src/layers.txt)' % (f, h, o, g))
    if errors:
        print('\n'.join(errors))
        return 1
    return 0


sys.exit(main())
