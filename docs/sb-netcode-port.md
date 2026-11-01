# Porting ShieldBattery netcode (rally-point2) into Hyperion

Status: **in progress** (Phases 0–6 done; Phase 7 next) · Branch: `netcode/sb-rally-port` · Owner: saintromuald

This document is the ground-up plan for giving Hyperion internet multiplayer by
integrating ShieldBattery's netcode v2 ("rally-point2"). It records the
architecture decision, the exact integration seam, the phased work, and the
known risks. Revisit the "Status" lines as phases land.

## 1. Goal and non-goals

**Goal.** Two or more Hyperions play a 1.17 lockstep match over the internet,
using the ShieldBattery rally-point2 relay stack as the transport, with no
port forwarding, decent high-latency/loss behaviour, server-side desync
detection, and clean drop/leave semantics — without changing 1.17's game
logic.

**Non-goals for v1** (deliberately parked):

- Porting ShieldBattery's *platform*: matchmaking, ladder, accounts, ladder
  maps, Electron launcher. Hyperion's room (`--room`) stays the lobby and is
  extended to hand out online sessions.
- ShieldBattery's rollback module (`game/src/rollback/`) — SCR-specific
  machinery that doesn't map onto 1.17's loop; revisit only if latency
  compensation is ever needed.
- Save/load over the network, skins, in-game chat side channel (1.17's chat
  path can keep using its own message route initially).
- Running on ShieldBattery's production relays as a tenant. The plan works
  against a self-hosted relay + coordinator; becoming a tenant on SB's
  production infra is an operations/relationship question, and the protocol
  work is identical.

## 2. What "ShieldBattery netcode" is

Two repos matter:

- `ShieldBattery/ShieldBattery` — platform + the **game DLL** (`game/`, Rust).
  The DLL hooks the *official SCR binary* at three points (`game/src/bw_scr/network.rs`,
  patched via `patching.rs`) and replaces Storm's UDP turn transport wholesale
  with rally-point2. The game-thread half of that seam is
  `rally_point_client::driver::TurnChannels` (`client/src/driver/channels.rs`).
- `ShieldBattery/rally-point2` — the netcode platform itself (Rust, MIT/Apache-2.0):

| Crate         | Role                                                        | ~Lines |
|---------------|-------------------------------------------------------------|--------|
| `proto`       | wire framing (protobuf envelope), tokens, versioning, **SC:R turn-command table** (`commands.rs`) | 1.7k |
| `transport`   | per-link reliable delivery over unreliable QUIC datagrams: ack + redundancy re-carry, no retransmit-on-timeout | 1.9k |
| `client`      | `LinkDriver` + `TurnChannels`: dial, reorder, retention, reconnect replay, buffer directives, leaves | 6k+ |
| `relay`       | validating relay: auth, per-command validation, mesh+dedup, latency-buffer consensus, 0x37 desync detection, synced leaves, reconnect replay ring | 5k+ |
| `coordinator` | control plane: relay registry, session setup, per-tenant tokens/quotas, rehome; HTTP API: `POST /session/create`, `/session/rehome`, `/sessions/alive`, `/session/evidence`, `GET /regions`, `WS /relay/control` | — |

Key properties that matter for this port:

- A **payload** is one player's command bytes for one turn (origin `seq`,
  source `slot`). A **packet** is one QUIC datagram carrying a fresh payload
  plus still-unacked ones (redundancy = loss recovery; latency budget is a
  full RTT cheaper than TCP retransmit).
- The relay **validates every turn** by parsing command bytes against the SCR
  opcode table and binds turns to the token's slot. Desync detection compares
  clients' 0x37 sync checksums from the same turn stream, server-side.
- The relay's **authority** node broadcasts `BufferDirective` frames
  (latency-buffer/turn-pipe depth changes applied at an agreed future frame)
  and `LeaveDirective` frames (synced player leaves applied at a
  survivor-reachable frame).
- A session is created by the **coordinator** (signed token + pinned relay
  cert + slot roster + initial buffer depth) and handed to the client, which
  dials the relay over QUIC and then just pumps turns.

## 3. Why Hyperion is not Cosmonarchy, and why that's good news

