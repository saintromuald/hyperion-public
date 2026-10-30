"""The shaders' uniform blocks laid out as Direct3D packs them and as SPIR-V does (std140,
dxc's layout for -fspv-target-env=vulkan1.0): each field must land at the same offset in both,
since the C++ struct that fills a block follows Direct3D's; and no block may pass 4 KB, all of a
block SDL's Vulkan backend binds (MAX_UBO_SECTION_SIZE), past which a shader reads zeros.

    python tools/shader_layout.py [shader folder]

tools/build_engine_x64.py runs check() before it compiles the shaders. A three-component
vector after a scalar is what moves: Direct3D packs it beside the scalar, std140 starts it on a
16-byte boundary, and every field after it then reads past what the C++ pushes (as zeros under
Vulkan). Write it as a scalar and a two-component vector instead. A larger block is split into
blocks of its own, each pushed to its own slot, as hd.hlsl's base pass is.
"""
import glob
import os
import re
import sys


def blocks(text):
    """Each cbuffer's name and fields, a field as (name, components, array length or 0)."""
    for m in re.finditer(r"cbuffer\s+(\w+)[^{]*\{(.*?)\n\};", text, re.S):
        fields = []
        for line in m.group(2).split("\n"):
            line = re.sub(r"//.*", "", line).strip()
            if not line:
                continue
            f = re.match(r"(?:float|uint|int|bool)([234])?\s+(.+);", line)
            if f is None:
                raise ValueError("%s: cannot read %r" % (m.group(1), line))
            for name in f.group(2).split(","):
                a = re.match(r"(\w+)\[(\d+)\]", name.strip())
                fields.append((a.group(1) if a else name.strip(), int(f.group(1) or 1), int(a.group(2)) if a else 0))
        yield m.group(1), fields


def d3d_offsets(fields):
    """Direct3D's packing: a field does not cross a 16-byte row; an array starts a row and
    each element takes one."""
    out, at = [], 0
    for _, n, count in fields:
        if count or at // 16 != (at + n * 4 - 1) // 16:
            at = (at + 15) // 16 * 16
        out.append(at)
        at += 16 * (count - 1) + n * 4 if count else n * 4
    return out


def std140_offsets(fields):
    """std140: a scalar on 4 bytes, a two-component vector on 8, a three- or four-component
    one and each array element on 16."""
    out, at = [], 0
    for _, n, count in fields:
        align = 16 if count or n >= 3 else 4 * n
        at = (at + align - 1) // align * align
        out.append(at)
        at += 16 * count if count else n * 4
    return out


BLOCK_MAX = 4096


def std140_size(fields):
    if not fields:
        return 0
    _, n, count = fields[-1]
    return std140_offsets(fields)[-1] + (16 * count if count else n * 4)


def check(folder):
    """A line for each field whose offsets differ and each block past 4 KB, empty when every
    block is right."""
    problems = []
    for path in sorted(glob.glob(os.path.join(folder, "*.hlsl"))):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for block, fields in blocks(text):
            if std140_size(fields) > BLOCK_MAX:
                problems.append("%s, %s: %d bytes, past the %d SDL's Vulkan backend binds" %
                                (os.path.basename(path), block, std140_size(fields), BLOCK_MAX))
            for (name, _, _), a, b in zip(fields, d3d_offsets(fields), std140_offsets(fields)):
                if a != b:
                    problems.append("%s, %s.%s: at %d in Direct3D's layout, %d in SPIR-V's" %
                                    (os.path.basename(path), block, name, a, b))
    return problems


if __name__ == "__main__":
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src117", "port", "render", "shaders")
    found = check(folder)
    print("\n".join(found) if found else "every uniform block lays out the same in both and fits in 4 KB")
    sys.exit(1 if found else 0)
