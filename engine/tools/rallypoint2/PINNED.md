# The pinned rally-point2

ShieldBattery's netcode v2, vendored as a subtree under `engine/tools/rallypoint2/`.
The port builds the `rally-point-bridge` static library from it and ships its
relay and coordinator binaries; the engine's source needs none of it unless
the SB provider is built (`--no-sb` skips it).

- Upstream: https://github.com/ShieldBattery/rally-point2
- Fork: https://github.com/saintromuald/rally-point2
- Branch: `bridge` (the C ABI shim, on top of the upstream client)
- Pinned commit: `95dd16fd2c3f7c56109d0d07135ef54a0f66c58d`

Pull the pinned branch in with:

    git subtree pull --prefix=engine/tools/rallypoint2 \
        https://github.com/saintromuald/rally-point2 bridge --squash

The bridge's C ABI (`bridge/ctest/rp_bridge.h`) is the only contract the engine
links against; a rebase is deliberate, and the gates under `engine/tools/sbtest/`
(`run_game.sh`, `desync.sh`, `reconnect.sh`, `drop.sh`, `sb_room.sh`,
`cmdtable.sh`) run against it after one. License: MIT / Apache-2.0, kept in
`THIRD-PARTY.md`. The port plays Hyperion games over Hyperion's own
self-hosted relay stack; it talks to Battle.net in no way (the upstream
project's AGENTS.md constraint, respected).
