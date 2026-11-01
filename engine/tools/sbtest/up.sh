#!/usr/bin/env bash
# Phase 4: bring up the coordinated stack — a dev-tenant coordinator and one
# relay enrolled on it — and mint a session through the coordinator's API.
#
#   tools/sbtest/up.sh [--slots N] [--out-dir DIR]
#
# Prints the work dir holding the two processes' logs and the per-slot launch
# JSONs (s0.json, s1.json, ...) that rp_start consumes. The stack runs in the
# background; kill it with the printed PIDs (the seats' tests start it fresh
# each run — this script is for bring-up and for long-lived sessions).
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/
SLOTS="${SLOTS:-2}"
WORK="${OUT_DIR:-$(mktemp -d)}"
mkdir -p "$WORK"
R2="$(pwd)/tools/rallypoint2"
TARGET="$(pwd)/src117/build/linux-x64/rallypoint"

CARGO_NET_OFFLINE=true pkill -9 -x rally-point-coo 2>/dev/null || true
pkill -9 -x rally-point-rel 2>/dev/null || true
sleep 0.2
cargo build -q --release -p rp-dev-setup -p rally-point-relay -p rally-point-coordinator \
    --manifest-path "$R2/Cargo.toml" --target-dir "$TARGET"
BIN="$TARGET/release"

"$BIN/rp_dev_setup" gen-cert --cert "$WORK/cert.pem" --key "$WORK/key.pem" >/dev/null
"$BIN/rp_dev_setup" gen-tenant > "$WORK/tenant.txt"
TENANT_PKCS8="$(sed -n '1p' "$WORK/tenant.txt")"
CLIENT_SEED="$(head -c 32 /dev/urandom | xxd -p -c 64)"

echo "[up] coordinator on 127.0.0.1:14910"
"$BIN/rally-point-coordinator" \
    --listen 127.0.0.1:14910 \
    --dev-tenant --tenant hyperion-dev --kid dev-key-1 \
    --allow-insecure-control \
    --tenant-key "$TENANT_PKCS8" \
    --dev-tenant-client-key "$CLIENT_SEED" \
    > "$WORK/coordinator.log" 2>&1 &
COORD_PID=$!
trap 'kill -9 ${COORD_PID:-} ${RELAY_PID:-} 2>/dev/null || true' EXIT

RELAY_PORT=$((15000 + RANDOM % 1000))
echo "[up] relay on 127.0.0.1:$RELAY_PORT, enrolled on the coordinator"
"$BIN/rally-point-relay" \
    --listen "127.0.0.1:$RELAY_PORT" \
    --relay-id 1 --coordinator-url http://127.0.0.1:14910 \
    --cert "$WORK/cert.pem" --key "$WORK/key.pem" \
    > "$WORK/relay.log" 2>&1 &
RELAY_PID=$!

# the relay enrolls over its control connection; give it a beat, then retry the
# create until the coordinator places the session on it
echo "[up] minting a $SLOTS-slot session"
for i in $(seq 1 20); do
    sleep 0.5
    if "$BIN/rp_dev_setup" coord-create \
        --coordinator http://127.0.0.1:14910 \
        --tenant hyperion-dev --kid dev-key-1 \
        --client-seed "$CLIENT_SEED" \
        --slots "$SLOTS" --name "Coordinated Relay" \
        --out-dir "$WORK" > "$WORK/mint.log" 2>&1; then
        break
    fi
done
if ! ls "$WORK"/s*.json > /dev/null 2>&1; then
    echo "[up] FAIL: the session was not minted; coordinator log:" >&2
    tail -20 "$WORK/coordinator.log" >&2
    exit 1
fi
echo "[up] work dir: $WORK (coordinator pid $COORD_PID, relay pid $RELAY_PID)"
cat "$WORK/mint.log"
