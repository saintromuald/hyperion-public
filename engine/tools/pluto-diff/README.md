# Pluto Phase 1 differential tooling

Offline, standard-library-Python tooling for capturing and comparing two complete
Pluto runs. It **does not start, attach to, or communicate with StarCraft, Pluto,
`pluto-bridge`, or an inference process**. Nothing in gameplay, the bridge,
`port_pluto`, or `port_bench` is changed.

The executable is `./pluto-diff`; its implementation is in `pluto_diff/`, tests
are in `tests/`, and the optional typed native snapshot inspector is
`gamedata_inspector.cpp`.

## What a trace contains

A trace directory has `manifest.jsonl` and SHA-256-content-addressed files under
`blobs/`. The first JSONL record is a `pluto-diff/v2` header, followed by one
record per game frame and a final `end` record. Version 2 deliberately replaces
the earlier incorrect boundary names; version 1 traces are rejected rather than
silently reinterpreted. Binary references contain the expected hash, exact size,
and canonical relative path. Readers verify size and hash before parsing or
opaque comparison.

The header records:

- `engine_hash`, map name and map SHA-256, ordered races, and random seed;
- bandit configuration/history identity, inference hash, explicit `pluto_dll_hash`
  and `bridge_hash` implementation identities, and opponent identity;
- `config_hash`, computed over canonical JSON for all of the above (plus any
  additional configuration fields supplied by the caller).

Every frame record contains all of the following. A field set to JSON `null` or
omitted means **not captured**, not equal. Empty bytes, `{}`, and `[]` are present
values with distinct meanings.

The boundary order is Hyperion state input → real-DLL inference request → raw
inference response → DLL classic-command translation → optional HXRP bridge reply
→ native receive/accept → native simulation. These stages are not aliases.

| Field | Canonical contents |
|---|---|
| `state_input` | Exact Hyperion→bridge HXP2/HXP3/HXP4/HXPF **state-input** bytes in a blob, plus a strict parsed numeric/metadata cache checked against those bytes. This is not Pluto's inference observation. |
| `observation` | Exact raw PLSM/BWRL_SHM inference-request bytes produced by the real DLL. The holder is versioned `PLSM/BWRL_SHM-request/v1`; payload content remains opaque and is never parsed as HXP. |
| `game_data` | Relevant numeric GameData **input** state: nested objects/arrays containing only finite numbers and booleans. No float tolerance is applied. |
| `model_response` | Exact raw PLSM/shared-memory inference-response bytes **before the DLL translates them into classic commands**. The holder is versioned `PLSM/BWRL_SHM-response/v1`; no response offsets are guessed or parsed. This is not HXRP. |
| `emitted_commands` | Command `frame`, exact classic TurnBuffer bytes emitted by the DLL after response translation, and strictly decoded commands. |
| `received_commands` | The same three items at the native receive boundary. |
| `accepted_commands` | The same three items after native filtering/application acceptance. |
| `bridge_reply` | Optional exact later HXRP reply blob with strict parsed header, classic commands, and `win`. Its internal HXRP frame must equal the containing frame record. JSON `null` means not captured. |
| `unit_state` | Numeric native state for every tracked unit on **every** frame, keyed by stable decimal unit-table slot. Every record includes `exists` (0 or 1). |
| `observation_of_change` | Events `{change_frame, observed_frame, unit_id?, field?}`. An event is stored in the record for `observed_frame`; an empty array means no observation event. |

On a non-inference frame, `observation` and/or `model_response` may be a present
zero-length blob (`size: 0`, SHA-256 of empty bytes). That is explicitly different
from a required holder that is `null` or omitted, which is incomplete capture. No
fake HXRP record is needed for an empty raw inference response. If optional
`bridge_reply` is present on only one side, comparison is incomplete because the
coverage differs; if present on both, its exact bytes are compared.

The unit ID set and fields must remain stable throughout a capture. Use an
`exists: 0` record for dead/not-yet-created units; do not omit them. This makes
unit creation/deletion and `t+1`/`t+3`/`t+4` changes unambiguous. The native
inspector emits all 10,000 `GameData.units` slots for this reason.

`finish --status complete` commits a complete contiguous trace. `partial`, a
missing end record, a frame gap, a missing stage/blob, or an uncommitted final
JSONL line produces incomplete status. `validate` checks every required stage,
structured holder, binary payload, parsed command/HXP/HXRP cache, internal frame
number, and stable unit ID/field coverage—not just blob hashes and the end
record. Comparison applies the same per-frame validation. Malformed committed
JSON, inconsistent or missing parsed caches, and hash damage are invalid rather
than divergences.

