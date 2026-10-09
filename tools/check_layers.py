#!/usr/bin/env python3
"""Checks that the src/ groups form the hierarchy declared in src/layers.txt.

Every quoted #include of a project header is a dependency of the including
group on the group that owns the header.  A dependency is allowed when the
owner is the same group or sits on a lower line of layers.txt.  Violations
that predate the check are listed in src/layers.baseline as
"<file> -> <header>"; the list may only shrink:

  * a violation not in the baseline fails the build (a new upward include);
  * a baseline entry that no longer occurs fails the build (delete the line,
    the ratchet moves down);
  * a group missing from layers.txt fails the build.

  check_layers.py <source dir>                  check
  check_layers.py <source dir> --update-baseline   rewrite the baseline
  check_layers.py <source dir> --report         print the group cycles too
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


def cycles(groups, edges):
    adj = {g: set() for g in groups}
    for _, g, _, o in edges:
        adj[g].add(o)
    index, low, stack, on, out = {}, {}, [], set(), []

    def visit(v):
        index[v] = low[v] = len(index)
        stack.append(v)
        on.add(v)
        for w in adj[v]:
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on.discard(w)
                comp.append(w)
                if w == v:
                    break
            if len(comp) > 1:
                out.append(sorted(comp))

    for g in groups:
        if g not in index:
            visit(g)
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    flags = {a for a in sys.argv[1:] if a.startswith('--')}
    root = args[0] if args else '.'
    src = os.path.join(root, 'src')
    rank = load_layers(os.path.join(src, 'layers.txt'))
    baseline_path = os.path.join(src, 'layers.baseline')
    groups, edges = scan(src)

    errors = []
    for g in groups:
        if g not in rank:
            errors.append('src/%s: group is not in src/layers.txt' % g)
    for g in rank:
        if g not in groups:
            errors.append('src/layers.txt: %s is not a group under src/' % g)
    if errors:
        print('\n'.join(errors))
        return 1

    violations = sorted({'%s -> %s' % (f, h) for f, g, h, o in edges if rank[o] >= rank[g]})
    if '--update-baseline' in flags:
        with open(baseline_path, 'w') as fh:
            fh.write('# Include violations of src/layers.txt that predate the check, as\n'
                     '# "<file> -> <header>".  Only ever delete lines; regenerate with\n'
                     '# tools/check_layers.py . --update-baseline after fixing some.\n')
            fh.writelines(v + '\n' for v in violations)
        print('wrote %d violations to %s' % (len(violations), baseline_path))
        return 0

    baseline = set()
    if os.path.exists(baseline_path):
        baseline = {l.strip() for l in open(baseline_path)
                    if l.strip() and not l.startswith('#')}
    new = [v for v in violations if v not in baseline]
    gone = sorted(baseline - set(violations))
    for v in new:
        f, h = v.split(' -> ')
        errors.append('src/%s includes %s: %s sits on the same or a higher layer '
                      '(src/layers.txt)' % (f, h, h.split('/')[0]))
    for v in gone:
        errors.append('src/layers.baseline: "%s" no longer occurs; delete the line' % v)
    if '--report' in flags:
        for c in cycles(groups, edges):
            print('cycle among groups:', ' '.join(c))
        print('%d baseline violations remain' % len(baseline & set(violations)))
    if errors:
        print('\n'.join(errors))
        return 1
    return 0


sys.exit(main())
