#!/usr/bin/env bash
# The LAN reference flow: the room hosts a match and two seats play it, driven
# by a scripted console over the room's own JSON protocol. Used to compare the
# LAN lobby's behaviour with the SB relay's.
#
#   tools/sbtest/lan_ref.sh <map.scm> [seconds]
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/
MAP="${1:?usage: lan_ref.sh <map.scm> [seconds]}"
LIMIT="${2:-60}"
HX="$(pwd)/src117/build/linux-x64/Hyperion.x86_64"
WORK="$(mktemp -d)"
ENV=(HYPERION_WINDOW=hidden SDL_AUDIO_DRIVER=dummy HYPERION_LAN_BIND=127.0.0.1
     HYPERION_REMASTERED="${HYPERION_REMASTERED:-}")
MAPNAME="$(basename "$MAP")"

mkdir -p "$WORK/room" "$WORK/seat0/maps" "$WORK/seat1/maps"
cp "$MAP" "$WORK/seat0/maps/"
cp "$MAP" "$WORK/seat1/maps/"

echo "[lan] room"
env "${ENV[@]}" "$HX" --data "$WORK/room" --room --room-key testkey --room-name ref --room-password refpw \
    > "$WORK/room.log" 2>&1 &
ROOM_PID=$!
trap 'kill -9 ${ROOM_PID:-} ${S0_PID:-} ${S1_PID:-} 2>/dev/null || true' EXIT
sleep 1

echo "[lan] two seats join the room"
env "${ENV[@]}" "$HX" --data "$WORK/seat0" --join 127.0.0.1 --room-password refpw --lan-name Alpha --lan-ready --race 1 \
    > "$WORK/s0.log" 2>&1 &
S0_PID=$!
env "${ENV[@]}" "$HX" --data "$WORK/seat1" --join 127.0.0.1 --room-password refpw --lan-name Bravo --lan-ready --race 2 \
    > "$WORK/s1.log" 2>&1 &
S1_PID=$!
sleep 3

echo "[lan] the console makes and starts the match"
python3 - "$WORK" "$MAP" <<'EOF'
import json, socket, sys, time
work, mappath = sys.argv[1], sys.argv[2]
s = socket.create_connection(("127.0.0.1", 6112), timeout=10)

def send(obj):
    body = json.dumps(obj)
    frame = (len(body) + 1).to_bytes(4, "little") + b"\x20" + body.encode()  # the frame length covers the kind byte too
    s.sendall(frame)
    s.settimeout(0.4)
    try:
        while True:
            hdr = s.recv(5)
            if len(hdr) < 5:
                break
            ln = int.from_bytes(hdr[:4], "little")
            kind = hdr[4]
            data = b""
            while len(data) < ln - 1:
                data += s.recv(ln - 1 - len(data))
            print("reply:", kind, data.decode(errors="replace"), flush=True)
    except socket.timeout:
        pass

send({"t": "hello", "name": "console", "key": "testkey"})
time.sleep(0.5)
send({"t": "newmatch", "map": mappath, "mapName": "ref", "type": "melee", "speed": "fastest", "rules": "hyperion", "players": 2})
# wait until both seats sit and ready, then start and hold the console open
for _ in range(30):
    time.sleep(0.5)
    s.settimeout(0.4)
    try:
        got = s.recv(4096).decode(errors="replace")
        if '"t":"room"' in got and got.count('"ready":true') >= 2:
            break
    except socket.timeout:
        pass
send({"t": "start"})
time.sleep(3)
s.close()
EOF

set +e
for i in $(seq 1 "$LIMIT"); do
    sleep 1
    if grep -q "the game ended" "$WORK/seat0/hyperion_lan.log" 2>/dev/null; then break; fi
done
kill -9 "$S0_PID" 2>/dev/null || true
kill -9 "$S1_PID" 2>/dev/null || true
wait "$S0_PID" 2>/dev/null
wait "$S1_PID" 2>/dev/null
set -e

echo "---- seat0 lobby ----"; grep -h "lobby:\|countdown\|MutatorsForGame" "$WORK/seat0/hyperion_lan.log" "$WORK/seat0/hyperion_lobbystate.log" "$WORK/seat0/hyperion_mutators.log" 2>/dev/null | head -8
echo "---- seat1 lobby ----"; grep -h "lobby:\|countdown\|MutatorsForGame" "$WORK/seat1/hyperion_lan.log" "$WORK/seat1/hyperion_lobbystate.log" "$WORK/seat1/hyperion_mutators.log" 2>/dev/null | head -8
echo "---- room ----"; tail -6 "$WORK/room.log"
echo "WORK: $WORK"