## Capture interface

The CLI is a transport adapter, not a game hook. A capture integration supplies
files already obtained at each boundary. Create a configuration JSON such as:

```json
{
  "engine_hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "map": {
    "name": "example.scm",
    "sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
  },
  "races": ["Zerg", "Terran"],
  "seed": 305419896,
  "bandit": {
    "arm": 2,
    "history_hash": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
  },
  "inference_hash": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
  "pluto_dll_hash": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
  "bridge_hash": "9999999999999999999999999999999999999999999999999999999999999999",
  "opponent": {
    "name": "reference",
    "hash": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
  }
}
```

Initialize a new trace (the directory must not already exist):

```sh
./pluto-diff init run-a --config config.json
```

For each frame, write a numeric/timing spec like this. `state-input` is the HXP
frame, `observation` is the opaque inference request, and `model-response` is the
opaque pre-DLL inference response. The request/response files may be empty on
non-inference frames. Each classic command file may also be empty, but its path
and timing are still required. `bridge-reply` is optional and, when supplied, is
one actual HXRP record.

```json
{
  "frame": 0,
  "game_data": {
    "frame": 0,
    "random_seed": 305419896,
    "players": [{"race": 0, "minerals": 50, "gas": 0}],
    "latency": 4
  },
  "emitted_frame": 0,
  "received_frame": 1,
  "accepted_frame": 4,
  "unit_state": {
    "7": {"exists": 1, "type": 37, "player": 0, "x": 100, "y": 200, "hp": 500}
  },
  "observation_of_change": []
}
```

Append and finish it:

```sh
./pluto-diff add run-a \
  --spec frame-0.json \
  --state-input state-0.hxp3 \
  --observation inference-request-0.bin \
  --model-response inference-response-0.bin \
  --bridge-reply bridge-reply-0.hxrp \
  --emitted emitted-turnbuffer-0.bin \
  --received received-0.bin \
  --accepted accepted-0.bin
./pluto-diff finish run-a --status complete
./pluto-diff validate run-a
```

For in-process capture adapters, `TraceWriter.create()` and
`TraceWriter.append_frame()` expose the same validation without CLI files. The
writer requires contiguous frames starting at zero. The tool deliberately has no
adapter that launches or instruments a process in Phase 1.

## Compare

```sh
./pluto-diff compare run-a run-b --json divergence.json --report divergence.txt
```

With no output options, the human report goes to stdout. `--json -` and/or
`--report -` select stdout explicitly. JSON includes `status`,
`first_divergent_frame`, `class`, `class_name`, a precise path, left/right values,
and detailed hashes/decoded commands/timing as applicable. The human report
prints actual numeric left/right values, not just hashes. Every binary mismatch
reports the first differing byte offset, both numeric byte values, exact lengths,
and both SHA-256 hashes. A common-prefix length mismatch reports the first added
numeric byte, marks the shorter side missing, and includes both sizes.

Exit codes are `0` equivalent, `1` diverged, `2` incomplete, and `3` invalid or a
CLI/parse error. `equivalent` requires complete traces and exact equality at
every captured stage. Matching prefixes of partial traces return `incomplete`.

Classification is deterministic and uses this pipeline order:

| Class | Meaning | First differing evidence |
|---|---|---|
| **1** | observation/state input | Same initial config, but exact HXP `state_input`, raw inference `observation`, or numeric GameData input differs. |
| **2** | model-response transport/translation | Same observation/input, but raw pre-DLL `model_response` bytes differ. DLL-emitted classic commands, received payloads, or a present-on-both `bridge_reply` differing at the same event frame also belong here. |
| **3** | native command application | Same raw response, translation, and transport, but accepted native command payloads differ at the same application event frame. |
| **4** | action latency/timing | Emitted/received/accepted event frames differ, the same command event appears in different frame records, observation-of-change timing differs, or both sides make the same unit-state transition at different frames. |
| **5** | native simulation | Accepted commands and transition timing agree, but exact numeric unit state differs. A one-sided change with no evidence of a matching later change is also numeric simulation divergence. |
| **6** | opponent/configuration mismatch | Initial map/race/seed/bandit/inference/engine/Pluto-DLL/bridge/opponent configuration differs; this check precedes frame comparison. |

Timing comparisons look ahead only inside the contiguous common capture window.
A future transition is never inferred beyond a gap. For unit state, both sides
must actually have a matching transition at different frame numbers to select
class 4, with exactly the same old and new values; same-frame different values
or different resulting transition values select class 5. This avoids relabeling
a one-sided or numerically different change as a latency issue.

