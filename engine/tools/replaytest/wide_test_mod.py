"""The hxsuite's wide-test mod: the hybrid-test mod with rows of padding beside its own,
so that its techs, upgrades, orders, weapons and flingy are past 255, its sprite past 1,023 and
its images past 2,047, with 300 more unit types after the Hybrid Reaver, which stays at 256,
where the Hybrid Test map places it. A table's records take their ids in the order of their
names, so the padding's names sort before the hybrid's, and the units' after. Its image
scripts have 200 KiB of scripts no image runs before the hybrid's, which so run past 64 KiB
.

It goes to testrun/test_mods/wide-test/, beside the suite's maps and like them kept local: a
mod in engine/mods/ ships to players (tools/package.py). The harness copies it into a run's
data folder as it does engine/mods' (replaytest.py, hx_mod_args).

    python tools/replaytest/wide_test_mod.py
"""
import json
import shutil
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[2]
SRC = ENGINE / "mods" / "hybrid-test"
DST = ENGINE / "testrun" / "test_mods" / "wide-test"

# each table's first mod row, and the id the hybrid's first row of it takes
FIRST = {"techs": 45, "upgrades": 62, "orders": 190, "weapons": 131, "flingy": 209, "sprites": 517, "images": 999}
AT = {"techs": 256, "upgrades": 256, "orders": 260, "weapons": 260, "flingy": 268, "sprites": 1100, "images": 2100}
PAD_SCRIPTS, PAD_WAITS = 100, 1024  # about 200 KiB of scripts
BASE = {"techs": 19, "upgrades": "terran_infantry_armor", "orders": "attack_unit", "weapons": "gauss_rifle",
        "flingy": "terran_marine", "sprites": "terran_marine", "images": "terran_marine"}


def main():
    src = json.loads((SRC / "data" / "types.json").read_text(encoding="utf-8"))
    out = {}
    for table, rows in src.items():
        if table in AT:
            pad = {"aa_pad_%s_%04d" % (table, i): {"base": BASE[table]} for i in range(AT[table] - FIRST[table])}
            out[table] = {**pad, **rows}
        elif table == "units":
            pad = {"zz_pad_unit_%03d" % i: {"base": "terran_marine", "name": "Padding %d" % i} for i in range(300)}
            out[table] = {**rows, **pad}
        else:
            out[table] = rows
    shutil.rmtree(DST / "data", ignore_errors=True)
    (DST / "data").mkdir(parents=True)
    pad = []
    for i in range(PAD_SCRIPTS):
        pad += [".headerstart", "Name aa_pad_script_%03d" % i, "Type 0", "Init aa_pad_%03d" % i,
                "Death aa_pad_%03d" % i, ".headerend", "aa_pad_%03d:" % i, "  useweapon gauss_rifle"]
        pad += ["  wait 1"] * PAD_WAITS + ["  goto aa_pad_%03d" % i, ""]
    script = (SRC / "data" / "iscript.txt").read_text(encoding="utf-8")
    (DST / "data" / "iscript.txt").write_text("\n".join(pad) + "\n" + script, encoding="utf-8", newline="\r\n")
    # the hybrid's art where this machine has it, the player's own (tools/hxunit.py)
    for f in (SRC / "data" / "art").glob("*.grp"):
        (DST / "data" / "art").mkdir(exist_ok=True)
        shutil.copy2(f, DST / "data" / "art" / f.name)
    (DST / "data" / "types.json").write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8", newline="\r\n")
    mod = json.loads((SRC / "mod.json").read_text(encoding="utf-8"))
    mod.update({"id": "wide-test", "name": "Wide Test",
                "line": "The Hybrid Test's unit with its techs, upgrades, orders, weapons, flingy, sprite and "
                        "images past 1.17's limits, to test with."})
    for k in ("modes", "channel"):
        mod.pop(k, None)
    mod["holds"] = ["units"]
    (DST / "mod.json").write_text(json.dumps(mod, indent=2) + "\n", encoding="utf-8", newline="\r\n")
    print(f"wide-test -> {DST}: " + ", ".join(f"{len(v)} {t}" for t, v in out.items() if isinstance(v, dict)))


if __name__ == "__main__":
    main()
