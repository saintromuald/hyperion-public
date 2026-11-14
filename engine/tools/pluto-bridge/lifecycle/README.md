# Standalone lifecycle tracker

`tracker.h` is a fixed-bounds C++17, header-only implementation of the unit
lifecycle transitions in genuine BWAPI 4.4.0
`BWAPI/Source/BWAPI/GameUnits.cpp`. It has no BWAPI, Windows, game, or inference
dependency. Packed ids are permanent for a logical `(slot, generation)` lifetime
and are never inherited by a replacement generation.

Run the deterministic native tests:

```sh
./run-tests.sh
```

The tests cover initial/create discovery, fog evade/rediscovery, visible and
invisible death, slot reuse, morph, completion, renegade, legacy zero-generation
captures, destination caps, malformed/duplicate slots, and packed-id exhaustion.

Ordering follows `GameUnits.cpp`: Create, optional Complete, Discover, Show in
the live pass; Hide/Evade on fog; Hide/Evade/Destroy in the dying pass; then
Morph/Renegade in the secondary pass. Slot replacement retires the old identity
before creating the new one. Frame zero emits only initial Discover events, and
create-time completion is suppressed. As in BWAPI, a morph resets completion so
an already-completed morphed type emits Complete on the next frame. Detection is
kept as state but does not substitute for BWAPI visibility in accessibility or
Show/Hide transitions.
