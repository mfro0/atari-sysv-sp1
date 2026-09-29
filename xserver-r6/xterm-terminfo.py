#!/usr/bin/env python3
"""xterm-terminfo.py TERMINFO OUT - the entries of XFree86's xterm terminfo
(xc/programs/xterm/terminfo) that the XFree86 xterm on ASV needs, for the
system's tic: xterm-xfree86 (alias xterm-new; the name this xterm gives
TERM), xterm-color and xterm-16color, xterm-vt220, and xterm-r6, which
xterm-color is built on. The system's own "xterm" entry is not replaced."""
import sys

WANT = ['xterm-xfree86', 'xterm-r6', 'xterm-color', 'xterm-16color', 'xterm-vt220']
entries, name, cur = {}, None, []
for line in open(sys.argv[1], encoding='latin1').read().split('\n'):
    if line and not line[0].isspace() and not line.startswith('#'):
        if name:
            entries[name] = cur
        name, cur = line.split('|')[0], [line]
    elif name and line.strip() and not line.startswith('#'):
        cur.append(line)
if name:
    entries[name] = cur
out = ['# From XFree86 3.3.6 (xc/programs/xterm/terminfo), for its xterm on',
       '# Atari System V; compile with tic. meml/memu are unknown to SVR4.0 tic',
       '# (two warnings) and are left out of the compiled entries.']
for w in WANT:
    out += entries[w] + ['']
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
