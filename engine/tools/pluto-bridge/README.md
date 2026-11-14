# pluto-bridge: official Pluto as a server for Hyperion

The bridge runs the **official unmodified** `pluto.dll` and
`pluto/pluto_infer.exe` under 32-bit Wine. Hyperion supplies native game state;
the bridge returns the classic StarCraft commands emitted by that DLL. Policy,
build-order selection, inference, and model weights are not replaced.

The accepted v2.0 binaries are pinned by SHA-256:

- `pluto.dll`:
  `603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc`
- `pluto_infer.exe`:
  `3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc`
- `pluto_weights.bin`:
  `c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86`

The bridge opens the explicit `pluto.dll` path with read-only sharing (write and
delete sharing denied), hashes bytes from that open handle, rejects a mismatch,
and holds the handle through `LoadLibraryA`. Under Win32 sharing rules this
prevents write/rename/delete replacement between verification and execution;
it depends on Wine/the filesystem enforcing those rules and on the loader using
the same file object. It is not an in-memory hash or a signature measurement.
The post-load pathname hash is ordinary consistency evidence, not a race-safe
pre-execution check. Acceptance therefore retains recurring file-integrity
checks. Binaries and weights are local validation inputs, not source artifacts.

## Genuine StarCraft addresses

The official DLL accesses StarCraft 1.16.1 memory directly. It cannot be renamed
and run over the old relocated stand-ins.

`build.sh` therefore produces a fixed PE image with:

- image base `0x400000`;
- `.game` at `0x401000..0x800000`, an uninitialized RWX section for the SC image;
- actual bridge code/data at or above `0x801000`;
- `LARGE_ADDRESS_AWARE`, no dynamic base, and no relocation directory;
- a checked RW allocation covering the worker stack at `0xF0B5D710`.

`Sc(addr)` is the identity mapping. Unit nodes and links use genuine addresses
such as `0x59CCA8 + slot * 0x150`; `0x485A40` holds the host's `sendTurn` return
stub. No existing Wine allocation is freed or repurposed. Startup queries the
image header, first/last `.game` pages, sendTurn, `0x5124D8`, and `0x6D1260` for
committed `MEM_IMAGE`, allocation base, complete region coverage, and actual
writable/executable protection (`0x40` or Wine's `0x80`). The last `.game` page
is `0x7FF000` because the range ends at `0x800000`; literal `0x7FFFF000` is far
outside this 4 MiB reservation and is not misrepresented as a valid SC page.

The pinned official PE declares `.bss` at RVA `0x72000`, virtual size `0x2678`,
ending at RVA `0x74678`. The bridge protects the complete page-rounded RVA
`0x72000..0x75000` range and queries the first/last pages plus stores at
`0x745E0`, `0x74608`, and `0x7466C..0x74674`.

`pluto_pe.py` performs the validated post-link transformation and
`verify_pe.py` rejects an incorrect layout using explicit checks, including
section ends/nonoverlap, raw bounds, `SizeOfImage`, executable entrypoint, and
TLS internal VAs/first-callback null. Debug data is separated first to keep the
PE header bounded.

## Build and run

Place the official files locally:

- `pluto.dll` beside `pluto-bridge.exe`;
- `pluto/pluto_infer.exe` and `pluto/pluto_weights.bin` beside it as well.

Then, with llvm-mingw i686 and the BWAPI checkout described in `build.sh`:

**Host-provenance limitation:** the host build also uses the external
`PLUTO_SHIM` include overlay and a BWAPI checkout with the local header edits
listed in `build.sh`. Those dependencies affect the host ABI and are not covered
by the official DLL digest or vendored here. Pin/review them separately when
reproducing the host binary; they are not modifications to `pluto.dll`.

```sh
./build.sh
./lifecycle/run-tests.sh
./metadata/run-tests.sh
./host/run-tests.sh
wine pluto-bridge.exe < frames.bin > replies.bin
```

The build checks the PE layout and official DLL hash. HXP2/HXP3/HXP4/HXPF frames,
optional HXPM static map metadata, and HXRP replies are binary formats, not text.
See `docs/pluto-protocol.md` for their current contract and known limitations.
Lobby-only opcodes are not valid in-game replies.

## Current evidence

The reviewed official-DLL integration has passed:

- the two-frame HXP3 smoke with empty HXRP replies 0/1/2 and clean EOF;
- the 54-frame HXP3 replay with replies 0..54, 231 classic command bytes,
  and 36 strictly validated commands;
- the two-frame HXP4 replay with empty replies 0/1/2, including native queue,
  research, player-tech, rotated UnitData, masked PlayerData, and
  pre-publication evidence;
- the 54-frame HXP4 replay with replies 0..54, five nonempty replies, 168
  classic command bytes, and 24 strictly validated commands;
- all malformed HXP4 cases (wrong sizes, queue/research violations, truncation,
  and trailing bytes), rejected without a failure flush;
- inference readiness, `onStart`, correct opponent identity/race, and clean EOF;
- real synchronous and asynchronous `Client::connect` ownership replacement;
- ordered live-pipe publication, finalization, and HXPM startup;
- rejection of malformed/truncated input, invalid frame sequences, early
  disconnects, and finalization-boundary failures;
- six native host-logic tests, 14 `PYTHONOPTIMIZE`-safe PE verifier groups, ten
  lifecycle tests, HXPM codec tests, and six normal plus six sanitizer late-state
  groups.

The HXP4 bridge SHA-256 is
`eb6bc9f8d5fb6433a697c5ac839d48e5e4e1418a2c971b53dacf150d31ac3fdc`.
Matching artifacts are in `/tmp/opencode/pluto-hxp4-official`. The two-frame
HXP4 input is `dfaa3147371d320ce406ae309885ed1d823f5992302f85c6998696d004306d34`;
its reply stream is `5a20f4eb9aa5e95b86842ad7cc6e436c70815b235659e463a17d1326d1b4bcf2`.
The 54-frame HXP4 input is
`e1c3fba8209a68bcdc7ddd9dec17bc20638cca9faee5ab57c9283cdf18e0cb35`, and its
reply stream is `54c9b1159d3018961512d18d1da44e7ed0ae62ef642ac6ab72d2b2c12396a348`.
Command selection/timing is not byte-deterministic across runs, so these are not
model-response determinism claims.

These are startup, replay, and transport results only. They do not prove exact
command/model fidelity, a complete natural match, genuine BWAPI equivalence, or
competitive performance. Those require the genuine baseline and natural-series
stages.
