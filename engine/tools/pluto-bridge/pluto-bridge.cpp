// pluto-bridge.cpp -- run Pluto's real BWAPI 4.4.0 module against Hyperion's
// live game state. The bridge provides the client shared memory, pipe, and the
// StarCraft 1.16.1 image that this Pluto build reads. Hyperion sends HXP2 frames
// and applies the module's classic StarCraft commands from the HXRP replies.
// Pluto launches its unmodified pluto/pluto_infer.exe inference engine itself.
//
// Build with llvm-mingw i686 (build.sh); run under Wine beside pluto.dll and
// pluto/ (the real engine and weights).

#include <BWAPI/AIModule.h>
#include <BWAPI/Client/GameData.h>
#include <BWAPI/Client/Client.h>
#include <BWAPI/Client/GameImpl.h>
#include <BWAPI/Client/PlayerData.h>
#include <BWAPI/Client/UnitData.h>
#include <BWAPI/Client/Event.h>
#include <BWAPI/Client/GameTable.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace BWAPI;
using namespace BWAPIC;

static GameData *g;
static const int MAP_TILES = 128;

static void FillMap() {
  g->mapWidth = MAP_TILES;
  g->mapHeight = MAP_TILES;
  for (int y = 0; y < MAP_TILES; ++y)
    for (int x = 0; x < MAP_TILES; ++x) {
      g->getGroundHeight[y][x] = 0;
      g->isWalkable[y][x] = true;
      g->isBuildable[y][x] = true;
      g->isVisible[y][x] = true;
      g->isExplored[y][x] = true;
      g->hasCreep[y][x] = false;
      g->isOccupied[y][x] = false;
      g->mapTileRegionId[y][x] = 0;
    }
}

static void FillPlayer(int i, Races::Enum::Enum race, const char *name) {
  PlayerData &p = g->players[i];
  snprintf(p.name, sizeof p.name, "%s", name);
  p.race = race;
  p.type = PlayerTypes::Enum::Player;
  p.force = 0;
  p.isNeutral = false;
  p.isParticipating = true;
  for (int j = 0; j < 12; ++j) {
    p.isAlly[j] = (j == i);
    p.isEnemy[j] = (j != i && j < 2);
  }
  p.minerals = 50;
  p.gas = 0;
  p.supplyTotal[0] = 400;
  p.supplyUsed[0] = 6;
}

static void AddUnit(UnitTypes::Enum::Enum t, int player, int x, int y, int hp, int shields = 0) {
  const int n = g->initialUnitCount;
  if (n >= 9999)
    return;
  UnitData &u = g->units[n];
  memset(&u, 0, sizeof u);
  u.clearanceLevel = 2;
  u.id = 1000 + n;
  u.player = player;
  u.type = t;
  u.positionX = x;
  u.positionY = y;
  u.hitPoints = hp;
  u.lastHitPoints = hp;
  u.shields = shields;
  u.isCompleted = true;
  u.isIdle = true;
  u.exists = true;
  // The obs holds only what the bot can see (it plays without maphack): mark every
  // scripted unit visible to both players, or the record builder skips them all and
  // the record list stays empty.
  u.isDetected = true;
  for (int k = 0; k < 9; ++k)
    u.isVisible[k] = true;
  u.order = Orders::Enum::PlayerGuard;
  g->unitArray[n] = n;
  ++g->initialUnitCount;
  ++g->players[player].allUnitCount[t];
  ++g->players[player].completedUnitCount[t];
}

typedef AIModule *(WINAPI *FnNewAIModule)();
typedef void(WINAPI *FnGameInit)(Game *);

// The bot's Player vtable slot 4 is a race getter that writes the race id to *out
// (when non-null) and returns it in eax (Races: Zerg 0, Terran 1, Protoss 2, Random 6,
// Unknown 8). BWAPI 4.4.0 puts getType at that slot, so the bot's interface is a fork.
static int __attribute__((thiscall)) PlayerRace4(void *self, int *out) {
  const int race = static_cast<PlayerImpl *>(self)->getRace().getID();
  if (out)
    *out = race;
  return race;
}

// The fork's vPrintf thunk passes (code, format, va_list) and expects the
// callee to clean all three stack arguments. Logging the format is enough for
// the optional chat output; matching ret $12 is required for control flow.
static void __attribute__((thiscall)) GamePrint3(void *self, int code, const char *text,
                                                 va_list args) {
  (void)self;
  (void)args;
  FILE *tf = fopen("c:\\pluto-chat.log", "a");
  if (tf) {
    fprintf(tf, "[chat %d] %s\n", code, text ? text : "(null)");
    fclose(tf);
  }
}

// An empty set for the bot's set-returning Unit slots. The bot's fork-ABI returns its
// set by hidden pointer (12 bytes, likely {begin,end,cap}); the shim writes an empty one
// and returns the pointer, whatever the convention.
struct MsSet {
  void *head;
  unsigned size;
};
static struct {
  void *left, *parent, *right;
} g_emptyHead;
static MsSet g_emptyMs;

static void *__attribute__((thiscall)) UnitEmptySet(void *self, void *sret) {
  (void)self;
  if (sret) {
    memset(sret, 0, 12);  // an empty {begin,end,cap}: all null
  }
  return sret;
}

// The fork's obs walk dispatches a per-unit callback through a vtable in the
// bridge's own exe (0x200e59c0, slot 0xE0) that is NULL in our harness; the
// stub answers 1 and cleans the walk's four args.
static __attribute__((naked)) void StubRet12() {
  __asm__("movl $1, %eax\n\tret $12");
}

// --- StarCraft 1.16.1 memory emulation ---------------------------------------
//
// Pluto reads the real game's memory directly, by absolute address. patch_pluto.py
// remaps every such address onto the rig's stand-in buffers:
//   addr <  0x400000  ->  0x21000000 + addr
//   addr >= 0x400000  ->  0x21400000 + (addr - 0x400000)
// so the stand-ins must hold a miniature 1.16.1 image (the bot's reads land there).
// Offsets are BWAPI 4.4.0's (Offsets.h, BWGame.h, CUnit.h); sc-addresses.txt lists
// every address the v2.0 DLL touches.
static unsigned char *Sc(unsigned addr) {
  return reinterpret_cast<unsigned char *>(
      addr < 0x400000 ? 0x21000000u + addr : 0x21400000u + (addr - 0x400000));
}
template <typename T> static T *ScT(unsigned addr) {
  return reinterpret_cast<T *>(Sc(addr));
}

#pragma pack(push, 1)
struct ScUnit {  // CUnit, 0x150 bytes
  uint32_t prev, next;         // 0x00, 0x04
  int32_t  hitPoints;          // 0x08  (x256)
  uint32_t sprite;             // 0x0C
  uint16_t moveTargetX;        // 0x10
  uint16_t moveTargetY;        // 0x12
  uint32_t moveTargetUnit;     // 0x14
  uint8_t  _p18[0x20 - 0x18];
  uint8_t  movementFlags;      // 0x20
  uint8_t  currentDirection1;  // 0x21
  uint8_t  turnRadius;         // 0x22
  uint8_t  velocityDirection1; // 0x23
  uint8_t  _p24[0x28 - 0x24];
  uint16_t posX;               // 0x28
  uint16_t posY;               // 0x2A
  uint8_t  _p2c[0x40 - 0x2C];
  int16_t  speedX;             // 0x40
  int16_t  speedY;             // 0x42
  uint8_t  _p44[0x4C - 0x44];
  uint8_t  playerID;           // 0x4C
  uint8_t  orderID;            // 0x4D
  uint8_t  orderState;         // 0x4E
  uint8_t  orderSignal;        // 0x4F
  uint16_t orderUnitType;      // 0x50
  uint8_t  _p52[0x54 - 0x52];
  uint8_t  mainOrderTimer;     // 0x54
  uint8_t  groundCooldown;     // 0x55
  uint8_t  airCooldown;        // 0x56
  uint8_t  spellCooldown;      // 0x57
  uint16_t orderTargetX;       // 0x58
  uint16_t orderTargetY;       // 0x5A
  uint32_t orderTargetUnit;    // 0x5C
  uint32_t shieldPoints;       // 0x60
  uint16_t unitType;           // 0x64
  uint8_t  _p66[0x68 - 0x66];
  uint32_t prevPlayerUnit;     // 0x68
  uint32_t nextPlayerUnit;     // 0x6C
  uint8_t  _p70[0x80 - 0x70];
  uint32_t connectedUnit;      // 0x80 (a larva's hatchery)
  uint8_t  _p84[0x88 - 0x84];
  uint16_t previousUnitType;   // 0x88
  uint8_t  _p8A[0x98 - 0x8A];
  uint16_t buildQueue[5];      // 0x98
  uint16_t energy;             // 0xA2
  uint8_t  buildQueueSlot;     // 0xA4
  uint8_t  _pA5[0xAC - 0xA5];
  uint16_t remainingBuildTime; // 0xAC
  uint8_t  _pAE[0xD0 - 0xAE];
  uint16_t resourceCount;      // 0xD0
  uint8_t  _pD2[0xDC - 0xD2];
  uint32_t statusFlags;        // 0xDC
  uint8_t  _pE0[0xE4 - 0xE0];
  int32_t  visibilityStatus;   // 0xE4
  uint8_t  _pE8[0xEC - 0xE8];
  uint32_t currentBuildUnit;   // 0xEC
  uint8_t  _pF0[0x150 - 0xF0];
};
#pragma pack(pop)
static_assert(sizeof(ScUnit) == 0x150, "ScUnit must be a 1.16.1 CUnit");

