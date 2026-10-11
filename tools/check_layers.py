#!/usr/bin/env python3
"""Checks the src/ groups' declared dependencies against their includes and
against the layering in src/layers.yaml.

Each group (a directory under src/) declares the groups it depends on with
target_link_libraries in its own src/<group>/CMakeLists.txt, under PUBLIC
(the dependency's headers are used by this group's headers, so every user of
this group needs them too) or PRIVATE (only its .cpp files use them).  CMake
turns those links into the include paths, so an undeclared include does not
compile; this checks the same things ahead of the compiler and gives the
reason.

Every quoted #include of a project header is an edge from the including group
to the group that owns the header.  Errors:
  - a group missing from src/layers.yaml, or listed there without a directory;
  - a link to a group that is not lower in src/layers.yaml;
  - an include of a group the including group does not link;
  - a group whose headers include a group it links PRIVATE;
  - a group linked PUBLIC that only its .cpp files include;
  - a group linked that nothing includes.
Names in target_link_libraries that are not groups (imgui, sdl3, ...) are
ignored.  Exits 1 if there are any errors.

  check_layers.py <source dir>
"""
import os
import re
import sys

INCLUDE = re.compile(r'\s*#\s*include\s+"([^"]+)"')
LINK = re.compile(r'target_link_libraries\(\s*(\w+)\s+(.*?)\)', re.S)
KEYWORDS = ('PUBLIC', 'PRIVATE', 'INTERFACE')


def load_layers(path):
    """Reads the rows of layers.yaml: `layers:` and then one `- [a, b]` per row,
    lowest first.  A subset of YAML, so that no YAML library is needed."""
    rank = {}
    layer = 0
    for line in open(path):
        m = re.match(r'\s*-\s*\[(.*)\]', line.split('#', 1)[0])
        if not m:
            continue
        for g in m.group(1).replace(',', ' ').split():
            rank[g] = layer
        layer += 1
    return rank


def load_links(src, groups):
    """Returns {group: {dep: 'PUBLIC' | 'PRIVATE' | 'INTERFACE'}} for the group
    names each CMakeLists.txt links; keywords with no group name are ignored."""
    links = {}
    for g in groups:
        path = os.path.join(src, g, 'CMakeLists.txt')
        links[g] = {}
        if not os.path.exists(path):
            continue
        text = ''.join(line.split('#', 1)[0] + '\n' for line in open(path, errors='replace'))
        for m in LINK.finditer(text):
            if m.group(1) != g:
                continue
            section = 'PRIVATE'
            for tok in m.group(2).split():
                if tok in KEYWORDS:
                    section = tok
                elif tok in groups and tok != g:
                    links[g][tok] = section
    return links


def scan(src):
    groups = sorted(g for g in os.listdir(src) if os.path.isdir(os.path.join(src, g)))
    owner = {}
    for g in groups:
        for f in os.listdir(os.path.join(src, g)):
            if f.endswith('.h'):
                owner[f] = g
    edges = []  # (file, group, header, owning group, is a header file)
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
                        edges.append((g + '/' + f, g, owner[h] + '/' + h, owner[h],
                                      f.endswith('.h')))
    return groups, edges


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    src = os.path.join(root, 'src')
    rank = load_layers(os.path.join(src, 'layers.yaml'))
    groups, edges = scan(src)
    links = load_links(src, groups)

    errors = []
    for g in groups:
        if g not in rank:
            errors.append('src/%s: group is not in src/layers.yaml' % g)
    for g in rank:
        if g not in groups:
            errors.append('src/layers.yaml: %s is not a group under src/' % g)

    # The declared links: each must point at a lower layer.
    for g in groups:
        for d in sorted(links[g], key=lambda d: (rank.get(d, 0), d)):
            if g in rank and d in rank and rank[d] >= rank[g]:
                errors.append('src/%s/CMakeLists.txt: %s links %s, which sits on the same '
                              'or a higher layer (src/layers.yaml)' % (g, g, d))

    # The includes: each must be linked, with the right visibility.
    header_deps = {g: set() for g in groups}
    cpp_deps = {g: set() for g in groups}
    for f, g, h, o, from_header in edges:
        if from_header:
            header_deps[g].add(o)
        else:
            cpp_deps[g].add(o)
        if o not in links[g]:
            errors.append('src/%s includes %s: %s does not link %s (src/%s/CMakeLists.txt)'
                          % (f, h, g, o, g))
        elif from_header and links[g][o] == 'PRIVATE':
            errors.append("src/%s/CMakeLists.txt: %s's headers include %s, so it must link "
                          "it PUBLIC" % (g, g, o))

    # The links: each must be used, and PUBLIC only when a header uses it.
    for g in groups:
        for d, section in sorted(links[g].items()):
            if d not in header_deps[g] and d not in cpp_deps[g]:
                errors.append('src/%s/CMakeLists.txt: %s links %s but includes nothing from it'
                              % (g, g, d))
            elif section == 'PUBLIC' and d not in header_deps[g]:
                errors.append('src/%s/CMakeLists.txt: %s links %s PUBLIC but only its .cpp '
                              'files include it; make it PRIVATE' % (g, g, d))

    errors = list(dict.fromkeys(errors))
    if errors:
        print('\n'.join(errors))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
