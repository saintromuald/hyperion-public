"""Types from an object's CodeView records (the .debug$T and .debug$S sections /Z7 writes).

    tt = TypeTable(obj)             # coff.ObjFile or a path
    ti = tt.global_type("name")     # a global the object defines: its type index
    tt.leaves(ti)                   # [(offset, size, kind, path)]

A type is flattened into its leaves: every scalar, pointer and bit-field unit, at its
offset, in declaration order, arrays element by element. The kind is "p" for a
pointer (a data, function or member pointer), "i" for a signed integer, "u" for an
unsigned one or a character, "f" for a floating-point number, "b" for a bit-field's
storage unit and "?" for a union, whose bytes are one leaf. A member's path names it,
so two widths' leaves can be paired. Only what the globals' layout work needs.
"""
import struct

import coff

# simple types, the low byte of an index under 0x1000: (size, kind)
_SIMPLE = {
    0x03: (0, "u"),                              # void
    0x08: (4, "i"),                              # HRESULT
    0x10: (1, "i"), 0x20: (1, "u"), 0x70: (1, "i"), 0x68: (1, "i"), 0x69: (1, "u"),
    0x71: (2, "u"), 0x7A: (2, "u"), 0x7B: (4, "u"),
    0x11: (2, "i"), 0x21: (2, "u"), 0x72: (2, "i"), 0x73: (2, "u"),
    0x12: (4, "i"), 0x22: (4, "u"), 0x74: (4, "i"), 0x75: (4, "u"),
    0x13: (8, "i"), 0x23: (8, "u"), 0x76: (8, "i"), 0x77: (8, "u"),
    0x40: (4, "f"), 0x41: (8, "f"), 0x42: (10, "f"),
    0x30: (1, "u"), 0x31: (2, "u"), 0x32: (4, "u"), 0x33: (8, "u"),
}

LF_MODIFIER, LF_POINTER, LF_PROCEDURE, LF_MFUNCTION = 0x1001, 0x1002, 0x1008, 0x1009
LF_FIELDLIST, LF_BITFIELD = 0x1203, 0x1205
LF_ARRAY, LF_CLASS, LF_STRUCTURE, LF_UNION, LF_ENUM = 0x1503, 0x1504, 0x1505, 0x1506, 0x1507
LF_BCLASS, LF_VBCLASS, LF_IVBCLASS, LF_INDEX, LF_VFUNCTAB = 0x1400, 0x1401, 0x1402, 0x1404, 0x1409
LF_FRIENDCLS, LF_VFUNCOFF = 0x140B, 0x140C
LF_ENUMERATE, LF_FRIENDFCN, LF_MEMBER, LF_STMEMBER = 0x1502, 0x150C, 0x150D, 0x150E
LF_METHOD, LF_NESTTYPE, LF_ONEMETHOD = 0x150F, 0x1510, 0x1511
S_LDATA32, S_GDATA32 = 0x110C, 0x110D


def _numeric(d, p):
    """A numeric leaf at p -> (value, next)."""
    v = struct.unpack_from("<H", d, p)[0]
    if v < 0x8000:
        return v, p + 2
    fmt = {0x8000: "<b", 0x8001: "<h", 0x8002: "<H", 0x8003: "<i", 0x8004: "<I",
           0x8009: "<q", 0x800A: "<Q"}[v]
    return struct.unpack_from(fmt, d, p + 2)[0], p + 2 + struct.calcsize(fmt)


def _cstr(d, p):
    e = d.index(b"\0", p)
    return d[p:e].decode("latin-1"), e + 1


