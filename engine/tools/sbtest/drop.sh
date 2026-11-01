#!/usr/bin/env bash
# Phase 6, part 2: the drop drill.
#
# Two headless Hyperions play through the coordinated stack; mid-game the joiner
# is SIGKILLed. The survivor's driver sees the peer's link go down (the relay
# stamps every member's link state), asks the relay's authority for a synced
# drop after a grace period, and the game continues alone once the authority
# confirms it — the relay decides the apply frame, both seats' step streams
# stay synced, and the survivor never stalls waiting for the dead seat.
#
#   tools/sbtest/drop.sh <map.scm> [seconds]
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/
COORDINATED=0
if [ "${1:-}" = "--coordinated" ]; then COORDINATED=1; shift; fi
MAP="${1:?usage: drop.sh [--coordinated] <map.scm> [seconds]}"
LIMIT="${2:-120}"
HX="$(pwd)/src117/build/linux-x64/Hyperion.x86_64"
WORK="$(mktemp -d)"
R2="$(pwd)/tools/rallypoint2"
TARGET="$(pwd)/src117/build/linux-x64/rallypoint"

pkill -9 -x rally-point-coo 2>/dev/null || true
pkill -9 -x rally-point-rel 2>/dev/null || true
sleep 0.2
CARGO_NET_OFFLINE=true cargo build -q --release -p rp-dev-setup -p rally-point-relay -p rally-point-coordinator \
    --manifest-path "$R2/Cargo.toml" --target-dir "$TARGET"
BIN="$TARGET/release"

RELAY_PORT=$((15000 + RANDOM % 1000))
RELAY_ARGS=(--listen "127.0.0.1:$RELAY_PORT" --relay-id 1 --coordinator-url http://127.0.0.1:14910
            --cert "$WORK/cert.pem" --key "$WORK/key.pem")

if [ "$COORDINATED" -eq 1 ]; then
  "$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem"
  "$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
  TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
  CLIENT_SEED="$(head -c 32 /dev/urandom | xxd -p -c 64)"
  "$BIN/rally-point-coordinator" --listen 127.0.0.1:14910 \
      --dev-tenant --tenant hyperion-dev --kid dev-key-1 \
      --allow-insecure-control --tenant-key "$TENANT_PKCS8" \
      --dev-tenant-client-key "$CLIENT_SEED" > "$WORK/coordinator.log" 2>&1 &
  COORD_PID=$!
  "$BIN/rally-point-relay" "${RELAY_ARGS[@]}" > "$WORK/relay.log" 2>&1 &
  RELAY_PID=$!
  for i in $(seq 1 20); do
    sleep 0.5
    if "$BIN/rp_dev_setup" coord-create --coordinator http://127.0.0.1:14910 \
        --tenant hyperion-dev --kid dev-key-1 --client-seed "$CLIENT_SEED" \
        --slots 2 --name "Drop Drill" --out-dir "$WORK" > "$WORK/mint.log" 2>&1; then
      break
    fi
  done
  ls "$WORK"/s*.json > /dev/null 2>&1 || { echo "no session minted"; tail -10 "$WORK/coordinator.log"; exit 1; }
else
  "$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem"
  "$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
  TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
  TENANT_PUB="$(sed -n '2p' "$WORK/tenant.txt")"
  "$BIN/rally-point-relay" --listen "127.0.0.1:$RELAY_PORT" \
      --cert "$WORK/cert.pem" --key "$WORK/key.pem" \
      --tenant-pubkey "$TENANT_PUB" --kid dev-key-1 --tenant hyperion-dev > "$WORK/relay.log" 2>&1 &
  RELAY_PID=$!
  sleep 1
  "$BIN/rp_dev_setup" setup --tenant-pkcs8 "$TENANT_PKCS8" --kid dev-key-1 --tenant hyperion-dev \
      --session 1 --slot 0 --relay "127.0.0.1:$RELAY_PORT" --server-name localhost \
      --cert-file "$WORK/cert.pem" --players 2 --name "Drop Drill" --out "$WORK/s0.json"
  "$BIN/rp_dev_setup" setup --tenant-pkcs8 "$TENANT_PKCS8" --kid dev-key-1 --tenant hyperion-dev \
      --session 1 --slot 1 --relay "127.0.0.1:$RELAY_PORT" --server-name localhost \
      --cert-file "$WORK/cert.pem" --players 2 --name "Drop Drill" --out "$WORK/s1.json"
fi

