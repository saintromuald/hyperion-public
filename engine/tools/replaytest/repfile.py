"""Read a 1.16/1.17 replay's recorded commands, and find the frames 1.17 loses.

A replay is a series of sections, each written by compress (src117/lang/compress.cpp):
a CRC, a block count, then per 0x2000-byte block its size and the block, PKWARE
imploded unless that did not make it smaller. The first four sections are the
"reRS" magic, the 0x279-byte header, the command stream's length and the command
stream: records of u32 frame, u8 size, then that many bytes of (u8 storm id, command)
pairs, several records to a frame when a frame has more than one record's worth.

lost_frames() emulates how net_mgr_replay_turns reads them (docs/replay-test.md, "The restart
in M1G2-Leta"): a frame read one call early whose commands grow the turn buffer past
0x200 bytes is played by the original from stale memory. Hyperion plays the recorded
commands (src117/port/replays/port_replay.cpp), so from the first such frame on its playback
and the original's differ, and the original's is the wrong one.

header() reads the map's name and the players from the 0x279-byte header (LoadReplayData,
replay.cpp), and quits() the frames at which players left (command 0x57, recv_net_cmd_Quit),
for compare.py's account of a game's result.

    python tools/replaytest/repfile.py <replay> [...]    lists each replay's lost frames
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# PKWARE DCL explode, for the binary (uncoded literal) mode StarCraft writes with
# (comp_implode passes type 0). Written from the format: a two-byte header (literal
# mode, dictionary bits), then an LSB-first bit stream of literals and
# length/distance pairs, with Huffman codes stored bit-inverted.

_MAXBITS = 13
# Code lengths, run-length packed: the high nibble plus one is the run, the low
# nibble the length.
_LEN_CODE = (2, 35, 36, 53, 38, 23)
_DIST_CODE = (2, 20, 53, 230, 247, 151, 248)
_LEN_BASE = (3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264)
_LEN_EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8)


def _huffman(packed):
    lengths = []
    for b in packed:
        lengths += [b & 15] * ((b >> 4) + 1)
    count = [0] * (_MAXBITS + 1)
    for n in lengths:
        count[n] += 1
    offs = [0] * (_MAXBITS + 2)
    for n in range(1, _MAXBITS + 1):
        offs[n + 1] = offs[n] + count[n]
    symbol = [0] * len(lengths)
    for s, n in enumerate(lengths):
        if n:
            symbol[offs[n]] = s
            offs[n] += 1
    return count, symbol


_LENS = _huffman(_LEN_CODE)
_DISTS = _huffman(_DIST_CODE)


def explode(data: bytes) -> bytes:
    pos = 0
    buf = 0
    cnt = 0

    def bits(n):
        nonlocal pos, buf, cnt
        while cnt < n:
            buf |= data[pos] << cnt
            pos += 1
            cnt += 8
        v = buf & ((1 << n) - 1)
        buf >>= n
        cnt -= n
        return v

    def decode(table):
        count, symbol = table
        code = first = index = 0
        for n in range(1, _MAXBITS + 1):
            code |= bits(1) ^ 1
            c = count[n]
            if code < first + c:
                return symbol[index + code - first]
            index += c
            first = (first + c) << 1
            code <<= 1
        raise ValueError("bad Huffman code")

    lit = bits(8)
    dict_bits = bits(8)
    if lit != 0 or not 4 <= dict_bits <= 6:
        raise ValueError(f"unsupported implode header {lit} {dict_bits}")
    out = bytearray()
    while True:
        if bits(1):
            sym = decode(_LENS)
            length = _LEN_BASE[sym] + bits(_LEN_EXTRA[sym])
            if length == 519:
                return bytes(out)
            s = 2 if length == 2 else dict_bits
            dist = (decode(_DISTS) << s) + bits(s) + 1
            if dist > len(out):
                raise ValueError("distance before the start of the output")
            start = len(out) - dist
            for i in range(length):
                out.append(out[start + i])
        else:
            out.append(bits(8))


# ---------------------------------------------------------------------------
# Sections and records.

def _section(f, length: int) -> bytes:
    if length == 0:
        return b""  # compress writes nothing for an empty section, as in a game with no commands
    crc, nblocks = struct.unpack("<II", f.read(8))
    data = bytearray()
    for i in range(nblocks):
        size, = struct.unpack("<I", f.read(4))
        chunk = min(0x2000, length - i * 0x2000)
        raw = f.read(size)
        data += raw if size == chunk else explode(raw)
    return bytes(data)


def read_commands(path) -> bytes:
    """The replay's command stream."""
    with open(path, "rb") as f:
        # reRS is 1.17's; hxRS a Hyperion game's, the same blocks with its sections after
        if _section(f, 4) not in (b"reRS", b"hxRS"):
            raise ValueError("not a replay")
        _section(f, 0x279)
        n, = struct.unpack("<I", _section(f, 4))
        return _section(f, n)


RACES = {0: "Zerg", 1: "Terran", 2: "Protoss", 5: "Random", 6: "Random"}
# A slot's type: the ones that play (net_cmds.h, PlayerData; 1 and 5 are computers)
PLAYING_TYPES = {1: "computer", 2: "human", 5: "computer"}


def _text(b: bytes) -> str:
    """A name from the header: Korean names are CP949, and a map's name may hold colour codes."""
    b = b.split(b"\0")[0]
    for enc in ("utf-8", "cp949", "latin-1"):
        try:
            t = b.decode(enc)
            break
        except UnicodeDecodeError:
            continue
    return "".join(c for c in t if c >= " ").strip()


