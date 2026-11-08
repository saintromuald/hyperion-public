"""Self-contained deterministic HXM fixtures for the native engine scenarios."""
import io
import json
import os
import struct
import zipfile


HXM_TYPE = {
    "marine": "terran_marine",
    "scv": "terran_scv",
    "science_vessel": "terran_science_vessel",
    "larva": "zerg_larva",
    "drone": "zerg_drone",
    "overlord": "zerg_overlord",
    "dark_templar": "protoss_dark_templar",
    "command_center": "terran_command_center",
    "supply_depot": "terran_supply_depot",
    "hatchery": "zerg_hatchery",
    "lair": "zerg_lair",
    "spawning_pool": "zerg_spawning_pool",
    "mineral_field_1": "mineral_field_1",
}

WIDTH = HEIGHT = 256
BASE_MAP = os.environ.get(
    "HYPERION_NATIVE_BASE_MAP",
    "/home/saintbellarmine/Games/hyperion/Data/maps/Hyperion/Big Game Hunters 2x.hxm",
)
_BASE = None
REQUIRED_OVERRIDE_FIELDS = {"hp", "shields", "armor", "build_time", "minerals", "gas"}


def base_package():
    """Known-good terrain, metadata and default rules for a playable UMS fixture."""
    global _BASE
    if _BASE is None:
        with zipfile.ZipFile(BASE_MAP) as package:
            _BASE = (json.loads(package.read("map.json")),
                     {name: package.read(name) for name in package.namelist() if name != "map.json"})
    return _BASE
REQUIRED_OVERRIDE_FIELDS = {"hp", "shields", "armor", "build_time", "minerals", "gas"}


def unit(kind, x, y, owner=0, **properties):
    # The reference base around (384, 384) is buildable and supports creep;
    # (512, 512) in the source map is not a valid hatchery site.
    record = {"type": HXM_TYPE.get(kind, kind), "x": x - 128, "y": y - 128, "owner": owner}
    record.update(properties)
    return record


def start(owner, x=256, y=256):
    return {"type": "start_location", "x": x, "y": y, "owner": owner}


def starts():
    return [start(0, 384, 240), start(1, 2368, 304)]


def resource_trigger():
    """One-shot initial balances; no repeating trigger affects later assertions."""
    return {
        "players": [0, 1],
        "conditions": [{"type": "always"}],
        "actions": [
            {"type": "set_resources", "player": "current_player", "number": 1000,
             "unit": 0, "modifier": 7},
            {"type": "set_resources", "player": "current_player", "number": 500,
             "unit": 1, "modifier": 7},
        ],
    }


def make_document(name, units, races=("zerg", "terran"), allied=False, settings=None, shared_vision=True):
    document, _ = base_package()
    document = json.loads(json.dumps(document))  # deep copy of the known-good template
    force_flags = ["allies", "allied_victory"] if allied else []
    if allied and shared_vision:
        force_flags.append("shared_vision")
    document.update({
        "name": f"Native deterministic test: {name}",
        "description": "Phase 2 native test fixture; no AI or inference",
        "players": [
            {"slot": "human", "race": races[0], "force": 0},
            {"slot": "computer", "race": races[1], "force": 0 if allied else 1},
        ],
        "forces": [
            {"name": "Test force A", "flags": force_flags},
            {"name": "Test force B", "flags": []},
        ],
        "units": starts() + units,
        "locations": [],
        "triggers": [resource_trigger()],
    })
    if settings:
        document["settings"] = {**document.get("settings", {}), **settings}
    return document


def archive(document):
    """Write a playable, byte-identical, self-contained archive on every call."""
    _, assets = base_package()
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as package:
        rows = [("map.json", (json.dumps(document, sort_keys=True, indent=1, allow_nan=False) + "\n").encode())]
        rows += sorted(assets.items())
        for name, data in rows:
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o600 << 16
            package.writestr(info, data)
    return buffer.getvalue()


def hatchery_only(name, settings=None, associated_larvae=False):
    units = [unit("hatchery", 512, 368)]
    if associated_larvae:
        # The map loader links these to the nearby hatchery, as with its ordinary
        # initial larva. Unrelated production tests need not wait for two spawns.
        units += [unit("larva", 500, 400), unit("larva", 524, 400)]
    return archive(make_document(name, units, settings=settings))


