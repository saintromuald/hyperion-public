# Replay Kraken workspace

## Protocol revision and fixtures

The client targets the self-hosted Replay Kraken protocol, not cosmonarchy.com. The pinned
reference is `/home/saintbellarmine/Projects/replay-kraken` **0.1.0**, Git
`bdc2308292013487fb13cc0983893a58c945f87f`, including its then-current uncommitted API changes.
Relevant source hashes (SHA-256):

| File | Hash |
|---|---|
| `README.md` | `dcaf5b6502fa0a95e3743c35ca8fb1a0f410b9ca4bdbd95574050d42d191fc03` |
| `src/model.rs` | `f895e8b9277b9ae65e9e9f81f170509cd07698f09dfa6017f5d7fbde4463eedf` |
| `src/api.rs` | `1dfdf5a837e4783ef39cc2d18fbf2f1e8bb0abd9fdbc9e21d7eeca68babe9fe0` |
| `src/db.rs` | `0d7a1e5b07fa4f849bcb7c81d0be88eacaee613fb96cf5ab5c82673468fd708d` |
| `Cargo.lock` | `a42b55a6912e957f32ed83d9392aa516ce47bccc081055d7522d6a11b9890d62` |

Versioned native fixtures are in `engine/tools/replay-tests/fixtures/`: two list pages with
Unicode/long names, player type and Unknown results. Core tests compile the production sources.
No live archive or real token is used.

### URL and HTTP policy

`https://host`, `https://host/` and `https://host/api` all canonicalize to
`https://host/api`. A base path is preserved: `https://host/kraken/api` is used unchanged.
There is exactly one `/api` suffix. Schemes, hosts and default ports are normalized. URL user
information, query/fragment-bearing *settings* URLs, and non-loopback plain HTTP are rejected.
Concrete request URLs may, of course, carry pagination/filter query strings.

Implemented routes:

- `GET /api/users/@me` for bearer identity validation;
- `POST /api/matches` with a stable UUID, Base64 replay, Hyperion version and authoritative metadata;
- `GET /api/matches?limit=50&offset=N` plus `player`, `race`, `result`, `matchup`, `from`, `to`,
  and `followed_by` filters;
- `GET /api/matches/{uuid}/replay` for download and preflight conflict inspection;
- `PUT`/`DELETE /api/players/{name}/follow` and `GET /api/me/following` where a token exists.

The server clamps pages to 200; Hyperion deliberately uses 50. Total is the filtered count before
paging. Search over map/uploader/version/UUID and favourites/Started/notes is explicitly labeled
*fetched rows only*. Matchup filtering uses the server's direction-insensitive normalization,
including mirrors. Server `slot` is never treated as a team/force. Missing teams, results and other
facts remain Unknown.

Native HTTPS uses pinned **libcurl 8.23.0**, Mbed TLS and the **2026-11-05 Mozilla CA bundle**.
Certificate and hostname verification are always on. HTTP is accepted only for explicit loopback
fixtures. Redirects are manual, limited to three: no HTTPS-to-HTTP downgrade, no credential
forwarding to another origin. Requests/responses have time and byte limits. Browser builds return
an explicit unavailable result instead of emulating threads.

### Upload fields and the 2 MiB protocol limit

A request always includes `matchUuid`, `gameVersion`, `replayFile`, and `meta.players`. Map,
game type, duration frames and matchup are included when authoritative. Player name/race/slot and
human/computer type come from the replay header or captured live records. Results are omitted when
unknown; the server stores `unknown`. Desync/random artifacts are not synthesized: Hyperion state
hashes are not those file formats. Force/team and ranked/desync states are not guessed.

The unmodified server has a **2 MiB total JSON body limit**. Hyperion bounds raw replay input at
1,500,000 bytes and checks the final encoded body exactly. Oversized uploads become
**Unsupported / Protocol**, never a stripped or guessed payload. The protocol allows omitted
optional metadata and an empty `meta.players` array; it does not require invented results.

The reference server writes replay bytes before returning `409`. Therefore post-conflict bytes do
not prove the previous content. Every POST has a preflight first: an existing replay and its
available metadata are compared with the immutable snapshot. Equivalent rows become Uploaded
without another POST, different/unknown rows become Conflict / Needs review, and transient
inspection failures retry without sending a new payload. A `409` race after a clear preflight also
becomes Conflict. Metadata not found in a bounded 10,000-row inspection is treated as unprovable,
not assumed equal; a 409 race is rechecked once.

## Identity and ownership

- **File identity:** managed normalized path + size + mtime, used for incremental cache validity.
- **Content identity:** SHA-256 (`sha256:<hex>`), used for rename-safe favourites, notes, Started,
  fixed-list membership and duplicate downloads.
- **Online identity:** canonical server API + match UUID. Every normalized/cached/downloaded row
  stores both `serverOrigin` and `serverApi`, partitioned by server.
- **Native upload UUID:** a fresh UUID v4 per completed game/manual submission, persisted before
  the first network attempt and reused on every retry/restart. Repeated games are distinct even
  when deterministic replay bytes match.
- **Library record identity:** content identity locally, server-origin identity online. Downloading
  links the online record to content, merging notes/favourites/Started without losing either.

Renames retain content organization. Replacing bytes creates a new content record; duplicate
content shares it intentionally. Repeated downloads never overwrite an unrelated file: sanitized,
collision-safe names are used and existing identical bytes are reused. Downloaded provenance is
never Submit-eligible, including a copied download with its source link still present.

