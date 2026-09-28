#!/usr/bin/env python3
"""Check that every function declared in a src/ header is defined in
the .cpp of the same name (COHESION_PLAN.md, "a header's functions live in
its own .cpp").

Reads the compiled objects, not the source: for every external function
symbol defined in build/obj/<folder>/Y.o, find the header that owns it --

  * a method  Class::name   -> the header that DEFINES `class/struct Class {`
  * a free /   fn  -> the header(s) that DECLARE `name(`

-- and report it when that header is not Y.h.  Symbols defined in more than
one object are inline (header-defined) and skipped; so are functions no
header declares (file-local exports patch.py binds by name).

Usage:  python3 tools/check_homes.py [src dir] [obj dir]    exit 1 on a finding

Header and object stems are matched by name alone; the folders under src/
do not take part, so stems must stay unique across them.
"""
import os
import re
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'src')
OBJ = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'build', 'obj')


def walk(top, ext):
    """(stem, path) for every file under top ending in ext, sorted."""
    out = []
    for d, _, fs in os.walk(top):
        out += [(f[:-len(ext)], os.path.join(d, f)) for f in fs if f.endswith(ext)]
    return sorted(out)
OBJDUMP = 'i686-w64-mingw32-objdump'


def headers():
    out = {}
    for stem, path in walk(SRC, '.h'):
        if True:
            with open(path, errors='replace') as fp:
                text = fp.read()
            # drop comments so a mention in prose is not a declaration
            text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
            text = re.sub(r'//[^\n]*', ' ', text)
            out[stem] = text
    return out


def defined_symbols():
    """(demangled name) -> {object stems} for every external function that
    is defined OUT OF LINE: in plain .text.  Inline functions (a body in a
    class or a header) are emitted into COMDAT sections named .text$<sym>
    and are skipped -- their definition is the header, which is correct."""
    per = defaultdict(set)
    for stem, path in walk(OBJ, '.o'):
        hdr = subprocess.run([OBJDUMP, '-h', path], capture_output=True,
                             text=True, check=True).stdout
        names = {}
        for line in hdr.splitlines():
            m = re.match(r'\s*(\d+)\s+(\S+)\s', line)
            if m:
                names[int(m.group(1)) + 1] = m.group(2)   # -t is 1-based
        tab = subprocess.run([OBJDUMP, '-t', '-C', path], capture_output=True,
                             text=True, check=True).stdout
        for line in tab.splitlines():
            m = re.match(r'\[\s*\d+\]\(sec\s+(-?\d+)\)\(fl [^)]*\)\(ty\s+([0-9a-f]+)\)'
                         r'\(scl\s+(\d+)\) \(nx \d+\) 0x[0-9a-f]+ (.*)$', line)
            if not m:
                continue
            sec, ty, scl, name = int(m.group(1)), m.group(2), int(m.group(3)), m.group(4)
            if scl != 2 or ty != '20':          # external, function
                continue
            if names.get(sec) != '.text':        # .text$... = inline COMDAT
                continue
            per[name].add(stem)
    return per


def owner_of(sym, hdrs):
    """The header stems that own `sym`, or [] when no header does."""
    name = re.sub(r'\(.*$', '', sym)            # drop the argument list
    name = re.sub(r'@\d+$', '', name)           # stdcall decoration
    name = name.lstrip('_') if '::' not in name else name
    if '::' in name:
        cls, meth = name.rsplit('::', 1)
        cls = cls.split('::')[-1]
        if meth.startswith('~') or meth == cls or meth.startswith('operator'):
            pass
        pat = re.compile(r'\b(?:class|struct)\s+(?:__attribute__\(\([^)]*\)\)\s*)?'
                         + re.escape(cls) + r'\b[^;{]*\{')
        return [h for h, t in hdrs.items() if pat.search(t)]
    pat = re.compile(r'\b' + re.escape(name) + r'\s*\(')
    return [h for h, t in hdrs.items() if pat.search(t)]


def main():
    hdrs = headers()
    findings = []
    for sym, objs in sorted(defined_symbols().items()):
        if len(objs) != 1:
            continue                                # inline in a header
        obj = next(iter(objs))
        owners = owner_of(sym, hdrs)
        if not owners or obj in owners:
            continue
        findings.append((obj, sym, owners))
    for obj, sym, owners in findings:
        print(f'{obj}.cpp defines {sym}  -- declared in {", ".join(o + ".h" for o in owners)}')
    print(f'{len(findings)} function(s) defined outside their header\'s .cpp')
    return 1 if findings else 0


if __name__ == '__main__':
    sys.exit(main())
