"""Metal Shading Language for one shader, made from the SPIR-V dxc compiled.

SDL's GPU API on a Mac takes MSL, with its resources numbered its own way
(SDL_gpu.h, SDL_CreateGPUShader and SDL_CreateGPUComputePipeline): textures and samplers from 0,
sampled first; buffers from 0, uniform buffers first, then storage buffers, a compute shader's
read-only ones before its read-write ones. The SPIR-V numbers them as SDL's Vulkan path wants:
one descriptor set for the resources and one for the uniforms, each resource's binding its
place in that set. SPIRV-Cross (tools/fetch_spirv_cross.py) writes the MSL, with
--msl-decoration-binding taking each index from the SPIR-V's binding, so the bindings are set
first to what SDL's Metal path expects.

Where a resource's index depends on how many of another kind the pipeline declares (a storage
buffer's comes after every uniform buffer the C++ declares, used or not, which the SPIR-V cannot
show, as dxc drops unused resources), the MSL names it by those counts: HX_SAMPLERS,
HX_STORAGE_TEXTURES, HX_UNIFORMS and, for compute, HX_RO_TEXTURES, HX_RO_BUFFERS and
HX_RW_TEXTURES, which port_gpu.cpp defines ahead of the source from the counts it gives SDL.

    python tools/shader_msl.py <entry.spv.h> <stage> <entry> <out.msl.h> <name>

<stage> is vertex, fragment or compute. The input is dxc's -Fh header; the output defines
<name> as the MSL source, a byte array ending in 0.
"""

import os
import re
import struct
import subprocess
import sys
import tempfile

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPIRV_CROSS_TAG = "vulkan-sdk-1.4.363.0"
SPIRV_CROSS = os.path.join(ENGINE, "third_party", "SPIRV-Cross-" + SPIRV_CROSS_TAG, "bin", "spirv-cross.exe")
MSL_VERSION = "20100"  # MSL 2.1; macOS 13, the Mac build's lowest, has 3.0

# SPIR-V's numbers
OP_DECORATE, OP_MEMBER_DECORATE, OP_VARIABLE = 71, 72, 59
OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE = 25, 26, 27
OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER = 28, 29, 30, 32
DEC_BLOCK, DEC_BUFFER_BLOCK, DEC_NON_WRITABLE, DEC_BINDING, DEC_SET = 2, 3, 24, 33, 34
SC_UNIFORM_CONSTANT, SC_UNIFORM, SC_STORAGE_BUFFER = 0, 2, 12

# where SDL's Vulkan path keeps each stage's resources and uniforms
SETS = {"vertex": (0, 1), "fragment": (2, 3)}
# the placeholder a binding takes when its index has to be written as an expression
PLACEHOLDER = 7000


def read_header(path):
    """The SPIR-V words in dxc's -Fh header (the bytes of its array, after the disassembly)."""
    text = open(path, encoding="utf-8").read()
    body = text[text.index("[] = {"):]
    data = bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", body))
    if len(data) % 4 or struct.unpack_from("<I", data)[0] != 0x07230203:
        sys.exit("[shader_msl] %s: not SPIR-V" % path)
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


def instructions(words):
    i = 5
    while i < len(words):
        count, op = words[i] >> 16, words[i] & 0xFFFF
        yield i, op, words[i + 1:i + count]
        i += count


def resources(words):
    """Each descriptor of the module: its variable, set, binding, the word holding the binding,
    and its kind (sampled, storage-texture, uniform, storage-buffer) with read-only for the last
    two kinds of storage."""
    types, decorations, members, variables, binding_at = {}, {}, {}, [], {}
    for at, op, args in instructions(words):
        if op in (OP_TYPE_IMAGE, OP_TYPE_SAMPLER, OP_TYPE_SAMPLED_IMAGE, OP_TYPE_STRUCT, OP_TYPE_POINTER,
                  OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY):
            types[args[0]] = (op, args[1:])
        elif op == OP_DECORATE:
            decorations.setdefault(args[0], {})[args[1]] = args[2:]
            if args[1] == DEC_BINDING:
                binding_at[args[0]] = at + 3
        elif op == OP_MEMBER_DECORATE:
            members.setdefault(args[0], set()).add(args[2])
        elif op == OP_VARIABLE:
            variables.append((args[1], args[0], args[2]))
    found = []
    for var, ptr, storage in variables:
        dec = decorations.get(var, {})
        if DEC_SET not in dec or DEC_BINDING not in dec:
            continue
        op, targs = types[ptr]
        tid = targs[1]
        while types[tid][0] in (OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY):
            tid = types[tid][1][0]
        top, tval = types[tid]
        readonly = DEC_NON_WRITABLE in dec
        if top == OP_TYPE_SAMPLED_IMAGE or top == OP_TYPE_SAMPLER:
            kind = "sampled"
        elif top == OP_TYPE_IMAGE:
            kind = "sampled" if tval[5] == 1 else "storage-texture"
        elif top == OP_TYPE_STRUCT:
            sdec = decorations.get(tid, {})
            if storage == SC_UNIFORM and DEC_BLOCK in sdec:
                kind = "uniform"
            elif (storage == SC_UNIFORM and DEC_BUFFER_BLOCK in sdec) or storage == SC_STORAGE_BUFFER:
                kind = "storage-buffer"
                readonly = readonly or DEC_NON_WRITABLE in members.get(tid, set())
            else:
                sys.exit("[shader_msl] a struct resource in storage class %d" % storage)
        else:
            sys.exit("[shader_msl] a resource of SPIR-V type %d" % top)
        found.append({"var": var, "set": dec[DEC_SET][0], "binding": dec[DEC_BINDING][0],
                      "at": binding_at[var], "kind": kind, "readonly": readonly})
    return found


