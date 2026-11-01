#!/usr/bin/env bash
# Phase 3 gate, part 1: the turn-command table conformance check.
#
# Extracts Hyperion's command sizes from its authoritative receive walker
# (net_process_game_turn in src117/lang/net_mgr.cpp — the dispatcher SC:R
# inherited) and ShieldBattery's SC:R table (tools/rallypoint2/proto/src/
# commands.rs), then diffs them. Also lists Hyperion's wide commands (opcodes
# past 0x80) and the special variable commands, which the relay's table must
# carry for Hyperion games. Fails when a classic opcode's size disagrees.
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/

python3 - <<'EOF'
import re, sys

# ---- ShieldBattery's table (proto/src/commands.rs FIXED_LEN) ----
rs = open("tools/rallypoint2/proto/src/commands.rs").read()
rows = re.search(r"static FIXED_LEN: \[u8; 0x68\] = \[(.*?)\];", rs, re.S).group(1)
sb = {}
for line in rows.splitlines():
    m = re.search(r"/\*0x([0-9A-Fa-f]{2})\*/", line)
    if not m:
        continue
    base = int(m.group(1), 16)
    # the values follow the row comment; strip any trailing comment
    tail = line.split("*/")[1].split("//")[0]
    vals = [int(v, 16) if v.lower().startswith("0x") else int(v)
            for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", tail)]
    for i, v in enumerate(vals):
        sb[base + i] = v

# ---- Hyperion's walker (net_mgr.cpp, net_process_game_turn) ----
src = open("src117/lang/net_mgr.cpp").read()
body = re.search(r"void net_process_game_turn\(char \*pData, unsigned int nSize, int nFlag\)\n\{(.*?)\n\}", src, re.S).group(1)
lines = body.splitlines()

sizes = {}   # opcode -> (kind, a, b): ("fixed", n, 0) or ("var", header, stride)
cur = []
for i, line in enumerate(lines):
    m = re.search(r"case (0x[0-9A-Fa-f]+):", line)
    if m:
        cur.append(int(m.group(1), 16))
        continue
    if not cur:
        continue
    # the size appears within the next few lines of the case label
    window = "\n".join(lines[i:i+6])
    m = re.search(r"nBytes = (\d+|0x[0-9A-Fa-f]+);", window)
    if m:
        n = int(m.group(1), 16 if m.group(1).lower().startswith("0x") else 10)
        for op in cur:
            sizes[op] = ("fixed", n, 0)
        cur = []
        continue
    m = re.search(r"nBytes = sizeof\((\w+)\);", window)
    if m:
        struct_sizes = {"net_cmd_rclick_w_t": 12, "net_cmd_action_w_t": 13,
                        "net_cmd_action_o_t": 14, "net_cmd_placebox_o_t": 9,
                        "net_cmd_word_t": 3, "net_cmd_dword_t": 5}
        n = struct_sizes[m.group(1)]
        for op in cur:
            sizes[op] = ("fixed", n, 0)
        cur = []
        continue
    m = re.search(r"nBytes = pCmd\[1\] \* (\d+);", window)
    if m:
        header = int(re.search(r"nLeft -= (\d+);", window).group(1))
        for op in cur:
            sizes[op] = ("var", header, int(m.group(1)))
        cur = []
        continue
    m = re.search(r"nBytes = nText \+ (\d+);", window)
    if m:
        for op in cur:
            sizes[op] = ("string", 0, 0)
        cur = []
        continue
    if re.search(r"default:", window):
        cur = []

# the shared multi-labels: 0x38/0x39 and 0x3A/0x3B share bodies (the parser
# keeps the first label's window, so fill the peers by hand where needed)
for a, b in ((0x38, 0x39), (0x3A, 0x3B)):
    if a in sizes and b not in sizes:
        sizes[b] = sizes[a]

# ---- Hyperion's lobby walker (net_mgr_process_lobby_turns): 1.17 carries the
# pre-game lobby commands inside turns, so their sizes ride the same stream the
# relay validates. Shape: `nLeft -= K;` then `pCmd += K;` (K = 1 for the
# decrement form). ----
body = re.search(r"void net_mgr_process_lobby_turns\(\)\n\{(.*?)\n\}", src, re.S).group(1)
cur = []
for line in body.splitlines():
    m = re.search(r"case (0x[0-9A-Fa-f]+):", line)
    if m:
        cur.append(int(m.group(1), 16))
        continue
    if not cur:
        continue
    m = re.search(r"nLeft -= (0x[0-9A-Fa-f]+|\d+);", line)
    if m:
        n = int(m.group(1), 16 if m.group(1).lower().startswith("0x") else 10)
        for op in cur:
            sizes[op] = ("fixed", n, 0)
        cur = []
        continue
    if "--nLeft" in line:
        for op in cur:
            sizes[op] = ("fixed", 1, 0)
        cur = []
        continue

# ---- The classic range ----
fails = []
must_add = []
benign = []
for op in range(0x68):
    hx = sizes.get(op, ("fixed", 0, 0))
    sbv = sb.get(op, 0)
    if op in (0x09, 0x0A, 0x0B, 0x63, 0x64, 0x65):
        continue  # both sides variable select commands
    if hx[0] == "fixed" and hx[1] > 0 and sbv == 0:
        must_add.append((op, hx[1]))
    elif hx[0] == "fixed" and hx[1] == 0 and sbv > 0:
        benign.append((op, sbv))
    elif hx[0] == "fixed" and hx[1] > 0 and hx[1] != sbv:
        fails.append((op, hx[1], sbv))
    elif hx[0] == "string" and sbv != 0:
        fails.append((op, "string", sbv))

print(f"classic opcodes: Hyperion walker has {len(sizes)} entries")
if fails:
    print("CLASSIC TABLE DIFFERS:")
    for op, hx, s in fails:
        print(f"  opcode 0x{op:02X}: Hyperion {hx}, rally-point {s}")
    sys.exit(1)

if must_add:
    print()
    print("Hyperion sends these; the relay's table rejects them (must be added):")
    for op, n in must_add:
        print(f"  0x{op:02X}: fixed {n}")

if benign:
    print()
    print("SC:R-only commands Hyperion never sends (the relay accepting them is harmless):")
    print("  " + ", ".join(f"0x{op:02X}" for op, _ in benign))

wide = [op for op in sorted(sizes) if op >= 0x80]
if wide:
    print()
    print("Hyperion's wide commands (the relay's table extension):")
    for op in wide:
        kind = sizes[op]
        desc = {"fixed": f"fixed {kind[1]}", "var": f"header {kind[1]}, stride {kind[2]}"}[kind[0]]
        print(f"  0x{op:02X}: {desc}")

print()
print("save/load (0x06, 0x07) are string-variable; the relay rejects them by design (no")
print("multiplayer saves) — keep rejected, as Hyperion's own multiplayer save is unsupported.")
EOF
