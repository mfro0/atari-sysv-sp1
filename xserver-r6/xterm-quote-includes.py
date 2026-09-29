#!/usr/bin/env python3
"""xterm-quote-includes.py DIR - include xterm's own headers with quotes.

XFree86's xterm writes #include <menu.h>, <main.h>, <data.h>... and the
cross compiler searches the system headers first, where SVR4 has a curses
<menu.h> (and more). Quoted, the directory of the source comes first."""
import glob, os, re, sys

d = sys.argv[1]
local = set(os.path.basename(h) for h in glob.glob(os.path.join(d, '*.h')))
n = 0
for f in glob.glob(os.path.join(d, '*.c')) + glob.glob(os.path.join(d, '*.h')):
    s = open(f, encoding='latin1').read()

    def fix(m):
        global n
        if m.group(2) not in local:
            return m.group(0)
        n += 1
        return '%s"%s"' % (m.group(1), m.group(2))
    t = re.sub(r'(#\s*include\s+)<([^>/]+)>', fix, s)
    if t != s:
        open(f, 'w', encoding='latin1').write(t)
print('%d includes quoted' % n)