def indices(stage, found):
    """Each resource's MSL index, a number or an expression in the HX_ counts, by SDL's rules.
    A resource SDL's Vulkan path would not find where the SPIR-V puts it is an error here."""
    out = []
    for r in found:
        s, b, kind = r["set"], r["binding"], r["kind"]
        if stage in SETS:
            res, uni = SETS[stage]
            if s == uni and kind == "uniform":
                index = str(b)
            elif s == res and kind in ("sampled", "storage-texture"):
                index = str(b)  # sampled textures from 0, storage textures after them, in both
            elif s == res and kind == "storage-buffer":
                index = "(HX_UNIFORMS + %d - HX_SAMPLERS - HX_STORAGE_TEXTURES)" % b
            else:
                sys.exit("[shader_msl] a %s in set %d of a %s shader" % (kind, s, stage))
        else:
            if s == 2 and kind == "uniform":
                index = str(b)
            elif s == 0 and kind in ("sampled", "storage-texture"):
                index = str(b)  # sampled from 0, read-only storage textures after them, in both
            elif s == 0 and kind == "storage-buffer":
                index = "(HX_UNIFORMS + %d - HX_SAMPLERS - HX_RO_TEXTURES)" % b
            elif s == 1 and kind == "storage-texture":
                index = "(HX_SAMPLERS + HX_RO_TEXTURES + %d)" % b
            elif s == 1 and kind == "storage-buffer":
                index = "(HX_UNIFORMS + HX_RO_BUFFERS + %d - HX_RW_TEXTURES)" % b
            else:
                sys.exit("[shader_msl] a %s in set %d of a compute shader" % (kind, s))
        out.append(index)
    return out


def convert(spv_header, stage, entry):
    words = read_header(spv_header)
    found = resources(words)
    exprs = indices(stage, found)
    placeholders = {}
    for n, (r, index) in enumerate(zip(found, exprs)):
        if index.isdigit():
            words[r["at"]] = int(index)
        else:
            words[r["at"]] = PLACEHOLDER + n
            placeholders[PLACEHOLDER + n] = index
    with tempfile.TemporaryDirectory() as tmp:
        spv = os.path.join(tmp, entry + ".spv")
        msl = os.path.join(tmp, entry + ".metal")
        with open(spv, "wb") as f:
            f.write(struct.pack("<%dI" % len(words), *words))
        r = subprocess.run([SPIRV_CROSS, spv, "--msl", "--msl-version", MSL_VERSION, "--msl-decoration-binding",
                            "--entry", entry, "--output", msl], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("[shader_msl] spirv-cross %s: %s" % (entry, (r.stderr or r.stdout).strip()))
        text = open(msl, encoding="utf-8").read()
    for value, index in placeholders.items():
        text, n = re.subn(r"\[\[(buffer|texture|sampler)\(%d\)\]\]" % value,
                          lambda m: "[[%s(%s)]]" % (m.group(1), index), text)
        if n == 0:
            sys.exit("[shader_msl] %s: binding %d not in the MSL" % (entry, value))
    if re.search(r"\[\[(buffer|texture|sampler)\(%d\d{3}\)\]\]" % (PLACEHOLDER // 1000), text):
        sys.exit("[shader_msl] %s: a placeholder left in the MSL" % entry)
    if not re.search(r"\b%s\s*\(" % re.escape(entry), text):
        sys.exit("[shader_msl] %s: SPIRV-Cross renamed the entry point" % entry)
    return text


def write_header(text, out, name, entry):
    data = text.encode("utf-8") + b"\0"
    rows = ["  " + ", ".join("0x%02x" % c for c in data[i:i + 16]) for i in range(0, len(data), 16)]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("// %s -- written by tools/shader_msl.py: %s in Metal Shading Language, from its SPIR-V\n"
                % (os.path.basename(out), entry))
        f.write("const unsigned char %s[] = {\n%s\n};\n" % (name, ",\n".join(rows)))


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    spv_header, stage, entry, out, name = sys.argv[1:]
    if stage not in ("vertex", "fragment", "compute"):
        sys.exit("[shader_msl] stage %s: vertex, fragment or compute" % stage)
    if not os.path.exists(SPIRV_CROSS):
        sys.exit("[shader_msl] no %s: run python tools/fetch_spirv_cross.py" % SPIRV_CROSS)
    write_header(convert(spv_header, stage, entry), out, name, entry)


if __name__ == "__main__":
    main()
