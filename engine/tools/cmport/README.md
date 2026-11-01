# cmport — Cosmonarchy data into Hyperion mod data

Converts the Cosmonarchy (Plus) Samase work tree into the Hyperion mod
`engine/mods/cosmonarchy/data/types.json` (`docs/mod-data.md`).

```
python engine/tools/cmport/cmport.py tables   # decode every table, verify costs
python engine/tools/cmport/cmport.py gen      # write data/types.json
python engine/tools/cmport/cmport.py check    # re-run the cost verification
python engine/tools/cmport/cmport.py --mod <path-to-mpq> gen   # other build
```

The default source is the local plus-prerelease install
(`~/Games/cosmonarchy/prefix/drive_c/Cosmonarchy/Prerelease/mpq`).

## What it decodes

- `arr/<table>.datex` — the mod's full extended dat tables, column-major (one
  file region per column, each region `rows x width` bytes). Layouts in
  `cmport.py` (kUnitsWidths and friends); every table is validated by exact
  byte size, and unit costs are re-verified against
  `~/Games/cosmonarchy/CMBW-reference.txt` (the Plus build's `plus.yml`
  overrides applied, so the expected differences are deliberate balance).
- `samase/*_ext.txt` — text extensions applied after the datex.
- `samase/config.ini` `data_rewrite` lines — applied after the extensions.
- `plus.yml` — the Plus build's cost overrides, applied last, by unit name.
- `arr/sfxdata.txt` and `arr/portdata.tbl` — the sound/portrait string tables,
  resolved to the paths the engine's mod loader wants.

## Still to decode (see `docs/cosmonarchy-port.md`)

- `mpq/datreq/*.graft` — tech trees and command-card buttons.
- `mpq/scripts/*.bin` — iscripts (some `.txt` forms ship with the mod).
- The art files (GRPs, PCX, SMK, WAV) — an art copier is the next step; the
  loader keeps running without them, drawing bases and playing nothing.
