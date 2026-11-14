"""Classic StarCraft command packing for the deterministic native tests.

Only Python's standard library is used.  These packets go through the sandbox
`turn` command; none is queued through the UI, Pluto, or an inference process.
"""
import struct


UNIT = {
    "marine": 0,
    "scv": 7,
    "science_vessel": 9,
    "larva": 35,
    "egg": 36,
    "zergling": 37,
    "drone": 41,
    "overlord": 42,
    "dark_templar": 61,
    "command_center": 106,
    "supply_depot": 109,
    "hatchery": 131,
    "lair": 132,
    "mineral_field_1": 176,
    "start_location": 214,
    "none": 228,
}

ORDER = {
    "stop": 1,
    "player_guard": 3,
    "move": 6,
    "attack_unit": 10,
    "attack_move": 14,
    "place_building": 30,
    "constructing_building": 33,
    "train": 38,
    "zerg_birth": 41,
    "zerg_unit_morph": 42,
    "zerg_building_morph": 43,
    "incomplete_building": 44,
    "incomplete_morphing": 45,
    "follow": 49,
    "larva": 77,
    "move_to_minerals": 85,
    "wait_for_minerals": 86,
    "mining_minerals": 87,
    "return_minerals": 90,
    "right_click_action": 151,
    "patrol": 152,
    "guard_post": 156,
}


def integer(name, value, low, high):
    """Return an integral packet field, rejecting bools and silent truncation."""
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in {low}..{high}, got {value!r}")
    return value


def u16(name, value):
    return integer(name, value, 0, 0xFFFF)


def select(*unit_ids):
    """0x09 Select: replace the active player's selection (12 units at most)."""
    integer("selection count", len(unit_ids), 1, 12)
    payload = bytearray((0x09, len(unit_ids)))
    for unit_id in unit_ids:
        payload += struct.pack("<H", u16("unit id", unit_id))
    return bytes(payload)


def right_click(x, y, target_id=0, target_type=UNIT["none"], queued=0):
    """0x14 RightClick, including the target's packed id and target type."""
    return struct.pack(
        "<BHHHHB",
        0x14,
        u16("x", x),
        u16("y", y),
        u16("target id", target_id),
        u16("target type", target_type),
        integer("queued", queued, 0, 1),
    )


def action(x, y, order, target_id=0, target_type=UNIT["none"], queued=0):
    """0x15 Action: RightClick's fields plus an explicit classic order id."""
    return struct.pack(
        "<BHHHHBB",
        0x15,
        u16("x", x),
        u16("y", y),
        u16("target id", target_id),
        u16("target type", target_type),
        integer("order", order, 0, 0xFF),
        integer("queued", queued, 0, 1),
    )


def word_command(opcode, value):
    integer("opcode", opcode, 0, 0xFF)
    return struct.pack("<BH", opcode, u16("argument", value))


def build(unit_type):
    """0x1F Build: put a unit type in the selected producer's queue."""
    return word_command(0x1F, unit_type)


def morph(unit_type):
    """0x23 Morph: larva/mutalisk morph, with the destination unit type."""
    return word_command(0x23, unit_type)


def upgrade_building(unit_type):
    """0x35 UpgradeBldg: hatchery to lair, lair to hive, and so on."""
    return word_command(0x35, unit_type)


def placebox(order, unit_type, tile_x, tile_y):
    """0x0C Placebox.  Coordinates are tile indices, not pixel coordinates."""
    return struct.pack(
        "<BBHHH",
        0x0C,
        integer("order", order, 0, 0xFF),
        u16("tile x", tile_x),
        u16("tile y", tile_y),
        u16("unit type", unit_type),
    )


def kill_egg():
    """0x19 KillEgg: cancel every selected incomplete unit of the active player."""
    return b"\x19"


def stop(queued=0):
    return struct.pack("<BB", 0x1A, integer("queued", queued, 0, 1))


def alliance_word(player, allied_with=()):
    """Pack twelve two-bit alliance entries, with allied victory on the named slots."""
    integer("player", player, 0, 11)
    others = set(allied_with)
    for slot in others:
        integer("allied slot", slot, 0, 11)
    others.add(player)
    word = 0
    for slot in others:
        word |= 2 << (2 * slot)
    return word


def set_allies(player, allied_with=()):
    """0x0E SetAllies for one active player and its chosen allies."""
    return struct.pack("<BI", 0x0E, alliance_word(player, allied_with))


def turn_bytes(*parts):
    """Concatenate commands for one call, without changing their boundaries."""
    payload = b"".join(parts)
    integer("turn byte count", len(payload), 0, 65535)
    return payload
