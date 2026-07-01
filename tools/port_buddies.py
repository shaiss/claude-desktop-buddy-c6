#!/usr/bin/env python3
"""One-shot port of upstream species files into src/characters/buddies/.

Mechanical include swap only (species draw exclusively via buddyPrint*
helpers; the TFT_eSprite extern was vestigial upstream). Fails loudly if a
file doesn't match the expected header shape so nothing gets silently mangled.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
SRC = HERE.parent / "upstream" / "src" / "buddies"
DST = HERE / "src" / "characters" / "buddies"

EXPECT = [
    '#include "../buddy.h"',
    '#include "../buddy_common.h"',
    "#include <M5StickCPlus.h>",
]
DROP = set(EXPECT[1:]) | {"extern TFT_eSprite spr;"}

DST.mkdir(parents=True, exist_ok=True)
count = 0
for f in sorted(SRC.glob("*.cpp")):
    lines = f.read_text(encoding="utf-8").splitlines(keepends=True)
    head = [l.strip() for l in lines[:8]]
    for e in EXPECT:
        if e not in head:
            sys.exit(f"{f.name}: expected header line missing: {e}")
    out = []
    for l in lines:
        s = l.strip()
        if s == EXPECT[0]:
            out.append(l.replace("../buddy.h", "../ascii_pets.h"))
        elif s in DROP:
            continue
        else:
            out.append(l)
    (DST / f.name).write_text("".join(out), encoding="utf-8", newline="\n")
    count += 1
    print(f"  {f.name} ok")
print(f"ported {count} species files")
