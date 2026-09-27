#!/usr/bin/env python3
"""patch-clock.py IN OUT - ASV's clock driver (/boot/CLOCK) with the clock
chip's year counted from 1968, as TOS counts it (see README.md).

rtodc and wtodc count the chip's year byte from 1970 - a loop over the
years since 1970, with a leap-year test on 1970 + i. Starting both loops at
2 and testing 1968 + i makes the byte count from 1968 while the result
stays seconds since 1970. Six same-size edits; each is checked against the
original bytes first, so a different CLOCK is refused, not damaged."""
import sys

TEXT = 0x318            # file offset of .text in the 1991 CLOCK (6792 bytes)
EDITS = [
    (0x384, "4281", "7202", "rtodc: count from year 2: clrl d1 -> moveq #2,d1"),
    (0x39a, "07b2", "07b0", "rtodc: year loop's leap test: 1970 -> 1968"),
    (0x3e8, "07b2", "07b0", "rtodc: February's leap test: 1970 -> 1968"),
    (0x4a4, "4286", "7c02", "wtodc: count from year 2: clrl d6 -> moveq #2,d6"),
    (0x4b2, "07b2", "07b0", "wtodc: year loop's leap test: 1970 -> 1968"),
    (0x4f6, "07b2", "07b0", "wtodc: month loop's leap test: 1970 -> 1968"),
]

d = bytearray(open(sys.argv[1], "rb").read())
for addr, old, new, what in EDITS:
    o = TEXT + addr
    if d[o:o + 2] != bytes.fromhex(old):
        sys.exit("not the CLOCK this was made for: %s at 0x%x reads %s"
                 % (what, o, d[o:o + 2].hex()))
    d[o:o + 2] = bytes.fromhex(new)
    print("0x%x  %s" % (o, what))
open(sys.argv[2], "wb").write(d)
