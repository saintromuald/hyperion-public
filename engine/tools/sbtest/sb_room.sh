#!/usr/bin/env bash
# Phase 5: the room launches an online (SB) match.
#
# The room hosts a match whose transport is "sb": at its start the room mints
# a session through the coordinator (rp_dev_mint in the bridge, signed with
# the tenant's client seed from the room's lanroom.json), embeds each seat's
# setup JSON in its launch, and the seats play through the relay — the same
# provider path the --sb-setup drills take, but launched from the room.
#
#   tools/sbtest/sb_room.sh <map.scm> [seconds]
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/
MAP="${1:?usage: sb_room.sh <map.scm> [seconds]}"
LIMIT="${2:-90}"
HX="$(pwd)/src117/build/linux-x64/Hyperion.x86_64"
WORK="$(mktemp -d)"
R2="$(pwd)/tools/rallypoint2"
TARGET="$(pwd)/src117/build/linux-x64/rallypoint"
MAPNAME="$(basename "$MAP")"

pkill -9 -x rally-point-coo 2>/dev/null || true
pkill -9 -x rally-point-rel 2>/dev/null || true
sleep 0.2
CARGO_NET_OFFLINE=true cargo build -q --release -p rp-dev-setup -p rally-point-relay -p rally-point-coordinator \
    --manifest-path "$R2/Cargo.toml" --target-dir "$TARGET"
BIN="$TARGET/release"

# the coordinated stack: coordinator + one enrolled relay
"$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem"
"$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
CLIENT_SEED="$(head -c 32 /dev/urandom | xxd -p -c 64)"
"$BIN/rally-point-coordinator" --listen 127.0.0.1:14910 \
    --dev-tenant --tenant hyperion-dev --kid dev-key-1 \
    --allow-insecure-control --tenant-key "$TENANT_PKCS8" \
    --dev-tenant-client-key "$CLIENT_SEED" > "$WORK/coordinator.log" 2>&1 &
COORD_PID=$!
RELAY_PORT=$((15000 + RANDOM % 1000))
"$BIN/rally-point-relay" --listen "127.0.0.1:$RELAY_PORT" --relay-id 1 \
    --coordinator-url http://127.0.0.1:14910 \
    --cert "$WORK/cert.pem" --key "$WORK/key.pem" > "$WORK/relay.log" 2>&1 &
RELAY_PID=$!

mkdir -p "$WORK/room" "$WORK/seat0/maps" "$WORK/seat1/maps"
cp "$MAP" "$WORK/seat0/maps/"
cp "$MAP" "$WORK/seat1/maps/"

# the room's settings: its coordinator and the tenant's client seed, so the
# match's launch can mint through the coordinator (the console would set these)
cat > "$WORK/room/lanroom.json" <<EOF
{
  "password": false,
  "sbCoordinator": "http://127.0.0.1:14910",
  "sbTenant": "hyperion-dev",
  "sbKid": "dev-key-1",
  "sbClientSeed": "$CLIENT_SEED",
  "sbServerName": "localhost"
}
EOF

ENV=(HYPERION_WINDOW=hidden SDL_AUDIO_DRIVER=dummy HYPERION_LAN_BIND=127.0.0.1
     HYPERION_REMASTERED="${HYPERION_REMASTERED:-}")
trap 'kill -9 ${ROOM_PID:-} ${S0_PID:-} ${S1_PID:-} ${RELAY_PID:-} ${COORD_PID:-} 2>/dev/null || true' EXIT

echo "[sb_room] the room hosts the match"
env "${ENV[@]}" "$HX" --data "$WORK/room" --room --room-key testkey --room-name online --room-password refpw \
    > "$WORK/room.log" 2>&1 &
ROOM_PID=$!
sleep 1

echo "[sb_room] two seats join the room"
env "${ENV[@]}" "$HX" --data "$WORK/seat0" --join 127.0.0.1 --room-password refpw --lan-name Alpha --lan-ready --race 1 \
    > "$WORK/s0.log" 2>&1 &
S0_PID=$!
env "${ENV[@]}" "$HX" --data "$WORK/seat1" --join 127.0.0.1 --room-password refpw --lan-name Bravo --lan-ready --race 2 \
    > "$WORK/s1.log" 2>&1 &
S1_PID=$!
sleep 3

echo "[sb_room] the console makes an online match and starts it"
python3 - "$WORK" "$MAP" <<'EOF'
import json, socket, sys, time
work, mappath = sys.argv[1], sys.argv[2]
s = socket.create_connection(("127.0.0.1", 6112), timeout=10)

def send(obj):
    body = json.dumps(obj)
    frame = (len(body) + 1).to_bytes(4, "little") + b"\x20" + body.encode()
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
send({"t": "newmatch", "map": mappath, "mapName": "online", "type": "melee",
      "speed": "fastest", "rules": "hyperion", "transport": "sb", "players": 2})
for _ in range(30):
    time.sleep(0.5)
    s.settimeout(0.4)
    try:
        got = s.recv(4096).decode(errors="replace")
        if '"t":"room"' in got and got.count('"ready":true') >= 2:
            break
    except socket.timeout:
        pass
send({"t": "start", "match": 1})
print("console: start sent", flush=True)
# read the room's answer to the start (an error says why it refused), then hold
# the connection while the seats play: the room stops when the console is gone
deadline = time.time() + 85
while time.time() < deadline:
    s.settimeout(0.4)
    try:
        got = s.recv(65536)
        if not got:
            break
        print("room:", got.decode(errors="replace"), flush=True)
    except socket.timeout:
        pass
s.close()
EOF

# the seats' games: both truly in-game through the relay, in lockstep
set +e
PASS=0
for i in $(seq 1 "$LIMIT"); do
    sleep 1
    if grep -q "MutatorsForGame" "$WORK/seat0/hyperion_mutators.log" 2>/dev/null &&
       grep -q "MutatorsForGame" "$WORK/seat1/hyperion_mutators.log" 2>/dev/null; then
        for j in $(seq 1 45); do
            sleep 1
            kill -0 "$S0_PID" 2>/dev/null && kill -0 "$S1_PID" 2>/dev/null || break
        done
        if kill -0 "$S0_PID" 2>/dev/null && kill -0 "$S1_PID" 2>/dev/null &&
           ! grep -qi "desync" "$WORK/relay.log" &&
           ! grep -q "left (reason" "$WORK/seat0/hyperion_sb.log" 2>/dev/null &&
           ! grep -q "left (reason" "$WORK/seat1/hyperion_sb.log" 2>/dev/null; then
            PASS=1
        fi
        break
    fi
done
kill -9 "$S0_PID" 2>/dev/null || true
kill -9 "$S1_PID" 2>/dev/null || true
wait "$S0_PID" 2>/dev/null
wait "$S1_PID" 2>/dev/null
set -e

echo "---- room ----"
grep -hE "minted|launched|sb" "$WORK/room/hyperion_lan.log" 2>/dev/null | tail -5
echo "---- seats ----"
tail -3 "$WORK/seat0/hyperion_sb.log" 2>/dev/null
tail -3 "$WORK/seat1/hyperion_sb.log" 2>/dev/null

if [ "$PASS" -eq 1 ]; then
    echo "[sb_room] PASS: the room launched the match online and both seats played in lockstep"
else
    echo "[sb_room] FAIL" >&2
    exit 1
fi
