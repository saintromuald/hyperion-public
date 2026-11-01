#!/usr/bin/env python3
"""cmport.py -- port Cosmonarchy's data tables into Hyperion's mod-data format.

Decodes the mod's Samase work tree (the `mpq/` folder of a Cosmonarchy build)
into `types.json` for the engine's mod loader (docs/mod-data.md).

Formats decoded here (reverse-engineered against the mod's own GPTP source,
verified against CMBW-reference.txt and the shipped data):

  arr/*.datex   column-major full-table images, one column after the other,
                every column holding the table's full extended row count; a
                column's element width is the extended width the mod's GPTP
                DatExt layer uses (flingy u16, orders' weapon u16, etc.).
                Units: 3000 rows, widths in kUnitsWidths below.
  samase/*_ext.txt  text extensions applied after the datex:
                `#####<table>_<row>` blocks with `key: value` fields.
  samase/config.ini  `data_rewrite = (unit, <field>, <row>, <value>)` lines,
                applied after the extensions.
  arr/*.tbl / *.txt  string tables (sfxdata.tbl -> arr/sfxdata.txt) and the
                per-table row-name lists (arr/units.txt and friends).

Source selection: --mod DIR points at a build's mpq folder; the default is the
local plus-prerelease install this machine plays.

Usage:
  python engine/tools/cmport/cmport.py tables          # decode + verify
  python engine/tools/cmport/cmport.py gen             # write types.json
  python engine/tools/cmport/cmport.py check           # re-verify types.json
"""

import json
import os
import re
import struct
import sys
import unicodedata

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
MOD_DATA = os.path.join(REPO, "engine", "mods", "cosmonarchy", "data")
DEFAULT_MOD = os.path.expanduser(
    "~/Games/cosmonarchy/prefix/drive_c/Cosmonarchy/Prerelease/mpq")

# ---------------------------------------------------------------------------
# Table specifications.  Each column: (field, width) in file order.  The
# widths are the extended (GPTP DatExt) ones; the datex files are laid out
# with these widths and the row counts below.
# ---------------------------------------------------------------------------

kUnitsWidths = [
    ("flingy", 2, "flingy"), ("subunit", 2, "units"), ("subunit2", 2, "units"),
    ("infestation", 2, "units"), ("construction_image", 4, "images"),
    ("direction", 1, None), ("shields_enabled", 1, None), ("shields", 2, None),
    ("hit_points", 4, None), ("elevation", 1, None), ("movement_flags", 1, None),
    ("rank", 1, None), ("ai_idle_order", 1, "orders"), ("idle_order", 1, "orders"),
    ("return_to_idle_order", 1, "orders"), ("attack_unit_order", 1, "orders"),
    ("attack_move_order", 1, "orders"), ("ground_weapon", 2, "weapons"),
    ("max_ground_hits", 1, None), ("air_weapon", 2, "weapons"), ("max_air_hits", 1, None),
    ("ai_internal", 1, None), ("flags", 4, None), ("target_range", 1, None),
    ("sight_range", 1, None), ("armor_upgrade", 1, "upgrades"), ("size", 1, None),
    ("armor", 1, None), ("right_click", 1, None), ("ready_sound", 2, "sounds"),
    ("what_sound_first", 2, "sounds"), ("what_sound_last", 2, "sounds"),
    ("pissed_sound_first", 2, "sounds"), ("pissed_sound_last", 2, "sounds"),
    ("yes_sound_first", 2, "sounds"), ("yes_sound_last", 2, "sounds"),
    ("placement_box", 4, None), ("addon_position", 4, None), ("bounds", 8, None),
    ("portrait", 2, "portraits"), ("minerals", 2, None), ("gas", 2, None),
    ("build_time", 2, None), ("requirements", 2, None), ("group_flags", 1, None),
    ("supply_provided", 1, None), ("supply_required", 1, None),
    ("space_required", 1, None), ("space_provided", 1, None), ("build_score", 2, None),
    ("destroy_score", 2, None), ("map_string", 2, None), ("broodwar", 1, None),
    ("unknown_53", 1, None), ("unknown_54", 1, None),
]