static const unsigned SC_UNITS = 0x59CCA8;    // UnitNodeTable, 1700 x 0x150
static const unsigned SC_VIS_FIRST = 0x628430;  // UnitNodeList_VisibleUnit_First
static const unsigned SC_PLAYERS = 0x57EEE0;    // PlayerInfo[12], stride 0x24
static const unsigned SC_STATUS = 0x57F0B8;     // playerStatusArray[8]
static const unsigned SC_GAME = 0x57F0F0;       // BWGame
static const unsigned SC_SENDTURN = 0x485A40;   // BWFXN_sendTurn, called by the bot

struct HxUnit {
  unsigned id, type, player, x, y, hp, shields, energy, order;
  unsigned char visible, completed, direction, pad;
};
struct HxUnitExtra {
  unsigned statusFlags, orderTargetId, moveTargetId, connectedId, currentBuildUnitId;
  int speedX, speedY;
  unsigned buildType, previousType, resourceCount, orderUnitType;
  unsigned short moveTargetX, moveTargetY, orderTargetX, orderTargetY;
  unsigned short remainingBuildTime;
  unsigned char buildQueueSlot, orderState, groundCooldown, airCooldown;
  unsigned char spellCooldown, movementFlags, currentDirection, orderFlags;
  unsigned char detected, mainOrderTimer;
};
static_assert(sizeof(HxUnitExtra) == 64, "HXP3 unit extension size");
struct HxPlayer {
  unsigned race, minerals, gas, supplyTotal, supplyUsed;
  unsigned startX, startY;
};

static void ClearUnitData(UnitData &u) {
  memset(&u, 0, sizeof u);
  u.buildType = UnitTypes::Enum::None;
  for (int &type : u.trainingQueue)
    type = UnitTypes::Enum::None;
  u.tech = TechTypes::Enum::None;
  u.upgrade = UpgradeTypes::Enum::None;
  u.buildUnit = -1;
  u.target = -1;
  u.orderTarget = -1;
  u.rallyUnit = -1;
  u.addon = -1;
  u.nydusExit = -1;
  u.powerUp = -1;
  u.transport = -1;
  u.carrier = -1;
  u.hatchery = -1;
}