Cosmonarchy BW runs *inside* the official StarCraft: Remastered client, so it
inherits the SB game DLL whole: binary hooks into SCR, the Electron app, SB's
platform lobby. Hyperion is its own engine, so none of that applies. But
Hyperion already rebuilt the same *seam* in source:

| SB hook (injected into SCR) | Hyperion equivalent (plain source) |
|---|---|
| OUT — hook `send_turn_message` | `SNetSendTurn` → `port_stormnet.cpp` → provider's `LanSendTurn` (`port_stormlan.cpp`) |
| IN — replace `receive_storm_turns` | `SNetReceiveTurns` → provider's `LanReceiveTurns` |
| PIPE — replace `flush_local_turns_to_latency_depth` | `ProgressNetwork`/`DoNetworkTurn` in `gameloop.cpp` + `SNetGetTurnsInTransit` |

Because these are ordinary function calls in Hyperion's own code, the port is
an *in-source integration*, not a hooking exercise — strictly easier and far
more debuggable than what SB itself has to do on SCR. The one thing Hyperion
lacks that Cosmonarchy gets for free is the platform around the game
(launcher, session handoff UI), which the plan provides in Phase 5.

## 4. Architecture decision (and the reasoning behind it)

### Decision 1 — Reuse the Rust crates as a linked library, don't rewrite in C++

Options weighed:

1. **Link `rally-point-client` (+ `transport`, `proto`) into Hyperion as a
   staticlib behind a small C ABI shim.** ~8k lines of battle-tested lockstep
   transport comes over for free; the crates are portable (SB builds the
   client for `i686-pc-windows-msvc`, i.e. 32-bit, plus our 64-bit targets)
   and `proto` is target-agnostic with no `unsafe`. Cost: a Rust toolchain in
   Hyperion's build (its build already pins external libraries at exact
   versions — one more fits the existing `get_libraries.py` pattern) and a
   ~500-line C ABI shim.
2. **Pure C++ port** of proto framing + redundancy transport + driver over
   msquic. Estimated 10–15k lines of correctness-critical code reimplementing
   subtle behaviour (ack-beacon, unacked-window cap, reconnect replay,
   directive collapse). Months of work and permanent drift risk vs. upstream.
3. **Bridge process** (sidecar speaking Hyperion's LAN protocol on loopback,
   rally-point on the WAN side). Fast demo but a permanent wart: double
   framing, extra process lifecycle, and the LAN relay semantics don't map
   1:1 onto rally-point's (step gating vs. per-turn fan-out).

**Chosen: (1).** The deciding argument is the one SB themselves make in their
architecture doc: the data plane is deliberately *not* a standard
reliable-ordered protocol, and the edge cases (reverse-path loss, capped
unacked windows, per-slot ack beacons, at-most-one directive application)
are exactly the parts a rewrite would get wrong. SB's own docs warn reviewers
against "fixing" this into TCP-shape; copying it verbatim in Rust keeps that
correctness, and the shim is thin.

### Decision 2 — A third Storm provider (`port_stormsb.cpp`), not a change to the LAN one

Hyperion already proved the provider seam: 1.17's provider list is replaced
with a loopback (`port_stormnet.cpp`) and a LAN provider (`port_stormlan.cpp`,
registered under 1.17's LAN id `'UDPN'`). The SB port adds a **third provider**
with the same function table (`port_stormlan.h` is the shape). Reasons:

- The game code (`net_cmds.cpp`, `gameloop.cpp`, the lobby screens) calls
  `SNet*` and never knows which provider is behind it — zero game changes,
  exactly the property that made the LAN provider clean.
- The LAN/browser/WebRTC paths keep working untouched; online play is purely
  additive, and the LAN room's replay gate (`replay-test.md`) keeps covering
  the untouched paths while we develop.
- Risk isolation: a broken online transport can't break the LAN stack, and
  vice versa.

### Decision 3 — Self-host the relay + coordinator; Hyperion's room is the lobby

`rally-point-relay` and `rally-point-coordinator` are standalone Linux
binaries we can run unchanged. The coordinator is multi-tenant by design
(per-tenant Ed25519 keys sign `/session/create`), so Hyperion is one tenant
with its own key. The Hyperion room process, which already handles match
setup, map distribution, launch tokens and device management
(`port_lanroom*.cpp`), becomes the "app server" of SB's flow: it calls
`POST /session/create`, receives `{token, homeRelay{addrs, serverName,
cert}, roster, initialBufferTurns}`, and hands each seat its own slice in the
existing `launch` JSON. Nothing in the relay cares that the lobby was
Hyperion's room rather than SB's platform.

### Decision 4 — Vendor rally-point2 at a pinned rev into Hyperion's build

SB's netcode is under active development and its protocol is not a frozen
public API. We vendor the workspace (git subtree or the `get_libraries.py`
pattern) at a pinned commit, patch nothing if possible, and rebase
deliberately. The C ABI shim lives in Hyperion's tree and is the only
interface we commit to keep stable; upstream churn stays below that line.

## 5. The seam, mapped exactly

### OUT (send a local turn)

- 1.17 assembles the turn and calls `SNetSendTurn(data, size)`
  (`net_cmds.cpp:2329`). `port_stormnet.cpp` routes to the active provider.
- SB provider: `SnpSendTurn` → `rp_submit_turn(bytes)` (non-blocking;
  `try_send` into `TurnChannels.outbound`, a tokio mpsc whose `try_send` is
  sync-safe). The driver assigns the payload `seq`; slot stays 0 (relay
  rebinds it from the token). 1.17's turn cap (`kTurnMost`, 16 KB in
  `port_lanproto.h`) is checked before enqueue, mirroring the LAN relay's
  behaviour; a too-large turn is a drop-worthy fault, never a silent loss.