kWeaponsWidths = [
    ("label", 2, None), ("flingy", 4, "flingy"), ("unused", 1, None),
    ("target_flags", 2, None), ("min_range", 4, None), ("max_range", 4, None),
    ("damage_upgrade", 1, "upgrades"), ("damage_type", 1, None), ("behavior", 1, None),
    ("remove_after", 1, None), ("explosion", 1, None), ("inner_splash", 2, None),
    ("middle_splash", 2, None), ("outer_splash", 2, None), ("damage", 2, None),
    ("damage_bonus", 2, None), ("cooldown", 1, None), ("damage_factor", 1, None),
    ("attack_angle", 1, None), ("launch_spin", 1, None), ("forward_offset", 1, None),
    ("upward_offset", 1, None), ("target_error", 2, None), ("icon", 2, None),
]

kFlingyWidths = [
    ("sprite", 2, "sprites"), ("top_speed", 4, None), ("acceleration", 2, None),
    ("halt_distance", 4, None), ("turn_radius", 1, None), ("unused", 1, None),
    ("movement_control", 1, None),
]

kSpriteWidths = [
    ("image", 2, "images"), ("health_bar", 1, None), ("unknown", 1, None),
    ("visible", 1, None), ("selection_circle", 1, None), ("selection_offset", 1, None),
]

kImageWidths = [
    ("grp", 4, None), ("turns", 1, None), ("clickable", 1, None),
    ("use_full_iscript", 1, None), ("draw_if_cloaked", 1, None),
    ("draw_function", 1, None), ("remapping", 1, None), ("script", 4, "scripts"),
    ("shield_overlay", 4, "images"), ("attack_overlay", 4, "images"),
    ("damage_overlay", 4, "images"), ("special_overlay", 4, "images"),
    ("landing_dust_overlay", 4, "images"), ("liftoff_dust_overlay", 4, "images"),
]

kOrderWidths = [
    ("label", 2, None), ("use_weapon_targeting", 1, None), ("secondary", 1, None),
    ("non_subunit", 1, None), ("subunit_inheritance", 1, None),
    ("subunit_can_use", 1, None), ("interruptible", 1, None), ("stop_moving", 1, None),
    ("can_be_queued", 1, None), ("keep_target_disabled", 1, None),
    ("can_be_obstructed", 1, None), ("fleeable", 1, None), ("requires_movable", 1, None),
    ("weapon", 2, "weapons"), ("tech", 1, "techs"), ("animation", 1, None),
    ("highlight", 1, None), ("requirements", 2, None), ("obscured", 2, "orders"),
]

kTechWidths = [
    ("minerals", 2, None), ("gas", 2, None), ("research_time", 2, None),
    ("energy", 2, None), ("requirements", 2, None), ("use_requirements", 2, None),
    ("icon", 2, None), ("label", 2, None), ("race", 1, None), ("researched", 1, None),
    ("broodwar", 1, None),
]

kUpgradeWidths = [
    ("minerals", 2, None), ("minerals_factor", 2, None), ("gas", 2, None),
    ("gas_factor", 2, None), ("time", 2, None), ("time_factor", 2, None),
    ("requirements", 2, None), ("icon", 2, None), ("label", 2, None),
    ("race", 1, None), ("max_level", 1, None), ("broodwar", 1, None),
]

kSoundWidths = [
    ("file", 4, None), ("priority", 1, None), ("flags", 1, None),
    ("length_adjust", 2, None), ("unknown", 1, None),
]

kPortraitWidths = [
    ("idle", 4, None), ("talking", 4, None),
    ("idle_chance_0", 1, None), ("idle_chance_1", 1, None),
    ("idle_chance_2", 1, None), ("idle_chance_3", 1, None),
]

# The 1.17 rows each table holds; a mod's rows go past these in Hyperion.
kV117 = {
    "units": 228, "weapons": 130, "flingy": 209, "sprites": 517,
    "images": 999, "orders": 189, "techs": 44, "upgrades": 61,
    "sounds": 1144, "portraits": 110, "mapdata": 16,
}

kTABLES = [
    # id, datex file, rows, widths, name file, is media-only
    ("units", "units.datex", 3000, kUnitsWidths, "units.txt", False),
    ("weapons", "weapons.datex", 1001, kWeaponsWidths, "weapons.txt", False),
    ("flingy", "flingy.datex", 1000, kFlingyWidths, "flingy.txt", False),
    ("sprites", "sprites.datex", 2000, kSpriteWidths, "sprites.txt", False),
    ("images", "images.datex", 5000, kImageWidths, "images.txt", False),
    ("orders", "orders.datex", 500, kOrderWidths, "orders.txt", False),
    ("techs", "techdata.datex", 100, kTechWidths, "techdata.txt", False),
    ("upgrades", "upgrades.datex", 100, kUpgradeWidths, "upgrades.txt", False),
    ("sounds", "sfxdata.dat", 20000, kSoundWidths, "sounds.txt", True),
    ("portraits", "portdata.datex", 1003, kPortraitWidths, "portdata.txt", True),
    ("mapdata", "mapdata.datex", 65, [("map_string", 4, None)], "mapdata.txt", False),
]

