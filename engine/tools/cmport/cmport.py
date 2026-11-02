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

# Hyperion's engine holds one sight pattern per range, 0 to 14 (1.17's twelve
# plus the port's three); the mod's own exe reaches past that. Its sight_range
# values clamp here, so the loader's own refusal (a range past 14) never fires
# on this mod's data.
SIGHT_MAX = 14

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
        d = {f: c[r] for (f, _, _), c in zip(self.widths, self.cols)}
        if "sight_range" in d and d["sight_range"] > SIGHT_MAX:
            d["sight_range"] = SIGHT_MAX
        return d


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

# ---------------------------------------------------------------------------
# Requirement grafts (mpq/datreq/*.graft): the game's requirement scripts,
# one run per subject, [subject id][opcodes...][0xFFFF], 0xFF01 joining the
# opcodes of one group (or). The opcode set is the game's own: TTAllowed in
# engine/src117/lang/TechTree.cpp and CheckAllowed in Allowed.cpp.
# ---------------------------------------------------------------------------

kGraftKinds = {
    "units": ("units", "requires", False),
    "research": ("techs", "requires", False),
    "upgrades": ("upgrades", "requires", False),
    "tech": ("techs", "use_requires", True),
}


def read_graft_runs(path):
    raw = open(path, "rb").read()
    words = struct.unpack("<%dH" % (len(raw) // 2), raw)
    runs = []
    cur = []
    # word 0 is sgTree's unused index 0, as the game's own indexer skips it
    # (TechTree.cpp sub_4B3230 starts at 1)
    for w in words[1:]:
        if w == 0xFFFF:
            if cur:
                runs.append(cur)
            cur = []
        else:
            cur.append(w)
    if cur:
        runs.append(cur)
    return runs


def graft_requires(mod_dir, tables, names):
    """Decode the four requirement grafts into (table, row) -> requires lists,
    and a list of unsupported constructs for the report."""
    req_map = {}
    unsupported = []

    def uref(v):
        """A graft operand naming a unit."""
        if v in kNoneMarkers["units"] or v < kV117["units"]:
            return v
        if v < len(names["units"]):
            keep_targets.add(("units", v))
            return names["units"][v]
        return v

    def tref(v):
        if v == 44 or v < kV117["techs"]:
            return v
        if v < len(names["techs"]):
            keep_targets.add(("techs", v))
            return names["techs"][v]
        return v

    keep_targets = set()
    for graft, (t_id, field, bUse) in kGraftKinds.items():
        path = os.path.join(mod_dir, "datreq", graft + ".graft")
        if not os.path.exists(path):
            path = os.path.join(mod_dir, "datreq", graft.capitalize() + ".graft")
        if not os.path.exists(path):
            continue
        t = tables[t_id]
        for run in read_graft_runs(path):
            if not run:
                continue
            subject = run[0]
            if subject >= t.rows:
                unsupported.append((graft, subject, "subject past the table"))
                continue
            keep_targets.add((t_id, subject))
            groups = []
            i = 1
            while i < len(run):
                group = []
                while i < len(run):
                    w = run[i]
                    if w == 0xFF02:
                        op = run[i + 1] if i + 1 < len(run) else 0
                        i += 2
                        group.append({("caster" if bUse else "at"): uref(op)})
                    elif w == 0xFF25:
                        op = run[i + 1] if i + 1 < len(run) else 0
                        i += 2
                        group.append({"tech": tref(op)})
                    elif w == 0xFF0F:
                        i += 1
                        group.append("researched")
                    elif w == 0xFF24:
                        i += 1
                        group.append("broodwar")
                    elif w == 0xFF03:
                        op = run[i + 1] if i + 1 < len(run) else 0
                        i += 2
                        group.append(uref(op))
                        unsupported.append((graft, subject,
                                            f"0xFF03 counts {op} with its in-progress ones too"))
                    elif w == 0xFF04:
                        op = run[i + 1] if i + 1 < len(run) else 0
                        i += 2
                        group.append({"addon": uref(op)})
                    elif w == 0xFF01:
                        i += 1  # the next opcode continues this group
                    elif w < 0xFF00:
                        group.append(uref(w))
                        i += 1
                    else:
                        unsupported.append((graft, subject, f"opcode {w:#x} dropped"))
                        i += 1
                    if i >= len(run) or run[i] != 0xFF01:
                        break
                    i += 1
                if group:
                    groups.append(group if len(group) > 1 else group[0])
            if groups:
                req_map[(t_id, subject, field)] = groups
    return req_map, keep_targets, unsupported


# ---------------------------------------------------------------------------
# Button grafts (mpq/datreq/Buttons.graft): the command cards.  Layout per
# GPTP's load_button_grafts: u32 button_count, set_count, stat_count, then
# that many Buttons (position u16, icon u16, req_id u32, act_id u32,
# req_var u16, act_var u16, req_str u16, act_str u16), then ButtonSets
# (buttons_in_set u32, first_button u32 - an index, connected_unit u32).
# The action ids index button_act_funcs[] in the same order as
# ButtonActions (button_functions.h).
# ---------------------------------------------------------------------------

kActTrain = 3
kActResearch = 22
kActUpgrade = 26
kActUseTech = 24
kActStim = 25
kActBuildSubunit = 19
kActMorphBuilding = 35
kActMorphUnit = 38

g_promoted = {}


kButtonActionNames = {
    0: "cancel_infestation", 1: "rally", 2: "select_larva", 3: "train",
    4: "cancel_last", 5: "tank_mode", 6: "siege_mode", 7: "cancel_construction",
    8: "cancel_morph", 9: "move", 10: "stop", 11: "attack", 12: "attack_suicide",
    13: "attack_building", 14: "carrier_move", 15: "carrier_stop", 16: "reaver_stop",
    17: "carrier_attack", 18: "reaver_attack", 19: "build_subunit", 20: "patrol",
    21: "hold", 22: "research", 23: "cancel_research", 24: "use_tech", 25: "stimpack",
    26: "upgrade", 27: "cancel_upgrade", 28: "cancel_addon", 29: "build_terran",
    30: "place_cop", 31: "build_protoss", 32: "create_addon", 33: "build_zerg",
    34: "place_nydus", 35: "building_morph", 36: "land", 37: "repair", 38: "unit_morph",
    39: "gather", 40: "return_cargo", 41: "burrow", 42: "unburrow", 43: "cloak",
    44: "decloak", 45: "lift", 46: "load", 47: "unload", 48: "archon_warp",
    49: "dark_archon_meld", 50: "recharge_shields", 51: "nuke", 52: "cancel_nuke",
    53: "heal", 54: "replay_slow", 55: "replay_play_pause", 56: "replay_fast",
    57: "cancel", 58: "cancel_place", 59: "change_buttons",
}


def read_hxm_unit_names(repo):
    """The original's 1.17 unit names by id, from port_hxm_names.h."""
    path = os.path.join(repo, "engine", "src117", "port", "data", "port_hxm_names.h")
    src = open(path).read()
    m = re.search(r"\{0, \"terran_marine\"\},(.*?)\n\};", src, re.S)
    pairs = [(0, "terran_marine")]
    for mm in re.finditer(r"\{(\d+), \"([a-z0-9_]+)\"\}", m.group(1)):
        pairs.append((int(mm.group(1)), mm.group(2)))
    names = [""] * 228
    for i, n in pairs:
        if i < 228:
            names[i] = n
    return names


def button_cards(mod_dir, tables, names, repo, researchable=frozenset()):
    """Decode Buttons.graft into cards: {card unit -> [buttons]}, plus notes."""
    path = os.path.join(mod_dir, "datreq", "Buttons.graft")
    if not os.path.exists(path):
        path = os.path.join(mod_dir, "datreq", "buttons.graft")
    if not os.path.exists(path):
        return {}, []
    raw = open(path, "rb").read()
    (n_buttons, n_sets, n_stats) = struct.unpack_from("<III", raw, 0)
    at = 12
    buttons = []
    for _ in range(n_buttons):
        pos, icon, req_id, act_id, req_var, act_var, req_str, act_str = \
            struct.unpack_from("<HHIIHHHH", raw, at)
        buttons.append((pos, icon, req_id, act_id, req_var, act_var))
        at += 20
    sets = []
    for _ in range(n_sets):
        count, first, unit = struct.unpack_from("<III", raw, at)
        sets.append((count, first, unit))
        at += 12

    hxm = read_hxm_unit_names(repo)
    orders = tables["orders"]

    def uref(v):
        if v in kNoneMarkers["units"] or v < kV117["units"]:
            return v
        if v < len(names["units"]):
            keep_targets.add(("units", v))
            return names["units"][v]
        return v

    def tref(v):
        if v == 44 or v < kV117["techs"]:
            return v
        if v < len(names["techs"]):
            keep_targets.add(("techs", v))
            return names["techs"][v]
        return v

    def upref(v):
        if v < kV117["upgrades"]:
            return v
        if v < len(names["upgrades"]):
            keep_targets.add(("upgrades", v))
            return names["upgrades"][v]
        return v

    def oref(v):
        if v == 189 or v < kV117["orders"]:
            return v
        if v < len(names["orders"]):
            keep_targets.add(("orders", v))
            return names["orders"][v]
        return v

    def order_of_tech(tech):
        for r in range(orders.rows):
            if orders.cols[14][r] == tech:  # tech column
                return r
        return -1

    keep_targets = set()
    cards = {}
    notes = []
    skipped = {}
    for unit, (count, first, _connected) in enumerate(sets):
        # the sets array is positional: set[i] is unit i's card, the way the
        # game indexes its button-set table; connected_unit is the original
        # game's field and the mod leaves it 0xFFFF
        if unit >= 3000:
            continue
        if count == 0:
            continue
        key = hxm[unit] if unit < 228 and hxm[unit] else \
            (names["units"][unit] if unit < len(names["units"]) else None)
        if key is None:
            continue
        card = []
        for b in buttons[first:first + count]:
            pos, icon, req_id, act_id, req_var, act_var = b
            entry = {"position": pos}
            if act_id == kActTrain:
                if act_var < kV117["units"]:
                    continue  # the vanilla card already trains 1.17's units
                entry["train"] = uref(act_var)
            elif act_id == kActResearch:
                if act_var not in researchable:
                    continue  # the loader needs the tech's research requires
                entry["research"] = tref(act_var)
            elif act_id == kActUpgrade:
                if act_var < kV117["upgrades"]:
                    continue  # the vanilla card already upgrades 1.17's
                entry["upgrade"] = upref(act_var)
            elif act_id in (kActMorphBuilding, kActMorphUnit):
                entry["morph"] = uref(act_var)
            elif act_id in (kActUseTech, kActStim):
                order = order_of_tech(act_var)
                if order < 0:
                    notes.append((key, f"tech {act_var} has no order; button skipped"))
                    continue
                if order < kV117["orders"]:
                    continue  # a vanilla order's button is on the card already
                entry["cast"] = oref(order)
                if orders.cols[1][order]:  # use_weapon_targeting
                    entry["target"] = True
            else:
                skipped[kButtonActionNames.get(act_id, hex(act_id))] = \
                    skipped.get(kButtonActionNames.get(act_id, hex(act_id)), 0) + 1
                continue
            card.append(entry)
        if card:
            cards[key] = card
            if unit >= kV117["units"]:
                keep_targets.add(("units", unit))
    for kind, n in sorted(skipped.items(), key=lambda kv: -kv[1]):
        notes.append((f"{n} buttons", f"kind {kind} has no card expression yet"))
    return cards, keep_targets, notes


# ---------------------------------------------------------------------------
# Iscripts: the mod ships complete text scripts (mpq/scripts/iscript_*.txt)
# in the engine's own format, but split into sections.  An image's script
# value = (section << 16) | IsId: terran 1, protoss 2, zerg 3, askosi 4,
# bloodsummon 5; the general and kogri files override the original's scripts
# in place (section 0, IsId = the vanilla script id).  The assembler appends
# every section script to the mod's iscript.txt with a Name (the Init
# label), and the images' script column names them.
# ---------------------------------------------------------------------------

kIscriptSections = {
    "iscript_terran.txt": 1,
    "iscript_protoss.txt": 2,
    "iscript_zerg.txt": 3,
    "iscript_askosi.txt": 4,
    "iscript_bloodsummon.txt": 5,
}

kIscriptArgTypes = {
    "playfram": "W", "playframtile": "W", "sethorpos": "B", "setvertpos": "B",
    "setpos": "BB", "wait": "B", "waitrand": "BB", "goto": "L",
    "imgol": "IMG BB", "imgul": "IMG BB", "imgolorig": "IMG", "switchul": "IMG",
    "imgoluselo": "IMG BB", "imguluselo": "IMG BB", "sprol": "SPR BB",
    "highsprol": "SPR BB", "lowsprul": "SPR BB", "uflunstable": "FLI",
    "spruluselo": "SPR BB", "sprul": "SPR BB", "sproluselo": "SPR B",
    "end": "", "setflipstate": "B", "playsnd": "SND", "playsndrand": "SND+",
    "playsndbtwn": "SND SND", "domissiledmg": "", "attackmelee": "W+",
    "followmaingraphic": "", "randcondjmp": "B L", "turnccwise": "B",
    "turncwise": "B", "turn1cwise": "", "turnrand": "B", "setspawnframe": "B",
    "sigorder": "B", "attackwith": "B", "attack": "", "castspell": "",
    "useweapon": "WPN", "move": "B", "gotorepeatattk": "", "engframe": "B",
    "engset": "B", "__2d": "", "nobrkcodestart": "", "nobrkcodeend": "",
    "ignorerest": "", "attkshiftproj": "B", "tmprmgraphicstart": "",
    "tmprmgraphicend": "", "setfldirect": "B", "call": "L", "return": "",
    "setflspeed": "W", "creategasoverlays": "B", "pwrupcondjmp": "L",
    "trgtrangecondjmp": "W L", "trgtarccondjmp": "W W L",
    "curdirectcondjmp": "W W L", "imgulnextid": "B B", "liftoffcondjmp": "L",
    "warpoverlay": "W", "orderdone": "B", "grdsprol": "SPR B B",
    "dogrddamage": "",
}


def parse_iscript_file(path):
    """A .txt iscript file: header blocks (.headerstart/.headerend) and label
    blocks. Returns a list of {is_id, fields, labels: [(name, lines)]}."""
    headers = []
    cur = None
    in_header = False
    cur_label = None
    labels = {}
    for raw in open(path, encoding="utf-8", errors="replace"):
        line = raw.rstrip("\n")
        if not line.strip() or line.strip().startswith("#"):
            continue
        if line.strip().startswith(".headerstart"):
            if cur is not None:
                headers.append(cur)
            cur = {"is_id": -1, "fields": [], "labels": []}
            in_header = True
            cur_label = None
            continue
        if line.strip().startswith(".headerend"):
            in_header = False
            continue
        if in_header and cur is not None:
            m = re.match(r"(\w+)\s+(\S.*)$", line)
            if m:
                key, val = m.group(1), m.group(2).strip()
                if key == "IsId":
                    cur["is_id"] = int(val)
                else:
                    cur["fields"].append((key, val))
            continue
        if cur is None:
            continue
        m = re.match(r"(\w+):\s*$", line)
        if m:
            cur_label = m.group(1)
            cur["labels"].append((cur_label, []))
            continue
        if line[0] in " \t" and cur_label is not None:
            cur["labels"][-1][1].append(line.strip())
    if cur is not None:
        headers.append(cur)
    return headers


def iscript_arg_transforms(names, tables, ref_fn, line):
    """Rewrite one opcode line's typed arguments to names where they reference
    extended rows."""
    parts = line.split(None, 1)
    if not parts:
        return line
    op = parts[0]
    rest = parts[1] if len(parts) > 1 else ""
    comment = ""
    if "#" in rest:
        rest, comment = rest.split("#", 1)
        comment = "#" + comment
    rest = rest.strip()
    if op == "unused_0c":
        return None  # 0x0C: the engine's compiler has no form; a no-op anyway
    sig = kIscriptArgTypes.get(op)
    if sig is None:
        return line  # unknown opcode: leave; the compiler reports it
    kinds = sig.split()
    vals = rest.split() if rest else []
    if len(kinds) != len(vals):
        return line
    out = []
    for kind, val in zip(kinds, vals):
        if kind in ("IMG", "SPR", "FLI", "SND", "WPN"):
            table_id = {"IMG": "images", "SPR": "sprites", "FLI": "flingy",
                        "SND": "sounds", "WPN": "weapons"}[kind]
            v = int(val, 0)
            if table_id == "images" and v in g_promoted:
                out.append(g_promoted[v])
                continue
            if v in kNoneMarkers.get(table_id, ()) or v < kV117[table_id]:
                out.append(val)
            elif v < len(names[table_id]):
                ref_fn(table_id, v)
                out.append(names[table_id][v])
            else:
                out.append(val)
        else:
            out.append(val)
    return "    " + op + (" " + " ".join(out) if out else "") + \
           ((" " + comment) if comment else "")


def _label_opcodes():
    return {"goto", "call", "randcondjmp", "pwrupcondjmp", "trgtrangecondjmp",
            "trgtarccondjmp", "curdirectcondjmp", "liftoffcondjmp",
            "imgolcondjmp", "imgulcondjmp", "overlaycondjmp"}


def _emit_script(parts, name, fields, labels, file_index, names, tables, ref_fn):
    """One script: its header (with label references prefixed) and its blocks."""
    parts.append(".headerstart")
    parts.append(f"Name          {name}")
    for key, val in fields:
        if key == "Type" and val.isdigit() and int(val) not in (
                0, 1, 2, 12, 13, 14, 15, 20, 21, 23, 24, 26, 27, 29):
            val = "2" if int(val) == 3 else "24"  # the mod's classes 3 and 25
        if key != "Name" and val != "[NONE]" and val in file_index:
            val = _label_name(file_index[val], val)
        parts.append(f"{key:<14} {val}")
    parts.append(".headerend")
    parts.append("")
    for label, lines in labels:
        parts.append(f"{_label_name(name, label)}:")
        for ln in lines:
            t = iscript_arg_transforms(names, tables, ref_fn, ln)
            if t is not None:
                t = _rewrite_label_args(t, name, file_index)
                parts.append(t)
        parts.append("")


def _label_name(owner, label):
    return f"{owner}__{label}"


def _rewrite_label_args(line, owner, file_index):
    line = line.strip()
    m = re.match(r"(\w+)(.*)$", line)
    if not m:
        return line
    op, rest = m.group(1), m.group(2)
    if op not in _label_opcodes():
        return line
    vals = rest.split()
    if not vals:
        return line
    if vals[-1] in file_index:
        vals[-1] = _label_name(file_index[vals[-1]], vals[-1])
    else:
        vals[-1] = _label_name(owner, vals[-1])
    return "    " + op + (" " + " ".join(vals) if vals else "")


def assemble_iscripts(mod_dir, tables, names, out_path, ref_fn):
    """Write the mod's iscript.txt; return the script-name maps
    {section: {IsId: name}} and the section-0 (vanilla rewrite) map."""
    notes = []
    script_map = {}  # section -> IsId -> name
    gen_map = {}     # IsId -> name: the general/kogri rewrites, loaded into
    #                  every section (and over the vanilla table at section 0)
    parts = []
    used = set()
    # labels are one global namespace across the files (the mod's terran
    # Minotaur even loops into the zerg Skryrokor's local block), so the
    # index maps a label to its first definition's final (de-duplicated) name
    all_headers = []  # (section or None, is_id, fields, labels)
    for fname, section in sorted(kIscriptSections.items(), key=lambda kv: kv[1]):
        path = os.path.join(mod_dir, "scripts", fname)
        if not os.path.exists(path):
            continue
        for h in parse_iscript_file(path):
            if h["is_id"] >= 0 and h["labels"]:
                all_headers.append((section, h["is_id"], h["fields"], h["labels"]))
    for fname in ("iscript_general.txt", "iscript_kogri.txt"):
        path = os.path.join(mod_dir, "scripts", fname)
        if not os.path.exists(path):
            continue
        for h in parse_iscript_file(path):
            if h["is_id"] >= 0 and h["labels"]:
                all_headers.append((None, h["is_id"], h["fields"], h["labels"]))
    # names first, in file order
    base_of = {}
    final_names = []
    for section, is_id, fields, labels in all_headers:
        base = labels[0][0]
        name = base
        i = 2
        while name in used:
            name = f"{base}_{i}"
            i += 1
        used.add(name)
        base_of[(section, is_id)] = name
        final_names.append(name)
    global_index = {}
    for (section, is_id, fields, labels), name in zip(all_headers, final_names):
        for label, _ in labels:
            global_index.setdefault(label, name)
    script_map = {}
    gen_map = {}
    for (section, is_id, fields, labels), name in zip(all_headers, final_names):
        if section is None:
            gen_map[is_id] = name
        else:
            script_map.setdefault(section, {})[is_id] = name
        _emit_script(parts, name, fields, labels, global_index, names, tables,
                     ref_fn)
    if out_path:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", encoding="utf-8") as f:
            f.write("\n".join(parts) + "\n")
        print(f"wrote {out_path} ({len(parts)} lines)")
    return script_map, gen_map, notes


# ---------------------------------------------------------------------------
# Art: an image's frames come from its grp value (1-based index into the
# mod's images.tbl; 0 = none).  Vanilla-range image rows the mod redefines
# art for cannot take art through an edit (the loader keeps an edit's grp),
# so those rows are promoted to new records that carry the file, and every
# reference to them resolves to the new record's name.
# ---------------------------------------------------------------------------

def read_images_tbl(path):
    d = open(path, "rb").read()
    n = struct.unpack_from("<H", d, 0)[0]
    out = []
    for i in range(n):
        off = struct.unpack_from("<H", d, 2 + 2 * i)[0]
        end = off
        while end < len(d) and d[end] != 0:
            end += 1
        out.append(d[off:end].decode("latin1") if off < len(d) else "")
    return out


def image_art(mod_dir, tables, vanilla_tbl_path):
    """{image row: art path} for rows whose art differs from the original's
    (or is a mod file), and the full path map for the copier."""
    tbl = read_images_tbl(os.path.join(mod_dir, "arr", "images.tbl"))
    vanilla = read_images_tbl(vanilla_tbl_path) if vanilla_tbl_path and \
        os.path.exists(vanilla_tbl_path) else []
    grp = tables["images"].cols[0]
    paths = {}
    for r, g in enumerate(grp):
        if g == 0 or g > len(tbl):
            paths[r] = ""
            continue
        paths[r] = tbl[g - 1]
    replaced = {}
    for r in range(kV117["images"]):
        p = paths[r]
        if not p:
            continue
        g = grp[r]
        v = vanilla[g - 1] if g - 1 < len(vanilla) else ""
        if p.lower() != v.lower():
            replaced[r] = p
    return paths, replaced


# ---------------------------------------------------------------------------
# The art copier: links the mod's media files into the mod's data folder
# (player-side, gitignored).  The mod's tree is lowercase on disk while the
# string tables keep the MPQ's mixed case, so a case-folded index resolves
# every path.
# ---------------------------------------------------------------------------

def tree_index(root):
    idx = {}
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            rel = os.path.relpath(os.path.join(dirpath, f), root)
            idx[rel.lower()] = rel
    return idx


def link_art(mod_dir, data_dir, tables, img_paths, do_link=os.symlink):
    """Link the referenced grps, sounds, portraits and console art into the
    mod's data folder.  Returns (linked, missing)."""
    root = mod_dir
    idx = tree_index(root)
    linked = []
    missing = []

    def link_one(src_rel, dst_rel):
        key = src_rel.replace("\\", "/").lower()
        if key not in idx:
            missing.append(dst_rel)
            return False
        dst = os.path.join(data_dir, dst_rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst):
            try:
                do_link(os.path.join(root, idx[key]), dst)
            except OSError:
                import shutil
                shutil.copyfile(os.path.join(root, idx[key]), dst)
        linked.append(dst_rel)
        return True

    # 1. image frames: the tbl paths sit under the MPQ's unit\ folder;
    # paths of the original's files are the player's own and are no miss
    tbl = read_images_tbl(os.path.join(mod_dir, "arr", "images.tbl"))
    vanilla = read_images_tbl(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                           "build", "vanilla-images.tbl"))
    grp = tables["images"].cols[0]
    vanilla_paths = {v.lower() for v in vanilla}
    for r, p in img_paths.items():
        if not p:
            continue
        if p.lower() in vanilla_paths:
            continue  # the player's install has it
        if not link_one(os.path.join("unit", p), "art/" + p.replace("\\", "/")):
            if not link_one(os.path.join("anim", p), "art/" + p.replace("\\", "/")):
                link_one(p, "art/" + p.replace("\\", "/"))

    # 2. sounds
    sfx = read_sfxdata_paths(mod_dir)
    t = tables["sounds"]
    filecol = t.cols[0]
    seen = set()
    for r, f in enumerate(filecol):
        if f == 0 or f >= len(sfx):
            continue
        s = sfx[f]
        if not s or s == "No sound":
            continue
        base = re.sub(r"\s*\(\d+\)\s*$", "", s)
        if base in seen:
            continue
        seen.add(base)
        link_one(os.path.join("sound", base), "sounds/" + base.replace("\\", "/"))

    # 3. portraits: link the files the tbl names, under the tbl's own names
    # (the engine adds the digit and tries the extensions), resolving the
    # source case-insensitively against the lowercase tree
    t = tables["portraits"]
    port_tbl = read_tbl_binary(os.path.join(mod_dir, "arr", "portdata.tbl"))
    idcol = t.cols[0]
    tkcol = t.cols[1]
    seen = set()
    for r in range(t.rows):
        for c in (idcol, tkcol):
            if r >= len(c) or c[r] >= len(port_tbl):
                continue
            s = port_tbl[c[r]]
            if not s or s in seen:
                continue
            seen.add(s)
            s2 = s.replace("\\", "/")
            base = ("portrait/" + s2).lower()
            fam = [v for k, v in idx.items()
                   if k == base + ".smk" or (k.startswith(base) and
                                              re.search(r"\.[a-z0-9]{2,4}$", v))]
            for v in fam:
                ext = os.path.splitext(v)[1]
                link_one(v, "portraits/" + s2 + ext)

    # 4. console art + hud.json
    for race, prefix in (("terran", "t"), ("zerg", "z"), ("protoss", "p")):
        for pcx in (f"{prefix}console.pcx", f"{prefix}console_ws.pcx",
                    f"{prefix}conover.pcx"):
            if ("game/" + pcx).lower() in idx:
                link_one("game/" + pcx, "art/game/" + pcx)
    hud = {"console": {"art": {"terran": "art/game/tconsole.pcx",
                               "zerg": "art/game/zconsole.pcx",
                               "protoss": "art/game/pconsole.pcx"}}}
    hud_path = os.path.join(data_dir, "hud.json")
    with open(hud_path, "w", encoding="utf-8") as f:
        json.dump(hud, f, indent=1)
    return linked, missing


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
    # upgrades: 61 is the original's "none" for the in-progress slot, but the
    # mod's row 61 is its first extended upgrade and its dat columns and
    # buttons reference the row, so no marker here
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
    r"""'tmarine\TMaFid0' -> 'portraits/tmarine/TMaFid' (the game adds the digit)."""
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
        if table_id == "orders" and r == 0xFFFF:
            return 189  # the mod's "no order" is 0xFFFF; the original's is 189
        if table_id == "images" and r in promoted:
            return promoted[r]  # its art was replaced: the new record has it
        if r < kV117[table_id]:
            return r
        if r < len(names[table_id]):
            return names[table_id][r]
        return r

    def fields_of(table_id, r, id_of=None):
        t = tables[table_id]
        row = t.row(r)
        out = {}
        iscript_missing = fields_of.missing
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
            if f.startswith("unknown_") or (table_id == "sounds" and f == "unknown"):
                continue
            if table_id == "images" and f == "grp":
                continue  # the mod's numeric grp index is meaningless to the
                # loader: an edit keeps its vanilla art, a new record takes
                # real art files (the art step, not the dat table)
            if table_id == "images" and f == "script":
                # the mod's script ids: each section's table is the vanilla
                # scripts plus the section's file and the general file,
                # addressed as (section << 16) | IsId
                sec = v >> 16
                iid = v & 0xFFFF
                if sec in script_map and iid in script_map[sec]:
                    out[f] = script_map[sec][iid]
                elif iid in gen_map:
                    out[f] = gen_map[iid]
                elif iid < 1000:
                    out[f] = iid
                else:
                    iscript_missing.append((t.names[r].strip() if r < len(t.names) else r, hex(v)))
                continue
            if table_id == "images" and f == "draw_function" and v >= 18:
                continue  # the mod's extended draw functions need engine
                # support; keep the base's until they are ported
            if table_id == "images" and f == "remapping" and v >= 8:
                continue  # the loader clamps remapping to 0..7
            if table_id == "images" and f in ("shield_overlay", "attack_overlay",
                                              "damage_overlay", "special_overlay",
                                              "landing_dust_overlay", "liftoff_dust_overlay"):
                # overlay columns have no ref table in this build: numbers only.
                # An extended (or promoted) image's overlay -> the loader's id.
                if v in promoted or v >= kV117["images"]:
                    key = promoted.get(v) or (names["images"][v] if v < len(names["images"]) else None)
                    if id_of is None or key is None or key not in id_of:
                        continue
                    out[f] = id_of[key]
                else:
                    out[f] = v
                continue
            if table_id == "sounds" and f == "file":
                s = sfx[v] if v < len(sfx) else ""
                out[f] = "sounds/" + s.replace("\\", "/") if s and s != "No sound" else None
                continue
            if table_id == "portraits" and f in ("idle", "talking"):
                s = port_tbl[v] if v < len(port_tbl) else ""
                out[f] = portrait_path(s) if s else None
                continue
            if table_id == "orders" and f == "animation" and v >= 28:
                continue  # the mod's extended animations need the iscript
                # step; keep the base's until then
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

    # The requirement grafts: tech trees and use requirements, and their
    # subjects and operands join the kept rows.
    req_map, graft_targets, graft_notes = graft_requires(mod_dir, tables, names)
    researchable = frozenset(r for (t, r, f) in req_map if t == "techs" and f == "requires")
    cards, card_targets, card_notes = button_cards(mod_dir, tables, names, REPO, researchable)
    graft_targets.update(card_targets)

    # Art: the rows whose frames the mod replaces (or gives of its own) become
    # new records that carry the file, and references name them.
    vanilla_tbl = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "build", "vanilla-images.tbl")
    img_paths, img_replaced = image_art(mod_dir, tables, vanilla_tbl)
    promoted = {r: names["images"][r] for r in img_replaced}
    vanilla_img_paths = {v.lower() for v in read_images_tbl(vanilla_tbl)}
    global g_promoted
    g_promoted = promoted

    # The iscripts: assembled beside types.json; images name the scripts.
    iscript_targets = set()

    def iscript_ref(t_id, v):
        if v < tables[t_id].rows:
            iscript_targets.add((t_id, v))

    script_map, gen_map, iscript_notes = assemble_iscripts(
        mod_dir, tables, names, os.path.join(MOD_DATA, "iscript.txt"), iscript_ref)
    graft_targets.update(iscript_targets)

    # the mod's own stat_txt: data/stat_txt.txt, one string per line, "<0>" empty
    stat_path = os.path.join(mod_dir, "rez", "stat.txt")
    if os.path.exists(stat_path):
        lines = []
        for ln in open(stat_path, encoding="utf-8", errors="replace"):
            ln = ln.rstrip("\n")
            lines.append("" if ln == "<0>" else ln)
        with open(os.path.join(MOD_DATA, "stat_txt.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"wrote {os.path.join(MOD_DATA, 'stat_txt.txt')} ({len(lines)} strings)")
    fields_of.missing = []

    def refs_of(table_id, r):
        t = tables[table_id]
        row = t.row(r)
        out = set()
        for (f, _, ref_table) in t.widths:
            if not ref_table:
                # the images' overlay columns have no ref table in the build
                # but still reference the images table's rows
                if table_id == "images" and f in (
                        "shield_overlay", "attack_overlay", "damage_overlay",
                        "special_overlay", "landing_dust_overlay",
                        "liftoff_dust_overlay"):
                    ref_table = "images"
                else:
                    continue
            if ref_table not in kV117:
                continue
            v = row[f]
            if v in kNoneMarkers.get(ref_table, ()) or v < kV117[ref_table]:
                continue
            if v < tables[ref_table].rows:
                out.add((ref_table, v))
        return out

    # Rows kept as records: real extended rows, the grafts' subjects and
    # operands, plus the closure of what the edits and the kept rows
    # reference.
    keep = {t_id: set(real[t_id]) for t_id in tables}
    for (t_id, v) in graft_targets:
        if v < tables[t_id].rows:
            keep[t_id].add(v)
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
    # The loader's ids for the mod's new records: nModFirst + the record's
    # place among the new records sorted by name.  Overlay refs (which the
    # build only accepts as numbers) use these.
    kModFirst = {"units": 256, "weapons": 131, "flingy": 209, "sprites": 517,
                 "images": 999, "orders": 190, "techs": 45, "upgrades": 62,
                 "sounds": 1144, "portraits": 110}
    id_of = {}
    for t_id in tables:
        if t_id == "mapdata":
            continue
        rows = {r for r in keep[t_id] if r >= kV117[t_id]}
        if t_id == "images":
            rows.update(promoted)
        keys = sorted(names[t_id][r] for r in rows)
        id_of[t_id] = {k: kModFirst[t_id] + i for i, k in enumerate(keys)}
    for t_id in tables:
        if t_id == "mapdata":
            continue  # not a table the mod-data loader reads
        t = tables[t_id]
        v117 = kV117[t_id]
        recs = {}
        for r in range(v117):
            if t_id == "images" and r in promoted:
                continue  # its art was replaced: a new record carries it
            rec = {"edit": r, **fields_of(t_id, r, id_of.get(t_id))}
            if t_id in ("sounds", "portraits"):
                rec = {k: v for k, v in rec.items() if v is not None}
            if is_real_name(t, r):
                rec["name"] = t.names[r].strip()
            for fld in ("requires", "use_requires"):
                if (t_id, r, fld) in req_map:
                    rec[fld] = req_map[(t_id, r, fld)]
            recs[names[t_id][r]] = rec
        emit_rows = set(keep[t_id])
        if t_id == "images":
            emit_rows.update(promoted)
        for r in sorted(emit_rows):
            if r < v117 and not (t_id == "images" and r in promoted):
                continue
            # a new record's base must be a row the loader can find: an
            # original row by number (edit records' names do not resolve)
            rec = {"base": 0 if r >= v117 else r}
            flds = fields_of(t_id, r, id_of.get(t_id))
            if t_id == "images" and img_paths.get(r) and \
                    img_paths[r].lower() not in vanilla_img_paths:
                flds["grp"] = "art/" + img_paths[r].replace("\\", "/")
            if is_real_name(t, r):
                flds["name"] = t.names[r].strip()
            if t_id == "sounds":
                # a sound without a file plays nothing; drop empty columns
                flds = {k: v for k, v in flds.items() if v is not None}
                if not flds.get("file") and not is_real_name(t, r):
                    continue
            if t_id == "portraits":
                flds = {k: v for k, v in flds.items() if v is not None}
            for fld in ("requires", "use_requires"):
                if (t_id, r, fld) in req_map:
                    flds[fld] = req_map[(t_id, r, fld)]
            rec.update(flds)
            recs[names[t_id][r]] = rec
        doc[t_id] = recs
        stats[t_id] = len(recs)
    for note in graft_notes[:40]:
        print(f"graft note: {note[0]} {note[1]}: {note[2]}")
    if len(graft_notes) > 40:
        print(f"graft note: ... {len(graft_notes) - 40} more")
    for note in card_notes:
        print(f"cards note: {note[0]}: {note[1]}")
    if fields_of.missing:
        print(f"iscript note: {len(fields_of.missing)} images name scripts with no "
              f"assembled form (kept their base's): {fields_of.missing[:10]}")
    if cards:
        doc["cards"] = cards

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
    elif cmd == "art":
        tables = load_all(mod_dir)
        apply_ext(tables, mod_dir)
        apply_rewrites(tables, mod_dir)
        vanilla_tbl = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "build", "vanilla-images.tbl")
        img_paths, _ = image_art(mod_dir, tables, vanilla_tbl)
        linked, missing = link_art(mod_dir, MOD_DATA, tables, img_paths)
        print(f"art: linked {len(linked)}, missing {len(missing)}")
        for m in missing[:12]:
            print("  missing:", m)
    elif cmd == "grafts":
        tables = load_all(mod_dir)
        apply_ext(tables, mod_dir)
        apply_rewrites(tables, mod_dir)
        apply_plus_yml(tables, mod_dir)
        names = build_names(tables)
        req_map, targets, notes = graft_requires(mod_dir, tables, names)
        print(f"{len(req_map)} graft requirement runs decoded; "
              f"{len(targets)} rows they touch; {len(notes)} notes")
        for n in notes[:40]:
            print(f"  note: {n[0]} {n[1]}: {n[2]}")
    elif cmd == "check":
        tables = load_all(mod_dir)
        verify(tables)


if __name__ == "__main__":
    main()