## Main-thread and worker ownership

Game/UI calls are main-thread only. `ReplayHxPeek`, `MapPictureRead`, `PortLaunchReplayFile`, Clay,
FrontKit, settings and compatibility decisions stay there. One ReplayOnline worker owns network,
outbox, downloads, token files and library writes. One catalog worker owns directory enumeration,
stat and content hashing; at most one cache writer is active. Workers never read mutable live game
state or invoke engine/UI functions. JSON parsing uses immutable input. Results return through
mutex-protected snapshots.

`ReplayHxPeek` cannot be moved off the main thread because it reaches game-data archives and global
expansion/mod state. It is the narrow engine-bound exception: catalog indexing parses at most
**1.5 ms per UI batch**, records each batch and the worst individual call, and reports those values
on screen close. Map preview/compatibility/playback remain main-thread. The project budget is
**2 ms for an engine-bound batch and 8 ms for ordinary list layout**, with first-use initialization
reported separately. The synthetic 10,000-summary check renders 12 rows and measures query/bound
cost; native front-end logs supply actual layout and parser costs.

The writer tees successful replay bytes into memory during the existing end-of-game write. It does
no upload file I/O, Base64 or networking on the game/UI thread. A worker then atomically commits an
immutable snapshot before Queued is shown. Thus a subsequent overwrite/delete of `LastReplay.rep`
cannot alter a pending upload or its determinism.

## Outbox, retry and retention

State lives under `<data>/replay-kraken/`:

- `jobs/<uuid>/{replay.rep,job.json,state.json,claim}`: immutable bytes/metadata/destination and
  mutable retry state, committed through an incoming directory rename;
- `downloads.json`: completed server/origin/path/content mappings;
- `cache-<origin-hash>.json`: source-partitioned normalized archive rows;
- `library.json`: favourites, Started, notes/tags, dynamic queries and fixed collections;
- `token-<origin-hash>.secret`: private-file bearer token, never settings/logs/metadata;
- `../replay-index-v1.json`: local metadata cache with its compatibility key.

The application has one outbox/scheduler, not independent GUI/engine queues. GUI startup resumes
committed jobs. A create-exclusive per-job claim, with atomic stale-rename recovery, prevents two
instances from draining one job, with owner-verified heartbeat and release. Active limits are
**64 jobs and 256 MiB**. Successful history is separately capped at 32 jobs/64 MiB and seven days;
terminal jobs have an explicit Discard action. Capacity/space failures are immediately visible,
leave committed jobs unchanged, and do not pretend that a new failure record was persisted when
storage is unavailable. Pending, failed and conflict jobs are retained until the user discards them.

Endpoint and token hashes bind each job. Changing endpoint never reroutes it. Changing a token stops
before network. Disabling automatic upload cancels only automatic activity, stops automatic
enqueue/retry, and preserves snapshots; manual Submit/Retry may proceed with `manualOverride`.
Shutdown cancels transport through `app_add_exit`, does not wait indefinitely, and resumes committed
jobs next launch. Empty/partial initialization is safe even when `_exit` skips C++ destructors.

Only transient failures retry, at most eight attempts with bounded exponential backoff, jitter and
`Retry-After` (seconds or HTTP date). Authentication/validation failures stop. A timeout reuses the
UUID and preflight resolves an already-accepted match without a second POST. Same UUID/different
payload is Conflict, never success by UUID alone.

Automatic upload defaults to **off**. The only automatic policy is eligible newly completed live
games: replay writing succeeded, not playback/benchmark/test/record mode or failed writes. AutoSave
is independent and may be off. One automatic submission per LAN room uses the lowest occupied human
slot; manual Submit remains available for local recordings and never downloaded replays. Hyperion
does not claim ranked/desync status it cannot prove.

## Organization and export schemas

`hyperion/replay-library` version 1 stores favourites, Started (accepted playback, not a claimed full
watch), per-note tags, `shared` visibility (default false), dynamic queries and fixed collections.
Dynamic queries evaluate full filters against the catalog, never captured visible rows. Fixed
collections contain record IDs and actually restrict results to members.

`hyperion/replay-library-export` version 1 is the explicit sharing schema. Users select record,
query and collection IDs and whether favourites/shared notes are included. Private notes and their
tags are excluded inside the export service itself. Import merges by stable IDs and timestamps with
lexical tie-breaking, is idempotent on reimport, and never deletes local private data. Cosmonarchy
format interoperability is not claimed.

## Keep / Adapt / Improve / Not applicable / Blocked

| Disposition | Decisions |
|---|---|
| Keep | Existing native Clay details, CHK preview, reRS/hxRS metadata/refusal, native playback, safe file actions, profile/format search. |
| Adapt | ReplayKraken pages, source-aware cache/download, native Submit and completed-game tee, favourites/Started/private notes, follow endpoints. |
| Improve | Cancellable incremental index, virtualized rows, durable immutable outbox, preflight conflict equivalence, bounded retries, atomic validated downloads, stable-ID export. |
| Not applicable | Avalonia, GPTP injection, Windows memory hooks, Cosmonarchy repository switching, OG/Plus tabs, Askosi races, OCR, menu key injection, Git historical-build switching, leaderboards/continuous online Replay TV. Ranked/desync labels are not inferred. |
| Blocked | Licensed oracle/corpus and Windows verification are environmental, not silently passed. Browser has no online feature. The protocol cannot represent force/team, authoritative results, or desync/random artifacts Hyperion lacks. |