def header(path) -> dict:
    """The replay's header: its magic, frame count, map and the slots that play. The game's
    header (sgReplayHeader_g) starts at 0x14, its 12 player slots of 36 bytes (PlayerData)
    at 0xA1. Raises ValueError for a file that is not a 1.16/1.17 or Hyperion replay."""
    with open(path, "rb") as f:
        magic = _section(f, 4)
        if magic not in (b"reRS", b"hxRS"):
            raise ValueError("not a 1.16 or 1.17 replay")
        h = _section(f, 0x279)
    players = []
    for i in range(12):
        s = h[0xA1 + 36 * i:0xA1 + 36 * (i + 1)]
        slot, storm = struct.unpack_from("<Ii", s, 0)
        name = _text(s[11:36])
        if s[8] in PLAYING_TYPES and name:
            players.append({"slot": i, "storm": storm, "type": PLAYING_TYPES[s[8]],
                            "race": RACES.get(s[9], f"race {s[9]}"), "team": s[10], "name": name})
    return {"magic": magic.decode(), "expansion": bool(h[0]), "frames": struct.unpack_from("<I", h, 1)[0],
            "map": _text(h[0x61:0x7B]), "size": struct.unpack_from("<HH", h, 0x34), "players": players}


def quits(path, sizes: list[int]) -> list[tuple[int, int]]:
    """(frame, storm id) of each player's quit (0x57) the replay recorded, in order."""
    out = []
    for frame, body in records(read_commands(path)):
        i = 0
        while i < len(body):
            n = _size(body, i + 1, sizes)
            if body[i + 1] == 0x57:
                out.append((frame, body[i]))
            if n <= 0:
                break
            i += 1 + n
    return out


def records(cmds: bytes):
    """(frame, body) for each record, in file order."""
    i = 0
    while i < len(cmds):
        frame, size = struct.unpack_from("<IB", cmds, i)
        yield frame, cmds[i + 5:i + 5 + size]
        i += 5 + size


# ---------------------------------------------------------------------------
# The command sizes are the original's own table, cmd_size_bytes_g (0x4EF048,
# 118 ints), read from the exe, with GetCommandByteSize's rule for the three
# selection commands (count * 2 + 2). Hyperion's commands of wide unit ids
# (src117/h/net_cmds.h) are 1.17's with the top bit set and a DWORD
# for each id; only a Hyperion replay has them.

CMD_SIZES_VA = 0x4EF048
CMD_SIZES_N = 118


def command_sizes(exe) -> list[int]:
    data = Path(exe).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    rva = CMD_SIZES_VA - base
    for s in range(nsec):
        h = pe + 24 + opt + 40 * s
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, h + 8)
        if va <= rva < va + vsize:
            return list(struct.unpack_from(f"<{CMD_SIZES_N}i", data, rawptr + rva - va))
    raise ValueError("cmd_size_bytes_g is not in any section")


# and the commands of a mod's order, tech and upgrade past 255: Action 0x96,
# Placebox 0x8D, Research 0x97, Upgrade 0x98
WIDE_ID_SIZES = {0x94: 12, 0x95: 13, 0xA9: 5, 0x96: 14, 0x8D: 9, 0x97: 3, 0x98: 3}


def _size(body: bytes, i: int, sizes: list[int]) -> int:
    op = body[i]
    if 9 <= op <= 11:
        return body[i + 1] * 2 + 2
    if 0x89 <= op <= 0x8B:
        return body[i + 1] * 4 + 2
    if op == 0x8C:  # a chunk of Hyperion's selection (net_cmds.h, NET_CMD_SELECT_CHUNK)
        return body[i + 1] * 4 + 3
    if op in WIDE_ID_SIZES:
        return WIDE_ID_SIZES[op]
    return sizes[op] if op < len(sizes) else 0


def lost_frames(path, sizes: list[int]) -> list[int]:
    """Frames the original plays partly from stale memory, in order.

    net_record_read_turn reads all the records of the next frame into the turn buffer,
    growing it to the running byte count plus the record's size whenever that
    reaches its capacity (0x200 at every call). net_mgr_replay_turns reads the next frame on
    the call after the last one it read (dword_508D68 starts at -1); when that
    frame is later than the current one it returns, the grown buffer is freed, and
    the frame is played from the static arrays on a later call.
    """
    by_frame: dict[int, list[bytes]] = {}
    order = []
    for frame, body in records(read_commands(path)):
        if frame not in by_frame:
            by_frame[frame] = []
            order.append(frame)
        by_frame[frame].append(body)
    lost = []
    prev = -1
    for frame in order:
        cap, used, grew = 0x200, 0, False
        for body in by_frame[frame]:
            if len(body) + used >= cap:
                cap, grew = len(body) + used, True
            i = 0
            while i < len(body):
                n = _size(body, i + 1, sizes)
                used += n
                i += 1 + n
        if grew and frame > prev + 1:
            lost.append(frame)
        prev = frame
    return lost


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here))
    from replaytest import oracle_exe
    table = command_sizes(oracle_exe())
    for p in sys.argv[1:]:
        lf = lost_frames(p, table)
        print(f"{p}: {len(lf)} lost frames" + (f", first {lf[0]}: {lf[:8]}" if lf else ""))
