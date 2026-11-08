# Completed `ApplyImageState` integration

The HXP4 receiver now follows this checklist. The hardened bridge's
publication, ownership, visibility, lifecycle, and error handling structure is
preserved. This document remains the field-by-field reference for that work.

## 1. Parse HXP4 explicitly

Keep the existing HXP2/HXPF/HXP3 reader unchanged. Add `HXP4` to accepted magic.
After the common 24 bytes (`magic, frame, nUnits, mapW, mapH, nPlayers`), HXP4
requires eight more bytes:

```cpp
uint32_t sizeWords[2];
ReadRequired(sizeWords, sizeof sizeWords, "HXP4 section sizes");
if (sizeWords[0] != pluto_late::kHxp4ExtraBytes ||  // 78
    sizeWords[1] != pluto_late::kPlayerTechBytes) {  // 119
  reject("HXP4 explicit section sizes do not match version 4");
}
```

The header is therefore exactly 32 bytes. Read, in order:

1. `nUnits * 40` existing unit records;
2. `nUnits * 78` packed `pluto_late::UnitExtra` records (the first 64 bytes are
   the old extra);
3. `nPlayers * 28` existing player records;
4. `nPlayers * 119` packed `pluto_late::PlayerTech` records;
5. `mapW * mapH * 4` existing tile flags.

Validate every unit with the shared helper before writing SC state:

```cpp
std::string error;
for (unsigned i = 0; i < nUnits; ++i) {
  if (!pluto_late::ValidateHxp4Unit(units[i], extras[i], &error))
    reject(error);
}
```

This enforces slot 0..4, queue types 0..228, active `buildType`, and research
neutrality. HXP3 keeps 64-byte extras and its one-active-entry behavior. HXP3
with trailing bytes is not HXP4.

## 2. Extend `ScUnit` without changing its 0x150 layout

Replace the opaque bytes at native `+0xAE..+0xCF` only with this equivalent
layout (other members stay as they are):

```cpp
uint8_t  _pAE[0xC6 - 0xAE];
uint16_t upgradeResearchTime;  // 0xC6
uint8_t  techType;             // 0xC8
uint8_t  upgradeType;          // 0xC9
uint8_t  _pCA[0xD0 - 0xCA];
```

Retain `static_assert(sizeof(ScUnit) == 0x150)` and add offset assertions for
`buildQueue == 0x98`, `buildQueueSlot == 0xA4`,
`upgradeResearchTime == 0xC6`, `techType == 0xC8`, and `upgradeType == 0xC9`.

At the existing per-unit reset point, HXP4 applies:

```cpp
for (int q = 0; q < 5; ++q)
  su.buildQueue[q] = extras[i].buildQueue[q];
su.buildQueueSlot = extras[i].buildQueueSlot;
su.upgradeResearchTime = 0;
su.techType = pluto_late::kTechNone;          // 44
su.upgradeType = pluto_late::kUpgradeNone;    // 61
if (units[i].order == pluto_late::kResearchTechOrder) {  // 75
  su.upgradeResearchTime = extras[i].remainingResearchTime;
  su.techType = extras[i].techType;
} else if (units[i].order == pluto_late::kUpgradeOrder) { // 76
  su.upgradeResearchTime = extras[i].remainingResearchTime;
  su.upgradeType = extras[i].upgradeType;
}
```

HXP3 continues to fill all queue entries with 228 and then only the active
entry. HXP2/HXPF leave all entries empty. Do not serialize pointer/hangar union
arms as research.

## 3. Write native `BWGame` player tech

For every reordered player `p`, use `playerTech[p]`. With
`unsigned char *gg = Sc(SC_GAME)` (`SC_GAME == 0x57F0F0`):

```cpp
for (unsigned t = 0; t < 44; ++t) {
  if (t < 24)
    gg[0xDE54 + p * 24 + t] = playerTech[p].hasResearched[t];
  else
    gg[0x10050 + p * 20 + (t - 24)] = playerTech[p].hasResearched[t];
}
for (unsigned u = 0; u < 61; ++u) {
  if (u < 46)
    gg[0xE1C0 + p * 46 + u] = playerTech[p].upgradeLevel[u];
  else
    gg[0x1023C + p * 15 + (u - 46)] = playerTech[p].upgradeLevel[u];
}
memcpy(gg + 0x10140 + p * 6,
       playerTech[p].techInProgress.data(), 6);
memcpy(gg + 0x102F0 + p * 8,
       playerTech[p].upgradeInProgress.data(), 8);
```

Absolute base addresses are respectively `0x58CF44`, `0x58F140`, `0x58D2B0`,
`0x58F32C`, `0x58F230`, and `0x58F3E0`. These match BWAPI 4.4.0 `BWGame.h` and
official reads. Owner levels feed upgrade time `base + factor * currentLevel`.

Clear those dynamic fields for legacy input so an earlier HXP4 frame cannot
leak into a legacy one. Preserve existing availability/max-level initialization;
HXP4 does not describe custom availability rules.

## 4. Publish matching BWAPI data

For own/inside-access units, project the native queue into
`UnitData.trainingQueue` by starting at `buildQueueSlot`, rotating modulo five,
and stopping at the first 228 or after five entries. Raw `{8,9,10,11,12}` with
slot 2 becomes `{10,11,12,8,9}`.

- order 75: set `UnitData.tech` and `remainingResearchTime`; keep upgrade 61 and
  `remainingUpgradeTime = 0`;
- order 76: set `UnitData.upgrade` and `remainingUpgradeTime`; keep tech 44 and
  `remainingResearchTime = 0`;
- other orders: count/time/research fields retain neutral values.

Populate `PlayerData.upgradeLevel`, `hasResearched`, `isResearching`, and
`isUpgrading` from the same record. Preserve all existing enemy production and
resource masks. Do not add minitile or unit-search data.

Commit units, BWGame, UnitData, and PlayerData under the existing state lock,
before code 2 and before frame-zero `onStart`. The next write must retain the
current acknowledgement/backpressure rule.

## 5. Host tests to add with the receiver edit

Reuse `late-state` fixtures and add bridge-side assertions for:

- all five `ScUnit` words and active slot;
- research order 75 and upgrade order 76, plus neutral non-research arms;
- SC/BW researched/current-level split tables and unified progress bitsets;
- rotated UnitData queue and enemy masking;
- wrong size words, queue/research violations, truncation, and trailing bytes;
- mixed HXP2/HXPF/HXP3/HXP4 input with unchanged legacy fallbacks;
- state publication occurring only after all late-state writes.

No game or inference launch is needed for these checks.
