# Porting ShieldBattery's platform (matchmaking + ladder) into Hyperion

Status: **in progress** (Phases 0–1) · Branch: `pre-release` · Owner: saintromuald

This document is the plan for giving Hyperion a Battle.net-style ladder by
adopting ShieldBattery's *platform* — accounts, matchmaking, MMR, ladder
points, divisions, seasons — while keeping Hyperion's own transport (the
rally-point2 port from `docs/sb-netcode-port.md`, which this work builds on).
It records the architecture decisions, the exact seam, the phased work, and
the risks. Revisit the "Status" lines as phases land.

## 1. Goal and non-goals

**Goal.** Two Hyperions (later: teams) press a Queue button, get matched on
skill, play a ranked 1.17 lockstep match over Hyperion's self-hosted relay
stack, and have the result feed a persistent ladder: hidden Glicko-2 MMR,
visible points that converge on the rating, divisions, seasons with
placements, promotions, bonus pool, and inactivity decay.

**Non-goals for the pre-release** (parked, revisit later):

- 2v2/team matchmaking (the Rust matchmaker's formation logic is upstream's;
  it lands after 1v1 is green end to end).
- The Electron app, the injected game DLL, and anything Windows-only from the
  ShieldBattery repo. Hyperion is its own engine and reports its own results.
