#!/usr/bin/env python3
"""Lint src/ comments against the comment policy (OPEN_PLAN.md Part C).

    python3 tools/commentlint.py [FILES...]      exit 1 on a finding

Fails on what the policy says never goes in a comment: addresses in the
original executable, decompiler names, the reverse-engineering tools and
databases, plan references, dates, and history phrasing.  A `FORMAT:`
comment may cite byte offsets; nothing may cite an address.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctok import comments  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

RULES = [
    ("address", re.compile(r"\b0x0*4[0-6][0-9a-fA-F]{4}\b")),
    ("decompiler name", re.compile(r"\b(?:FUN|DAT|LAB|PTR|thunk_FUN)_[0-9a-fA-F]{6,}")),
    ("RE tooling", re.compile(r"\b(?:Ghidra|IDA|decompil\w*|disassembl\w*|listing|xref\.py|patch\.py|"
                              r"Karoo\.exe\.orig|UD2|hybrid|karoo_hooks\.dll|DllMain)\b", re.I)),
    ("plan reference", re.compile(r"\b\w+_PLAN\.md\b|\b(?:Band|Stage|Phase) \d|\bE\d{1,2}\b")),
    ("date", re.compile(r"\b20\d\d-\d\d-\d\d\b")),
    ("history", re.compile(r"\b(?:used to|no longer|now ours|formerly|previously|"
                           r"was renamed|is gone|reimplementation|reimplements?)\b", re.I)),
]


def lint(path):
    with open(path, errors="replace") as fh:
        src = fh.read()
    found = []
    for a, _, text in comments(src):
        line = src.count("\n", 0, a) + 1
        for name, rx in RULES:
            for m in rx.finditer(text):
                found.append((line + text.count("\n", 0, m.start()), name, m.group(0)))
    return found


def main(argv):
    files = argv or sorted(glob.glob(os.path.join(ROOT, "src", "*", "*.[ch]*")))
    total = 0
    for f in files:
        for line, name, what in lint(f):
            print("%s:%d: %s: %s" % (os.path.relpath(f, ROOT), line, name, what))
            total += 1
    print("%d finding(s) in %d file(s)" % (total, len(files)))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
