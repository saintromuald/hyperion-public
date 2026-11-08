#!/usr/bin/env bash
# Phase 2 gate: two headless Hyperions play a lockstep game through a
# standalone rally-point relay on one machine.
#
#   tools/sbtest/run_game.sh <map.scm> [seconds]
#
# Needs a built Hyperion (src117/build/linux-x64/Hyperion.x86_64) and the game
# data (HYPERION_GAME pointing at a StarCraft install, or pass --game below).
# The map file is copied into each seat's maps folder; both seats must hold the
# same bytes. Each seat's log and replay land in the seat's data folder.
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/
COORDINATED=0
if [ "${1:-}" = "--coordinated" ]; then COORDINATED=1; shift; fi
MAP="${1:?usage: run_game.sh [--coordinated] <map.scm> [seconds]}"
LIMIT="${2:-150}"
HX="$(pwd)/src117/build/linux-x64/Hyperion.x86_64"
GAME="${HYPERION_GAME:-}"
WORK="$(mktemp -d)"
R2="$(pwd)/tools/rallypoint2"
TARGET="$(pwd)/src117/build/linux-x64/rallypoint"

echo "[game] building the dev harness"
CARGO_NET_OFFLINE=true pkill -9 -x rally-point-coo 2>/dev/null || true
pkill -9 -x rally-point-rel 2>/dev/null || true
sleep 0.2
cargo build -q --release -p rp-dev-setup -p rally-point-relay -p rally-point-coordinator \
    --manifest-path "$R2/Cargo.toml" --target-dir "$TARGET"
BIN="$TARGET/release"

# --- the coordinated stack (--coordinated): a dev-tenant coordinator and one
# enrolled relay mint the session through the signed API; the setups land in the
# same paths the standalone flow writes. ---
stack_up() {
  echo "[game] coordinated stack: coordinator + enrolled relay"
  "$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem"
  "$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
  TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
  CLIENT_SEED="$(head -c 32 /dev/urandom | xxd -p -c 64)"
  "$BIN/rally-point-coordinator" --listen 127.0.0.1:14910       --dev-tenant --tenant hyperion-dev --kid dev-key-1       --allow-insecure-control --tenant-key "$TENANT_PKCS8"       --dev-tenant-client-key "$CLIENT_SEED" > "$WORK/coordinator.log" 2>&1 &
  COORD_PID=$!
  PORT=$((15000 + RANDOM % 1000))
  "$BIN/rally-point-relay" --listen "127.0.0.1:$PORT"       --relay-id 1 --coordinator-url http://127.0.0.1:14910       --cert "$WORK/cert.pem" --key "$WORK/key.pem" > "$WORK/relay.log" 2>&1 &
  RELAY_PID=$!
  for i in $(seq 1 20); do
    sleep 0.5
    if "$BIN/rp_dev_setup" coord-create --coordinator http://127.0.0.1:14910         --tenant hyperion-dev --kid dev-key-1 --client-seed "$CLIENT_SEED"         --slots 2 --name "SB Relay Test" --out-dir "$WORK" > "$WORK/mint.log" 2>&1; then
      return 0
    fi
  done
  echo "[game] the coordinated session was not minted" >&2
  tail -10 "$WORK/coordinator.log" >&2
  return 1
}

if [ "$COORDINATED" -eq 1 ]; then
  stack_up
else
echo "[game] generating the relay cert and tenant key"
"$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem"
"$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
TENANT_PUB="$(sed -n '2p' "$WORK/tenant.txt")"

# a port of its own, so a stray relay (or another test) never answers in its place
PORT=$((15000 + RANDOM % 1000))
echo "[game] starting the relay on 127.0.0.1:$PORT"
"$BIN/rally-point-relay" \
    --listen "127.0.0.1:$PORT" \
    --cert "$WORK/cert.pem" --key "$WORK/key.pem" \
    --tenant-pubkey "$TENANT_PUB" --kid dev-key-1 --tenant hyperion-dev \
    > "$WORK/relay.log" 2>&1 &
RELAY_PID=$!
trap 'kill -9 ${C0_PID:-} ${C1_PID:-} ${RELAY_PID:-} ${COORD_PID:-} 2>/dev/null || true; wait $RELAY_PID 2>/dev/null || true' EXIT
sleep 1

echo "[game] minting the two seats' launches"
"$BIN/rp_dev_setup" setup --tenant-pkcs8 "$TENANT_PKCS8" --kid dev-key-1 --tenant hyperion-dev \
    --session 1 --slot 0 --relay "127.0.0.1:$PORT" --server-name localhost \
    --cert-file "$WORK/cert.pem" --players 2 --name "SB Relay Test" --out "$WORK/s0.json"
"$BIN/rp_dev_setup" setup --tenant-pkcs8 "$TENANT_PKCS8" --kid dev-key-1 --tenant hyperion-dev \
    --session 1 --slot 1 --relay "127.0.0.1:$PORT" --server-name localhost \
    --cert-file "$WORK/cert.pem" --players 2 --name "SB Relay Test" --out "$WORK/s1.json"
