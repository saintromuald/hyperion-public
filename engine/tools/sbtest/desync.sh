#!/usr/bin/env bash
# Phase 3 gate, part 2: the desync drill.
#
# Two headless Hyperions join the same relay session with different mutator
# lists (different starting resources): the simulations diverge from the first
# frame, the seats drop each other through 1.17's own 0x37 sync check, and the
# relay's authority reports the divergence server-side ("relay-side desync
# detected" in its log).
#
#   tools/sbtest/desync.sh <map.scm> [seconds]
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/
COORDINATED=0
if [ "${1:-}" = "--coordinated" ]; then COORDINATED=1; shift; fi
MAP="${1:?usage: desync.sh [--coordinated] <map.scm> [seconds]}"
LIMIT="${2:-90}"
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

# --- the coordinated stack (--coordinated) ---
stack_up() {
  "$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem"
  "$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
  TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
  CLIENT_SEED="$(head -c 32 /dev/urandom | xxd -p -c 64)"
  "$BIN/rally-point-coordinator" --listen 127.0.0.1:14910 \
      --dev-tenant --tenant hyperion-dev --kid dev-key-1 \
      --allow-insecure-control --tenant-key "$TENANT_PKCS8" \
      --dev-tenant-client-key "$CLIENT_SEED" > "$WORK/coordinator.log" 2>&1 &
  COORD_PID=$!
  PORT=$((15000 + RANDOM % 1000))
  "$BIN/rally-point-relay" --listen "127.0.0.1:$PORT" \
      --relay-id 1 --coordinator-url http://127.0.0.1:14910 \
      --cert "$WORK/cert.pem" --key "$WORK/key.pem" > "$WORK/relay.log" 2>&1 &
  RELAY_PID=$!
  for i in $(seq 1 20); do
    sleep 0.5
    if "$BIN/rp_dev_setup" coord-create --coordinator http://127.0.0.1:14910 \
        --tenant hyperion-dev --kid dev-key-1 --client-seed "$CLIENT_SEED" \
        --slots 2 --name "Desync Drill" --out-dir "$WORK" > "$WORK/mint.log" 2>&1; then
      return 0
    fi
  done
  echo "[desync] the coordinated session was not minted" >&2
  tail -10 "$WORK/coordinator.log" >&2
  return 1
}

if [ "$COORDINATED" -eq 1 ]; then
  stack_up
else
"$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem"
"$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
TENANT_PUB="$(sed -n '2p' "$WORK/tenant.txt")"

PORT=$((15000 + RANDOM % 1000))
echo "[desync] relay on 127.0.0.1:$PORT"
"$BIN/rally-point-relay" --listen "127.0.0.1:$PORT" \
    --cert "$WORK/cert.pem" --key "$WORK/key.pem" \
    --tenant-pubkey "$TENANT_PUB" --kid dev-key-1 --tenant hyperion-dev \
    > "$WORK/relay.log" 2>&1 &
RELAY_PID=$!
trap 'kill -9 ${C0_PID:-} ${C1_PID:-} ${RELAY_PID:-} ${COORD_PID:-} 2>/dev/null || true; wait $RELAY_PID 2>/dev/null || true' EXIT
sleep 1

"$BIN/rp_dev_setup" setup --tenant-pkcs8 "$TENANT_PKCS8" --kid dev-key-1 --tenant hyperion-dev \
    --session 1 --slot 0 --relay "127.0.0.1:$PORT" --server-name localhost \
    --cert-file "$WORK/cert.pem" --players 2 --name "Desync Drill" --out "$WORK/s0.json"
"$BIN/rp_dev_setup" setup --tenant-pkcs8 "$TENANT_PKCS8" --kid dev-key-1 --tenant hyperion-dev \
    --session 1 --slot 1 --relay "127.0.0.1:$PORT" --server-name localhost \
    --cert-file "$WORK/cert.pem" --players 2 --name "Desync Drill" --out "$WORK/s1.json"
fi

mkdir -p "$WORK/seat0/maps" "$WORK/seat1/maps"
cp "$MAP" "$WORK/seat0/maps/"
cp "$MAP" "$WORK/seat1/maps/"
MAPNAME="$(basename "$MAP")"

ENV=(HYPERION_WINDOW=hidden SDL_AUDIO_DRIVER=dummy HYPERION_LAN_BIND=127.0.0.1
     HYPERION_REMASTERED="${HYPERION_REMASTERED:-}")

echo "[desync] seat 0 starts with 50 minerals, seat 1 with 500"
env "${ENV[@]}" "$HX" --data "$WORK/seat0" \
    --sb-setup "$WORK/s0.json" --sb-role create \
    --sb-map "$WORK/seat0/maps/$MAPNAME" --sb-name Alpha --race 1 --sb-speed 6 --sb-type melee \
    --rules hyperion --mutators "start=50/0" > "$WORK/c0.log" 2>&1 &
C0_PID=$!
env "${ENV[@]}" "$HX" --data "$WORK/seat1" \
    --sb-setup "$WORK/s1.json" --sb-role join \
    --sb-map "$WORK/seat1/maps/$MAPNAME" --sb-name Bravo --race 2 --sb-speed 6 --sb-type melee \
    --rules hyperion --mutators "start=500/0" > "$WORK/c1.log" 2>&1 &
C1_PID=$!

set +e
DESYNC=0
for i in $(seq 1 "$LIMIT"); do
    sleep 1
    if grep -q "relay-side desync detected" "$WORK/relay.log" 2>/dev/null; then
        DESYNC=1
        break
    fi
    if ! kill -0 "$C0_PID" 2>/dev/null && ! kill -0 "$C1_PID" 2>/dev/null; then
        break
    fi
done
kill -9 "$C0_PID" 2>/dev/null || true
kill -9 "$C1_PID" 2>/dev/null || true
wait "$C0_PID" 2>/dev/null
wait "$C1_PID" 2>/dev/null
set -e

echo "---- relay ----"
grep -E "desync|leave|authorized" "$WORK/relay.log" | head -8
echo "---- seats ----"
grep -h "left (reason\|first step" "$WORK/seat0/hyperion_sb.log" "$WORK/seat1/hyperion_sb.log" 2>/dev/null | head -6

if [ "$DESYNC" -eq 1 ]; then
    echo "[desync] PASS: the relay reported the divergence"
else
    echo "[desync] FAIL: no desync report; seat logs:" >&2
    tail -8 "$WORK/seat0/hyperion_sb.log" >&2
    tail -8 "$WORK/seat1/hyperion_sb.log" >&2
    exit 1
fi
