"""Names the ranges in a snapprobe.txt (port_snapprobe.cpp, HYPERION_SNAPPROBE=a,b): the .data
ranges by the 1.17 global they fall in (re/globals117.json, the nearest at or below), the wide
block's by gen/wide117.h's table (x64 layout), each summed by global.
Usage: python tools/snapprobe.py <snapprobe.txt>"""
import bisect
import json
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RANGE = re.compile(r'^\s+0x([0-9A-F]+) (\d+)$', re.M)


def data(text):
    part = text[text.index('data:'):text.index('wide:')]
    ranges = [(int(m.group(1), 16), int(m.group(2))) for m in RANGE.finditer(part)]
    g = json.load(open(os.path.join(ENGINE, 're', 'globals117.json'), encoding='utf-8'))['globals']
    rows = sorted((int(va, 16), v) for va, v in g.items())
    vas = [r[0] for r in rows]
    info = dict(rows)
    by = {}
    for va, cb in ranges:
        at = va
        while at < va + cb:
            i = bisect.bisect_right(vas, at) - 1
            nxt = vas[i + 1] if i + 1 < len(vas) else 1 << 32
            take = min(va + cb, nxt) - at
            key = vas[i] if i >= 0 else 0
            by[key] = by.get(key, 0) + take
            at += take
    print('data:')
    print('%-10s %-44s %-22s %8s  %s' % ('va', 'global', 'tu', 'bytes', 'type'))
    for key in sorted(by):
        v = info.get(key, {})
        print('0x%06X   %-44s %-22s %8d  %s' % (key, v.get('name') or '(unnamed)', v.get('tu') or '', by[key],
                                              v.get('type') or ''))


def wide_rows():
    """(offset, size, va, name) for each global in g_wide117, from gen/wide117.h's x64 table."""
    src = open(os.path.join(ENGINE, 'src117', 'gen', 'wide117.h'), encoding='utf-8').read()
    x64 = src[src.index('#if defined(_WIN64)'):]
    x64 = x64[x64.index('kWideGlobals[]'):]
    x64 = x64[:x64.index('};')]
    rows = []
    for m in re.finditer(r'\{0x([0-9A-F]+)u, 0x[0-9A-F]+u, 0x([0-9A-F]+)u, 0x([0-9A-F]+)u,[^}]*\},\s*// (\S+)', x64):
        rows.append((int(m.group(2), 16), int(m.group(3), 16), int(m.group(1), 16), m.group(4)))
    return sorted(rows)


def wide(text):
    part = text[text.index('wide:'):]
    ranges = [(int(m.group(1), 16), int(m.group(2))) for m in RANGE.finditer(part)]
    rows = wide_rows()
    offs = [r[0] for r in rows]
    by = {}
    for off, cb in ranges:
        i = bisect.bisect_right(offs, off) - 1
        key = rows[i] if i >= 0 and off < rows[i][0] + rows[i][1] else (off, 0, 0, '(between)')
        by[key] = by.get(key, 0) + cb
    print('\nwide:')
    print('%-10s %-10s %-44s %8s' % ('offset', 'va', 'global', 'bytes'))
    for key in sorted(by):
        print('0x%07X  0x%06X   %-44s %8d' % (key[0], key[2], key[3], by[key]))


if __name__ == '__main__':
    t = open(sys.argv[1], encoding='utf-8').read()
    data(t)
    wide(t)