## Offline worker-production stalls

Analyze two files that have already been captured; this command starts no game,
bridge, inference, or other process:

```sh
./pluto-diff worker-stalls frames.bin replies-raw.bin \
  --threshold 240 --json worker-stalls.json --report worker-stalls.txt
```

The analysis examines player slot 0 (the Pluto slot in the acceptance captures),
reported as `player_slot` in JSON. `frames.bin` uses the same strict
HXP2/HXP3/HXP4/HXPF parser as `parse-hxp`. Some captures contain a short initial
frame-zero scaffold followed by the real run.
The analyzer splits at frame-zero records and selects the **unique longest
strictly contiguous generation**. Equal-length candidates are an error rather
than an arbitrary choice. Selection indices and discarded scaffold coverage are
included in JSON.

`replies-raw.bin` is different from a clean reply stream: it can contain foreign
or resynchronised bytes. The analyzer scans for candidates but never accepts
magic alone. A candidate must have a complete strict HXRP record, a fully valid
classic command payload and trailing `win`, and exactly the next expected frame.
A strict frame-zero reply starts a new candidate generation, and the unique
longest generation is selected. Noise and wrong-frame or malformed candidates
are counted in diagnostics. If a genuine expected record is absent, later frame
numbers are not silently accepted as a repair; reply coverage becomes
incomplete. The existing `parse_reply_stream` remains strict and non-resyncing.

The worker commands are exactly:

| Race | Native command | Produced worker |
|---|---|---|
| Zerg | `UnitMorph` opcode `0x23`, unit type `41` | Drone |
| Terran | `TrainUnit` opcode `0x1F`, unit type `7` | SCV |
| Protoss | `TrainUnit` opcode `0x1F`, unit type `64` | Probe |

Every decoded request records its emitted frame, expected application at
`emitted + 3`, exact command and reply bytes, all decoded commands, selected
unit IDs, and exact minerals/gas/supply snapshots at emission and expected
application. HXP supply is Brood War's doubled numeric supply, so a worker needs
two numeric supply units. Selected-unit snapshots retain the numeric unit type,
`order`, `completed`, `build_type`, `order_unit_type`, remaining build time, and
production links. If classic-command and HXP ID namespaces differ, an ID offset
is used only when every selected compatible producer in that reply demonstrates
one unique constant offset; otherwise attribution is explicitly incomplete.
HXP3/HXP4 unit identity is the full `(slot, generation)` pair, not the slot alone.
Producer selection, producer-to-worker links, production chains, and completion
events retain that pair. A generation change terminates the old chain and is
never treated as its completion, including immediate same-slot replacement by a
completed worker of the same type.

### Stall and completion evidence

A completion is a numeric worker transition to `completed=1`, not the presence
of an egg, training state, or incomplete worker. Initial completed workers do
not count as new production. Half-open intervals
`[start_frame, end_frame_exclusive)` contain only frames without such a
completion, so their exact length is `end - start`. Every interval of at least
`--threshold` frames is emitted, including a pending or eventually completed
one; `is_actionable_stall` distinguishes actual fault evidence from normal
in-progress production.

For a proven interval fault, `classification` is one of:

- `no_worker_request`: complete expected-frame reply coverage contains no
  qualifying request;
- `worker_request_rejected_or_misapplied`: a qualifying request has exhausted
  the relevant application/completion evidence window without production.

`native_rule_subtype` is `supply_blocked`, `insufficient_resources`, or
`no_state_transition` when the corresponding numeric native precondition or
state evidence proves that much. Native-rule economy checks use the expected
application snapshot; an emission-only shortage is not enough if resources or
supply changed before application. For a no-request interval, the subtype
reports the conditions at the classification frame, with
`no_state_transition` meaning that neither a request nor a production
transition is present—not that a command was rejected.

Application gets four observation frames after the expected `+4` apply frame.
This is capture-observation slack, not a second claimed native latency. If a
request has no matching production transition after that window, the numeric
apply economy distinguishes supply/resource rejection from
`no_state_transition`. Unmatched production with ambiguous unit attribution
instead remains uncertain.

Once an egg or training state starts, rejection is **not** inferred from a fixed
300-frame deadline. The valid completion window is based on the first observed
`remaining_build_time`, plus 120 frames of diagnostic post-timer grace. The
first incomplete worker observation extends that grace, and an increasing or
reset timer extends it again. This explicitly accommodates the normal Zerg
sequence larva → incomplete egg → incomplete drone → completed drone. The grace
is deliberately conservative and is not asserted to be a native build-time
constant. HXP2/HXPF have no unit extras, so if neither a remaining timer nor an
incomplete-worker transition establishes a bounded window, the request remains
pending rather than receiving a fabricated deadline. Expiry of the observed
timer plus grace is not itself proof of rejection: paused production has
`status: completion_window_expired_inconclusive`, no fault classification, and
no actionable stall unless independent cancellation, destruction, or native
rejection evidence proves one.