static void UpdateScMemory() {
  // The ActiveTileArray at 0x6D1260 is a POINTER to a 4-byte-a-tile array; the bot
  // indexes it by map tile. Point it at a rig buffer: visible, explored, walkable.
  static void *s_tiles;
  if (!s_tiles) {
    s_tiles = VirtualAlloc(NULL, 256 * 256 * 4, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    unsigned char *t = static_cast<unsigned char *>(s_tiles);
    for (int i = 0; i < 256 * 256; ++i) {
      t[i * 4 + 0] = 1;  // visibility flags: visible
      t[i * 4 + 1] = 1;  // explored flags: explored
      t[i * 4 + 2] = 1;  // walkable
      t[i * 4 + 3] = 0;
    }
    *ScT<void *>(0x6D1260) = s_tiles;
    fprintf(stderr, "[bridge] active tile array at %p\n", s_tiles);
  }
  ScUnit *units = ScT<ScUnit>(SC_UNITS);
  // Every slot: no unit. Type 228 = None; player 0xFF = nobody.
  for (int i = 0; i < 1700; ++i) {
    units[i].unitType = 228;
    units[i].playerID = 0xFF;
    units[i].statusFlags = 0;
    units[i].prev = units[i].next = 0;
  }
  // The scripted units, linked through the visible-unit list.
  unsigned prevPtr = 0;
  for (int i = 0; i < g->initialUnitCount && i < 1700; ++i) {
    const UnitData &u = g->units[i];
    ScUnit &su = units[i];
    su.hitPoints = u.hitPoints * 256;
    su.posX = static_cast<uint16_t>(u.positionX);
    su.posY = static_cast<uint16_t>(u.positionY);
    su.playerID = static_cast<uint8_t>(u.player);
    su.orderID = static_cast<uint8_t>(u.order);
    su.shieldPoints = u.shields * 256;
    su.unitType = static_cast<uint16_t>(u.type);
    su.energy = u.energy * 256;
    su.statusFlags = 1;  // Completed
    su.visibilityStatus = 1;  // visible
    unsigned me = 0x21400000u + (SC_UNITS - 0x400000) + i * 0x150;
    su.prev = prevPtr;
    su.next = 0;
    if (prevPtr)
      units[i - 1].next = me;
    else
      *ScT<unsigned>(SC_VIS_FIRST) = me;
    prevPtr = me;
  }
  // Players.
  for (int i = 0; i < 12; ++i) {
    unsigned char *p = Sc(SC_PLAYERS) + i * 0x24;
    if (i < 2) {
      *reinterpret_cast<unsigned *>(p + 0x00) = i;              // dwPlayerID
      *reinterpret_cast<unsigned *>(p + 0x04) = i;              // dwStormId
      p[0x08] = 2;                                              // nType: human
      p[0x09] = static_cast<unsigned char>(g->players[i].race); // nRace
      p[0x0A] = 0;                                              // nTeam
      snprintf(reinterpret_cast<char *>(p + 0x0B), 25, "%s", g->players[i].name);
    } else {
      p[0x08] = 0;
      p[0x0B] = '\0';
    }
  }
  for (int i = 0; i < 8; ++i)
    ScT<unsigned>(SC_STATUS)[i] = 1;  // playing
  unsigned char *gg = Sc(SC_GAME);
  for (int i = 0; i < 12; ++i) {
    *reinterpret_cast<int *>(gg + 0x00 + i * 4) = g->players[i].minerals;
    *reinterpret_cast<int *>(gg + 0x30 + i * 4) = g->players[i].gas;
  }
  *reinterpret_cast<uint16_t *>(gg + 0xE4) = g->mapWidth;
  *reinterpret_cast<uint16_t *>(gg + 0xE6) = g->mapHeight;
  *reinterpret_cast<uint32_t *>(gg + 0x14C) = g->frameCount;
  snprintf(reinterpret_cast<char *>(gg + 0xC4C), 0x100, "HyperionRig.scm");
  // Supplies: SuppliesPerRace[3] { available[12], used[12], max[12] } at +0x3054.
  for (int r = 0; r < 3; ++r)
    for (int i = 0; i < 12; ++i) {
      unsigned char *s = gg + 0x3054 + r * 0x90 + i * 4;
      *reinterpret_cast<int *>(s) = 0;
      *reinterpret_cast<int *>(s + 0x30) = 0;
      *reinterpret_cast<int *>(s + 0x60) = 0;
    }
  for (int i = 0; i < 2; ++i) {
    *reinterpret_cast<int *>(gg + 0x3054 + 0 * 0x90 + i * 4) = g->players[i].supplyTotal[0];  // avail
    *reinterpret_cast<int *>(gg + 0x3054 + 0 * 0x90 + 0x30 + i * 4) = g->players[i].supplyUsed[0];  // used
  }
  // Unit counts: all[type][player], 12 dwords a type row, at +0x3234. The
  // compiled base of g->players misbehaves here under Wine; the bot reads the
  // counts for its opening choice, so a stale zero table is acceptable.
  for (int i = 0; i < 12; ++i)
    for (int t = 0; t < 234; ++t)
      *reinterpret_cast<int *>(gg + 0x3234 + t * 0x30 + i * 4) = 0;
  // NetMode (LAN), gwGameMode.
  *reinterpret_cast<int *>(Sc(0x59688C)) = 2;
  *reinterpret_cast<uint16_t *>(Sc(0x596904)) = 0;
  // The bot calls SC's sendTurn (0x485A40): a ret stub in the stand-in.
  Sc(SC_SENDTURN)[0] = 0xC3;
  // The bot treats 0x400001 as a string: an empty one.
  Sc(0x400001)[0] = '\0';
}

// The production path: the Hyperion frame is written straight into the SC image
// (the fork's input). The fork's own server converts the image into the GameData
// every frame -- the GameData is the fork's private output, and the bridge writing
// it concurrently broke the server's unit sync (the bot ended up observing one
// zero unit and issuing no commands).
static void ApplyImageState(const HxUnit *units, const HxUnitExtra *extras,
                            unsigned nUnits, const HxPlayer *players,
                            unsigned nPlayers, unsigned frame, unsigned mapW, unsigned mapH,
                            const unsigned char *rawTiles, const unsigned char *seen,
                            const unsigned char *explored, unsigned nPlaneBytes) {
  // ActiveTileArray: the same one-time setup UpdateScMemory does.
  // The tile array, sized for the map (the bench's Big Game Hunters maps are
  // wider than the old fixed 256-tile buffer): 4 bytes a tile, the fork indexes
  // it by the map's width.
  static void *s_tiles;
  static unsigned s_tileBytes = 0;
  const unsigned need = mapW * mapH * 4;
  if (s_tileBytes < need) {
    void *bigger = VirtualAlloc(NULL, need, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!bigger) {
      fprintf(stderr, "[bridge] tile allocation failed: %u bytes for %ux%u\n", need, mapW, mapH);
      return;
    }
    s_tiles = bigger;
    s_tileBytes = need;
    fprintf(stderr, "[bridge] tile array: %u bytes for %ux%u\n", need, mapW, mapH);
  }
  if (s_tiles) {
    unsigned char *t = static_cast<unsigned char *>(s_tiles);
    if (rawTiles != NULL) {
      memcpy(t, rawTiles, need);
    } else {
      // Legacy HXPF captures carried only two visibility bitplanes.
      for (unsigned ti = 0; ti < nPlaneBytes * 8u && ti < mapW * mapH; ++ti) {
        const bool bSeen = (seen[ti >> 3] >> (ti & 7)) & 1;
        const bool bExplored = (explored[ti >> 3] >> (ti & 7)) & 1;
        t[ti * 4 + 0] = bSeen ? 1 : 0;
        t[ti * 4 + 1] = bExplored ? 1 : 0;
        t[ti * 4 + 2] = 1;
      }
    }
    *ScT<void *>(0x6D1260) = s_tiles;
  }

  ScUnit *nodes = ScT<ScUnit>(SC_UNITS);
  // The node table is set up once at start-up by UpdateScMemory and is not
  // wiped here: the fork's engine reads it concurrently, and a torn walk of a
  // half-memset table would leave its model empty.

  // Hyperion sends its stable unit-table slot as the id. The fork's server
  // tracks units by their node addresses, so keeping that slot stable preserves
  // identity as the used list reorders; command ids are 9363 + slot. The id
  // table only serves ids beyond 1700 (the test harness's legacy ids).
  static unsigned s_slotOfId[0x20000];
  static unsigned s_idOfSlot[1700];
  static bool s_inited = false;
  if (!s_inited) {
    for (unsigned i = 0; i < 0x20000; ++i)
      s_slotOfId[i] = ~0u;
    for (int i = 0; i < 1700; ++i)
      s_idOfSlot[i] = ~0u;
    s_inited = true;
  }
  // Free the table-managed slots whose ids left the stream.
  for (int s = 0; s < 1700; ++s) {
    const unsigned id = s_idOfSlot[s];
    if (id == ~0u)
      continue;
    bool alive = false;
    for (unsigned i = 0; i < nUnits && i < 9999; ++i)
      if (units[i].id == id) {
        alive = true;
        break;
      }
    if (!alive) {
      if (id < 0x20000)
        s_slotOfId[id] = ~0u;
      s_idOfSlot[s] = ~0u;
    }
  }
  // Direct slots: Hyperion's stable unit-table ids.
  for (unsigned i = 0; i < nUnits && i < 9999; ++i)
    if (units[i].id < 1700) {
      s_idOfSlot[units[i].id] = units[i].id;
      if (units[i].id < 0x20000)
        s_slotOfId[units[i].id] = units[i].id;
    }
  // Fallback slots: legacy ids, lowest free.
  for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
    const unsigned id = units[i].id;
    if (id < 1700 || (id < 0x20000 && s_slotOfId[id] != ~0u))
      continue;
    for (int s = 0; s < 1700; ++s)
      if (s_idOfSlot[s] == ~0u) {
        if (id < 0x20000)
          s_slotOfId[id] = static_cast<unsigned>(s);
        s_idOfSlot[s] = id;
        break;
      }
  }
  // Clear per-type counts before the unit loop accumulates them. Clearing
  // them in the later player block would erase every count again.
  for (unsigned i = 0; i < nPlayers && i < 12; ++i) {
    PlayerData &p = g->players[i];
    for (int t = 0; t < UnitTypes::Enum::MAX; ++t) {
      p.allUnitCount[t] = 0;
      p.visibleUnitCount[t] = 0;
      p.completedUnitCount[t] = 0;
    }
  }
  auto nodeAddress = [&](unsigned id) -> uint32_t {
    if (id >= 0x20000)
      return 0;
    const unsigned slot = s_slotOfId[id];
    if (slot == ~0u || slot >= 1700)
      return 0;
    return 0x21400000u + (SC_UNITS - 0x400000) + slot * 0x150;
  };
  // 3. The dead slots are marked gone in place; the live ones are filled in
  // place (a 0x150-byte window per node, not a 1700-node one).
  for (int sl = 0; sl < 1700; ++sl)
    if (s_idOfSlot[sl] == ~0u && nodes[sl].playerID != 0xFF) {
      nodes[sl].unitType = 228;
      nodes[sl].playerID = 0xFF;
      nodes[sl].statusFlags = 0;
    }
  unsigned char visibleSlots[1700] = {};
  for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
    const HxUnit &u = units[i];
    const unsigned slot = u.id < 0x20000 ? s_slotOfId[u.id] : ~0u;
    if (slot == ~0u || slot >= 1700)
      continue;
    ScUnit &su = nodes[slot];
    memset(&su, 0, sizeof su);
    for (int q = 0; q < 5; ++q)
      su.buildQueue[q] = 0xE4;  // UnitTypes::None, not Terran_Marine
    const HxUnitExtra *e = extras != NULL ? &extras[i] : NULL;
    if (e != NULL) {
      su.moveTargetX = e->moveTargetX;
      su.moveTargetY = e->moveTargetY;
      su.moveTargetUnit = nodeAddress(e->moveTargetId);
      su.movementFlags = e->movementFlags;
      su.speedX = static_cast<int16_t>(e->speedX);
      su.speedY = static_cast<int16_t>(e->speedY);
      su.orderState = e->orderState;
      su.orderSignal = e->orderFlags;
      su.orderUnitType = static_cast<uint16_t>(e->orderUnitType);
      su.mainOrderTimer = e->mainOrderTimer;
      su.groundCooldown = e->groundCooldown;
      su.airCooldown = e->airCooldown;
      su.spellCooldown = e->spellCooldown;
      su.orderTargetX = e->orderTargetX;
      su.orderTargetY = e->orderTargetY;
      su.orderTargetUnit = nodeAddress(e->orderTargetId);
      su.connectedUnit = nodeAddress(e->connectedId);
      su.currentBuildUnit = nodeAddress(e->currentBuildUnitId);
      su.previousUnitType = static_cast<uint16_t>(e->previousType);
      su.buildQueueSlot = e->buildQueueSlot < 5 ? e->buildQueueSlot : 0;
      if (e->buildType <= 0xE4)
        su.buildQueue[su.buildQueueSlot] = static_cast<uint16_t>(e->buildType);
      su.remainingBuildTime = e->remainingBuildTime;
      su.resourceCount = static_cast<uint16_t>(e->resourceCount);
    } else {
      su.buildQueueSlot = 0;
    }
    su.hitPoints = static_cast<int32_t>(u.hp) * 256;
    su.posX = static_cast<uint16_t>(u.x);
    su.posY = static_cast<uint16_t>(u.y);
    su.currentDirection1 = e != NULL ? e->currentDirection : u.direction;
    su.velocityDirection1 = u.direction;
    su.playerID = static_cast<uint8_t>(u.player);
    su.orderID = static_cast<uint8_t>(u.order);
    su.shieldPoints = static_cast<int32_t>(u.shields) * 256;
    su.unitType = static_cast<uint16_t>(u.type);
    su.energy = static_cast<int32_t>(u.energy) * 256;
    su.statusFlags = e != NULL ? e->statusFlags : (u.completed ? 1u : 0u);
    su.visibilityStatus = (u.visible && (e == NULL || e->detected)) ? 1 : 0;
    visibleSlots[slot] = u.visible;
    if (u.player < nPlayers && u.type < UnitTypes::Enum::MAX) {
      PlayerData &p = g->players[u.player];
      ++p.allUnitCount[u.type];
      if (u.visible)
        ++p.visibleUnitCount[u.type];
      if (u.completed)
        ++p.completedUnitCount[u.type];
    }
  }
  // HXP3 carries the actual connectedUnit. Legacy captures need a nearest-town
  // approximation for larvae because they have no parent field.
  if (extras == NULL)
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
    const HxUnit &larva = units[i];
    if (larva.type != UnitTypes::Enum::Zerg_Larva || larva.id >= 0x20000)
      continue;
    const unsigned larvaSlot = s_slotOfId[larva.id];
    if (larvaSlot == ~0u || larvaSlot >= 1700)
      continue;
    long long bestDistance = 0x7FFFFFFFFFFFFFFFLL;
    unsigned hatcherySlot = ~0u;
    for (unsigned j = 0; j < nUnits && j < 9999; ++j) {
      const HxUnit &candidate = units[j];
      if (candidate.player != larva.player ||
          (candidate.type != UnitTypes::Enum::Zerg_Hatchery &&
           candidate.type != UnitTypes::Enum::Zerg_Lair &&
           candidate.type != UnitTypes::Enum::Zerg_Hive) ||
          candidate.id >= 0x20000)
        continue;
      const long long dx = static_cast<long long>(candidate.x) - larva.x;
      const long long dy = static_cast<long long>(candidate.y) - larva.y;
      const long long distance = dx * dx + dy * dy;
      if (distance < bestDistance) {
        bestDistance = distance;
        hatcherySlot = s_slotOfId[candidate.id];
      }
    }
    if (hatcherySlot != ~0u && hatcherySlot < 1700)
      nodes[larvaSlot].connectedUnit =
          0x21400000u + (SC_UNITS - 0x400000) + hatcherySlot * 0x150;
  }
  // 4. Link the slots in ascending order through the visible-unit list. The
  // list is the local player's view: only the units seen by the bot are in it,
  // and the fork's engine keeps the rest as hidden last-seen entries.
  unsigned linkOrder[1700];
  unsigned nLinked = 0;
  for (int s = 0; s < 1700; ++s) {
    if (s_idOfSlot[s] == ~0u)
      continue;
    if (visibleSlots[s] == 0)
      continue;
    linkOrder[nLinked++] = static_cast<unsigned>(s);
  }
  unsigned prevPtr = 0;
  for (unsigned k = 0; k < nLinked; ++k) {
    const unsigned slot = linkOrder[k];
    const unsigned me = 0x21400000u + (SC_UNITS - 0x400000) + slot * 0x150;
    nodes[slot].prev = prevPtr;
    nodes[slot].next = 0;
    if (k)
      nodes[linkOrder[k - 1]].next = me;
    else
      *ScT<unsigned>(SC_VIS_FIRST) = me;
    prevPtr = me;
  }
  if (nLinked == 0)
    *ScT<unsigned>(SC_VIS_FIRST) = 0;

  // Players, from the stream. Hyperion sends Pluto first, the other lobby
  // players, and neutral last; unit player ids use this same index space.
  g->playerCount = static_cast<int>(nPlayers);
  g->enemy = nPlayers > 1 ? 1 : -1;
  g->neutral = nPlayers > 2 ? static_cast<int>(nPlayers - 1) : -1;
  // The client-side GameImpl does not run BWAPI's server-side player update.
  // Keep these fields live explicitly; unit synchronization stays in the SC
  // image path below, but resources and supplies must not remain at frame 0.
  for (unsigned i = 0; i < nPlayers && i < 12; ++i) {
    PlayerData &p = g->players[i];
    p.race = static_cast<int>(players[i].race);
    p.minerals = static_cast<int>(players[i].minerals);
    p.gas = static_cast<int>(players[i].gas);
    for (int r = 0; r < 3; ++r) {
      p.supplyTotal[r] = (r == static_cast<int>(players[i].race))
                             ? static_cast<int>(players[i].supplyTotal)
                             : 0;
      p.supplyUsed[r] = (r == static_cast<int>(players[i].race))
                            ? static_cast<int>(players[i].supplyUsed)
                            : 0;
    }
    p.startLocationX = static_cast<int>(players[i].startX) / 32;
    p.startLocationY = static_cast<int>(players[i].startY) / 32;
    p.isNeutral = static_cast<int>(i) == g->neutral;
    const bool occupied = (players[i].startX != 0 || players[i].startY != 0 || p.isNeutral);
    p.type = p.isNeutral ? PlayerTypes::Enum::Neutral
                         : occupied ? (i == 0 ? PlayerTypes::Enum::Player : PlayerTypes::Enum::Computer)
                                    : PlayerTypes::Enum::Closed;
    p.isParticipating = occupied;
    for (int j = 0; j < 12; ++j) {
      const bool otherOccupied = (players[j].startX != 0 || players[j].startY != 0 ||
                                  j == g->neutral);
      p.isAlly[j] = (j == static_cast<int>(i));
      p.isEnemy[j] = (j != static_cast<int>(i) && otherOccupied && j != g->neutral);
    }
    for (int t = 0; t < UnitTypes::Enum::MAX; ++t)
      p.isUnitAvailable[t] = true;
    for (int t = 0; t < TechTypes::Enum::MAX; ++t)
      p.isResearchAvailable[t] = true;
    for (int t = 0; t < UpgradeTypes::Enum::MAX; ++t)
      p.maxUpgradeLevel[t] = 3;
  }
  g->startLocationCount = 0;
  for (unsigned i = 0; i + 1 < nPlayers && i < 8; ++i) {
    if (players[i].startX == 0 && players[i].startY == 0)
      continue;
    g->startLocations[i].x = static_cast<int>(players[i].startX) / 32;
    g->startLocations[i].y = static_cast<int>(players[i].startY) / 32;
    ++g->startLocationCount;
  }
  for (int i = 0; i < 12; ++i) {
    unsigned char *p = Sc(SC_PLAYERS) + i * 0x24;
    if (i < static_cast<int>(nPlayers)) {
      const bool neutral = i == g->neutral;
      const bool occupied = (players[i].startX != 0 || players[i].startY != 0 || neutral);
      *reinterpret_cast<unsigned *>(p + 0x00) = i;   // dwPlayerID
      *reinterpret_cast<unsigned *>(p + 0x04) = i;   // dwStormId
      p[0x08] = neutral ? 7 : occupied ? (i == 0 ? 2 : 1) : 0;
      p[0x09] = static_cast<unsigned char>(players[i].race);  // nRace
      p[0x0A] = 0;                                   // nTeam
      if (occupied)
        snprintf(reinterpret_cast<char *>(p + 0x0B), 25, "%s", i == 0 ? "Pluto" : "Bravo");
      else
        p[0x0B] = '\0';
    } else {
      p[0x08] = 0;
      p[0x0B] = '\0';
    }
  }
  for (int i = 0; i < 8; ++i)
    ScT<unsigned>(SC_STATUS)[i] =
        (players[i].startX != 0 || players[i].startY != 0) ? 1 : 0;

  // Game state, from the stream. BWGame has its own availability tables,
  // separate from PlayerData's: every ordinary melee unit/tech starts allowed.
  unsigned char *gg = Sc(SC_GAME);
  for (unsigned i = 0; i < nPlayers && i < 12; ++i) {
    for (int t = 0; t < 228; ++t)
      gg[0x18C + i * 228 + t] = 1;
    for (int t = 0; t < 24; ++t)
      gg[0xDD34 + i * 24 + t] = 1;
    for (int t = 0; t < 46; ++t)
      gg[0xDF98 + i * 46 + t] = 3;
    for (int t = 0; t < 20; ++t)
      gg[0xFF60 + i * 20 + t] = 1;
    for (int t = 0; t < 15; ++t)
      gg[0x10188 + i * 15 + t] = 3;
  }
  gg[0x10350] = 1;  // Brood War
  for (int i = 0; i < 12; ++i) {
    *reinterpret_cast<int *>(gg + 0x00 + i * 4) =
        i < static_cast<int>(nPlayers) ? players[i].minerals : 0;
    *reinterpret_cast<int *>(gg + 0x30 + i * 4) =
        i < static_cast<int>(nPlayers) ? players[i].gas : 0;
  }
  *reinterpret_cast<uint16_t *>(gg + 0xE4) = static_cast<uint16_t>(mapW);
  *reinterpret_cast<uint16_t *>(gg + 0xE6) = static_cast<uint16_t>(mapH);
  *reinterpret_cast<uint32_t *>(gg + 0x14C) = frame;
  snprintf(reinterpret_cast<char *>(gg + 0xC4C), 0x100, "HyperionRig.scm");
  // Supplies: SuppliesPerRace[3] { available[12], used[12], max[12] } at +0x3054.
  // Available and used are per-player-race values; max is the melee cap in
  // doubled supply, not zero.
  for (int r = 0; r < 3; ++r)
    for (int i = 0; i < 12; ++i) {
      unsigned char *s = gg + 0x3054 + r * 0x90 + i * 4;
      *reinterpret_cast<int *>(s) = 0;
      *reinterpret_cast<int *>(s + 0x30) = 0;
      *reinterpret_cast<int *>(s + 0x60) = 400;
    }
  for (unsigned i = 0; i < nPlayers && i < 12; ++i) {
    const int r = static_cast<int>(players[i].race);
    if (r < 0 || r > 2)
      continue;
    *reinterpret_cast<int *>(gg + 0x3054 + r * 0x90 + i * 4) = players[i].supplyTotal;
    *reinterpret_cast<int *>(gg + 0x3054 + r * 0x90 + 0x30 + i * 4) = players[i].supplyUsed;
  }
  // Unit counts: all[type][player] from the stream (the fork reads these for its
  // opening choice).
  for (int i = 0; i < 12; ++i)
    for (int t = 0; t < 228; ++t) {
      *reinterpret_cast<int *>(gg + 0x3234 + t * 0x30 + i * 4) = 0;
      *reinterpret_cast<int *>(gg + 0x5CF4 + t * 0x30 + i * 4) = 0;
    }
  for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
    if (units[i].player < 12 && units[i].type < 228) {
      ++*reinterpret_cast<int *>(gg + 0x3234 + units[i].type * 0x30 + units[i].player * 4);
      if (units[i].completed)
        ++*reinterpret_cast<int *>(gg + 0x5CF4 + units[i].type * 0x30 + units[i].player * 4);
    }
  }
  // NetMode (LAN), gwGameMode.
  *reinterpret_cast<int *>(Sc(0x59688C)) = 2;
  *reinterpret_cast<uint16_t *>(Sc(0x596904)) = 0;
  Sc(SC_SENDTURN)[0] = 0xC3;
  Sc(0x400001)[0] = '\0';
}

