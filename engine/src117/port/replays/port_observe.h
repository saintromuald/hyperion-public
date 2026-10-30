// port_observe.h -- what the engine hands the replay harness's observer
// (tools/replaytest/rt_observer.cpp), shared by both. The observer reads the
// oracle's game state from 1.17's addresses; an engine build hands it the same
// values through oc_observe, in fixed widths, so the log does not depend on
// where the engine keeps them or on its word size.
#pragma once
#include <stdint.h>

#define OC_OBSERVE_VERSION 11

// The values the log's G, F and P lines and the harness's run control read.
struct OcObserveState {
  uint32_t dwGameMode;       // gwGameMode: 3 in a game, 4 in the menus
  int32_t nGlueScreen;       // glGluesMode, the menu screen
  uint32_t dwFrame;          // the game's frame counter
  uint32_t dwPlayback;       // gPlayback: nonzero while a replay plays
  uint32_t dwReplayFrames;   // the replay's length in frames
  uint32_t dwReplaySeed;
  uint32_t dwGameSpeed;      // gOptions, the speed 0 to 6
  uint32_t dwMsPerFrame;     // sgTicksPerFrame_g at that speed, 0 past 6
  uint32_t dwRandState;      // sgNetRandState_g
  uint32_t dwRandCount;      // sgNetRandCount_g
  int32_t anMinerals[12];
  int32_t anGas[12];
};

// A unit of the used list, with the fields the log's hash takes, in 1.17's
// widths. Slots count from 1 in the unit table, as unit ids do; 0 is none.
struct OcObserveUnit {
  uint32_t dwSlot;
  uint32_t dwTypePlayer;     // wType | bPlayer << 16
  uint32_t dwHitPoints;      // lHitPoints
  uint32_t dwShields;        // lShields
  uint32_t dwEnergy;         // wEnergy
  uint32_t dwPosition;       // wPositionX | wPositionY << 16
  uint32_t dwOrder;          // bOrderId | bOrderState << 8
  uint32_t dwOrderTarget;    // pOrderTarget's slot
  uint32_t dwFlags;          // dwFlags
};

// A slot's numbers, read through the player tables, so the slots of a Hyperion game past
// 1.17's 12 are there too (version 9).
struct OcObserveSlot {
  int32_t nMinerals, nGas;
  int32_t nMineralsGathered, nGasGathered;
  uint32_t adwScores[28];    // glGameCounts_g's rows, as the score screen reads them
  uint32_t dwTables;         // a hash of the slot's unit counts, tech and upgrades
};

// A Hyperion game's or replay's check of its state (port_hxreplay.h): its state (none,
// recording, ok, mismatch, refused, as REPLAY_SYNC_*), the checks recorded and made, and
// the first frame that differed with its domains.
struct OcObserveSync {
  uint32_t dwState, dwRecorded, dwChecked, dwFirstBad, dwBadDomains;
  char szReason[64];
};

// The state's hash now, a domain at a time (port_synchash.h's SyncHash).
struct OcObserveHash {
  uint32_t dwFrame, dwRandState, dwRandCount, dwReserved;
  uint64_t aqDomain[6];
};

// oc_observe's answer. The unit walk stops at nMax units, and sets *pbBroken
// when a link leaves the unit table, where the walk stops too. The calls are
// __cdecl written out: the engine compiles with /Gr, which makes an unmarked
// function __fastcall there and __cdecl in the observer.
struct OcObserveApi {
  uint32_t dwVersion;        // OC_OBSERVE_VERSION
  void(__cdecl *pfnState)(struct OcObserveState *pState);
  uint32_t(__cdecl *pfnUnits)(struct OcObserveUnit *pUnits, uint32_t nMax, uint32_t *pbBroken);
  const uint8_t *(__cdecl *pfnPalette)(void);  // 256 entries of r, g, b, flags
  // The harness's controls: the window counted as active, the updates per frame
  // (0 leaves them), and the cursor's animation held at its first frame until
  // the clock reaches dwNextTick.
  void(__cdecl *pfnControl)(uint32_t bKeepActive, uint32_t dwUpdates);
  void(__cdecl *pfnHoldCursor)(uint32_t dwNextTick);
  void(__cdecl *pfnCentre)(int32_t nTileX, int32_t nTileY);  // the view, on a megatile
  // Ends the active menu dialog with result nResult, as its button with that
  // index does. The menu script uses it for a button a screen does not show.
  void(__cdecl *pfnEndDialog)(int32_t nResult);
  // The address where the engine keeps what 1.17 kept at 0x4EB000, its .rdata and
  // .data in 1.17's layout (oc_data.h): a global at 1.17's address va is at this
  // plus va - 0x4EB000.
  uintptr_t(__cdecl *pfnDataBase)(void);
  // The game's window, and the active menu dialog or NULL: pointers, which an x64
  // engine does not keep at their places in that block.
  void *(__cdecl *pfnWindow)(void);
  const void *(__cdecl *pfnGlueDialog)(void);
  // Where the engine keeps what 1.17 kept at va, and the reverse, the 1.17 address an
  // engine address stands for, or 0: the globals with a pointer in them are not in
  // the block but in storage of their own.
  uintptr_t(__cdecl *pfnAddress)(uint32_t va);
  uint32_t(__cdecl *pfnOriginal)(uintptr_t p);
  // Saves the game under pszName, as the save menu's command does, with dwTimestamp
  // in place of the time, so that two runs write the same file; nonzero on success
  //.
  int32_t(__cdecl *pfnSave)(const char *pszName, uint32_t dwTimestamp);
  // Seeds every single-player game from now on with dwSeed in place of the clock, so
  // that two runs of a loaded game agree.
  void(__cdecl *pfnPinSeed)(uint32_t dwSeed);
  // The presenter in use ("sdl ..." or the GPU driver's name) and the GPU presenter's
  // check so far: frames read back and compared, and those that differed
  // (docs/renderer.md, R4a).
  void(__cdecl *pfnPresenter)(char *pszName, uint32_t nCap, uint32_t *pnChecked, uint32_t *pnMismatched);
  // Version 9. The slots in use, 12, or every one a Hyperion game has; a slot's
  // numbers; the unit table's size; the state's check; and its hash now.
  uint32_t(__cdecl *pfnSlots)(void);
  void(__cdecl *pfnSlot)(uint32_t nSlot, struct OcObserveSlot *pSlot);
  uint32_t(__cdecl *pfnUnitCap)(void);
  void(__cdecl *pfnSync)(struct OcObserveSync *pSync);
  void(__cdecl *pfnHash)(struct OcObserveHash *pHash);
  // Version 10. The engine calls the function set here after each frame of a Hyperion game,
  // once the frame's update and its check are done and before the saves a replay makes
  // again (port_hxreplay.cpp): where a frame is read whole. A clock call can come from
  // inside a frame, and a replay makes some at other frames than its game did.
  void(__cdecl *pfnSetFrameHook)(void(__cdecl *pfnFrame)(void));
};

typedef const struct OcObserveApi *(__stdcall *PFN_OC_OBSERVE)(void);

// A single-player game's seed (new_game): nClock, the clock's, unless the replay
// harness pinned one through oc_observe (port_observe.cpp).
int __cdecl PortObserveSeed(int nClock);

// After a Hyperion game's frame and its check (port_hxreplay.cpp's ReplayHxFrame): the harness's
// observer reads the frame.
void ObserveFrameDone();
