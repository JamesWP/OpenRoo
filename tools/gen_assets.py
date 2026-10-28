#!/usr/bin/env python3
"""gen_assets.py -- fill game/ with placeholder data made by this repository.

The counterpart of import_assets.py for when there is no copy of Ka'roo: every
file is generated here (tools/freeassets/), so the game runs from the source
tree alone.  The content is deliberately plain -- flat colours, boxes, beeps,
simple particle effects -- and holds one theme, Basic, with a handful of
levels.

    python3 tools/gen_assets.py                # fill game/
    python3 tools/gen_assets.py --out DIR      # somewhere else
    python3 tools/gen_assets.py --force        # replace a game/ that is a symlink

The game opens files by the names the original install used, and this writes
under those same names, so nothing in the game changes to read it.  game/
gets a marker file, .generated, and the tool never deletes a game/ that lacks
it (an import of the original data, say); a symlink is replaced, link only,
with --force.  Rerunning over its own output removes files it no longer makes.
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from freeassets import content  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARKER = ".generated"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=os.path.join(REPO, "game"))
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    out = args.out
    if os.path.islink(out):
        # A link to imported data (a worktree's, say): only the link goes.
        if not args.force:
            sys.exit("%s is a link to other data; --force to replace the link" % out)
        os.unlink(out)
    elif os.path.isdir(out) and os.listdir(out):
        if not os.path.exists(os.path.join(out, MARKER)):
            sys.exit("%s holds data this tool did not make; move it aside first" % out)
        shutil.rmtree(out)

    files = content.files()
    for path, data in sorted(files.items()):
        dst = os.path.join(out, *path.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(data)
    with open(os.path.join(out, MARKER), "w") as fh:
        fh.write("made by tools/gen_assets.py\n")
    print("%d files -> %s" % (len(files), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