// The bot's client thread syncs once and closes the pipe (its update is
// one-shot), so the rig's free-running pipe loop serves it: connect, send 2,
// read the ready codes. The per-frame game state is written on the host's
// thread only, which the client no longer shares by then.
static DWORD WINAPI ServePipe(LPVOID p) {  HANDLE hPipe = static_cast<HANDLE>(p);
  if (!ConnectNamedPipe(hPipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED)
    return 1;
  DWORD code = 2, n;
  WriteFile(hPipe, &code, sizeof code, &n, NULL);
  for (;;) {
    if (!ReadFile(hPipe, &code, sizeof code, &n, NULL) || n != sizeof code)
      break;
    DWORD two = 2;
    WriteFile(hPipe, &two, sizeof two, &n, NULL);
  }
  return 0;
}

static HANDLE s_hShm;
static void *s_view;
static void *s_bigview;  // the writer's real target: a big writable buffer (see main)

// The bot writes StarCraft 1.16.1's own globals by absolute address (the speed uncap and
// friends), so the rig stands in for that region. The exe itself is based at 0x20000000
// (see build.sh) to leave the low range free.
static void ReserveScMemory() {
  // The stand-in for StarCraft's address space (patch_pluto.py remaps the bot's
  // absolute references onto these two buffers).
  void *low = VirtualAlloc(reinterpret_cast<void *>(0x21000000), 0x400000,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  void *sc = VirtualAlloc(reinterpret_cast<void *>(0x21400000), 0x400000,
                          MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  fprintf(stderr, "[bridge] SC stand-in: low %p, data %p\n", low, sc);
  fflush(stdout);
  // A real game's onStart uses a custom stack at 0xF0B5D710: map it, or the
  // first call into the fork's worker faults writing the return address.
  VirtualAlloc(reinterpret_cast<void *>(0xF0B5D000), 0x1000,
               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

// The frame pipe's own fd: the fork's threads close fd 0 during their start-up
// (that is what cut the bridge's stdin at ~1-26 frames, in the harness and the
// real game alike). Dup'd as the first thing in main, before the fork loads.
static int s_frameIn = -1;
static bool ReadN(void *pv, size_t cb) {
  unsigned char *p = static_cast<unsigned char *>(pv);
  while (cb) {
    int n = read(s_frameIn, p, cb);
    if (n <= 0) {
      fprintf(stderr, "[bridge] ReadN: %d on fd %d errno %d\n", n, s_frameIn, errno);
      return false;
    }
    p += n;
    cb -= n;
  }
  return true;
}
static bool WriteN(const void *pv, size_t cb) {
  const unsigned char *p = static_cast<const unsigned char *>(pv);
  while (cb) {
    int n = write(1, p, cb);
    if (n <= 0)
      return false;
    p += n;
    cb -= n;
  }
  return true;
}

// A frame's state over the rig's scripted state: the units keep their slots by
// their frame order (v1: ids are 1000+n for Hyperion's, 2000+n for the enemy's
// first appearance; the bot tracks the slots between frames).

// The bot appends its commands to the TurnBuffer but its byte count does not
// land reliably in the harness; the commands are self-describing (the classic
// opcode sizes), so the reply scans the stream and sends only the new bytes.
static void FlushReply(unsigned frame) {
  static unsigned s_nSent = 0;
  unsigned char *tb = Sc(0x654880);
  unsigned end = s_nSent;
  while (end < 0x10000) {
    const unsigned char op = tb[end];
    unsigned sz = 0;
    if (op == 0x09 || op == 0x0A || op == 0x0B) {
      sz = 2 + static_cast<unsigned>(tb[end + 1]) * 2;
    } else if (op == 0x14) {
      sz = 10;
    } else if (op == 0x15) {
      sz = 11;
    } else if (op == 0x0C) {
      sz = 8;
    } else if (op == 0x1A || op == 0x2B) {
      sz = 2;
    } else if (op == 0x1F || op == 0x23 || op == 0x35) {
      sz = 3;
    } else if (op == 0x30 || op == 0x32 || op == 0x25 || op == 0x26 ||
               op == 0x2C || op == 0x2D || op == 0x21 || op == 0x22 ||
               op == 0x1E || op == 0x28) {
      sz = 2;
    } else if (op == 0x20 || op == 0x29) {
      sz = 3;
    } else if (op == 0x27 || op == 0x2A || op == 0x5A || op == 0x31 ||
               op == 0x33 || op == 0x34 || op == 0x36) {
      sz = 1;
    } else if (op == 0x16) {
      sz = 1;
    } else {
      if (getenv("HX_DEBUG") != NULL)
        fprintf(stderr, "[bridge] unknown turn opcode %02x at %u in frame %u\n",
                op, end, frame);
      break;  // a partial command at the tail: wait for the next frame
    }
    end += sz;
  }
  const unsigned used = end - s_nSent;
  if (getenv("HX_DEBUG") != NULL && used)
    fprintf(stderr, "[bridge] f%d: %u new turn bytes: %02x %02x %02x %02x %02x %02x\n",
            static_cast<int>(frame), used,
            tb[s_nSent], tb[s_nSent + 1], tb[s_nSent + 2],
            tb[s_nSent + 3], tb[s_nSent + 4], tb[s_nSent + 5]);
  unsigned out[3] = {0x50525848, frame, used};  // 'HXRP'
  WriteN(out, sizeof out);
  WriteN(tb + s_nSent, used);
  unsigned win = 0;
  WriteN(&win, 4);
  // Consuming a turn has the same effect as SC's sendTurn: the next frame
  // starts with an empty buffer. Leaving the bytes in place fills the 512-byte
  // queue during the opening and silently prevents every later action.
  memset(tb, 0, 0x220);
  *ScT<unsigned>(0x654AA0) = 0;
  s_nSent = 0;
  fflush(stdout);
  if (frame == 0) {
    // The console detach lands at the end of frame 0. Move input to the named
    // FIFO now, while frame 1 can still be read; switching at frame 1 was
    // already too late in a real game.
    const int fd = open("frames.fifo", O_RDONLY | O_BINARY);
    if (fd >= 0) {
      _setmode(fd, _O_BINARY);
      s_frameIn = fd;
      fprintf(stderr, "[bridge] frame input -> frames.fifo (fd %d)\n", fd);
    }
  }
}


int main(int argc, char **argv) {
  // Input: stdin for start-up, a named FIFO from frame 3 on. The fork's own
  // start-up is sensitive to the console's fd layout (it derails with a FIFO
  // at frame 0), but the console detach would kill the pipe a few frames in.
  // The protocol is binary. In particular, CRT text mode turns frame 10's
  // 0x0A byte into 0x0D 0x0A and corrupts both headers and command payloads.
  _setmode(0, _O_BINARY);
  _setmode(1, _O_BINARY);
  s_frameIn = dup(0);
  _setmode(s_frameIn, _O_BINARY);
  fprintf(stderr, "[bridge] frame fd %d (stdin %d)\n", s_frameIn, _fileno(stdin));
  const bool bDebug = getenv("HX_DEBUG") != NULL;
  const int scenario = argc > 1 ? atoi(argv[1]) : 1;
  ReserveScMemory();
  fprintf(stderr, "[bridge] scenario %d\n", scenario);
  fflush(stdout);

  // 1. The shared GameData block.
  const DWORD pid = GetCurrentProcessId();
  char name[128];
  snprintf(name, sizeof name, "Local\\bwapi_shared_memory_%lu", pid);
  HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                   sizeof(GameData), name);
  if (!hMap) {
    fprintf(stderr, "[bridge] GameData mapping failed\n");
    return 1;
  }
  g = static_cast<GameData *>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GameData)));
  memset(g, 0, sizeof *g);
  g->client_version = 10003;  // BWAPI 4.4.0's CLIENT_VERSION
  g->revision = 66625;
  g->instanceID = 1;
  g->gameType = GameTypes::Enum::Melee;
  g->latency = 2;
  g->latencyFrames = 6;
  g->hasGUI = false;
  g->frameCount = 0;
  g->elapsedTime = 0;
  g->countdownTimer = 0;
  g->isInGame = true;
  g->isMultiplayer = true;
  g->isReplay = false;
  g->isPaused = false;
  g->self = 0;
  g->enemy = 1;
  g->neutral = 2;
  g->playerCount = 2;
  g->forceCount = 1;
  snprintf(g->forces[0].name, sizeof g->forces[0].name, "Players");
  FillMap();
  FillPlayer(0, Races::Enum::Zerg, "Alpha");
  FillPlayer(1, Races::Enum::Terran, "Bravo");
  g->startLocationCount = 2;
  g->startLocations[0].x = 512;  g->startLocations[0].y = 1024;
  g->startLocations[1].x = 3072; g->startLocations[1].y = 3072;
  g->players[0].startLocationX = 512;  g->players[0].startLocationY = 1024;
  g->players[1].startLocationX = 3072; g->players[1].startLocationY = 3072;

  AddUnit(UnitTypes::Enum::Zerg_Hatchery, 0, 608, 928, 1250);
  AddUnit(UnitTypes::Enum::Zerg_Drone, 0, 672, 1064, 40);
  AddUnit(UnitTypes::Enum::Zerg_Drone, 0, 640, 864, 40);
  AddUnit(UnitTypes::Enum::Zerg_Overlord, 0, 512, 1224, 200);
  AddUnit(UnitTypes::Enum::Terran_Command_Center, 1, 2976, 2976, 1500);
  AddUnit(UnitTypes::Enum::Terran_SCV, 1, 2912, 3112, 60);
  AddUnit(UnitTypes::Enum::Terran_SCV, 1, 3112, 2912, 60);
  AddUnit(UnitTypes::Enum::Terran_Marine, 1, 3272, 3272, 40);
  fprintf(stderr, "[bridge] %d units\n", g->initialUnitCount);
  fflush(stdout);
  UpdateScMemory();

  // 2. The game list the client's connect() opens.
  HANDLE hList = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                    sizeof(GameTable), "Local\\bwapi_shared_memory_game_list");
  if (!hList) {
    fprintf(stderr, "[bridge] game list mapping failed\n");
    return 1;
  }
  GameTable *table = static_cast<GameTable *>(
      MapViewOfFile(hList, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GameTable)));
  memset(table, 0, sizeof *table);
  table->gameInstances[0].serverProcessID = pid;
  table->gameInstances[0].isConnected = false;
  table->gameInstances[0].lastKeepAliveTime = GetTickCount();

  // 3. The pipe, listening before the bot's thread starts.
  snprintf(name, sizeof name, "\\\\.\\pipe\\bwapi_pipe_%lu", pid);
  HANDLE hPipe = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                  1, 4096, 4096, 2000, NULL);
  if (hPipe == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "[bridge] pipe failed (%lu)\n", GetLastError());
    return 1;
  }

  // 4. Load the bot and hand it the game, as the real framework does at injection.
  HMODULE dll = LoadLibraryA("pluto.patched.dll");
  if (!dll) {
    fprintf(stderr, "[bridge] no pluto.dll (error %lu)\n", GetLastError());
    return 1;
  }
  FnNewAIModule newAIModule = reinterpret_cast<FnNewAIModule>(GetProcAddress(dll, "newAIModule"));
  FnGameInit gameInit = reinterpret_cast<FnGameInit>(GetProcAddress(dll, "gameInit"));
  if (!newAIModule || !gameInit) {
    fprintf(stderr, "[bridge] exports missing\n");
    return 1;
  }
  // The bot's zero-fill BSS tail (its uninitialised globals: the obs gates at
  // base+0x72654/0x72661 and the obs-state object at base+0x73120) is mapped
  // read-only by Wine; a real game's loader gives it write access. Make it writable
  // so the bot's own stores land.
  {
    void *bss = reinterpret_cast<void *>(0x7b202000);
    DWORD old;
    if (VirtualProtect(bss, 0x2000, PAGE_READWRITE, &old))
      fprintf(stderr, "[bridge] bss 0x7b202000..0x7b204000 protected RW\n");
    else
      fprintf(stderr, "[bridge] bss protect failed (%lu)\n", GetLastError());
    fflush(stdout);
  }
  {
    unsigned char *img = reinterpret_cast<unsigned char *>(GetModuleHandleA("pluto.patched.dll"));
    fprintf(stderr, "[bridge] image bytes at +0x153f: %02x %02x %02x %02x %02x %02x %02x\n",
           img[0x153f], img[0x1540], img[0x1541], img[0x1542], img[0x1543], img[0x1544], img[0x1545]);
    char path[512];
    GetModuleFileNameA(GetModuleHandleA("pluto.patched.dll"), path, sizeof path);
    fprintf(stderr, "[bridge] loaded: %s\n", path);
    fflush(stdout);
  }
  BWAPI::BWAPIClient.data = g;  // connect() would set this; the impls read it directly
  // Client::connect() deletes the existing BroodwarPtr before installing its
  // own GameImpl. Ownership therefore has to start on the heap, not the stack.
  GameImpl *handoff = new GameImpl(g);
  BWAPI::BroodwarPtr = handoff;  // the GameWrapper forwards through this

  // The bot's Player vtable slot 4 is a race getter that writes the race id to *out
  // (when non-null) and returns it in eax (Races: Zerg 0, Terran 1, Protoss 2,
  // Random 6, Unknown 8). BWAPI 4.4.0 puts getType at that slot, so the bot's
  // interface is a fork; patch the slot on the one shared PlayerImpl vtable.
  {
    PlayerImpl probe(0);
    void **vt = *reinterpret_cast<void ***>(&probe);
    DWORD old;
    VirtualProtect(vt, 0x100, PAGE_READWRITE, &old);
    vt[4] = reinterpret_cast<void *>(&PlayerRace4);
    VirtualProtect(vt, 0x100, old, &old);
    fprintf(stderr, "[bridge] PlayerImpl vtable slot 4 -> %p\\n", reinterpret_cast<void *>(&PlayerRace4));
  }

  // Same trick for the Game's chat/printf slot: the bot calls it as (int, const char *).
  {
    void (GameImpl::*pmf)(const char *, va_list) = &GameImpl::vPrintf;
    const unsigned p = *reinterpret_cast<unsigned *>(&pmf);
    const int slot = static_cast<int>(p - 1) / 4;
    void **vt = *reinterpret_cast<void ***>(handoff);
    DWORD old;
    VirtualProtect(vt, 0x200, PAGE_READWRITE, &old);
    vt[slot] = reinterpret_cast<void *>(&GamePrint3);
    VirtualProtect(vt, 0x200, old, &old);
    fprintf(stderr, "[bridge] GameImpl vPrintf slot %d -> %p\\n", slot,
           reinterpret_cast<void *>(&GamePrint3));
  }

  // The set-returning Unit slots: the bot expects an MSVC-shaped set by value; hand it
  // an empty one (slots 63 getLoadedUnits, 65 getInterceptors, 67 getLarva with the
  // inline destructor at 0).
  {
    g_emptyHead.left = g_emptyHead.parent = g_emptyHead.right = &g_emptyHead;
    // Slot audit: the set-returning methods' real slots (PMF encoding) and neighbours.
    {
      auto slotOf = [](auto pmf) -> int {
        return static_cast<int>((*reinterpret_cast<unsigned *>(&pmf) - 1) / 4);
      };
      fprintf(stderr, "[bridge] slots: loadedUnits=%d interceptors=%d larva=%d hatchery=%d transport=%d\\n",
             slotOf(static_cast<Unitset (UnitImpl::*)() const>(&UnitImpl::getLoadedUnits)),
             slotOf(static_cast<Unitset (UnitImpl::*)() const>(&UnitImpl::getInterceptors)),
             slotOf(static_cast<Unitset (UnitImpl::*)() const>(&UnitImpl::getLarva)),
             slotOf(static_cast<Unit (UnitImpl::*)() const>(&UnitImpl::getHatchery)),
             slotOf(static_cast<Unit (UnitImpl::*)() const>(&UnitImpl::getTransport)));
      fflush(stdout);
    }
    UnitImpl probe(0);
    void **vt = *reinterpret_cast<void ***>(&probe);
    DWORD old;
    VirtualProtect(vt, 0x200, PAGE_READWRITE, &old);
    for (int s : {64, 66})
      vt[s] = reinterpret_cast<void *>(&UnitEmptySet);
    VirtualProtect(vt, 0x200, old, &old);
    fprintf(stderr, "[bridge] UnitImpl set slots -> %p\\n", reinterpret_cast<void *>(&UnitEmptySet));
  }
  {
    HMODULE hDll = GetModuleHandleA("pluto.patched.dll");
    fprintf(stderr, "[bridge] pluto.dll base %p, newAIModule %p, gameInit %p\n",
           static_cast<void *>(hDll), reinterpret_cast<void *>(newAIModule),
           reinterpret_cast<void *>(gameInit));
    fflush(stdout);
  }
  // BWAPI 4.4 initializes the module's Game pointer before constructing it.
  gameInit(handoff);
  fprintf(stderr, "[bridge] gameInit handed over\n");
  AIModule *mod = newAIModule();
  fprintf(stderr, "[bridge] module %p\n", static_cast<void *>(mod));
  fflush(stdout);
  // MatchStart is driven after the first real frame has replaced the rig's
  // scripted units, not before it.

  // 5. The bot's thread now connects; grant it, then serve its updates.
  fprintf(stderr, "[bridge] waiting for the bot to connect\n");
  fflush(stdout);
  // The bot's client thread connects during its start-up, syncs once (its
  // update is one-shot: ready code, wait 2, one pass), then closes the pipe.
  // The rig's thread serves it; the per-frame state is written on the host's
  // thread, which the client no longer shares by then.
  {
    HANDLE hOpen = CreateThread(NULL, 0, ServePipe, hPipe, 0, NULL);
    (void)hOpen;
  }

  // 6. Drive the module through its own vtable, as the bridge does: its layout is not
  // BWAPI 4.4.0's AIModule (no virtual destructor; slots read from the disassembly):
  //   [1] onStart(), [2] onEnd(bool), [3] onFrame().
  typedef void(__thiscall *V0)(void *self);
  typedef void(__thiscall *V1)(void *self, unsigned char b);
  void **vt = *reinterpret_cast<void ***>(mod);
  V0 onStart = reinterpret_cast<V0>(vt[1]);
  V1 onEnd = reinterpret_cast<V1>(vt[2]);
  V0 onFrame = reinterpret_cast<V0>(vt[3]);
  fprintf(stderr, "[bridge] driving the module (vt[1]=%p vt[2]=%p vt[3]=%p)\n", vt[1], vt[2], vt[3]);
  fflush(stdout);
  // The bot's client (a static object at base+0x54160) never got its shm view
  // pointer (+0x18) or its send flag (+0x1C) from the handshake in the rig. The view
  // was set to the bot's own .data page (base+0x54000), but the writer writes the obs
  // up to view+0x85f1c: past .data (ends base+0x54678) into .rdata (read-only) and off
  // the image. Give it a big writable buffer instead, seeded with the .data page so
  // the PLO1 header the bot and engine expect is where they expect it.
  s_bigview = VirtualAlloc(NULL, 0x100000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!s_bigview) {
    fprintf(stderr, "[bridge] view buffer alloc failed\n");
    return 1;
  }
  memcpy(s_bigview, reinterpret_cast<void *>(0x7b1e4000), 0x20000);
  memset(reinterpret_cast<char *>(s_bigview) + 0x20000, 0, 0xE0000);
  {
    unsigned *client = reinterpret_cast<unsigned *>(0x7b1e4160);
    fprintf(stderr, "[bridge] client: view-slot %08x flag %02x -> setting view=%p+flag\n",
           client[6], *reinterpret_cast<unsigned char *>(client + 7), s_bigview);
    fflush(stdout);
    client[6] = reinterpret_cast<unsigned>(s_bigview);
    *reinterpret_cast<unsigned char *>(client + 7) = 1;
  }
  int nFramesDone = 0;
  unsigned endWinner = 0;
  bool hasMatchResult = false;
  for (;;) {
    DWORD t0 = GetTickCount();
    unsigned hdr[3];
    if (!ReadN(hdr, sizeof hdr)) {
      fprintf(stderr, "[bridge] stdin closed after %d frames\n", nFramesDone);
      break;
    }
    if (hdr[0] == 0x45505848) {  // 'HXPE': the actual match result
      endWinner = hdr[1] != 0;
      hasMatchResult = true;
      break;
    }
    if (hdr[0] != 0x50465848 && hdr[0] != 0x32505848 && hdr[0] != 0x33505848) {
      fprintf(stderr, "[bridge] bad magic %08x after %d frames\n", hdr[0], nFramesDone);
      break;
    }
    const unsigned frame = hdr[1];
    const unsigned nUnits = hdr[2] < 9999 ? hdr[2] : 9999;
    unsigned mapWH[2];
    ReadN(mapWH, sizeof mapWH);
    unsigned nPlayers = 2;
    ReadN(&nPlayers, 4);
    if (nPlayers > 12)
      nPlayers = 12;
    if (mapWH[0] == 0 || mapWH[1] == 0 || mapWH[0] > 1024 || mapWH[1] > 1024) {
      fprintf(stderr, "[bridge] invalid map dimensions %ux%u\n", mapWH[0], mapWH[1]);
      break;
    }
    static HxUnit units[9999];
    static HxUnitExtra extras[9999];
    static HxPlayer players[12];
    ReadN(units, nUnits * sizeof(HxUnit));
    const HxUnitExtra *unitExtras = NULL;
    if (hdr[0] == 0x33505848) {
      ReadN(extras, nUnits * sizeof(HxUnitExtra));
      unitExtras = extras;
    }
    ReadN(players, nPlayers * sizeof(HxPlayer));
    static std::vector<unsigned char> seen, explored, rawTiles;
    const unsigned nPlaneBytes = (mapWH[0] * mapWH[1] + 7) / 8;
    const unsigned char *raw = NULL;
    if (hdr[0] == 0x32505848 || hdr[0] == 0x33505848) {
      rawTiles.resize(mapWH[0] * mapWH[1] * 4);
      ReadN(rawTiles.data(), rawTiles.size());
      raw = rawTiles.data();
    } else {
      seen.resize(nPlaneBytes);
      explored.resize(nPlaneBytes);
      ReadN(seen.data(), nPlaneBytes);
      ReadN(explored.data(), nPlaneBytes);
    }
    DWORD t1 = GetTickCount();
    ApplyImageState(units, unitExtras, nUnits, players, nPlayers, frame, mapWH[0], mapWH[1],
                    raw, seen.data(), explored.data(), nPlaneBytes);
    int f = static_cast<int>(frame);
    g->frameCount = f;
    g->elapsedTime = f * 42;
    // The fork's per-unit obs walk dispatches through a vtable in the bridge's
    // exe whose slot 0xE0 is NULL in our harness (the call chain faults to 0).
    // Fill the slot with a stub that answers 1 and cleans the four args.
    {
      static bool s_patched = false;
      if (!s_patched) {
        void **slot = reinterpret_cast<void **>(0x200e59c0 + 0xE0);
        DWORD old;
        if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) {
          *slot = reinterpret_cast<void *>(&StubRet12);
          VirtualProtect(slot, 4, old, &old);
          s_patched = true;
          fprintf(stderr, "[bridge] obs-walk slot 0xE0 -> %p\n",
                  reinterpret_cast<void *>(&StubRet12));
        }
      }
    }
    static int packedOfId[1700];
    static int packedCount = 0;
    if (f == 0) {
      // BWAPI's initial GameData must describe this match, not the rig's eight
      // scripted units. The obs-state converter updates its own state from the
      // SC image, but onStart also reads GameData directly.
      memset(g->units, 0, sizeof g->units);
      memset(g->unitArray, 0xFF, sizeof g->unitArray);
      g->initialUnitCount = 0;
      packedCount = 0;
      for (int &index : packedOfId)
        index = -1;
      for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
        const HxUnit &src = units[i];
        if (src.id >= 1700)
          continue;
        const int index = g->initialUnitCount++;
        packedCount = g->initialUnitCount;
        packedOfId[src.id] = index;
        UnitData &u = g->units[index];
        ClearUnitData(u);
        u.clearanceLevel = 2;
        u.id = index;
        u.player = static_cast<int>(src.player);
        u.type = static_cast<int>(src.type);
        u.positionX = static_cast<int>(src.x);
        u.positionY = static_cast<int>(src.y);
        u.hitPoints = static_cast<int>(src.hp);
        u.lastHitPoints = static_cast<int>(src.hp);
        u.shields = static_cast<int>(src.shields);
        u.energy = static_cast<int>(src.energy);
        u.isCompleted = src.completed != 0;
        u.isIdle = src.order == Orders::Enum::PlayerGuard;
        u.exists = src.visible != 0;
        u.isDetected = src.visible != 0 &&
                       (unitExtras == NULL || unitExtras[i].detected != 0);
        for (int k = 0; k < 9; ++k)
          u.isVisible[k] = (k == 0 && src.visible != 0);
        u.order = static_cast<int>(src.order);
        if (unitExtras != NULL) {
          const HxUnitExtra &e = unitExtras[i];
          u.buildType = static_cast<int>(e.buildType);
          u.trainingQueueCount = e.buildType != UnitTypes::Enum::None ? 1 : 0;
          u.trainingQueue[0] = static_cast<int>(e.buildType);
          u.remainingBuildTime = static_cast<int>(e.remainingBuildTime);
          u.groundWeaponCooldown = e.groundCooldown;
          u.airWeaponCooldown = e.airCooldown;
          u.spellCooldown = e.spellCooldown;
        }
        g->unitArray[src.id] = index;
      }
      handoff->onMatchStart();
    }
    // BWAPI assigns a permanent packed id when a unit first appears. Later
    // discoveries must not reuse the frame-zero table or emit an id of -1.
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
      const HxUnit &src = units[i];
      if (src.id >= 1700 || packedOfId[src.id] >= 0)
        continue;
      const int index = packedCount++;
      packedOfId[src.id] = index;
      UnitData &u = g->units[index];
      ClearUnitData(u);
      u.clearanceLevel = 2;
      u.id = index;
      u.player = static_cast<int>(src.player);
      u.type = static_cast<int>(src.type);
      u.positionX = static_cast<int>(src.x);
      u.positionY = static_cast<int>(src.y);
      u.hitPoints = static_cast<int>(src.hp);
      u.lastHitPoints = static_cast<int>(src.hp);
      u.shields = static_cast<int>(src.shields);
      u.energy = static_cast<int>(src.energy);
      u.isCompleted = src.completed != 0;
      u.isIdle = src.order == Orders::Enum::PlayerGuard;
      u.exists = src.visible != 0;
      u.isDetected = src.visible != 0 &&
                     (unitExtras == NULL || unitExtras[i].detected != 0);
      for (int k = 0; k < 9; ++k)
        u.isVisible[k] = (k == 0 && src.visible != 0);
      u.order = static_cast<int>(src.order);
      if (unitExtras != NULL) {
        const HxUnitExtra &e = unitExtras[i];
        u.buildType = static_cast<int>(e.buildType);
        u.trainingQueueCount = e.buildType != UnitTypes::Enum::None ? 1 : 0;
        u.trainingQueue[0] = static_cast<int>(e.buildType);
        u.remainingBuildTime = static_cast<int>(e.remainingBuildTime);
        u.groundWeaponCooldown = e.groundCooldown;
        u.airWeaponCooldown = e.airCooldown;
        u.spellCooldown = e.spellCooldown;
      }
      g->unitArray[src.id] = index;
    }
    // Resolve links only after all current packed ids are known. Targets and
    // build units often appear later in discovery order than their owners.
    if (unitExtras != NULL) {
      for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
        const HxUnit &src = units[i];
        if (src.id >= 1700)
          continue;
        const HxUnitExtra &e = unitExtras[i];
        UnitData &u = g->units[packedOfId[src.id]];
        u.buildUnit = e.currentBuildUnitId < 1700 ? packedOfId[e.currentBuildUnitId] : -1;
        u.orderTarget = e.orderTargetId < 1700 ? packedOfId[e.orderTargetId] : -1;
      }
    }
    // Resolve larva links after every packed id is known; a larva may precede
    // its hatchery in the used list, and new larvae appear throughout a match.
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
      const HxUnit &src = units[i];
      if (src.type != UnitTypes::Enum::Zerg_Larva || src.id >= 1700)
        continue;
      int hatchery = -1;
      if (unitExtras != NULL && unitExtras[i].connectedId < 1700)
        hatchery = static_cast<int>(unitExtras[i].connectedId);
      long long bestDistance = 0x7FFFFFFFFFFFFFFFLL;
      if (hatchery < 0)
        for (unsigned j = 0; j < nUnits && j < 9999; ++j) {
        const HxUnit &candidate = units[j];
        if (candidate.player != src.player || candidate.id >= 1700 ||
            (candidate.type != UnitTypes::Enum::Zerg_Hatchery &&
             candidate.type != UnitTypes::Enum::Zerg_Lair &&
             candidate.type != UnitTypes::Enum::Zerg_Hive))
          continue;
        const long long dx = static_cast<long long>(candidate.x) - src.x;
        const long long dy = static_cast<long long>(candidate.y) - src.y;
        const long long distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
          bestDistance = distance;
          hatchery = static_cast<int>(candidate.id);
        }
      }
      if (hatchery >= 0 && packedOfId[hatchery] >= 0)
        g->units[packedOfId[src.id]].hatchery = packedOfId[hatchery];
    }
    g->eventCount = 1;
    g->events[0].type = f == 0 ? EventType::Enum::MatchStart : EventType::Enum::MatchFrame;
    g->events[0].v1 = f;
    g->events[0].v2 = 0;
    // GameImpl fills each PlayerImpl's unit set from these events. Without
    // them the observation has units but self()->getUnits() stays empty.
    static bool s_discovered[1700];
    bool visibleNow[1700] = {};
    if (f == 0)
      memset(s_discovered, 0, sizeof s_discovered);
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
      const HxUnit &u = units[i];
      if (u.id >= 1700 || !u.visible)
        continue;
      visibleNow[u.id] = true;
      if (!s_discovered[u.id]) {
        g->events[g->eventCount].type = EventType::Enum::UnitDiscover;
        g->events[g->eventCount].v1 = packedOfId[u.id];
        g->events[g->eventCount].v2 = 0;
        ++g->eventCount;
        s_discovered[u.id] = true;
      }
    }
    for (int id = 0; id < 1700; ++id) {
      if (!s_discovered[id] || visibleNow[id])
        continue;
      g->events[g->eventCount].type = EventType::Enum::UnitEvade;
      g->events[g->eventCount].v1 = packedOfId[id];
      g->events[g->eventCount].v2 = 0;
      ++g->eventCount;
      s_discovered[id] = false;
    }
    if (f == 0)
      onStart(mod);
    // The bot's client processes MatchFrame events through its own update.
    // Calling onMatchFrame here as well duplicates discovery events and can
    // dereference the GameImpl that its connect() has already replaced.
    onFrame(mod);
    if (bDebug && frame % 60 == 0) {
      fprintf(stderr,
              "[bridge] state f%d min=%d gas=%d self=%d enemy=%d neutral=%d players=%d "
              "avail=%d scavail=%d larvae=%d\n",
              f, g->players[0].minerals, g->players[0].gas, g->self, g->enemy,
              g->neutral, g->playerCount,
              g->players[0].isUnitAvailable[UnitTypes::Enum::Zerg_Zergling],
              *ScT<unsigned char>(SC_GAME + 0x18C + UnitTypes::Enum::Zerg_Zergling),
              g->players[0].allUnitCount[UnitTypes::Enum::Zerg_Larva]);
    }
    ++nFramesDone;
    DWORD t2 = GetTickCount();
    if (bDebug && (frame % 6 == 0 || frame < 3))
      fprintf(stderr, "[bridge] f%d: read=%lums fill=%lums frame=%lums\n",
              static_cast<int>(frame), t1 - t0, t2 - t1, GetTickCount() - t2);
    FlushReply(f);
    if (bDebug && f % 100 == 0)
      fprintf(stderr, "[bridge] frame %d\n", f);
    Sleep(16);
  }
  FlushReply(static_cast<unsigned>(nFramesDone));  // a last command may land late
  if (hasMatchResult)
    onEnd(mod, endWinner != 0);
  fprintf(stderr, "[bridge] done\n");
  return 0;
}