### IN (receive peers' turns)

- 1.17 calls `SNetReceiveTurns(first, count, ...)` each network turn
  (`net_cmds.cpp:2254`). The LAN provider hands back a step of every player's
  turn for one frame, gated in the relay (`LanRelay::Steps`).
- SB provider: `rp_poll_turns()` drains `TurnChannels.inbound`
  (`Payload {slot, seq, bytes}`), and `SnpReceiveTurns` assembles the same
  all-slots-present step that `receive_storm_turns` replacement does in
  `bw_scr/network.rs` — setting `0x10000|0x20000` (present|ready) flags per
  ready slot, applying a `LeaveDirective`'s departures at their
  `apply_at_frame`, and releasing the turn-state lock before the leave pass
  (the SB hook's own note: the leave pass can re-enter OUT).
- 1.17's sync check (command 0x37) rides inside the turn bytes and is
  untouched; the relay *also* compares these server-side, which Hyperion
  gets for free because its stream carries them already (`lan.md`:
  "1.17's own sync check").

### PIPE (latency-buffer flush)

- 1.17's loop drives `ProgressNetwork()` (`gameloop.cpp:262`), which asks the
  provider for turns and paces by `GetTurnsInTransit`.
- SB provider: replace the pacing source with the driver's
  `BufferDirective` — the relay's authority decides the turn-pipe depth from
  real network conditions and stamps an apply frame; `DirectiveTracker`
  (in the Rust crate) collapses the redundant stamp stream into at-most-one
  change, and `SnpGetTurnsInTransit` reports the current depth. The session's
  `initialBufferTurns` seeds the depth at start (SB: seeding anything else
  can sit one turn ahead of the relay indefinitely). The keep-alive record
  (`LOBBY_KEEP_ALIVE = 0x05`, a bare 1-byte command buffer 1.17 flushes when
  nothing was queued) must be synthesized for peers with nothing queued and
  recognised (skipped) when relaying the local flush — copy SB's
  `netcode_v2/mod.rs` logic.

### Session life

- `rp_start_session(setup)` takes the launch JSON (token base64, addrs,
  serverName, pinned cert DER base64, roster, initialBufferTurns, pkcs8
  client key), binds a QUIC endpoint, races the relay's candidate addresses
  (staggered, in preference order), dials, spawns `LinkDriver` on a dedicated
  tokio runtime thread (Hyperion's game thread stays polled and never blocks
  — the same discipline as `lan.md`'s gotchas), and stores the `TurnChannels`
  in the provider.
- Driver-side reconnect/rehome is entirely inside the crate: channel handles
  stay valid across re-dials; the game just sees turns keep flowing or a
  terminal `DriverError`, which `rp_driver_status()` surfaces to Hyperion
  (toast/log), and terminal cases (relay dead + rehome unavailable, slot
  departed, unacked window exhausted) end the session the way the LAN
  provider's `RelayLost`/drop path does.

### Lobby / launch

- `SNetCreateGame`/`SNetJoinGame` on the SB provider are thin local
  bookkeeping (game data, player names, ids from the roster's slot mapping),
  seeded by a launch handoff exactly like `LanSetLaunch` seeds the LAN
  provider today. The relay never sees create/join; it sees turns and control
  frames.
- v1 lobby flow: Hyperion's room hosts the match as today; at Start the room
  calls the coordinator, then sends each seat a `launch` whose transport
  field says `sb` and carries that seat's setup. Map transfer reuses
  `ROOM_FILE`. Watchers reuse the feed machinery (`port_lanfeed.cpp`) — the
  room's copy still records replays, which also keeps the replay gate green.

## 6. Command-table compatibility (Phase 3 gate)

The relay rejects turns it cannot parse. Its table (`proto/src/commands.rs`)
is the SCR opcode table (fixed lengths + 6 variable select opcodes), taken
from SCR's live `process_commands` dispatcher. Hyperion runs 1.17's own
command code (`GetCommandByteSize`, `net_cmds.h`), the same encoding SCR
inherited; Hyperion's extensions (mutators, expanded unit tables, 255-unit
selection) are lobby rules and a sim-revision fingerprint, not new wire
opcodes. Expected result: **subset compatibility out of the box**. The Phase
3 deliverable proves it mechanically: a conformance test that walks every
opcode 0x00–0x67 through `GetCommandByteSize` and through `proto::commands`
and diffs the tables, plus a live two-seat game through a local relay as the
real gate. If Hyperion ever grows a new opcode, the test fails and the table
extension is a one-line change in the vendored proto.

## 7. Phased plan

Each phase ends with a gate that must pass before the next begins. Sizes are
honest estimates for one experienced systems programmer.

### Phase 0 — Environment and baselines (this week)

- [x] Fork + clone Hyperion (`saintromuald/hyperion-public`, branch
      `netcode/sb-rally-port`), upstream remote set.
- [x] Clone `ShieldBattery/ShieldBattery` and `ShieldBattery/rally-point2`
      locally for reference.
- [ ] `cargo check --workspace` clean in rally-point2 (running).
- [ ] Hyperion builds on Linux: `get_libraries.py` currently fails on Linux
      (`fetch_spirv_cross.py` imports `ctypes.WinDLL`). Fix the fetcher (guard
      the Windows-only path) or use the script's Docker route; record the fix.
- [ ] Document the pinned rally-point2 rev and the Rust toolchain floor
      (1.99 locally; CI needs the same).
- [ ] Read the replay gate workflow (`replay-test.md`) and run it once so
      Phase 3+ has a before/after.

Gate: both builds green locally; baseline replay suite green on `master`.

### Phase 1 — The C ABI shim (`rallypoint_bridge`)

- [ ] New crate in the vendored rally-point2 workspace, `crate-type =
      ["staticlib", "cdylib"]`, `#![no_mangle]` + `extern "C"` exports:
      `rp_start_session`, `rp_end_session`, `rp_submit_turn`,
      `rp_poll_turns` (drains inbound + leaves into caller buffers),
      `rp_lobby_send`/`rp_lobby_poll` (v1 may stub), `rp_submit_result`,
      `rp_set_result_expected`, `rp_leave_intent`, `rp_game_started`,
      `rp_driver_status`, `rp_buffer_depth`, `rp_slot_map`.
- [ ] One dedicated tokio runtime thread per session (spawned by
      `rp_start_session`); game thread calls are all `try_*`/polled and
      never block. All buffers caller-owned with explicit capacity args —
      no allocation inside the C ABI.
- [ ] Ownership rules written down: caller frees nothing the crate owns;
      pointers are valid only for the call's duration; turn bytes are copied
      (turn sizes are ≤16 KB, copy cost is noise).
- [ ] A C header (`rp_bridge.h`) generated by hand, plus a tiny C test
      program that starts a session against a local relay and pumps
      synthetic turns.

Gate: two instances of the C test program complete a fake lockstep session
through a local `rally-point-relay` with a `coordinator`-minted token.

### Phase 2 — `port_stormsb.cpp`: the provider

- [ ] New files `src117/port/sb/port_stormsb.{h,cpp}` following
      `port_stormlan.h`'s table exactly (`SnpCreateGame` … `SnpReceiveTurns`,
      plus `SnpGetTurnsInTransit` for the PIPE seam).
- [ ] Wire into `port_stormnet.cpp` beside `g_bLan` (a `g_bSb` switch, same
      shape), and `SNetEnumProviders` lists it under a Hyperion-chosen
      provider id so 1.17's screens can name it.
- [ ] Launch handoff: `SbSetLaunch` reading the same JSON fields as
      `LanSetLaunch` but a `transport: "sb"` variant carrying the session
      setup; `--sb-session <file.json>` CLI path for tests.
- [ ] OUT/IN/PIPE mappings exactly as §5, including the lobby keep-alive
      record and the leaves-at-apply-frame pass.
- [ ] Drop/leave behaviour: map `LeaveDirective` onto 1.17's synced-leave
      expectations (the LAN relay's `DROP` semantics are the reference), and
      surface terminal `DriverError`s as the existing toast/log paths do.

Gate: two Hyperions on one machine (`--data` folders, like the LAN test in
`lan.md`) play a full networked game through a local relay+coordinator, and
**every seat's replay passes `replay-test.md`**. This is the big gate — it
proves the lockstep is byte-stable across the new transport.

### Phase 3 — Command-table conformance

- [ ] The opcode-table diff test (§6) wired into the build.
- [ ] Live desync drill: force a seat to run a different sim revision and
      confirm the relay's 0x37 comparator reports the divergence and the
      protocol drops/evicts correctly, without the game silently splitting.

Gate: table diff is empty; the drill produces a clean, attributable desync
report.

### Phase 4 — Server bring-up + operational shape

- [ ] `rally-point-relay` + `rally-point-coordinator` runnable via one
      script: tenant key generation, relay enrollment, region config.
- [ ] Session-create flow exercised from a small CLI (`hyperion-netctl` or a
      room subcommand) — mint token, print each seat's setup JSON.
- [ ] Pin QUIC ports/UDP, document firewall needs; NAT behaviour notes
      (client endpoint rebinds for stale NAT mappings are in the crate).

Gate: fresh machine can bring up the stack from the script and pass the
Phase 1 C test against it over real WAN (two homes, or a VPS relay).

### Phase 5 — Room integration (lobby → online)

- [ ] Room host calls `POST /session/create` at match start (tenant key kept
      in the room's settings, `lanroom.json`-style).
- [ ] `launch` JSON carries the SB transport variant; seats hand it to
      `port_stormsb` exactly as today's relay/token launch is handed.
- [ ] Room copy/watchers keep working (they watch the replay feed — no
      change needed, they never join the live relay).
- [ ] Console tab: relay region/health, per-seat RTT and buffer depth from
      `rp_driver_status`.

Gate: the LAN room's own test flow (`HYPERION_LAN_BIND=127.0.0.1`, two
seats) works with the room pointed at the SB stack, then a real two-machine
internet game, replays green on all seats.

### Phase 6 — Hardening and edge behaviour

- [ ] Reconnect drills: kill the relay process mid-game (driver re-dials),
      kill it permanently (rehome via `POST /session/rehome`), sever one
      client's path for 30s (unacked-window and beacon behaviour observed,
      game must not split silently).
- [ ] Leave matrix: clean F10 quit, process kill, network death, drop
      vote — each must end identically on all seats (the LAN relay's drop
      rules are the acceptance reference).
- [ ] Buffer-directive behaviour on jittered links; record RTT/pacing
      numbers for the docs.
- [ ] Result reporting: map 1.17's result onto `rp_submit_result` (replay
      uploads can stay LAN-side via the feed).

