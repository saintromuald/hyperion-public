#!/usr/bin/env bash
# The group's always-on netcode server: a dev-tenant rally-point coordinator and
# one relay, listening on every interface so rooms and seats anywhere on the
# (tail)net reach them. Keys and the certificate are made once and kept in the
# state dir; rerun the script to restart with the same identity.
#
#   tools/sbtest/netcode-server.sh [--state DIR] [--host 0.0.0.0] [--relay-port 15000]
#
# Prints the lanroom.json settings for the room (sbCoordinator, sbTenant, sbKid,
# sbClientSeed, sbServerName) and leaves the two processes running. The
# coordinator's URL must be an address the rooms can reach (a tailnet or public
# address), not 127.0.0.1 -- pass COORDINATOR_ADDR for it.
set -euo pipefail
cd "$(dirname "$0")/../.."            # engine/
STATE="${STATE:-$(pwd)/src117/build/linux-x64/netcode}"
HOST="${HOST:-0.0.0.0}"
COORD_PORT="${COORD_PORT:-14910}"
RELAY_PORT="${RELAY_PORT:-15000}"
mkdir -p "$STATE"
R2="$(pwd)/tools/rallypoint2"
TARGET="$(pwd)/src117/build/linux-x64/rallypoint"
BIN="$TARGET/release"

[ -x "$BIN/rally-point-coordinator" ] || cargo build -q --release -p rp-dev-setup -p rally-point-relay \
    -p rally-point-coordinator --manifest-path "$R2/Cargo.toml" --target-dir "$TARGET"

# the identity, made once and kept
[ -f "$STATE/cert.pem" ] || "$BIN/rp_dev_setup" gen-cert --cert "$STATE/cert.pem" --key "$STATE/key.pem" >/dev/null
[ -f "$STATE/tenant.txt" ] || "$BIN/rp_dev_setup" gen-tenant > "$STATE/tenant.txt"
[ -f "$STATE/seed.txt" ] || head -c 32 /dev/urandom | xxd -p -c 64 > "$STATE/seed.txt"

pkill -x rally-point-coo 2>/dev/null || true
pkill -x rally-point-rel 2>/dev/null || true
sleep 0.2

nohup "$BIN/rally-point-coordinator" \
    --listen "$HOST:$COORD_PORT" \
    --dev-tenant --tenant hyperion-dev --kid dev-key-1 \
    --allow-insecure-control \
    --tenant-key "$(sed -n '1p' "$STATE/tenant.txt")" \
    --dev-tenant-client-key "$(cat "$STATE/seed.txt")" \
    > "$STATE/coordinator.log" 2>&1 &

nohup "$BIN/rally-point-relay" \
    --listen "$HOST:$RELAY_PORT" \
    --advertise-addr "${RELAY_ADDR:-$HOST}:$RELAY_PORT" \
    --relay-id 1 --coordinator-url "http://127.0.0.1:$COORD_PORT" \
    --cert "$STATE/cert.pem" --key "$STATE/key.pem" \
    > "$STATE/relay.log" 2>&1 &

sleep 1
COORD_URL="http://${COORDINATOR_ADDR:-$HOST}:$COORD_PORT"
SERVER_NAME="$(openssl x509 -in "$STATE/cert.pem" -noout -subject 2>/dev/null | sed 's/.*CN = //')"
cat <<EOF
netcode server up (state in $STATE, logs beside it):
  coordinator:  $COORD_URL
  relay:        ${RELAY_ADDR:-$HOST}:$RELAY_PORT (cert name "$SERVER_NAME")

lanroom.json for every room that plays online:
  "sbCoordinator": "$COORD_URL",
  "sbTenant": "hyperion-dev",
  "sbKid": "dev-key-1",
  "sbClientSeed": "$(cat "$STATE/seed.txt")",
  "sbServerName": "$SERVER_NAME"
EOF