class TypeTable:
    def __init__(self, obj):
        self.obj = obj if isinstance(obj, coff.ObjFile) else coff.ObjFile(obj)
        self.recs = {}          # index -> (kind, bytes after the kind)
        self.defs = {}          # a forward reference's unique name -> its definition
        self.data = {}          # a defined global's name -> its type index
        self._cache = {}
        for sec in self.obj.sections:
            raw = self.obj.data[sec["rawptr"]:sec["rawptr"] + sec["rawsz"]]
            if sec["name"] == ".debug$T":
                self._types(raw)
            elif sec["name"] == ".debug$S":
                self._symbols(raw)

    def _types(self, raw):
        if struct.unpack_from("<I", raw, 0)[0] != 4:
            raise ValueError("%s: .debug$T is not CodeView C13" % self.obj.path)
        p, ti = 4, 0x1000
        while p + 4 <= len(raw):
            ln, kind = struct.unpack_from("<HH", raw, p)
            self.recs[ti] = (kind, raw[p + 4:p + 2 + ln])
            if kind in (LF_CLASS, LF_STRUCTURE, LF_UNION):
                u = self._udt(kind, self.recs[ti][1])
                if not u["fwd"]:
                    self.defs.setdefault(u["unique"] or u["name"], ti)
            p += 2 + ln
            ti += 1

    def _symbols(self, raw):
        p = 4
        while p + 8 <= len(raw):
            kind, ln = struct.unpack_from("<II", raw, p)
            body = raw[p + 8:p + 8 + ln]
            if kind == 0xF1:
                q = 0
                while q + 4 <= len(body):
                    rl, rk = struct.unpack_from("<HH", body, q)
                    if rk in (S_GDATA32, S_LDATA32):
                        ti = struct.unpack_from("<I", body, q + 4)[0]
                        name, _ = _cstr(body, q + 14)
                        self.data[name] = ti
                    q += 2 + rl
            p += 8 + ((ln + 3) & ~3)

    def global_type(self, name):
        return self.data[name]

    def by_name(self, name):
        """A struct or class the object defines, by its name."""
        for key in (".?AU%s@@" % name, ".?AV%s@@" % name, name):
            if key in self.defs:
                return self.defs[key]
        # a template's unique name is mangled; its display name is plain
        for ti in self.defs.values():
            kind, d = self.recs[ti]
            if self._udt(kind, d)["name"] == name:
                return ti
        raise KeyError("%s: no type %s" % (self.obj.path, name))

    def _udt(self, kind, d):
        count, prop, fl = struct.unpack_from("<HHI", d, 0)
        p = 8 if kind == LF_UNION else 16
        size, p = _numeric(d, p)
        name, p = _cstr(d, p)
        unique = _cstr(d, p)[0] if prop & 0x200 and p < len(d) else None
        return {"fwd": bool(prop & 0x80), "fields": fl, "size": size, "name": name,
                "unique": unique}

    def _resolve(self, ti):
        """Through modifiers and forward references."""
        while ti >= 0x1000:
            kind, d = self.recs[ti]
            if kind == LF_MODIFIER:
                ti = struct.unpack_from("<I", d, 0)[0]
                continue
            if kind in (LF_CLASS, LF_STRUCTURE, LF_UNION):
                u = self._udt(kind, d)
                if u["fwd"]:
                    real = self.defs.get(u["unique"] or u["name"])
                    if real is None:
                        raise KeyError("incomplete type %s" % u["name"])
                    ti = real
            return ti
        return ti

    def name(self, ti):
        ti = self._resolve(ti)
        if ti < 0x1000:
            return "simple%X" % ti
        kind, d = self.recs[ti]
        if kind in (LF_CLASS, LF_STRUCTURE, LF_UNION):
            return self._udt(kind, d)["name"]
        if kind == LF_POINTER:
            return self.name(struct.unpack_from("<I", d, 0)[0]) + " *"
        if kind == LF_ARRAY:
            return self.name(struct.unpack_from("<I", d, 0)[0]) + "[]"
        return "kind%X" % kind

    def size(self, ti):
        ti = self._resolve(ti)
        if ti < 0x1000:
            mode = (ti >> 8) & 0xF
            if mode:
                return {4: 4, 6: 8}[mode]
            return _SIMPLE[ti & 0xFF][0]
        kind, d = self.recs[ti]
        if kind == LF_POINTER:
            return (struct.unpack_from("<I", d, 4)[0] >> 13) & 0x3F
        if kind == LF_ARRAY:
            p = 8
            return _numeric(d, p)[0]
        if kind in (LF_CLASS, LF_STRUCTURE, LF_UNION):
            return self._udt(kind, d)["size"]
        if kind == LF_ENUM:
            return self.size(struct.unpack_from("<I", d, 4)[0])
        if kind == LF_BITFIELD:
            return self.size(struct.unpack_from("<I", d, 0)[0])
        raise ValueError("no size for type kind 0x%X" % kind)

    def members(self, ti):
        """A class's data members and bases: [(offset, type, name)], and its vfptr."""
        kind, d = self.recs[ti]
        fl = self._udt(kind, d)["fields"]
        out = []
        while fl:
            fk, fd = self.recs[fl]
            assert fk == LF_FIELDLIST
            fl, p = 0, 0
            while p < len(fd):
                if fd[p] >= 0xF0:
                    p += fd[p] & 0x0F
                    continue
                sub = struct.unpack_from("<H", fd, p)[0]
                p += 2
                if sub == LF_MEMBER:
                    _attr, t = struct.unpack_from("<HI", fd, p)
                    off, p = _numeric(fd, p + 6)
                    name, p = _cstr(fd, p)
                    out.append((off, t, name))
                elif sub == LF_BCLASS:
                    _attr, t = struct.unpack_from("<HI", fd, p)
                    off, p = _numeric(fd, p + 6)
                    out.append((off, t, "<base>"))
                elif sub in (LF_VBCLASS, LF_IVBCLASS):
                    raise ValueError("virtual base in %s" % self.name(ti))
                elif sub == LF_VFUNCTAB:
                    p += 6
                    out.append((0, None, "<vfptr>"))
                elif sub == LF_INDEX:
                    fl = struct.unpack_from("<I", fd, p + 2)[0]
                    p += 6
                elif sub == LF_STMEMBER:
                    p = _cstr(fd, p + 6)[1]
                elif sub == LF_METHOD:
                    p = _cstr(fd, p + 6)[1]
                elif sub == LF_NESTTYPE:
                    p = _cstr(fd, p + 6)[1]
                elif sub == LF_ONEMETHOD:
                    attr = struct.unpack_from("<H", fd, p)[0]
                    p += 6
                    if (attr >> 2) & 7 in (4, 6):
                        p += 4
                    p = _cstr(fd, p)[1]
                elif sub == LF_FRIENDFCN:
                    p = _cstr(fd, p + 6)[1]
                elif sub == LF_FRIENDCLS:
                    p += 6
                elif sub == LF_VFUNCOFF:
                    p += 10
                elif sub == LF_ENUMERATE:
                    _v, p = _numeric(fd, p + 2)
                    p = _cstr(fd, p)[1]
                else:
                    raise ValueError("field kind 0x%X in %s" % (sub, self.name(ti)))
        return out

    def leaves(self, ti):
        """[(offset, size, kind, path)] of a type, from offset 0."""
        ti = self._resolve(ti)
        if ti in self._cache:
            return self._cache[ti]
        out = []
        if ti < 0x1000:
            mode = (ti >> 8) & 0xF
            if mode:
                out = [(0, {4: 4, 6: 8}[mode], "p", "")]
            else:
                size, kind = _SIMPLE[ti & 0xFF]
                out = [(0, size, kind, "")] if size else []
        else:
            kind, d = self.recs[ti]
            if kind == LF_POINTER:
                out = [(0, self.size(ti), "p", "")]
            elif kind == LF_ENUM:
                out = [(o, s, k, p) for o, s, k, p in self.leaves(struct.unpack_from("<I", d, 4)[0])]
            elif kind == LF_ARRAY:
                et = struct.unpack_from("<I", d, 0)[0]
                es, n = self.size(et), self.size(ti)
                el = self.leaves(et)
                for i in range(n // es if es else 0):
                    out += [(i * es + o, s, k, "[%d]%s" % (i, p)) for o, s, k, p in el]
            elif kind in (LF_STRUCTURE, LF_CLASS):
                bits = set()
                for off, t, name in self.members(ti):
                    if t is None:
                        out.append((off, self._ptr_size, "p", "." + name))
                        continue
                    rt = self._resolve(t)
                    if rt >= 0x1000 and self.recs[rt][0] == LF_BITFIELD:
                        if off not in bits:
                            bits.add(off)
                            out.append((off, self.size(rt), "b", "." + name))
                        continue
                    out += [(off + o, s, k, "." + name + p) for o, s, k, p in self.leaves(t)]
            elif kind == LF_UNION:
                # a union with an arm that is one field as big as the union is
                # that field, a pointer first; any other is its bytes
                size = self.size(ti)
                whole = {k for _off, t, _n in self.members(ti) if t is not None
                         for k in [self.leaves(t)] if len(k) == 1 and k[0][:2] == (0, size)
                         for k in [k[0][2]]}
                kind = "p" if "p" in whole else sorted(whole)[0] if whole else "?"
                out = [(0, size, kind, "")]
                # a union of views of one record, each arm the same fields laid out the
                # same (a map point's x and y beside the point, src117/h/pos.h): its first
                # view's fields
                arms = [(t, n) for _off, t, n in self.members(ti) if t is not None]
                if not whole and len(arms) > 1:
                    first = self.leaves(arms[0][0])
                    shape = [(o, s) for o, s, _k, _p in first]
                    if len(first) > 1 and all([(o, s) for o, s, _k, _p in self.leaves(t)] == shape
                                              for t, _n in arms[1:]):
                        out = [(o, s, k, "." + arms[0][1] + p) for o, s, k, p in first]
            elif kind == LF_BITFIELD:
                out = [(0, self.size(ti), "b", "")]
            elif kind in (LF_PROCEDURE, LF_MFUNCTION):
                out = []
            else:
                raise ValueError("no leaves for type kind 0x%X" % kind)
        self._cache[ti] = out
        return out

    @property
    def _ptr_size(self):
        return 8 if self.obj.machine == 0x8664 else 4