- Running on ShieldBattery's production infrastructure, or touching
  Battle.net in any way (the upstream project's AGENTS.md constraint — this
  port plays Hyperion games over Hyperion's own stack).
- Tournaments, automated tournaments, replay uploads to the platform
  (Hyperion's replay feed already covers local replays), twitch integrations.

## 2. What the ShieldBattery platform is

One repo, several parts (`ShieldBattery/ShieldBattery`, MIT):

| Part | Stack | Role |
|---|---|---|
| `server/` | Node.js (Koa), PostgreSQL, Redis | accounts, sessions, lobby/chat services, HTTP API, WebSocket routes, result reporting and reconciliation, ladder jobs |
| `server-rs/` | Rust (Axum, async-graphql, SQLx) | GraphQL queries, the Rust matchmaker (queue pairing, map selection, latency, 2v2 formation) |
| `server/lib/matchmaking/rating.ts` | TypeScript | **the rating engine**: Glicko-2 MMR (rating/uncertainty/volatility), team means, inactivity decay, points + bonus pool + catch-up convergence |
| `client/` | React | the web UI (ladder, leagues, profiles, admin) |
| `common/` | TypeScript | shared types, season/division constants |
| `migrations/` | SQL | the whole schema (`matchmaking_ratings`, `matchmaking_rating_changes`, `leagues`, `league_users`, …) |
| `app/`, `game/` | Electron, Rust DLL | launch StarCraft: Remastered and hook its netcode + report results — **not applicable to Hyperion** |

The result flow upstream is: game DLL reports the outcome → Electron app POSTs
it → server reconciles both players' reports → `rating.ts` applies the rating
and ladder changes in one transaction.

## 3. Why Hyperion's shape is different, and why that's good news

The netcode port already established the seam pattern. The platform work
repeats it one layer up:

| SB mechanism (built around closed-source SCR) | Hyperion equivalent (plain source) |
|---|---|
| Electron app launches SCR and injects the game DLL | Hyperion's room and menus; the engine itself is the client |
| DLL hooks `send_turn_message` / result reporting | `port_stormsb.cpp` + an in-source result report at game end |
| SB's platform relay session creation at match start | Hyperion's room mints its own rally-point session (Decision 3) |
| Windows-only client machinery | the engine already runs on Windows, Linux and the web; the platform client is cross-platform by construction |

The one thing Hyperion does *not* get for free is the platform UI, which the
phased plan provides (Phases 2–3).

## 4. Architecture decisions

### Decision 1 — Vendor the SB platform at a pinned rev, trimmed

`engine/tools/shieldbattery/` carries the ShieldBattery tree at a pinned
upstream commit, following the rally-point2 convention (`PINNED.md` +
`THIRD-PARTY.md` entry). Trimmed on purpose: `app/`, `game/`, `integration/`,
`deployment/` and `server/public` (the built web client) are dropped, and the
pnpm workspace is adjusted to the remaining packages. The trim and the reason
for every removed directory are recorded in `PINNED.md`, so a deliberate
rebase can restore them. Keeping `client/` means the pre-release ships with a
working web surface (ladder pages, profiles) and not just a headless API.

### Decision 2 — The engine is the platform client

No Electron, no injected DLL, no Windows-only anything. Hyperion speaks the
platform's HTTP + WebSocket APIs directly: login (session cookie over HTTPS),
queue, match accept, result report, ladder reads. The room process is the
natural host for this because it already owns `lanroom.json` settings,
tenant keys and session minting; a per-seat console/UI drives it.

### Decision 3 — Matchmaking and transport stay separate layers

The platform matchmaker produces *match config* (opponent, mode, map) — it
never creates a transport session for Hyperion. Session minting stays exactly
where the netcode port put it: the room calls the self-hosted coordinator and
hands each seat its launch JSON. This keeps the two coordinate systems from
touching: SB's own relay-session code paths are simply not exercised for
Hyperion games, which is the smallest possible SB-side delta (one new game
source, see Phase 1).

### Decision 4 — The rating core moves to Rust (option 3), by parity

`server-rs/src/matchmaking/rating.rs` ports `rating.ts` (Glicko-2 update,
volatility solve, decay, points, bonus pool, catch-up) together with its test
suite. The TypeScript file remains the reference until a fixture-driven parity
suite proves the Rust port agrees with it on generated and recorded inputs;
after that the Rust implementation becomes authoritative and the TS one is
reduced to a thin caller (or removed). Same language as the matchmaker, one
rating implementation, one set of constants.

## 5. The seam, mapped

- **Login**: `POST /api/1/auth/login`-style account login; the client keeps a
  cookie jar. The engine's TLS comes from Mbed TLS, already vendored by
  `fetch_webrtc.py` for the streamed seats.
- **Queue**: the matchmaking HTTP endpoints (`server/lib/matchmaking/matchmaking-api.ts` —
  find, cancel, status, preferences) plus the match-found/accept WebSocket
  events; a plain HTTP poll fallback is acceptable for the first cut (no
  WebSocket client in the engine yet).
- **Match start**: the platform delivers match config (opponent id/name, map,
  mode, match id). Hyperion's room mints the rally-point session and both
  seats play the existing `"transport":"sb"` launch path.
- **Result**: the engine POSTs the outcome (winner, leaves, desync flag) to
  the platform's report endpoint; the server reconciles both sides and
  applies `rating.ts` in one transaction. The client then reads its new
  points/MMR/division from the ladder endpoints.
- **Ladder reads**: rankings, divisions, season info over HTTP; rendered by
  the in-game screens and/or the vendored web client.

## 6. Phased plan

Each phase ends with a gate that must pass before the next begins.

### Phase 0 — Vendor and bring-up

- [x] `pre-release` branch, created off `netcode/sb-rally-port` (the branch
      this work builds on).
- [x] This plan document.
- [ ] Vendor ShieldBattery at the pinned rev under `engine/tools/shieldbattery/`
      (trimmed per Decision 1), `PINNED.md` + `THIRD-PARTY.md` entry.
- [ ] `pnpm install` and the local dev stack (docker-compose Postgres/Redis)
      run clean on Linux from the vendored tree alone.

Gate: a fresh clone builds and boots the platform from the vendored tree.

### Phase 1 — SB-side Hyperion support (a new game source)

- [ ] A Hyperion game source in `server/lib/games/`: when the matchmaker
      matches Hyperion clients, the server produces match config for them and
      expects result reports over the normal HTTP endpoints — it never
      launches StarCraft, never mints an SB relay session, and never talks to
      the Electron app.
- [ ] Result reporting works for games whose participants are plain HTTP
      clients (no DLL, no app): a scripted two-client match lands a row in
      `matchmaking_rating_changes` and moves points.
- [ ] Map pool: confirm SB's map parsing accepts the BW maps Hyperion loads
      (same MPQ formats via StormLib); document any filter needed.

Gate: a shell script plays a fake match through the API and the DB shows
correct, reconciled rating and points changes.

### Phase 2 — The engine's platform client (C++)

- [ ] HTTP client + cookie jar + JSON in `port/platform` (JSON already
      exists: `port_json.*`), Mbed TLS for HTTPS.
- [ ] Login, queue (find/status/cancel), match accept over HTTP; handoff of
      the match config to the room's sb launch path.
- [ ] Post-game result report from the engine; desync/leave flags included.
- [ ] `lanroom.json`-style settings for the platform URL and credentials.

Gate: two Hyperions on one machine (`--data` folders, the LAN test's shape)
play a full ranked match through the local platform + local relay, and the
platform's DB shows both ratings updated with the reconciled result.

### Phase 3 — In-game screens

- [ ] Account (login) screen; queue screen with status; match-found accept;
      post-game points/MMR summary; ladder and division browsing.
- [ ] Season/placement states rendered correctly (placement countdown,
      promotion banner data from the server).

Gate: the manual flow (login → queue → match → result → ladder) is playable
end to end with no console.

### Phase 4 — The Rust rating port (option 3)

- [ ] `server-rs/src/matchmaking/rating.rs`: Glicko-2 core, volatility solve,
      team means, decay, points/bonus/convergence — ported from `rating.ts`
      with its constants and floors.
- [ ] Fixture generation from the TS implementation (recorded inputs +
      generated random walk), parity suite green on every fixture.
- [ ] The server applies ratings through the Rust path; TS reduced to the
      parity fixture generator (or dropped).

Gate: parity suite green in CI; a Phase 2-style live match produces identical
rating changes before/after the swap.

### Phase 5 — Hardening

- [ ] Result disputes and reconciliation drill (both sides disagree, one side
      never reports, drop vs. quit).
- [ ] Placement and smurf gates (placement match count, ranked unlock rules),
      bans and restrictions for ladder play.
- [ ] Season rollover drill (points wipe, MMR carries, division re-placement).
- [ ] Ops: single-machine bring-up script for the platform + relay +
      coordinator; backup and update runbook.

Gate: the leave/dispute matrix passes; a season rollover runs cleanly from
the runbook.

## 7. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| ShieldBattery API churn (unversioned client protocol) | breakage on rebase | pinned vendor; the trim and the Hyperion-specific delta are recorded in `PINNED.md`; rebase deliberately behind the Phase 1 gate |
| TLS/cookie handling in a C++ client | login broken or insecure | Mbed TLS is already vendored; cookie jar is a small, well-defined piece; dev loop can use plain HTTP to localhost |
| No WebSocket client in the engine | match-found events need WS | HTTP poll fallback for Phase 2; a minimal WS client only if polling proves unusable |
| Map-pool format drift (SB's parser vs Hyperion's loader) | matches on maps a seat can't load | verify in Phase 1; both read the same MPQ formats through StormLib-family code |
| Two coordinate systems (SB relay sessions vs Hyperion's) | double-minted or orphaned sessions | Decision 3: the platform never creates transport for Hyperion games; the room remains the only minter |
| Rating port drift | two rating implementations disagree | Decision 4: fixture parity gate before the Rust path becomes authoritative |

## 8. Licensing and attribution

- ShieldBattery: MIT. Hyperion: MIT. Compatible; the `THIRD-PARTY.md` entry
  carries the notice and the pinned rev (the upstream project's AGENTS.md asks
  for attribution, which this satisfies).
- The port plays Hyperion games over Hyperion's own self-hosted relay and
  platform stacks and talks to Battle.net in no way; this stays true by
  construction (same note as the netcode port).
- Nothing from Cosmonarchy is involved; the reused code is upstream
  ShieldBattery.

## 9. Current status

- [x] `pre-release` branch created off `netcode/sb-rally-port`.
- [x] Plan documented here.
- [ ] ShieldBattery vendored at the pinned rev (Phase 0).
- [ ] Hyperion game source in the vendored server (Phase 1).
- [ ] Engine platform client (Phase 2).
- [ ] In-game screens (Phase 3).
- [ ] Rust rating port + parity (Phase 4).
- [ ] Hardening and ops (Phase 5).