Gate: the leave/reconnect matrix passes; replay suite green throughout.

### Phase 7 — Packaging, pinning, CI, legal

- [ ] Vendor rally-point2 at the pinned rev into Hyperion's build
      (fetch + checksum, `THIRD-PARTY.md` entry, MIT/Apache-2.0 notices;
      SB's `AGENTS.md` asks for attribution — Hyperion's `THIRD-PARTY.md`
      already carries per-library notices, which satisfies MIT).
- [ ] `BUILDING.md`: Rust toolchain requirement, the bridge build, the
      relay/coordinator runbook.
- [ ] CI: the C ABI test, the opcode-table diff, and a local-relay smoke
      test of two headless seats.
- [ ] Update `engine/docs/modules/lan.md` → add an "online" section in the
      same voice; note the parked item is un-parked.

Gate: fresh clone builds everything from scratch on Windows x64 + Linux x64
with no manual steps beyond the toolchain installs.

## 8. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| rally-point2 protocol churn (unversioned internal API) | breakage on rebase | vendor at pinned rev; shim is the only contract; rebase deliberately with the Phase 2 gate |
| Rust-in-C++ build friction on Windows/VS2026 | build flakiness | cargo-as-external-project via CMake custom target (the `get_libraries.py` pattern); staticlib only, no dynamic deps |
| QUIC (noq) portability to our targets | one target can't play | crate already builds 32-bit Windows; Linux/mac are tier-1 Rust targets; WebAssembly browser seats stay on the LAN provider (out of scope, documented) |
| Game-thread blocking in the provider | stalls/desyncs | hard rule from `lan.md`: polled, `try_*` only; the shim API is designed so blocking is impossible (no `recv` calls, only polls) |
| Turn-command table drift (Hyperion extension someday) | relay rejects new opcodes | Phase 3 diff test fails loudly; table extension is a one-line proto change |
| Two network paths (LAN + SB) confusing the lobby screens | UX bugs | launch JSON is explicit about transport; screens render the same; only `port_stormnet.cpp`'s switch knows the difference |
| SB AGENTS.md constraint (no Battle.net interference) | legal/reputational | irrelevant by construction: Hyperion never talks to Battle.net; note kept in §9 |
| Multi-platform replay gate cost | slow iteration | LAN tests run on Linux headless already; the SB gate reuses the same harness |