A request inside its valid completion window has `classification: null`,
`native_rule_subtype: pending_or_incomplete_evidence`, and status
`pending_valid_production`; it is not called stalled or misapplied. The same
uncertainty representation is used for missing reply/state coverage, ambiguous
attribution, and interrupted chains where cancellation, unit loss, and
misapplication cannot be distinguished. The analyzer does not force one of the
two fault labels when the capture cannot prove one. A completed interval similarly has no fault
classification and retains its exact completion event. If an interval contains
both a valid pending/completing request and a different proven rejected request,
the rejected request determines the interval fault while the pending request's
own non-fault status remains explicit.

`worker_requests` retains per-request evidence even when the frames file is too
short to establish a threshold-length interval. Requests after the last numeric
state frame are counted but not classified. This makes a short frames capture
with a much longer reply capture visibly incomplete instead of producing
unsupported whole-game conclusions.

## Official PLSM request/response capture

`pluto_diff/plsm.py` implements the official v2.0 PLSM v4 publication layout,
cross-checked against the unmodified DLL and inference-engine disassembly. The
mapping declares `0x85F80` bytes and capacity 1024; its request serial is at
`0x40`, its response serial at `0x80`, and its `N/W/H` request header at `0xC0`.
The inference response is exactly 40 bytes at `0x85F40..0x85F68`.

A canonical request blob is the four-byte request serial, the 48-byte request
header, and only the active payload ranges in wire order: `48*N`, `436*N`, `N`,
`N`, 20, 233, 1168, `0x8000`, `0x4000`, and 32 bytes. Reserved capacity, padding,
and the response are excluded, so stale bytes cannot cause false divergences. A
canonical response blob is the four-byte response serial followed by the exact
40-byte response. These are deterministic copies of official bytes; the harness
does not interpret the model's action fields.

`PlsmCapture` emits only newly published nonzero serials, including when two
requests or responses have identical payloads. A backward serial is an error
until `reset()` explicitly marks a new handshake. Packing reads the original
live bytes/memoryview using a bounded (eight-attempt)
serial → payload → serial publication check; it does not make one immutable
image and recheck that copy. An unstable mapping raises `PlsmError`. This relies
on the official publication contract that active payload bytes associated with
a published serial are not rewritten without a corresponding serial change;
a mapping without that guarantee requires an external writer lock. A frame
adapter should pair a response with the request having the same serial and must
not infer game-frame numbers from either serial.

## Strict binary parsers

```sh
./pluto-diff parse-commands commands.bin
./pluto-diff parse-hxp frames.bin --kind frame
./pluto-diff parse-hxp replies.bin --kind reply
```

- `commands.py` covers every packed opcode layout in genuine BWAPI 4.4.0
  `BWAPI/Source/BW/OrderTypes.h`, including all 0x0c variants, lobby commands,
  and the signed/unsigned field types. Native `0x0b` SelectRemove is included
  with the same layout as `0x09`/`0x0a`; BWAPI's `CommandFilter` explicitly
  handles it. Selection counts are limited to `BW::MAX_SELECTION_COUNT == 12`.
- Replay/chat/SC:R extended-id/Hyperion wide-id opcodes are outside this strict
  BWAPI AI-command format. Unknown opcodes reject the entire stream. There is no
  skip, prefix success, magic scan, or resynchronization.
- HXP2/HXP3/HXP4 parsing applies only to Hyperion→bridge `state_input`. Those
  frames carry four-byte tile flags; HXP3 has exact 64-byte unit extras, while
  HXP4 has an exact 32-byte header with size words 78/119, 78-byte unit extras
  (all five queue entries and the active research arm), and 119-byte player-tech
  records. Legacy HXPF frames carry the two visibility bitplanes. All lengths,
  counts, map bounds, queue/research invariants, and trailing bytes are checked;
  HXP3 never acquires HXP4 fields through a trailing 14-byte fragment.
- HXRP parsing applies only to optional later `bridge_reply` data (or explicit
  `parse-hxp` use), never to raw `model_response`. Replies parse their complete
  native command payload and trailing `win`; HXPE end records are supported too.