# The flag names for units.dat's flags column, bit 0 first (port_moddata.cpp).
kUnitFlags = [
    "building", "addon", "flyer", "worker", "subunit", "flying_building", "hero",
    "regenerate", "animated_idle", "cloakable", "two_in_egg", "single_entity",
    "resource_depot", "resource_container", "robotic", "detector", "organic",
    "requires_creep", "unused", "requires_psi", "burrowable", "spellcaster",
    "permanent_cloak", "pickup_item", "ignores_supply", "medium_overlays",
    "large_overlays", "battle_reactions", "auto_attack", "invincible",
    "mechanical", "produces_units",
]


def slugify(name):
    name = unicodedata.normalize("NFKD", name)
    name = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").lower()
    return name or "row"


class Table:
    def __init__(self, spec):
        self.id, self.file, self.rows, self.widths, self.namefile, self.media = spec
        self.width_bytes = sum(w for _, w, _ in self.widths)
        self.row_bytes = self.rows * self.width_bytes
        self.cols = []       # list of lists, column by column
        self.names = []      # arr/<namefile> names, one per row (may be fewer)

    def load(self, mod_dir):
        path = os.path.join(mod_dir, "arr", self.file)
        raw = open(path, "rb").read()
        want = self.row_bytes
        if len(raw) != want:
            raise ValueError(
                f"{self.file}: {len(raw)} bytes; the layout gives {want} "
                f"({self.rows} rows x {self.width_bytes} B)")
        self.cols = []
        at = 0
        for field, width, _ref in self.widths:
            col = []
            if width == 1:
                col = list(raw[at:at + self.rows])
            elif width == 2:
                col = list(struct.unpack_from("<%dH" % self.rows, raw, at))
            elif width == 4:
                col = list(struct.unpack_from("<%dI" % self.rows, raw, at))
            elif width == 8:
                col = list(struct.unpack_from("<%dQ" % self.rows, raw, at))
            else:
                raise ValueError(f"{self.file}: width {width}?")
            self.cols.append(col)
            at += width * self.rows
        nf = os.path.join(mod_dir, "arr", self.namefile)
        if os.path.exists(nf):
            with open(nf, encoding="utf-8", errors="replace") as f:
                self.names = [ln.rstrip("\n") for ln in f]

    def row(self, r):
        return {f: c[r] for (f, _, _), c in zip(self.widths, self.cols)}


def load_all(mod_dir):
    tables = {}
    for spec in kTABLES:
        t = Table(spec)
        t.load(mod_dir)
        tables[t.id] = t
    return tables


# ---------------------------------------------------------------------------
# Text extensions (samase/*_ext.txt): applied after the datex.
# ---------------------------------------------------------------------------

kExtFiles = {
    "flingy": "flingy_ext.txt",
    "images": "images_ext.txt",
    "upgrades": "upgrades_ext.txt",
}

kExtFields = {
    "flingy": {
        "sprite_id": "sprite", "top_speed": "top_speed", "acceleration": "acceleration",
        "halt_distance": "halt_distance", "turn_radius": "turn_radius",
        "movement_type": "movement_control", "iscript_mask": "unused",
    },
    "images": {
        "grp": "grp", "turns": "turns", "clickable": "clickable",
        "use_full_iscript": "use_full_iscript", "draw_if_cloaked": "draw_if_cloaked",
        "draw_function": "draw_function", "remapping": "remapping",
        "iscript": "script", "shield_overlay": "shield_overlay",
        "attack_overlay": "attack_overlay", "damage_overlay": "damage_overlay",
        "special_overlay": "special_overlay",
        "landing_dust_overlay": "landing_dust_overlay",
        "liftoff_dust_overlay": "liftoff_dust_overlay",
    },
    "upgrades": {
        "ore_cost": "minerals", "ore_cost_factor": "minerals_factor",
        "gas_cost": "gas", "gas_cost_factor": "gas_factor",
        "time_cost": "time", "time_cost_factor": "time_factor",
        "reqindex": "requirements", "icon": "icon", "label": "label",
        "race": "race", "max_repeats": "max_level", "broodwar": "broodwar",
    },
}


