#!/usr/bin/env bash
# The Phase 1 gate (docs/sb-platform-port.md): boots the vendored ShieldBattery platform and runs
# a scripted two-client Hyperion match through it, proving the ladder's server half works without
# the Electron app, the game DLL, or a relay. See tools/hyperion-gate/gate.mjs for the driver.
#
# Prerequisites: Docker (daemon running), Node (>= 22), pnpm, a Rust toolchain (for server-rs),
# and — for the first run — a copy of `sample.env` values that match your machine (the script
# copies it to `.env`).
#
# Usage:  bash tools/hyperion-gate/gate.sh
set -euo pipefail

cd "$(dirname "$0")/../.."

if ! docker info > /dev/null 2>&1; then
  echo "gate: FAIL — the Docker daemon is not running; start it first" >&2
  exit 1
fi

export SB_DISABLE_THROTTLING=1

NODE_PID=""
RS_PID=""

cleanup() {
  set +e
  [[ -n "${NODE_PID}" ]] && kill "${NODE_PID}" 2>/dev/null
  [[ -n "${RS_PID}" ]] && kill "${RS_PID}" 2>/dev/null
  wait 2>/dev/null
}
trap cleanup EXIT

echo "gate: 1/6 — database and redis (docker compose)"
docker compose up -d db redis
until docker compose exec -T db pg_isready -U postgres -q 2>/dev/null; do
  sleep 1
done

echo "gate: 2/6 — env file"
[[ -f .env ]] || cp sample.env .env
# Export the env for the Rust server (it reads the process env, unlike the Node server's dotenv),
# and pin Redis to the compose file's host port (the sample's 6379 predates the mapping).
set -a
source .env
set +a
export SB_REDIS_PORT=6380
export SB_DISABLE_THROTTLING=1

echo "gate: 3/6 — the Rust GraphQL server + matchmaker (first run builds; this can take a while)"
(cd server-rs && cargo build --quiet)
(cd server-rs && cargo run --quiet > /tmp/hyperion-gate-server-rs.log 2>&1) &
RS_PID=$!

echo "gate: 4/6 — the Node server"
pnpm run start-server > /tmp/hyperion-gate-node.log 2>&1 &
NODE_PID=$!

echo "gate: 5/6 — seeding dev accounts (idempotent)"
for i in $(seq 1 30); do
  if pnpm run seed-dev > /dev/null 2>&1; then
    break
  fi
  if (( i == 30 )); then
    echo "gate: FAIL — seed-dev never succeeded (see /tmp/hyperion-gate-node.log)" >&2
    exit 1
  fi
  sleep 2
done

echo "gate: 6/6 — the scripted match"
node tools/hyperion-gate/gate.mjs