## 9. Licensing and attribution

- rally-point2: dual MIT / Apache-2.0. Hyperion: MIT. Compatible.
- SB's `game/AGENTS.md` asks: keep attribution (MIT requires the notice in
  copies — `THIRD-PARTY.md` covers this), and do not use their code against
  Blizzard's Battle.net. This port runs Hyperion games over Hyperion's own
  (self-hosted) relay stack and touches Battle.net in no way; note this
  stays true if the self-host decision ever changes.
- Cosmonarchy is not involved: it runs inside the official SCR client on
  SB's platform. Nothing from Cosmonarchy is copied here; the SB code we
  reuse is upstream ShieldBattery/rally-point2.

## 10. Current status

- [x] Fork, clone, branch, reference checkouts
- [x] Seam analysis (§5), architecture decision (§4)
- [x] SSH to GitHub fixed (dedicated `id_ed25519_github` account key; the
      old `github.com` config pinned the cosmonarchy deploy key, which only
      works for that one repo)
- [x] rally-point2 builds clean on Linux (cargo check --workspace)
- [x] Hyperion builds on Linux with the SB provider linked (a fetch_spirv_cross
      Linux fix, committed; `--no-sb` skips the bridge and stubs the provider)
- [x] **Phase 1**: `rallypoint2/bridge/` — the C ABI shim (`rp_start`,
      `rp_submit_turn`, `rp_poll`, `rp_lobby_send`, `rp_chat_send`, …) over
      `LinkDriver` + `TurnChannels`; `dev-setup/` (`rp_dev_setup`) for
      standalone-relay sessions; `bridge/ctest/` — two C clients exchange
      validated turns through a standalone `rally-point-relay` (the gate).