mkdir -p "$WORK/seat0/maps" "$WORK/seat1/maps"
cp "$MAP" "$WORK/seat0/maps/"
cp "$MAP" "$WORK/seat1/maps/"
MAPNAME="$(basename "$MAP")"

ENV=(HYPERION_WINDOW=hidden SDL_AUDIO_DRIVER=dummy HYPERION_LAN_BIND=127.0.0.1
     HYPERION_REMASTERED="${HYPERION_REMASTERED:-}")
trap 'kill -9 ${C0_PID:-} ${C1_PID:-} ${RELAY_PID:-} ${COORD_PID:-} 2>/dev/null || true' EXIT

echo "[drop] two seats play"
env "${ENV[@]}" "$HX" --data "$WORK/seat0" \
    --sb-setup "$WORK/s0.json" --sb-role create \
    --sb-map "$WORK/seat0/maps/$MAPNAME" --sb-name Alpha --race 1 --sb-speed 6 --sb-type melee \
    > "$WORK/c0.log" 2>&1 &
C0_PID=$!
env "${ENV[@]}" "$HX" --data "$WORK/seat1" \
    --sb-setup "$WORK/s1.json" --sb-role join \
    --sb-map "$WORK/seat1/maps/$MAPNAME" --sb-name Bravo --race 2 --sb-speed 6 --sb-type melee \
    > "$WORK/c1.log" 2>&1 &
C1_PID=$!

set +e
IN_GAME=0
for i in $(seq 1 "$LIMIT"); do
    sleep 1
    if grep -q "MutatorsForGame" "$WORK/seat0/hyperion_mutators.log" 2>/dev/null &&
       grep -q "MutatorsForGame" "$WORK/seat1/hyperion_mutators.log" 2>/dev/null; then
        IN_GAME=1
        break
    fi
done
if [ "$IN_GAME" -ne 1 ]; then
    echo "[drop] FAIL: the games did not start" >&2
    exit 1
fi

echo "[drop] killing the joiner mid-game"
kill -9 "$C1_PID" 2>/dev/null || true
wait "$C1_PID" 2>/dev/null || true

# the survivor asks for the synced drop after the grace period and the relay's
# authority confirms it as a leave at a frame every member shares
LEAVE_LINE=""
for i in $(seq 1 60); do
    sleep 1
    kill -0 "$C0_PID" 2>/dev/null || break
    LEAVE_LINE=$(grep -n "player 1 left (reason" "$WORK/seat0/hyperion_sb.log" 2>/dev/null | tail -1 | cut -d: -f1)
    if [ -n "$LEAVE_LINE" ]; then
        break
    fi
done

# the game resumes at the apply frame; wait for its step counter to keep moving
# past the leave (the relay holds the drop ~30 s, so the stall is expected)
CONTINUES=0
for i in $(seq 1 90); do
    sleep 1
    kill -0 "$C0_PID" 2>/dev/null || break
    if [ -n "$LEAVE_LINE" ] && awk -v n="$LEAVE_LINE" 'NR > n && /the game has read/ {found=1} END {exit !found}' \
        "$WORK/seat0/hyperion_sb.log" 2>/dev/null; then
        CONTINUES=1
        break
    fi
done

echo "---- survivor ----"
grep -hE "link went down|synced drop was asked|left \(reason" "$WORK/seat0/hyperion_sb.log" 2>/dev/null | tail -5
echo "---- relay ----"
tail -3 "$WORK/relay.log"

SURVIVOR_ALIVE=0
kill -0 "$C0_PID" 2>/dev/null && SURVIVOR_ALIVE=1
ASKED=$(grep -c "synced drop was asked" "$WORK/seat0/hyperion_sb.log" 2>/dev/null)
ALONE=$(grep -c "playing on alone" "$WORK/seat0/hyperion_sb.log" 2>/dev/null)

kill -9 "$C0_PID" 2>/dev/null || true
wait "$C0_PID" 2>/dev/null
set -e

if [ -n "$LEAVE_LINE" ] && [ "$SURVIVOR_ALIVE" -eq 1 ] && [ "$CONTINUES" -eq 1 ] && [ "$ALONE" -eq 0 ]; then
    echo "[drop] PASS: the relay's synced drop removed the dead joiner (asked=$ASKED) and the survivor kept playing"
else
    echo "[drop] FAIL: leave_line='$LEAVE_LINE' alive=$SURVIVOR_ALIVE continues=$CONTINUES alone=$ALONE asked=$ASKED" >&2
    exit 1
fi