def apply_ext(tables, mod_dir):
    applied = []
    for table_id, file in kExtFiles.items():
        path = os.path.join(mod_dir, "samase", file)
        if not os.path.exists(path):
            continue
        t = tables[table_id]
        cur = None
        for ln in open(path, encoding="utf-8", errors="replace"):
            ln = ln.rstrip()
            m = re.match(r"#####([a-z]+)_(\d+)", ln)
            if m:
                cur = int(m.group(2))
                continue
            if not ln or ln.startswith("###") or ln.startswith("#"):
                continue
            m = re.match(r"(\w+):\s*([-\w.]+)", ln)
            if not m or cur is None or cur >= t.rows:
                continue
            key, val = m.group(1), m.group(2)
            field = kExtFields.get(table_id, {}).get(key)
            if field is None:
                continue
            col = t.cols[[f for f, _, _ in t.widths].index(field)]
            col[cur] = int(val)
            applied.append((table_id, cur, field))
    return applied


# ---------------------------------------------------------------------------
# samase/config.ini data_rewrite lines: (unit, field, row, value)
# ---------------------------------------------------------------------------

kRewriteFields = {
    "flingy": "flingy", "construction": "construction_image",
    "snd_ready": "ready_sound", "snd_yes_first": "yes_sound_first",
    "snd_yes_last": "yes_sound_last", "snd_what_first": "what_sound_first",
    "snd_what_last": "what_sound_last", "snd_pss_first": "pissed_sound_first",
    "snd_pss_last": "pissed_sound_last", "armorupgrade": "armor_upgrade",
}