- [x] **Phase 3**: `tools/sbtest/cmdtable.sh` — the command-table conformance
      check (Hyperion's receive and lobby walkers vs the relay's table) is
      clean. It caught real gaps, all fixed in the vendored proto: 1.17's
      pause/resume (0x38/0x39) and voice-squelch (0x3A/0x3B), the whole
      pre-game lobby command family (0x3C–0x48, 0x54, 0x5B), and Hyperion's
      wide commands (0x89–0x8C, 0x8D, 0x94–0x98, 0xA9, with the new
      Variable3 rule for the selection chunk). The ctest exercises the mix.
- [x] **The lobby handshake** (the heart of Phase 2's gate): the provider now
      follows the LAN relay's step semantics — the creator's early steps form
      alone (its own slot commands carry it to lobby state 4), a peer joins
      the steps from its first turn, and EVENT_JOIN fires after the step that
      carried it (the LAN relay's JOINED ordering) so 1.17's handshake
      completes: the joiner is seated, raced, both sides set, Start Game
      pressed, and the creator's game runs **in-game lockstep through the
      relay** (sync turns validated server-side). 1.17's binary SNet messages
      (the 0x4A lobby state, 0x4F fd chunks) ride the relay's binary
      lobby-command channel; the provider's message cap (16 KB) keeps the
      chunks under the relay's frame cap. The LAN's map-download session is
      not part of the SB flow — the map is the launch's — and its three
      start gates are bypassed under `SbMapFromLaunch()` (gluChat.cpp,
      uiMulti.cpp, the SB tick).
- [x] **Phase 4**: `tools/sbtest/up.sh` brings up a dev-tenant coordinator and
      an enrolled relay; `rp_dev_setup coord-create` mints sessions through
      the signed `POST /session/create` API. The `--coordinated` game gate
      passes with the coordinator's decision maker alive (latency-buffer
      directives now reach the provider and are applied).
- [x] **The joiner's final transition** (the item above, resolved): three
      fixes, each found by a gate that had been passing falsely:
      - The joiner's game start failed its map load (no published map name —
        the LAN joiner's downloaded file publishes it; the SB flow has no
        transfer) and left via the 0x61 popup. The joiner now preloads the
        launch's own map copy at the launch.
      - The joiner's game-type block (TGAMEPARAMS) differed from the
        creator's: 1.17's join re-derives the template from the listed
        game's key, which does not reproduce the creator's (game type,
        resource mode, starting minerals), so the sims diverged at the first
        checksum (relay-side desync, frame ~35). The launch blob's whole
        32-byte block is adopted (`SbLaunchParams`).
      - The join-time stat-string copy was unbounded: the creator sends
        strlen bytes (no NUL) and SStrCopy ran past the payload into the poll
        buffer's next events, which the list parser read as the string's
        tail — deterministic once RP_EV_CONNECT events joined the stream.