- Raw PLSM/BWRL_SHM request and response blobs have versioned boundary labels,
  exact lengths, and SHA-256 hashes, but no inferred field layout. The harness
  does not read PLSM response offsets or reinterpret those bytes as HXP/HXRP.
  Binary handling is safe for NULs, non-UTF-8 data, signed HXP fields, and
  concatenated HXP/HXRP records.

## Native GameData inspector

`gamedata_inspector.cpp` includes genuine BWAPI headers and reads typed
`GameData`, `PlayerData`, and `UnitData` members. It does **not** redeclare those
structures or decode guessed offsets. Compile-time assertions cover key struct
sizes/offsets; runtime checks verify every listed offset and require
`sizeof(GameData) == 33017048`. The input may be that exact struct size or the
page-rounded file-mapping size (33017856 bytes for the genuine snapshot); only
`sizeof(GameData)` bytes are decoded. MSVC also gets a compile-time assertion for
the total size. A mismatch refuses inspection.

Build with the include path for a BWAPI 4.4.0 checkout and the ABI that produced
the snapshot:

```sh
BWAPI_INCLUDE=/path/to/bwapi/bwapi/include
c++ -std=c++17 -O2 -I"$BWAPI_INCLUDE" \
  gamedata_inspector.cpp -o gamedata_inspector
./gamedata_inspector --layout-only
./gamedata_inspector genuine-gamedata.bin > gamedata.json
```

`--layout-only` emits header-derived sizes and actual/expected offsets as JSON;
it returns 2 when the ABI is not the requested capture ABI. On this workspace's
native x86-64 GCC and 32-bit MinGW layouts, the headers report
`sizeof(GameData) == 33017048`; the reference file is 33017856 bytes because its
mapping is page-rounded. The inspector accepts that pair and decodes only the
struct bytes. Other ABIs still require the matching capture compiler/ABI.

On a matching ABI, output contains `layout`, canonical numeric `game_data`, and a
stable all-slots `unit_state` object suitable for a capture adapter. It includes
global timing/frame values, player economy/supply/scores, selected units, and
per-unit position, health, cooldowns, orders, targets, production, movement, and
status fields. Output is intentionally large because every slot/field is kept on
every snapshot. Non-finite floating-point values are rejected.

The inspector is an offline file reader. It neither maps live shared memory nor
runs any game/inference code.

## Tests

From this directory:

```sh
python3 -m unittest discover -s tests -t . -v
```

The deterministic synthetic suite covers all six divergence classes, isolated
HXP-only, raw-observation-only, raw-response-only, DLL-emitted-classic-only, and
bridge-reply-only differences, exact first-frame selection, present-empty versus
missing inference payloads, same commands at different frame numbers,
`t+1`/`t+3`/`t+4` unit changes, partial/missing data, malformed JSONL and blob
integrity, every supported opcode and its truncation, unknown opcodes, every
truncated HXP/HXRP prefix, legacy HXPF, concatenated streams, and binary-safe
signed/non-UTF-8 fields. Worker-stall tests cover no request, supply and resource
blocks, rejected/no-transition requests, SCV/probe training, a pending valid egg
and its incomplete-drone phase, paused/expired inconclusive production,
immediate generation replacement, completion evidence, duplicate frame zero,
strict expected-frame resynchronisation, and missing reply coverage. PLSM tests
include a real writer mutating a live memoryview during serial/payload/serial
capture and bounded-retry exhaustion. The suite uses no genuine game process or
external test fixture.

## Limitations

- **Existing sparse `genuine-shm-N` snapshots are not per-frame traces.** They
  lack the required command boundaries, stable per-frame native unit state,
  timing events, and complete run identity. They cannot establish equivalence or
  all six divergence classes. A single `genuine-gamedata.bin` is likewise only a
  snapshot, not a frame timeline.
- Phase 1 supplies capture/validation/comparison primitives only. Instrumenting
  the two implementations to emit every boundary is a separate integration step.
- Exact HXP state-input and opaque inference request/response blobs include all
  captured padding and transport bytes by design. `game_data`/`unit_state`
  compare their explicitly captured numeric fields, not uninitialized native
  padding. Coverage must be identical and meaningful.
- Sparse request-only `genuine-shm-N` files may supply an `observation` boundary
  after their capture version is identified, but they do not supply raw inference
  responses or any later boundary. The tooling does not infer a PLSM response
  layout from them.
- The classic parser is not a general replay parser and does not make unknown
  commands safe by guessing their lengths.
- Wall-clock performance, GPU inference internals, and bandit behavior beyond
  the recorded identity/configuration are outside this differential schema.