fi

mkdir -p "$WORK/seat0/maps" "$WORK/seat1/maps"
# the map keeps its own file name: the game resolves the map by the name
# inside the file (sub_477090 opens its transfer session by that name)
cp "$MAP" "$WORK/seat0/maps/"
cp "$MAP" "$WORK/seat1/maps/"
MAPNAME="$(basename "$MAP")"

ENV=(HYPERION_WINDOW=hidden SDL_AUDIO_DRIVER=dummy HYPERION_LAN_BIND=127.0.0.1)
[ -n "$GAME" ] && ENV+=(HYPERION_GAME="$GAME")

echo "[game] two seats play through the relay"
env "${ENV[@]}" stdbuf -oL -eL "$HX" --data "$WORK/seat0" --install-folder "$GAME" \
    --sb-setup "$WORK/s0.json" --sb-role create \
    --sb-map "$WORK/seat0/maps/$MAPNAME" --sb-name Alpha --race 1 --sb-speed 6 --sb-type melee \
    > "$WORK/c0.log" 2>&1 &
C0_PID=$!
env "${ENV[@]}" stdbuf -oL -eL "$HX" --data "$WORK/seat1" --install-folder "$GAME" \
    --sb-setup "$WORK/s1.json" --sb-role join \
    --sb-map "$WORK/seat1/maps/$MAPNAME" --sb-name Bravo --race 2 --sb-speed 6 --sb-type melee \
    > "$WORK/c1.log" 2>&1 &
C1_PID=$!

# The seats are players nobody plays: the game runs until the test ends it.
# Both must enter the game loop and advance well beyond the lobby's steps. Their
# first 600 dispatched steps must be byte-for-byte identical, with no drop or
# desync; process survival alone is not a lockstep check.
set +e
PASS=0
for i in $(seq 1 "$LIMIT"); do
    sleep 1
    if grep -q "first step" "$WORK/seat0/hyperion_sb.log" 2>/dev/null &&
       grep -q "first step" "$WORK/seat1/hyperion_sb.log" 2>/dev/null; then
        # hold the game 45 more seconds: no drops, no desync, both seats alive
        for j in $(seq 1 45); do
            sleep 1
            if ! kill -0 "$C0_PID" 2>/dev/null || ! kill -0 "$C1_PID" 2>/dev/null; then
                break
            fi
        done
        grep '^step ' "$WORK/seat0/hyperion_sb_trace.log" 2>/dev/null | head -n 600 > "$WORK/steps0.log"
        grep '^step ' "$WORK/seat1/hyperion_sb_trace.log" 2>/dev/null | head -n 600 > "$WORK/steps1.log"
        if kill -0 "$C0_PID" 2>/dev/null && kill -0 "$C1_PID" 2>/dev/null &&
           grep -q "running game loop" "$WORK/seat0/hyperion_sb.log" 2>/dev/null &&
           grep -q "running game loop" "$WORK/seat1/hyperion_sb.log" 2>/dev/null &&
           grep -q "the game has read 600 steps" "$WORK/seat0/hyperion_sb.log" 2>/dev/null &&
           grep -q "the game has read 600 steps" "$WORK/seat1/hyperion_sb.log" 2>/dev/null &&
           [ "$(wc -l < "$WORK/steps0.log")" -eq 600 ] && [ "$(wc -l < "$WORK/steps1.log")" -eq 600 ] &&
           cmp -s "$WORK/steps0.log" "$WORK/steps1.log" &&
           grep -q "MutatorsForGame" "$WORK/seat0/hyperion_mutators.log" 2>/dev/null &&
           ! grep -qi "desync" "$WORK/relay.log" &&
           ! grep -q "left (reason" "$WORK/seat0/hyperion_sb.log" 2>/dev/null &&
           ! grep -q "left (reason" "$WORK/seat1/hyperion_sb.log" 2>/dev/null; then
            PASS=1
        fi
        break
    fi
    if ! kill -0 "$C0_PID" 2>/dev/null && ! kill -0 "$C1_PID" 2>/dev/null; then
        break
    fi
done
kill -9 "$C0_PID" 2>/dev/null || true
kill -9 "$C1_PID" 2>/dev/null || true
wait "$C0_PID" 2>/dev/null; R0=$?
wait "$C1_PID" 2>/dev/null; R1=$?
set -e

echo "---- seat 0 (creator) ----"; tail -8 "$WORK/c0.log"; tail -6 "$WORK/seat0/hyperion_sb.log" 2>/dev/null
echo "---- seat 1 (joiner)  ----"; tail -8 "$WORK/c1.log"; tail -6 "$WORK/seat1/hyperion_sb.log" 2>/dev/null

if [ "$PASS" -eq 1 ]; then
    echo "[game] PASS: both seats dispatched 600 identical steps and stayed clean for 45 s"
else
    echo "[game] FAIL (seat0=$R0 seat1=$R1); relay log:" >&2
    tail -20 "$WORK/relay.log" >&2
    exit 1
fi