- [x] **Both seats truly in-game**: `run_game.sh --coordinated` now holds two
      healthy games (600+ steps each, zero relay-side desyncs, no drop
      requests) — the earlier gates had passed on the creator alone while the
      joiner silently dropped out after the lobby.
- [x] **The desync drill** (`desync.sh --coordinated`): divergent starting
      resources make the 0x37 checksums differ; the relay's authority logs
      `relay-side desync detected` (sync_ordinal=1).
- [x] **Phase 6 part 1 — reconnect**: `reconnect.sh --coordinated` kills the
      relay mid-game and restarts it (same cert/tenant); the drivers re-dial,
      the games resume, neither seat plays on alone.
- [x] **Phase 6 part 2 — the drop**: `drop.sh --coordinated` SIGKILLs the
      joiner mid-game; the survivor sees the peer's link-down stamp
      (RP_EV_CONNECT), asks the relay's authority for a synced drop after a
      10 s grace (retried every 5 s — the relay refuses below its 30 s unlock
      floor), the EV_LEAVE clears the departed slot's step gate, and the
      survivor keeps playing. `rp_request_drop`, `rp_turn_consumed` and
      `RP_EV_CONNECT` are bridge additions (`saintromuald/rally-point2`
      branch `bridge`), subtree-pulled.
- [x] **Phase 5 — the room's online launch**: a match with
      `"transport":"sb"` in its JSON is an online match. At its start the
      room mints the session through the coordinator's control API — the
      bridge's `rp_dev_mint` (blocking, launch-time only) signs the
      `POST /session/create` with the tenant's client seed from the room's
      `lanroom.json` settings (`sbCoordinator`, `sbTenant`, `sbKid`,
      `sbClientSeed`, `sbServerName`) — and embeds each seat's setup JSON in
      its launch; all the launches go out at once, and the match goes live
      once every seat's game has begun its feed. The seats hand the launch's
      setup to the provider (`SbSetLaunchJson`) and play the same path the
      `--sb-setup` drills take; the map is the room's file transfer.
      `tools/sbtest/sb_room.sh` passes: the scripted console makes an sb
      match, the room mints a 2-slot session, and both seats reach in-game
      lockstep through the relay (the console's `start` now carries the
      match id — the same fix lands in `lan_ref.sh`).
- [ ] Phase 7: pinning, CI, packaging (and the console's transport toggle,
      which today's scripted console sets directly).