def one_worker_mining_and_returning():
    return archive(make_document(
        "one worker mining and returning",
        [unit("command_center", 512, 368), unit("scv", 576, 368),
         unit("mineral_field_1", 640, 368, owner="neutral", resources=80)],
        races=("terran", "terran"),
    ))


def repeated_worker_replacement_after_losses():
    marines = [unit("marine", 512 + (i % 4) * 16, 480 + (i // 4) * 64, owner=1) for i in range(8)]
    return archive(make_document(
        "repeated worker replacement after losses",
        [unit("hatchery", 512, 368), unit("larva", 500, 400), unit("larva", 524, 400)] + marines,
        allied=True,
    ))


def larva_to_drone():
    return hatchery_only("larva to drone")


def larva_hatchery_association_and_spawning():
    return hatchery_only("larva hatchery association and spawning")


def egg_completion():
    return hatchery_only("egg completion")


def hatchery_to_lair():
    return archive(make_document(
        "hatchery to lair",
        [unit("hatchery", 512, 368), unit("spawning_pool", 576, 368)],
    ))


def building_placement_and_completion():
    return archive(make_document(
        "building placement and completion",
        [unit("scv", 528, 528)],
        races=("terran", "terran"),
    ))


def supply_production_and_cap_handling():
    return hatchery_only("supply production and cap handling", associated_larvae=True)


def small_army_attack_kills_target():
    marines = [unit("marine", 512 + (i % 4) * 16, 480 + (i // 4) * 64) for i in range(8)]
    return archive(make_document(
        "small army attack kills target",
        marines + [unit("scv", 576, 512, owner=1)],
        races=("terran", "terran"),
        allied=True,
    ))


def attack_vs_follow_and_patrol():
    return archive(make_document(
        "attack versus follow and patrol",
        [unit("marine", 512, 512), unit("scv", 576, 512, owner=1)],
        races=("terran", "terran"),
        allied=True,
    ))


def weapon_cooldown_facing_velocity_and_order_targets():
    return archive(make_document(
        "weapon cooldown facing velocity and order targets",
        [unit("marine", 576, 368), unit("hatchery", 512, 448, owner=1)],
        races=("terran", "zerg"),
        allied=True,
    ))


def team_and_alliance():
    return archive(make_document(
        "team and alliance",
        [unit("marine", 512, 512), unit("marine", 576, 512, owner=1)],
        races=("terran", "terran"),
        allied=True,
    ))


def visibility_vs_cloaked_detection():
    return archive(make_document(
        "visibility versus cloaked detection",
        [unit("marine", 512, 512), unit("science_vessel", 1024, 512),
         unit("dark_templar", 576, 512, owner=1, state=1, valid_state=1),
         unit("dark_templar", 1088, 512, owner=1, state=1, valid_state=1),
         unit("dark_templar", 1600, 512, owner=1, state=1, valid_state=1)],
        races=("terran", "protoss"),
        allied=True,
        shared_vision=False,
    ))


SCENARIOS = {
    "one_worker_mining_and_returning": one_worker_mining_and_returning,
    "repeated_worker_replacement_after_losses": repeated_worker_replacement_after_losses,
    "larva_to_drone": larva_to_drone,
    "larva_hatchery_association_and_spawning": larva_hatchery_association_and_spawning,
    "egg_completion": egg_completion,
    "hatchery_to_lair": hatchery_to_lair,
    "building_placement_and_completion": building_placement_and_completion,
    "supply_production_and_cap_handling": supply_production_and_cap_handling,
    "small_army_attack_kills_target": small_army_attack_kills_target,
    "attack_vs_follow_and_patrol": attack_vs_follow_and_patrol,
    "weapon_cooldown_facing_velocity_and_order_targets": weapon_cooldown_facing_velocity_and_order_targets,
    "team_and_alliance": team_and_alliance,
    "visibility_vs_cloaked_detection": visibility_vs_cloaked_detection,
}


def fixture_bytes(name):
    if name not in SCENARIOS:
        raise KeyError(f"no native fixture named {name}")
    return SCENARIOS[name]()


def fixture_document(name):
    """Decode a generated document for pure-Python validation and tooling."""
    with zipfile.ZipFile(io.BytesIO(fixture_bytes(name))) as package:
        return json.loads(package.read("map.json"))