def apply_rewrites(tables, mod_dir):
    path = os.path.join(mod_dir, "samase", "config.ini")
    if not os.path.exists(path):
        return []
    units = tables["units"]
    applied = []
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*data_rewrite\s*=\s*\(\s*unit\s*,\s*([a-z_0-9]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", ln)
        if not m:
            continue
        field, row, val = m.group(1), int(m.group(2)), int(m.group(3))
        field = kRewriteFields.get(field, field)
        if field not in [f for f, _, _ in units.widths] or row >= units.rows:
            continue
        col = units.cols[[f for f, _, _ in units.widths].index(field)]
        col[row] = val
        applied.append((row, field, val))
    return applied


def apply_plus_yml(tables, mod_dir):
    """plus.yml: the Plus build's cost overrides, by unit name.  The file is a
    tiny YAML subset (`overrides:` items of `unit:`/`minerals:`/`gas:`/
    `build_time:`), applied last, as the plus plugin does at game time."""
    units = tables["units"]
    applied = []
    for candidate in (os.path.join(mod_dir, "plus.yml"),
                      os.path.join(os.path.dirname(mod_dir.rstrip("/\\")), "plus.yml")):
        if os.path.exists(candidate):
            path = candidate
            break
    else:
        return []
    item = None
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*-\s*unit:\s*(.+?)\s*$", ln)
        if m:
            if item:
                _plus_one(units, item, applied)
            item = {"unit": m.group(1).strip().strip("'\"")}
            continue
        if item is None:
            continue
        m = re.match(r"\s*(\w+):\s*([-0-9]+)\s*$", ln)
        if not m:
            continue
        key, val = m.group(1), int(m.group(2))
        if key in ("minerals", "gas", "build_time"):
            item[key] = val
            continue
        if key == "unit":
            continue
        # any other key ends this item (it is not one of ours)
        item = None
        continue
    if item:
        _plus_one(units, item, applied)
    return applied


def _plus_one(units, item, applied):
    names = [n.strip() for n in units.names]
    if item["unit"] not in names:
        print(f"plus.yml: unit {item['unit']!r} not found in units.txt")
        return
    r = names.index(item["unit"])
    for key, field in (("minerals", "minerals"), ("gas", "gas"), ("build_time", "build_time")):
        if key in item:
            col = units.cols[[f for f, _, _ in units.widths].index(field)]
            col[r] = item[key]
            applied.append((r, field, item[key]))


# ---------------------------------------------------------------------------
# Reference costs for verification.
# ---------------------------------------------------------------------------

def reference_costs(path=None):
    path = path or os.path.expanduser("~/Games/cosmonarchy/CMBW-reference.txt")
    rows = {}
    if not os.path.exists(path):
        return rows
    for m in re.finditer(
            r"^\s*(\d+)\s+([A-Za-z0-9 '()\-.,]+?)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)",
            open(path).read(), re.M):
        rows[int(m.group(1))] = (int(m.group(3)), int(m.group(4)), int(m.group(5)))
    return rows


def verify(tables, verbose=True):
    ref = reference_costs()
    units = tables["units"]
    n = bad = 0
    problems = []
    for r, (mn, gs, tm) in sorted(ref.items()):
        if r >= units.rows:
            problems.append((r, "beyond the table"))
            continue
        n += 1
        row = units.row(r)
        got = (row["minerals"], row["gas"], row["build_time"])
        if got != (mn, gs, tm):
            bad += 1
            problems.append((r, f"cost {got} vs reference {(mn, gs, tm)}"))
            if verbose:
                print(f"  unit {r}: {got} != {(mn, gs, tm)}")
    print(f"verified {n} reference rows: {n - bad} exact, {bad} differ")
    return problems


# ---------------------------------------------------------------------------
# types.json generation.
# ---------------------------------------------------------------------------

def col_index(t, field):
    return [f for f, _, _ in t.widths].index(field)


def record_name(table_id, names, r):
    if r < len(names) and names[r].strip():
        return slugify(names[r])
    return f"{table_id}_{r}"


def build_names(tables):
    """Record names per table: the mod's own names, de-duplicated."""
    out = {}
    for t in tables.values():
        used = set()
        names = []
        for r in range(t.rows):
            base = record_name(t.id, t.names, r)
            nm = base
            i = 2
            while nm in used:
                nm = f"{base}_{i}"
                i += 1
            used.add(nm)
            names.append(nm)
        out[t.id] = names
    return out


# Row numbers that mean "none" in a table's references: they stay numbers.
kNoneMarkers = {
    "units": frozenset(range(228, 233)),
    "weapons": frozenset((130,)),
    "orders": frozenset((189,)),
    "techs": frozenset((44,)),
    "upgrades": frozenset((61,)),
}


def is_real_name(t, r):
    if r >= len(t.names):
        return False
    n = t.names[r].strip()
    if not n:
        return False
    return not n.startswith("Extended ")


def read_sfxdata_paths(mod_dir):
    """sfxdata.txt: one path per line, the sfxdata.dat 'file' column's table."""
    path = os.path.join(mod_dir, "arr", "sfxdata.txt")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8", errors="replace") as f:
        return [ln.rstrip("\n") for ln in f]


def read_tbl_binary(path):
    d = open(path, "rb").read()
    if len(d) < 2:
        return []
    n = d[0] | d[1] << 8
    out = []
    for i in range(n):
        if 2 + 2 * i + 1 >= len(d):
            break
        off = d[2 + 2 * i] | d[3 + 2 * i] << 8
        end = off
        while end < len(d) and d[end] != 0:
            end += 1
        out.append(d[off:end].decode("latin1") if off < len(d) else "")
    return out


def portrait_path(tbl_string):
    """'tmarine\TMaFid0' -> 'portraits/tmarine/TMaFid' (the game adds the digit)."""
    s = tbl_string.replace("\\", "/")
    s = re.sub(r"\d$", "", s)
    return f"portraits/{s}" if not s.startswith("portraits/") else s


def gen_types(tables, out_path, mod_dir):
    names = build_names(tables)
    sfx = read_sfxdata_paths(mod_dir)
    port_tbl = read_tbl_binary(os.path.join(mod_dir, "arr", "portdata.tbl"))

    def ref(table_id, r):
        """A cross-reference value for types.json: numbers stay numbers."""
        if table_id not in kV117:
            return r  # scripts: ids come from iscript.txt, kept numeric
        if r in kNoneMarkers.get(table_id, ()):
            return r
        if r < kV117[table_id]:
            return r
        if r < len(names[table_id]):
            return names[table_id][r]
        return r

    def fields_of(table_id, r):
        t = tables[table_id]
        row = t.row(r)
        out = {}
        for (f, _, ref_table) in t.widths:
            v = row[f]
            if table_id == "units" and f == "hit_points":
                v = v // 256
            if table_id == "units" and f == "flags":
                out[f] = [kUnitFlags[i] for i in range(32) if v & (1 << i)]
                continue
            if f == "placement_box":
                out[f] = [v & 0xFFFF, (v >> 16) & 0xFFFF]
                continue
            if f == "addon_position":
                out[f] = [v & 0xFFFF, (v >> 16) & 0xFFFF]
                continue
            if f == "bounds":
                out[f] = [(v >> s) & 0xFFFF for s in (0, 16, 32, 48)]
                continue
            if f.startswith("unknown_"):
                continue
            if table_id == "sounds" and f == "file":
                s = sfx[v] if v < len(sfx) else ""
                out[f] = "sounds/" + s.replace("\\", "/") if s and s != "No sound" else None
                continue
            if table_id == "portraits" and f in ("idle", "talking"):
                s = port_tbl[v] if v < len(port_tbl) else ""
                out[f] = portrait_path(s) if s else None
                continue
            if ref_table:
                v = ref(ref_table, v)
            out[f] = v
        return out

    # Extended rows that are real (named, not placeholders), per table.
    real = {}
    for t_id in tables:
        t = tables[t_id]
        real[t_id] = frozenset(
            r for r in range(kV117[t_id], t.rows) if is_real_name(t, r))

    def refs_of(table_id, r):
        t = tables[table_id]
        row = t.row(r)
        out = set()
        for (f, _, ref_table) in t.widths:
            if not ref_table or ref_table not in kV117:
                continue
            v = row[f]
            if v in kNoneMarkers.get(ref_table, ()) or v < kV117[ref_table]:
                continue
            if v < tables[ref_table].rows:
                out.add((ref_table, v))
        return out

    # Rows kept as records: real extended rows, plus the closure of what the
    # edits and the kept rows reference.
    keep = {t_id: set(real[t_id]) for t_id in tables}
    for t_id in tables:
        t = tables[t_id]
        for r in range(kV117[t_id]):
            for (rt, v) in refs_of(t_id, r):
                if v < tables[rt].rows:
                    keep[rt].add(v)
    changed = True
    while changed:
        changed = False
        for t_id in tables:
            t = tables[t_id]
            for r in list(keep[t_id]):
                for (rt, v) in refs_of(t_id, r):
                    if v < tables[rt].rows and v not in keep[rt]:
                        keep[rt].add(v)
                        changed = True

    doc = {}
    stats = {}
    for t_id in tables:
        if t_id == "mapdata":
            continue  # not a table the mod-data loader reads
        t = tables[t_id]
        v117 = kV117[t_id]
        recs = {}
        for r in range(v117):
            rec = {"edit": r, **fields_of(t_id, r)}
            if is_real_name(t, r):
                rec["name"] = t.names[r].strip()
            recs[names[t_id][r]] = rec
        for r in sorted(keep[t_id]):
            if r < v117:
                continue
            rec = {"base": names[t_id][v117 - 1]}
            flds = fields_of(t_id, r)
            if is_real_name(t, r):
                flds["name"] = t.names[r].strip()
            if t_id == "sounds":
                # a sound without a file plays nothing; drop empty columns
                flds = {k: v for k, v in flds.items() if v is not None}
                if not flds.get("file") and not is_real_name(t, r):
                    continue
            rec.update(flds)
            recs[names[t_id][r]] = rec
        doc[t_id] = recs
        stats[t_id] = len(recs)

    out = json.dumps(doc, indent=1, ensure_ascii=False)
    if out_path:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(out + "\n")
        print(f"wrote {out_path} ({len(out)} bytes)")
    return doc, stats


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "tables"
    mod_dir = None
    if "--mod" in sys.argv:
        mod_dir = sys.argv[sys.argv.index("--mod") + 1]
    mod_dir = mod_dir or DEFAULT_MOD
    if cmd == "gen":
        tables = load_all(mod_dir)
        n_ext = apply_ext(tables, mod_dir)
        n_rw = apply_rewrites(tables, mod_dir)
        n_plus = apply_plus_yml(tables, mod_dir)
        print(f"ext fields applied: {len(n_ext)}; data_rewrites: {len(n_rw)}; "
              f"plus.yml: {len(n_plus)}")
        problems = verify(tables)
        doc, stats = gen_types(tables, os.path.join(MOD_DATA, "types.json"), mod_dir)
        print(json.dumps(stats, indent=1))
        if problems:
            print(f"NOTE: {len(problems)} reference rows differ -- review before relying on them")
    elif cmd == "tables":
        tables = load_all(mod_dir)
        n_ext = apply_ext(tables, mod_dir)
        n_rw = apply_rewrites(tables, mod_dir)
        n_plus = apply_plus_yml(tables, mod_dir)
        print(f"ext fields applied: {len(n_ext)}; data_rewrites: {len(n_rw)}; "
              f"plus.yml: {len(n_plus)}")
        for t in tables.values():
            named = sum(1 for n in t.names if n.strip())
            print(f"{t.id:10s} rows={t.rows:6d} names={named:6d} file={t.file}")
        verify(tables)
    elif cmd == "check":
        tables = load_all(mod_dir)
        verify(tables)


if __name__ == "__main__":
    main()
