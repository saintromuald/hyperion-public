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
#include <BWAPI/Latency.h>
#include <BW/Pathing.h>  // genuine BW::SAI_Paths/region/split layouts
#include <Util/Sha1.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "lifecycle/tracker.h"
#include "hxpm.h"
#include "hxp_late.h"
#include "official-dll.h"
#include "publication.h"
#include "turn-commands.h"
#include "unit-visibility.h"

using namespace BWAPI;
using namespace BWAPIC;
namespace lifecycle = pluto_bridge::lifecycle;

static GameData *g;
static CRITICAL_SECTION g_stateLock;
static CONDITION_VARIABLE g_stateChanged;
static pluto_bridge::PublicationState s_publication;
static bool s_clientReplacementEnabled = false;
static bool s_onStartComplete = false;
static bool s_finalizing = false;
static bool s_shutdown = false;
static int s_hostEnemyVslot = -1;
struct StateGuard {
  bool released = false;
  StateGuard() { EnterCriticalSection(&g_stateLock); }
  ~StateGuard() { if (!released) LeaveCriticalSection(&g_stateLock); }
  void Release() {
    if (!released) {
      LeaveCriticalSection(&g_stateLock);
      released = true;
    }
  }
};
static pluto_hxpm::StreamState s_hxpmStream;
static BW::SAI_Paths *s_saiPaths;
static uint16_t *s_saiOverflowNeighbors;
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

// Opt-in ownership regression hooks.  These use the real BWAPIClient linked
// into the bridge, so connect() really deletes/replaces BroodwarPtr.  They are
// absent from normal operation and exist to prove synchronous and asynchronous
// onStart ownership without changing the official module's inference path.
static FnGameInit s_testGameInit = NULL;
static const void *s_testHandoffIdentity = NULL;
static HANDLE s_testClientReady = NULL;
static HANDLE s_testClientThread = NULL;
static volatile LONG s_testClientError = 0;
static bool s_testAsyncClient = false;

static bool TestConnectReplacement() {
  if (!BWAPIClient.connect() || BWAPI::BroodwarPtr == NULL) {
    InterlockedExchange(&s_testClientError, 1);
    if (s_testClientReady != NULL)
      SetEvent(s_testClientReady);
    return false;
  }
  void *replacement = static_cast<void *>(BWAPI::BroodwarPtr);
  // The allocator may reuse the deleted handoff's address for the replacement;
  // connect()'s success and its newly constructed GameImpl are the ownership
  // evidence, not pointer inequality.
  fprintf(stderr, "[bridge] test Client::connect replaced handoff %p -> %p\n",
          s_testHandoffIdentity, replacement);
  if (s_testClientReady != NULL)
    SetEvent(s_testClientReady);
  return true;
}

static void TestPrepareReplacement(void **gameVtable, void *originalSelfSlot) {
  GameImpl *replacement = static_cast<GameImpl *>(BWAPI::BroodwarPtr);
  // connect() constructs but does not initialize match state.  Initialize the
  // replacement under the host self() slot, then restore official enemy() for
  // Pluto's onStart.  The old handoff is never consulted.
  gameVtable[70] = originalSelfSlot;
  replacement->onMatchStart();
  gameVtable[70] = gameVtable[s_hostEnemyVslot];
  // The test Client belongs to the bridge, while Pluto's GameWrapper belongs
  // to the DLL.  Point that wrapper at the same live initialized replacement,
  // mirroring what the module's own Client does during its official connect.
  s_testGameInit(replacement);
}

static DWORD WINAPI TestClientUpdates(LPVOID) {
  if (s_testAsyncClient && !TestConnectReplacement())
    return 1;
  for (int frame = 0; frame < 2; ++frame) {
    if (!BWAPIClient.isConnected()) {
      InterlockedExchange(&s_testClientError, 3);
      return 1;
    }
    BWAPIClient.update();
    if (!BWAPIClient.isConnected() || BWAPI::BroodwarPtr == NULL) {
      InterlockedExchange(&s_testClientError, 4);
      return 1;
    }
    fprintf(stderr, "[bridge] test Client::update consumed frame %d\n", frame);
  }
  return 0;
}

static void TracePlayerRace4(const PlayerImpl *player, int race, const int *out,
                             void *caller);

// Pluto's MSVC BWAPI vtable has getRace in Player slot 4 (0x10).  The
// llvm-mingw/Itanium build has one additional destructor entry and places it in
// slot 5, so the compatibility shim occupies the official slot.  It writes the
// race id to *out (when non-null) and also returns it in eax.
static int __attribute__((thiscall)) PlayerRace4(void *self, int *out) {
  PlayerImpl *player = static_cast<PlayerImpl *>(self);
  const int race = player->getRace().getID();
  if (out)
    *out = race;
  TracePlayerRace4(player, race, out, __builtin_return_address(0));
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

// --- StarCraft 1.16.1 memory emulation ---------------------------------------
//
// The official DLL reads and writes genuine StarCraft absolute addresses.  The
// post-link PE layout reserves 0x400000..0x800000 in its .game section;
// ReserveScMemory validates that image-backed range before any SC state is
// initialized.  No patched DLL or VirtualAlloc stand-in is involved.
// Offsets are BWAPI 4.4.0's (Offsets.h, BWGame.h, CUnit.h).
static unsigned char *Sc(unsigned addr) {
  return reinterpret_cast<unsigned char *>(addr);
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
  uint8_t  buildQueueSlot;      // 0xA4
  uint8_t  uniquenessIdentifier; // 0xA5 (native packed-id generation)
  uint8_t  _pA6[0xAC - 0xA6];
  uint16_t remainingBuildTime; // 0xAC
  uint8_t  _pAE[0xC6 - 0xAE];
  uint16_t upgradeResearchTime; // 0xC6 (shared research/upgrade timer)
  uint8_t  techType;            // 0xC8
  uint8_t  upgradeType;         // 0xC9
  uint8_t  _pCA[0xD0 - 0xCA];
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
static_assert(offsetof(ScUnit, buildQueue) == 0x98, "CUnit::buildQueue");
static_assert(offsetof(ScUnit, buildQueueSlot) == 0xA4, "CUnit::buildQueueSlot");
static_assert(offsetof(ScUnit, upgradeResearchTime) == 0xC6,
              "CUnit::building.upgradeResearchTime");
static_assert(offsetof(ScUnit, techType) == 0xC8, "CUnit::building.techType");
static_assert(offsetof(ScUnit, upgradeType) == 0xC9, "CUnit::building.upgradeType");
static_assert(offsetof(ScUnit, uniquenessIdentifier) == 0xA5,
              "ScUnit generation must match CUnit::uniquenessIdentifier");

static const unsigned SC_UNITS = 0x59CCA8;    // UnitNodeTable, 1700 x 0x150
static const unsigned SC_VIS_FIRST = 0x628430;  // UnitNodeList_VisibleUnit_First
static const unsigned SC_PLAYERS = 0x57EEE0;    // PlayerInfo[12], stride 0x24
static const unsigned SC_STATUS = 0x57F0B8;     // playerStatusArray[8]
static const unsigned SC_GAME = 0x57F0F0;       // BWGame
static const unsigned SC_SENDTURN = 0x485A40;   // BWFXN_sendTurn, called by the bot

static void TracePlayerRace4(const PlayerImpl *player, int race, const int *out,
                             void *caller) {
  if (getenv("HX_DEBUG") == NULL)
    return;
  const int id = player->getID();
  if (id < 0 || id >= 12) {
    fprintf(stderr, "[bridge] PlayerRace4 invalid id=%d obj=%p race=%d caller=%p\n",
            id, static_cast<const void *>(player), race, caller);
    return;
  }
  const unsigned char *sc = Sc(SC_PLAYERS) + id * 0x24;
  fprintf(stderr,
          "[bridge] PlayerRace4 obj=%p id=%d pdata=%p pdata-race=%d g-race=%d "
          "SC{id=%u storm=%u type=%u race=%u name=\"%s\"} out=%d caller=%p "
          "starts=%d,%d;%d,%d\n",
          static_cast<const void *>(player), id, static_cast<const void *>(player->self),
          player->self->race, g->players[id].race,
          *reinterpret_cast<const unsigned *>(sc + 0x00),
          *reinterpret_cast<const unsigned *>(sc + 0x04), sc[0x08], sc[0x09],
          reinterpret_cast<const char *>(sc + 0x0B), out != NULL ? *out : -999, caller,
          g->startLocationCount > 0 ? g->startLocations[0].x : -1,
          g->startLocationCount > 0 ? g->startLocations[0].y : -1,
          g->startLocationCount > 1 ? g->startLocations[1].x : -1,
          g->startLocationCount > 1 ? g->startLocations[1].y : -1);
}

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

template <typename Extra>
static pluto_bridge::UnitVisibility UnitVisibilityFor(
    const HxUnit &unit, const Extra *extra) {
  pluto_bridge::UnitVisibilityInput input;
  input.raw_visible = unit.visible != 0;
  input.own_unit = unit.player == 0;
  input.has_hxp3 = extra != NULL;
  input.order = unit.order;
  if (extra != NULL) {
    input.status_flags = extra->statusFlags;
    input.movement_flags = extra->movementFlags;
    input.ground_cooldown = extra->groundCooldown;
    input.air_cooldown = extra->airCooldown;
    input.detected_by_us = extra->detected != 0;
  }
  return pluto_bridge::EffectiveUnitVisibility(input);
}

static pluto_bridge::UnitVisibility UnitVisibilityForFrame(
    const HxUnit &unit, const HxUnitExtra *extras,
    const pluto_late::UnitExtra *extras4, unsigned index) {
  if (extras4 != NULL)
    return UnitVisibilityFor(unit, &extras4[index]);
  if (extras != NULL)
    return UnitVisibilityFor(unit, &extras[index]);
  return UnitVisibilityFor(unit, static_cast<const HxUnitExtra *>(NULL));
}

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

static bool ApplyMapIdentity() {
  const char *file = getenv("HYPERION_PLUTO_MAP_FILE");
  if (file == NULL || *file == '\0')
    return false;
  const char *leaf = strrchr(file, '/');
  const char *backslash = strrchr(file, '\\');
  if (backslash != NULL && (leaf == NULL || backslash > leaf))
    leaf = backslash;
  snprintf(g->mapFileName, sizeof g->mapFileName, "%s", leaf != NULL ? leaf + 1 : file);
  snprintf(g->mapPathName, sizeof g->mapPathName, "%s", file);
  const char *name = getenv("HYPERION_PLUTO_MAP_NAME");
  snprintf(g->mapName, sizeof g->mapName, "%s", name != NULL ? name : "");
  g->mapHash[0] = '\0';
  FILE *f = fopen(file, "rb");
  if (f == NULL) {
    fprintf(stderr, "[bridge] cannot read declared map %s\n", file);
    return false;
  }
  std::vector<unsigned char> bytes;
  unsigned char block[65536];
  size_t n = 0;
  while ((n = fread(block, 1, sizeof block, f)) != 0)
    bytes.insert(bytes.end(), block, block + n);
  const bool readError = ferror(f) != 0;
  fclose(f);
  if (readError || bytes.empty()) {
    fprintf(stderr, "[bridge] cannot hash declared map %s\n", file);
    return false;
  }
  unsigned char hash[20];
  sha1::calc(bytes.data(), static_cast<int>(bytes.size()), hash);
  char hex[41];
  sha1::toHexString(hash, hex);
  snprintf(g->mapHash, sizeof g->mapHash, "%s", hex);
  g->startLocationCount = 0;
  const char *starts = getenv("HYPERION_PLUTO_STARTS");
  while (starts != NULL && *starts != '\0' && g->startLocationCount < 8) {
    int x = 0, y = 0;
    if (sscanf(starts, "%d,%d", &x, &y) != 2)
      break;
    g->startLocations[g->startLocationCount].x = x;
    g->startLocations[g->startLocationCount].y = y;
    ++g->startLocationCount;
    const char *next = strchr(starts, ';');
    if (next == NULL)
      break;
    starts = next + 1;
  }
  return true;
}

static void ClearStaticMapMetadata() {
  // Legacy streams are explicitly incomplete, never a fabricated one-region map.
  *ScT<BW::SAI_Paths *>(0x6D5BFC) = NULL;
  if (s_saiPaths != NULL)
    VirtualFree(s_saiPaths, 0, MEM_RELEASE);
  if (s_saiOverflowNeighbors != NULL)
    VirtualFree(s_saiOverflowNeighbors, 0, MEM_RELEASE);
  s_saiPaths = NULL;
  s_saiOverflowNeighbors = NULL;
  g->regionCount = 0;
  memset(g->mapTileRegionId, 0, sizeof g->mapTileRegionId);
  memset(g->mapSplitTilesMiniTileMask, 0, sizeof g->mapSplitTilesMiniTileMask);
  memset(g->mapSplitTilesRegion1, 0, sizeof g->mapSplitTilesRegion1);
  memset(g->mapSplitTilesRegion2, 0, sizeof g->mapSplitTilesRegion2);
  memset(g->regions, 0, sizeof g->regions);
}

// Parse has already bounded and validated every count and ID before this function
// allocates or writes anything. One match lives in a process, so these allocations
// remain stable until exit and no region/neighbor pointer can dangle.
static bool ApplyStaticMapMetadata(const pluto_hxpm::Metadata &metadata) {
  size_t poolWords = 0;
  for (const pluto_hxpm::Region &region : metadata.regions)
    if (region.neighbor_count > 10)
      poolWords += region.neighbor_count;
  BW::SAI_Paths *paths = static_cast<BW::SAI_Paths *>(
      VirtualAlloc(NULL, sizeof(BW::SAI_Paths), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  uint16_t *overflow = NULL;
  if (poolWords != 0) {
    // The genuine globalBuffer has 10,000 words. Allocate a bounded overflow for
    // the unusual case where long lists do not all fit; lists never straddle it.
    overflow = static_cast<uint16_t *>(VirtualAlloc(
        NULL, poolWords * sizeof(uint16_t), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  }
  if (paths == NULL || (poolWords != 0 && overflow == NULL)) {
    if (paths != NULL)
      VirtualFree(paths, 0, MEM_RELEASE);
    if (overflow != NULL)
      VirtualFree(overflow, 0, MEM_RELEASE);
    return false;
  }

  paths->regionCount = static_cast<uint32_t>(metadata.regions.size());
  paths->splitTiles_end = paths->splitTiles + metadata.split_tiles.size();
  paths->globalBuffer_ptr = paths->globalBuffer;
  for (unsigned y = 0; y < metadata.map_height; ++y)
    for (unsigned x = 0; x < metadata.map_width; ++x)
      paths->mapTileRegionId[y][x] = metadata.map_tile_region_id[y * metadata.map_width + x];
  for (size_t i = 0; i < metadata.split_tiles.size(); ++i) {
    paths->splitTiles[i].minitileMask = metadata.split_tiles[i].minitile_mask;
    paths->splitTiles[i].rgn1 = metadata.split_tiles[i].region1;
    paths->splitTiles[i].rgn2 = metadata.split_tiles[i].region2;
  }

  size_t globalUsed = 0;
  size_t overflowUsed = 0;
  for (size_t i = 0; i < metadata.regions.size(); ++i) {
    const pluto_hxpm::Region &source = metadata.regions[i];
    BW::region &target = paths->regions[i];
    target.accessabilityFlags = source.access_flags;
    target.groupIndex = source.group;
    target.tileCount = source.tile_count;
    target.pathCount = source.path_count;
    target.neighborCount = source.neighbor_count;
    target.rgnCenterX = static_cast<uint32_t>(source.center_x);
    target.rgnCenterY = static_cast<uint32_t>(source.center_y);
    target.rgnBox.left = static_cast<uint16_t>(source.left);
    target.rgnBox.top = static_cast<uint16_t>(source.top);
    target.rgnBox.right = static_cast<uint16_t>(source.right);
    target.rgnBox.bottom = static_cast<uint16_t>(source.bottom);
    target.defencePriority = source.defence_priority;
    target.neighborProperty = source.neighbor_property;
    // BWAPI declares localBuffer private, but its genuine packed region layout
    // ends with ten u16 entries at 0x2C. Populate that real inline storage.
    uint16_t *localBuffer = reinterpret_cast<uint16_t *>(
        reinterpret_cast<unsigned char *>(&target) + sizeof(BW::region) - 10 * sizeof(uint16_t));
    for (int j = 0; j < 10; ++j)
      localBuffer[j] = 0x1FFF;
    uint16_t *neighbors = localBuffer;
    if (source.neighbor_count > 10) {
      if (globalUsed + source.neighbor_count <= 10000) {
        neighbors = paths->globalBuffer + globalUsed;
        globalUsed += source.neighbor_count;
      } else {
        neighbors = overflow + overflowUsed;
        overflowUsed += source.neighbor_count;
      }
    }
    target.neighbors = neighbors;
    for (size_t j = 0; j < source.neighbors.size(); ++j)
      neighbors[j] = source.neighbors[j];
  }
  paths->globalBuffer_ptr = paths->globalBuffer + globalUsed;

  // Mirror BWAPI Server::onMatchStart's GameData view, including its x/y array
  // order and its fixed 5,000-entry split tables. SC retains all 25,000 splits.
  g->mapWidth = metadata.map_width;
  g->mapHeight = metadata.map_height;
  g->regionCount = static_cast<int>(metadata.regions.size());
  memset(g->mapTileRegionId, 0, sizeof g->mapTileRegionId);
  for (unsigned y = 0; y < metadata.map_height; ++y)
    for (unsigned x = 0; x < metadata.map_width; ++x)
      g->mapTileRegionId[x][y] = paths->mapTileRegionId[y][x];
  for (int i = 0; i < 5000; ++i) {
    g->mapSplitTilesMiniTileMask[i] = paths->splitTiles[i].minitileMask;
    g->mapSplitTilesRegion1[i] = paths->splitTiles[i].rgn1;
    g->mapSplitTilesRegion2[i] = paths->splitTiles[i].rgn2;
  }
  memset(g->regions, 0, sizeof g->regions);
  for (size_t i = 0; i < metadata.regions.size(); ++i) {
    const BW::region &source = paths->regions[i];
    RegionData &target = g->regions[i];
    target.id = static_cast<int>(i);
    target.islandID = source.groupIndex;
    // BW::region::getCenter narrows the shifted fixed-point values to s16.
    target.center_x = static_cast<int16_t>(source.rgnCenterX >> 8);
    target.center_y = static_cast<int16_t>(source.rgnCenterY >> 8);
    target.priority = source.defencePriority & 0x7F;
    target.leftMost = source.rgnBox.left;
    target.rightMost = source.rgnBox.right;
    target.topMost = source.rgnBox.top;
    target.bottomMost = source.rgnBox.bottom;
    target.neighborCount = source.neighborCount;
    for (int j = 0; j < source.neighborCount; ++j)
      target.neighbors[j] = source.neighbors[j];
    target.isAccessible = source.accessabilityFlags != 0x1FFD;
    target.isHigherGround = source.accessabilityFlags == 0x1FF9;
  }
  g->startLocationCount = static_cast<int>(metadata.starts.size());
  for (size_t i = 0; i < metadata.starts.size(); ++i) {
    g->startLocations[i].x = metadata.starts[i].x;
    g->startLocations[i].y = metadata.starts[i].y;
  }

  s_saiPaths = paths;
  s_saiOverflowNeighbors = overflow;
  // The official DLL's 0x6D5BFC read lands in the image-backed SC range.
  *ScT<BW::SAI_Paths *>(0x6D5BFC) = paths;
  return true;
}

static void FilterUnitData(const HxUnit &src, pluto_bridge::UnitVisibility visibility,
                           UnitData &u, bool initialFrame) {
  const bool initialNeutral = initialFrame && src.player == g->neutral;
  if (!visibility.visible && !initialNeutral) {
    u.type = UnitTypes::Enum::Unknown;
    u.player = g->neutral;
    u.positionX = u.positionY = -1;
    u.targetPositionX = u.targetPositionY = -1;
    u.orderTargetPositionX = u.orderTargetPositionY = -1;
    u.order = Orders::Enum::Unknown;
    u.isCompleted = false;
    u.isIdle = false;
    u.isDetected = false;
    u.hitPoints = u.lastHitPoints = u.shields = u.energy = 0;
    u.buildType = UnitTypes::Enum::None;
    u.trainingQueueCount = 0;
    for (int &type : u.trainingQueue)
      type = UnitTypes::Enum::None;
    u.tech = TechTypes::Enum::None;
    u.upgrade = UpgradeTypes::Enum::None;
    u.remainingResearchTime = u.remainingUpgradeTime = 0;
    u.remainingBuildTime = u.remainingTrainTime = 0;
    u.groundWeaponCooldown = u.airWeaponCooldown = u.spellCooldown = 0;
    u.buildUnit = u.orderTarget = u.hatchery = -1;
  } else if (src.player != 0) {
    // BWAPI's canAccessInside does not expose enemy production internals.
    u.energy = 0;
    u.buildType = UnitTypes::Enum::None;
    u.trainingQueueCount = 0;
    for (int &type : u.trainingQueue)
      type = UnitTypes::Enum::None;
    u.tech = TechTypes::Enum::None;
    u.upgrade = UpgradeTypes::Enum::None;
    u.remainingResearchTime = u.remainingUpgradeTime = 0;
    u.remainingBuildTime = u.remainingTrainTime = 0;
    u.buildUnit = -1;
    if (!initialNeutral && !visibility.detected) {
      u.hitPoints = u.lastHitPoints = u.shields = 0;
      u.order = Orders::Enum::Unknown;
      u.isIdle = false;
      u.orderTarget = -1;
    }
  }
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
    unsigned me = SC_UNITS + i * 0x150;
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
  snprintf(reinterpret_cast<char *>(gg + 0xC4C), 0x100, "%s", g->mapPathName);
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
  // The bot calls SC's sendTurn (0x485A40): a ret stub in the reserved image.
  Sc(SC_SENDTURN)[0] = 0xC3;
  // The bot treats 0x400001 as a string: an empty one.
  Sc(0x400001)[0] = '\0';
}

// The production path: the Hyperion frame is written straight into the SC image
// (the fork's input). The fork's own server converts the image into the GameData
// every frame -- the GameData is the fork's private output, and the bridge writing
// it concurrently broke the server's unit sync (the bot ended up observing one
// zero unit and issuing no commands).
static bool ApplyImageState(const HxUnit *units, const HxUnitExtra *extras,
                            const pluto_late::UnitExtra *extras4,
                            const pluto_late::PlayerTech *playerTech,
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
      return false;
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

  if (mapW <= 256 && mapH <= 256) {
    g->mapWidth = static_cast<int>(mapW);
    g->mapHeight = static_cast<int>(mapH);
    if (rawTiles != NULL) {
      for (unsigned y = 0; y < mapH; ++y)
        for (unsigned x = 0; x < mapW; ++x) {
          const unsigned char *tile = rawTiles + (y * mapW + x) * 4;
          const bool visible = (tile[0] & 1) == 0;
          g->getGroundHeight[x][y] = tile[3] & 7;
          g->isBuildable[x][y] = (tile[2] & 0x84) == 0;
          for (unsigned dy = 0; dy < 4; ++dy)
            for (unsigned dx = 0; dx < 4; ++dx)
              g->isWalkable[x * 4 + dx][y * 4 + dy] = (tile[2] & 1) != 0 && (tile[2] & 4) == 0;
          g->isVisible[x][y] = visible;
          g->isExplored[x][y] = (tile[1] & 1) == 0;
          g->hasCreep[x][y] = visible && ((tile[2] & 0x40) != 0 || (tile[3] & 0x20) != 0);
          g->isOccupied[x][y] = visible && (tile[3] & 0x08) != 0;
        }
    }
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
    return SC_UNITS + slot * 0x150;
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
    const pluto_late::UnitExtra *e4 = extras4 != NULL ? &extras4[i] : NULL;
    HxUnitExtra commonExtra;
    if (e == NULL && e4 != NULL) {
      static_assert(sizeof commonExtra == offsetof(pluto_late::UnitExtra, buildQueue),
                    "HXP4's common prefix must remain HXP3's 64 bytes");
      memcpy(&commonExtra, e4, sizeof commonExtra);
      e = &commonExtra;
    }
    const pluto_bridge::UnitVisibility visibility = UnitVisibilityFor(u, e);
    if (e4 != NULL) {
      for (int q = 0; q < 5; ++q)
        su.buildQueue[q] = e4->buildQueue[q];
      su.buildQueueSlot = e4->buildQueueSlot;
      su.upgradeResearchTime = 0;
      su.techType = pluto_late::kTechNone;
      su.upgradeType = pluto_late::kUpgradeNone;
      if (u.order == pluto_late::kResearchTechOrder) {
        su.upgradeResearchTime = e4->remainingResearchTime;
        su.techType = e4->techType;
      } else if (u.order == pluto_late::kUpgradeOrder) {
        su.upgradeResearchTime = e4->remainingResearchTime;
        su.upgradeType = e4->upgradeType;
      }
    }
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
      if (e4 == NULL) {
        su.buildQueueSlot = e->buildQueueSlot < 5 ? e->buildQueueSlot : 0;
        if (e->buildType <= 0xE4)
          su.buildQueue[su.buildQueueSlot] = static_cast<uint16_t>(e->buildType);
      }
      su.remainingBuildTime = e->remainingBuildTime;
      su.resourceCount = static_cast<uint16_t>(e->resourceCount);
    } else if (e4 == NULL) {
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
    su.uniquenessIdentifier = u.pad;
    su.visibilityStatus = visibility.detected ? 1 : 0;
    visibleSlots[slot] = visibility.visible ? 1 : 0;
    if (visibility.visible && u.player < nPlayers && u.type < UnitTypes::Enum::MAX) {
      PlayerData &p = g->players[u.player];
      const UnitType type(static_cast<int>(u.type));
      ++p.allUnitCount[u.type];
      ++p.visibleUnitCount[u.type];
      ++p.allUnitCount[UnitTypes::Enum::AllUnits];
      ++p.visibleUnitCount[UnitTypes::Enum::AllUnits];
      if (u.completed) {
        ++p.completedUnitCount[u.type];
        ++p.completedUnitCount[UnitTypes::Enum::AllUnits];
      }
      const int group = type.isBuilding() ? UnitTypes::Enum::Buildings : UnitTypes::Enum::Men;
      ++p.allUnitCount[group];
      ++p.visibleUnitCount[group];
      if (u.completed)
        ++p.completedUnitCount[group];
      if (type.isBuilding() && (type.canProduce() || type.producesLarva())) {
        ++p.allUnitCount[UnitTypes::Enum::Factories];
        ++p.visibleUnitCount[UnitTypes::Enum::Factories];
        if (u.completed)
          ++p.completedUnitCount[UnitTypes::Enum::Factories];
      }
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
      nodes[larvaSlot].connectedUnit = SC_UNITS + hatcherySlot * 0x150;
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
    const unsigned me = SC_UNITS + slot * 0x150;
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
  static bool s_loggedNodeAddresses = false;
  if (!s_loggedNodeAddresses) {
    fprintf(stderr, "[bridge] genuine SC nodes: table=%p first=%p visible-head=%08x\n",
            static_cast<void *>(nodes), static_cast<void *>(Sc(SC_VIS_FIRST)),
            *ScT<unsigned>(SC_VIS_FIRST));
    s_loggedNodeAddresses = true;
  }

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
    if (i == 0)
      snprintf(p.name, sizeof p.name, "PLuto");
    p.race = static_cast<int>(players[i].race);
    const bool own = i == 0;
    p.minerals = own ? static_cast<int>(players[i].minerals) : 0;
    p.gas = own ? static_cast<int>(players[i].gas) : 0;
    for (int r = 0; r < 3; ++r) {
      p.supplyTotal[r] = own && r == static_cast<int>(players[i].race)
                             ? static_cast<int>(players[i].supplyTotal)
                             : 0;
      p.supplyUsed[r] = own && r == static_cast<int>(players[i].race)
                            ? static_cast<int>(players[i].supplyUsed)
                            : 0;
    }
    // BWAPI hides the enemy's exact start but publishes every map start as a
    // possible location. The depot's center is 64,48 pixels from the map record.
    p.startLocationX = i == 0 ? (static_cast<int>(players[i].startX) - 64) / 32 : -1;
    p.startLocationY = i == 0 ? (static_cast<int>(players[i].startY) - 48) / 32 : -1;
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
      p.isUnitAvailable[t] = own;
    static const pluto_late::PlayerTech emptyTech;
    const pluto_late::PlayerTech &tech = playerTech != NULL ? playerTech[i] : emptyTech;
    for (int t = 0; t < TechTypes::Enum::MAX; ++t) {
      p.isResearchAvailable[t] = own;
      if (playerTech != NULL) {
        p.hasResearched[t] = own && t < 44 && tech.hasResearched[t] != 0;
        p.isResearching[t] = own && t < 44 && pluto_late::Bit(tech.techInProgress.data(), t);
      } else {
        p.hasResearched[t] = own && TechType(t).whatResearches() == UnitTypes::Enum::None;
        p.isResearching[t] = false;
      }
    }
    if (playerTech == NULL) {
      p.hasResearched[TechTypes::Enum::Nuclear_Strike] =
          own && p.isUnitAvailable[UnitTypes::Enum::Terran_Nuclear_Missile];
    }
    for (int t = 0; t < UpgradeTypes::Enum::MAX; ++t) {
      p.maxUpgradeLevel[t] = own ? UpgradeType(t).maxRepeats() : 0;
      p.upgradeLevel[t] = playerTech != NULL && own && t < 61 ? tech.upgradeLevel[t] : 0;
      p.isUpgrading[t] = playerTech != NULL && own && t < 61 &&
                         pluto_late::Bit(tech.upgradeInProgress.data(), t);
    }
  }
  if (s_hxpmStream.state() != pluto_hxpm::StreamMetadataState::kComplete &&
      getenv("HYPERION_PLUTO_STARTS") == NULL) {
    // Legacy HXP streams have no static map-start metadata. Preserve their old
    // occupied-start fallback; production streams supply every map start above.
    g->startLocationCount = 0;
    for (unsigned i = 0; i + 1 < nPlayers && i < 8; ++i) {
      if (players[i].startX == 0 && players[i].startY == 0)
        continue;
      g->startLocations[i].x = (static_cast<int>(players[i].startX) - 64) / 32;
      g->startLocations[i].y = (static_cast<int>(players[i].startY) - 48) / 32;
      ++g->startLocationCount;
    }
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
      gg[0xDF98 + i * 46 + t] = static_cast<unsigned char>(UpgradeType(t).maxRepeats());
    for (int t = 0; t < 20; ++t)
      gg[0xFF60 + i * 20 + t] = 1;
    for (int t = 0; t < 15; ++t)
      gg[0x10188 + i * 15 + t] = static_cast<unsigned char>(UpgradeType(t).maxRepeats());
  }
  // Dynamic researched/level/progress tables are separate from availability and
  // max-level defaults. HXP4 publishes them; legacy frames clear them without
  // touching the ordinary-melee availability/max setup above.
  static const pluto_late::PlayerTech emptyGameTech;
  for (unsigned i = 0; i < 12; ++i) {
    const pluto_late::PlayerTech &tech =
        playerTech != NULL && i < nPlayers ? playerTech[i] : emptyGameTech;
    for (unsigned t = 0; t < 44; ++t) {
      if (t < 24)
        gg[0xDE54 + i * 24 + t] = tech.hasResearched[t];
      else
        gg[0x10050 + i * 20 + (t - 24)] = tech.hasResearched[t];
    }
    for (unsigned u = 0; u < 61; ++u) {
      if (u < 46)
        gg[0xE1C0 + i * 46 + u] = tech.upgradeLevel[u];
      else
        gg[0x1023C + i * 15 + (u - 46)] = tech.upgradeLevel[u];
    }
    memcpy(gg + 0x10140 + i * 6, tech.techInProgress.data(), 6);
    memcpy(gg + 0x102F0 + i * 8, tech.upgradeInProgress.data(), 8);
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
  snprintf(reinterpret_cast<char *>(gg + 0xC4C), 0x100, "%s", g->mapPathName);
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
  return true;
}

// Opt-in receiver evidence, written after every native/BWAPI field is complete
// but before the matching publication can be delivered with pipe code 2. This
// has no effect in normal operation and exists for deterministic HXP4 tests.
static bool DumpLateStateEvidence(const char *path) {
  FILE *out = fopen(path, "ab");
  if (out == NULL)
    return false;
  const ScUnit *nodes = ScT<ScUnit>(SC_UNITS);
  const unsigned char *gg = Sc(SC_GAME);
  bool ok = fprintf(out, "frame=%d publication_before=%llu event_count=%d\n",
                    g->frameCount, static_cast<unsigned long long>(s_publication.ready()),
                    g->eventCount) >= 0;
  for (int slot = 0; slot < 1700 && ok; ++slot) {
    const ScUnit &u = nodes[slot];
    if (u.playerID == 0xFF || u.unitType == 228)
      continue;
    ok = fprintf(out,
                 "unit slot=%d order=%u queue=%u,%u,%u,%u,%u queue_slot=%u "
                 "research_time=%u tech=%u upgrade=%u\n",
                 slot, u.orderID, u.buildQueue[0], u.buildQueue[1], u.buildQueue[2],
                 u.buildQueue[3], u.buildQueue[4], u.buildQueueSlot,
                 u.upgradeResearchTime, u.techType, u.upgradeType) >= 0;
    const int packed = g->unitArray[slot];
    if (packed >= 0 && packed < 10000) {
      const UnitData &data = g->units[packed];
      ok = fprintf(out,
                   "unitdata slot=%d player=%d queue_count=%d queue=%d,%d,%d,%d,%d "
                   "tech=%d upgrade=%d research_time=%d upgrade_time=%d\n",
                   slot, data.player, data.trainingQueueCount, data.trainingQueue[0],
                   data.trainingQueue[1], data.trainingQueue[2], data.trainingQueue[3],
                   data.trainingQueue[4], data.tech, data.upgrade,
                   data.remainingResearchTime, data.remainingUpgradeTime) >= 0;
    }
  }
  for (int player = 0; player < 12 && ok; ++player) {
    ok = fprintf(out,
                 "player p=%d researched0=%u researched24=%u researched43=%u "
                 "level0=%u level46=%u level60=%u techbits=%02x%02x%02x%02x%02x%02x "
                 "upgradebits=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                 player, gg[0xDE54 + player * 24], gg[0x10050 + player * 20],
                 gg[0x10050 + player * 20 + 19], gg[0xE1C0 + player * 46],
                 gg[0x1023C + player * 15], gg[0x1023C + player * 15 + 14],
                 gg[0x10140 + player * 6], gg[0x10140 + player * 6 + 1],
                 gg[0x10140 + player * 6 + 2], gg[0x10140 + player * 6 + 3],
                 gg[0x10140 + player * 6 + 4], gg[0x10140 + player * 6 + 5],
                 gg[0x102F0 + player * 8], gg[0x102F0 + player * 8 + 1],
                 gg[0x102F0 + player * 8 + 2], gg[0x102F0 + player * 8 + 3],
                 gg[0x102F0 + player * 8 + 4], gg[0x102F0 + player * 8 + 5],
                 gg[0x102F0 + player * 8 + 6], gg[0x102F0 + player * 8 + 7]) >= 0;
    const PlayerData &data = g->players[player];
    ok = fprintf(out,
                 "playerdata p=%d has0=%d has24=%d has43=%d researching0=%d "
                 "researching24=%d researching43=%d level0=%d level46=%d level60=%d "
                 "upgrading0=%d upgrading46=%d upgrading60=%d\n",
                 player, data.hasResearched[0], data.hasResearched[24],
                 data.hasResearched[43], data.isResearching[0], data.isResearching[24],
                 data.isResearching[43], data.upgradeLevel[0], data.upgradeLevel[46],
                 data.upgradeLevel[60], data.isUpgrading[0], data.isUpgrading[46],
                 data.isUpgrading[60]) >= 0;
  }
  if (fclose(out) != 0)
    ok = false;
  return ok;
}

static HANDLE s_pipeThread;
static HANDLE s_pipe;
static bool s_pipeUnexpectedFailure = false;
static DWORD s_pipeFailureError = 0;
static bool s_pipeGracefulDisconnect = false;
static DWORD s_pipeDisconnectError = 0;
static unsigned long long s_pipeDisconnectGeneration = 0;
static bool s_pipePublicationRequestPending = false;

// All pipe status is committed under the same lock as publication.  A graceful
// close remembers the last generation released to the client: it is normal only
// when no later frame needs publishing.  I/O/protocol failures are always fatal
// unless shutdown has already begun (CancelIoEx intentionally causes those).
static void ClosePipePublication(bool graceful, DWORD error) {
  StateGuard guard;
  s_publication.ClientClosed();
  s_pipePublicationRequestPending = false;
  const bool expectedFinalCancel = s_finalizing && error == ERROR_OPERATION_ABORTED;
  if (!s_shutdown && !expectedFinalCancel) {
    if (graceful) {
      s_pipeGracefulDisconnect = true;
      s_pipeDisconnectError = error;
      s_pipeDisconnectGeneration = s_publication.delivered();
      if (getenv("HX_DEBUG") != NULL)
        fprintf(stderr, "[bridge] pipe client disconnected after generation %llu (%lu)\n",
                s_pipeDisconnectGeneration, error);
    } else {
      s_pipeUnexpectedFailure = true;
      s_pipeFailureError = error;
      fprintf(stderr, "[bridge] unexpected pipe/connection failure (%lu)\n", error);
    }
  }
  WakeAllConditionVariable(&g_stateChanged);
}

// Client::connect first waits for a code 2 but consumes no events.  Each later
// Client::update writes code 1, waits for code 2, and only then consumes the
// shared events.  Therefore code 1 acknowledges the previously delivered
// generation; code 2 is withheld until a newer complete publication is ready.
// The next host write waits for that delivery's acknowledgement (or pipe
// closure), so the client never observes a half-written GameData/SC image.
static DWORD WINAPI ServePipe(LPVOID p) {
  HANDLE hPipe = static_cast<HANDLE>(p);
  const bool debug = getenv("HX_DEBUG") != NULL;
  s_pipe = hPipe;
  {
    StateGuard guard;
    // The initial raw GameImpl must remain alive through onMatchStart and
    // onStart.  Client::connect deletes BroodwarPtr, so do not even allow the
    // pipe connection until the host has finished using that handoff object.
    while (!s_clientReplacementEnabled && !s_finalizing && !s_shutdown)
      SleepConditionVariableCS(&g_stateChanged, &g_stateLock, INFINITE);
    if (s_finalizing || s_shutdown)
      return 0;
  }
  if (!ConnectNamedPipe(hPipe, NULL)) {
    const DWORD error = GetLastError();
    if (error != ERROR_PIPE_CONNECTED) {
      ClosePipePublication(false, error);
      return 1;
    }
  }
  {
    StateGuard guard;
    if (s_finalizing || s_shutdown) {
      s_publication.ClientClosed();
      return 0;
    }
    // Backpressure begins with a real client, not merely the expectation that
    // the DLL might connect.  This official module's onFrame path can complete
    // without opening BWAPI's pipe; waiting for that optional client would
    // deadlock every subsequent host frame.
    s_publication.ClientConnected();
  }

  {
    StateGuard guard;
    if (s_finalizing || s_shutdown) {
      s_publication.ClientClosed();
      return 0;
    }
  }
  if (debug)
    fprintf(stderr, "[bridge] pipe connected; releasing Client::connect\n");
  // This releases connect() only; it is deliberately not a publication
  // delivery and has no generation associated with it.
  DWORD code = 2, n = 0;
  const BOOL connectWriteOk = WriteFile(hPipe, &code, sizeof code, &n, NULL);
  if (!connectWriteOk || n != sizeof code) {
    ClosePipePublication(false, connectWriteOk ? ERROR_INVALID_DATA : GetLastError());
    return 1;
  }

  for (;;) {
    // Never hold g_stateLock over a blocking pipe operation.  In particular,
    // the host must be able to publish and wake us while the client is away.
    {
      StateGuard guard;
      if (s_finalizing || s_shutdown) {
        s_publication.ClientClosed();
        break;
      }
    }
    code = 1;
    n = 0;
    const BOOL readOk = ReadFile(hPipe, &code, sizeof code, &n, NULL);
    const DWORD readError = readOk ? 0 : GetLastError();
    if (!readOk || n != sizeof code) {
      const bool graceful = !readOk &&
          (readError == ERROR_BROKEN_PIPE || readError == ERROR_PIPE_NOT_CONNECTED);
      ClosePipePublication(graceful, readOk ? ERROR_INVALID_DATA : readError);
      break;
    }
    if (code != 1) {
      ClosePipePublication(false, ERROR_INVALID_DATA);
      break;
    }
    DWORD releaseDelayMs = 0;
    {
      StateGuard guard;
      if (s_finalizing || s_shutdown)
        break;  // finalization does not accept a new update request
      s_publication.AcknowledgeDelivered();
      s_pipePublicationRequestPending = true;
      WakeAllConditionVariable(&g_stateChanged);
      // connect() may finish during onStart, but update() must not construct
      // match state while the shared enemy/self slot is temporarily remapped.
      while (!s_shutdown && !s_finalizing &&
             (!s_onStartComplete || !s_publication.HasUndeliveredPublication()))
        SleepConditionVariableCS(&g_stateChanged, &g_stateLock, INFINITE);
      if (s_shutdown || !s_onStartComplete ||
          (s_finalizing && !s_publication.HasUndeliveredPublication())) {
        s_pipePublicationRequestPending = false;
        break;
      }
      const unsigned long long generation =
          static_cast<unsigned long long>(s_publication.MarkDelivered());
      if (debug)
        fprintf(stderr, "[bridge] pipe code 1 -> publication %llu ready\n", generation);
      // A test-only hold makes finalization-during-release deterministic.  It
      // is outside the state lock and zero in every normal deployment.
      if (generation == 2 && getenv("HX_TEST_RELEASE_DELAY_MS") != NULL)
        releaseDelayMs = static_cast<DWORD>(atoi(getenv("HX_TEST_RELEASE_DELAY_MS")));
      // The publisher now sees an unacknowledged delivery and cannot modify
      // shared state until the client's next code 1 arrives.
    }
    if (releaseDelayMs != 0)
      Sleep(releaseDelayMs);
    code = 2;
    n = 0;
    const BOOL updateWriteOk = WriteFile(hPipe, &code, sizeof code, &n, NULL);
    bool stopAfterWrite = false;
    {
      StateGuard guard;
      s_pipePublicationRequestPending = false;
      stopAfterWrite = s_finalizing || s_shutdown;
    }
    if (!updateWriteOk || n != sizeof code) {
      ClosePipePublication(false, updateWriteOk ? ERROR_INVALID_DATA : GetLastError());
      break;
    }
    if (stopAfterWrite)
      break;
  }
  return 0;
}

// Quiesce the BWAPI pipe before any final HXRP or onEnd side effect.  A code-1
// request accepted before finalization is allowed to finish its current code-2
// release; no new request is accepted.  Everything else is cancelled and the
// thread is joined before final status is inspected.
static void FinalizePipe(bool &runtimeFailed) {
  bool cancelIo = false;
  {
    StateGuard guard;
    s_finalizing = true;
    cancelIo = !s_pipePublicationRequestPending;
    WakeAllConditionVariable(&g_stateChanged);
  }
  if (cancelIo && s_pipe != NULL)
    CancelIoEx(s_pipe, NULL);

  DWORD joined = s_pipeThread != NULL ? WaitForSingleObject(s_pipeThread, 3000) : WAIT_OBJECT_0;
  if (joined != WAIT_OBJECT_0) {
    runtimeFailed = true;
    if (s_pipe != NULL)
      CancelIoEx(s_pipe, NULL);
    joined = s_pipeThread != NULL ? WaitForSingleObject(s_pipeThread, 1000) : WAIT_OBJECT_0;
  }
  {
    StateGuard guard;
    if (s_pipeUnexpectedFailure)
      runtimeFailed = true;
    if (s_pipeGracefulDisconnect &&
        s_pipeDisconnectGeneration != s_publication.ready())
      runtimeFailed = true;
    s_shutdown = true;
    WakeAllConditionVariable(&g_stateChanged);
  }
  if (joined == WAIT_OBJECT_0) {
    DeleteCriticalSection(&g_stateLock);
  } else {
    fprintf(stderr, "[bridge] pipe thread did not quiesce; final side effects suppressed\n");
    runtimeFailed = true;
  }
  if (s_pipeThread != NULL) {
    CloseHandle(s_pipeThread);
    s_pipeThread = NULL;
  }
  if (s_pipe != NULL) {
    CloseHandle(s_pipe);
    s_pipe = NULL;
  }
}

static HANDLE s_hShm;
static void *s_view;
static void *s_bigview;  // the writer's real target: a big writable buffer (see main)

static bool ProtectionIsWritable(DWORD protection, bool executable) {
  switch (protection & 0xFF) {
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
      return !executable;
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
      return true;
    default:
      return false;
  }
}

// Validate the complete byte range, not merely the page containing its start.
// Wine commonly reports PAGE_EXECUTE_WRITECOPY (0x80) after VirtualProtect;
// both that and PAGE_EXECUTE_READWRITE are genuine writable/executable modes.
static bool QueryCommittedImageRange(uintptr_t address, SIZE_T size,
                                     uintptr_t allocationBase, bool executable,
                                     MEMORY_BASIC_INFORMATION *result) {
  MEMORY_BASIC_INFORMATION info;
  memset(&info, 0, sizeof info);
  if (size == 0 || VirtualQuery(reinterpret_cast<void *>(address), &info, sizeof info) != sizeof info) {
    fprintf(stderr, "[bridge] image range query failed at %p (%lu)\n",
            reinterpret_cast<void *>(address), GetLastError());
    return false;
  }
  const uintptr_t regionBase = reinterpret_cast<uintptr_t>(info.BaseAddress);
  const bool covered = address >= regionBase && address - regionBase <= info.RegionSize &&
                       size <= info.RegionSize - (address - regionBase);
  if (info.AllocationBase != reinterpret_cast<void *>(allocationBase) ||
      info.Type != MEM_IMAGE || info.State != MEM_COMMIT || !covered ||
      !ProtectionIsWritable(info.Protect, executable)) {
    fprintf(stderr,
            "[bridge] invalid image range %p+%lx: allocation=%p (expected %p) "
            "region=%p+%lx type=%lx state=%lx protect=%lx executable=%d\n",
            reinterpret_cast<void *>(address), size, info.AllocationBase,
            reinterpret_cast<void *>(allocationBase), info.BaseAddress, info.RegionSize,
            info.Type, info.State, info.Protect, executable ? 1 : 0);
    return false;
  }
  if (result != NULL)
    *result = info;
  return true;
}

// The bot accesses StarCraft 1.16.1's own globals by absolute address.  The PE's
// post-link .game section is 0x3ff000 bytes at RVA 0x1000, so 0x401000..0x800000
// belongs to this process image before the CRT or heap can allocate it.  Actual
// host code/data stays at or above 0x801000; .hdr at 0x800000 serves the CRT.
static bool ReserveScMemory() {
  DWORD oldProtection = 0;
  if (!VirtualProtect(reinterpret_cast<void *>(0x400000), 0x400000,
                      PAGE_EXECUTE_READWRITE, &oldProtection)) {
    fprintf(stderr, "[bridge] image-backed SC protect failed (%lu)\n", GetLastError());
    return false;
  }
  struct ScBoundary {
    uintptr_t address;
    SIZE_T size;
    const char *name;
  };
  static const ScBoundary boundaries[] = {
      {0x400000, 0x1000, "image-header"},
      {0x401000, 0x1000, "game-first-page"},
      {0x485A40, 1, "sendTurn-stub"},
      {0x5124D8, 4, "official-store"},
      {0x6D1260, 4, "active-tile-pointer"},
      {0x7FF000, 0x1000, "game-last-page"},  // 0x800000 - 0x1000
  };
  for (const ScBoundary &boundary : boundaries) {
    MEMORY_BASIC_INFORMATION info;
    if (!QueryCommittedImageRange(boundary.address, boundary.size, 0x400000, true, &info))
      return false;
    fprintf(stderr,
            "[bridge] SC %s: %p+%lx region=%p+%lx allocation=%p type=%lx "
            "state=%lx protect=%lx\n",
            boundary.name, reinterpret_cast<void *>(boundary.address), boundary.size,
            info.BaseAddress, info.RegionSize, info.AllocationBase, info.Type,
            info.State, info.Protect);
  }
  fprintf(stderr,
          "[bridge] SC boundaries validated: committed MEM_IMAGE, writable+executable\n");
  // Preserve the real module's high custom worker stack at 0xF0B5D710.  Wine
  // requires LARGE_ADDRESS_AWARE for this range.  Reserve the complete 64-KiB
  // allocation-granularity region and validate coverage of the actual ESP.
  const uintptr_t stackBase = 0xF0B50000u;
  const uintptr_t stackEnd = 0xF0B60000u;
  const uintptr_t workerEsp = 0xF0B5D710u;
  void *stack = VirtualAlloc(reinterpret_cast<void *>(stackBase), stackEnd - stackBase,
                             MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (stack != reinterpret_cast<void *>(stackBase)) {
    fprintf(stderr, "[bridge] high custom stack allocation failed: %p (%lu)\n",
            stack, stack == NULL ? GetLastError() : ERROR_INVALID_ADDRESS);
    return false;
  }
  MEMORY_BASIC_INFORMATION stackInfo;
  memset(&stackInfo, 0, sizeof stackInfo);
  if (VirtualQuery(reinterpret_cast<void *>(workerEsp), &stackInfo, sizeof stackInfo) != sizeof stackInfo) {
    fprintf(stderr, "[bridge] high custom stack query failed (%lu)\n", GetLastError());
    return false;
  }
  const uintptr_t committedBase = reinterpret_cast<uintptr_t>(stackInfo.BaseAddress);
  const uintptr_t committedEnd = committedBase + stackInfo.RegionSize;
  if (stackInfo.AllocationBase != stack || stackInfo.State != MEM_COMMIT ||
      stackInfo.Protect != PAGE_READWRITE || committedBase > workerEsp ||
      committedEnd < workerEsp + 4 || committedBase < stackBase || committedEnd > stackEnd) {
    fprintf(stderr,
            "[bridge] high custom stack coverage failed: allocation=%p base=%p size=%lx "
            "state=%lx protect=%lx\n",
            stackInfo.AllocationBase, stackInfo.BaseAddress, stackInfo.RegionSize,
            stackInfo.State, stackInfo.Protect);
    return false;
  }
  fprintf(stderr,
          "[bridge] high custom stack: %p..%p committed RW; ESP %p covered by %p+%lx\n",
          stack, reinterpret_cast<void *>(stackEnd), reinterpret_cast<void *>(workerEsp),
          stackInfo.BaseAddress, stackInfo.RegionSize);
  return true;
}

// The frame pipe's own fd: the fork's threads close fd 0 during their start-up
// (that is what cut the bridge's stdin at ~1-26 frames, in the harness and the
// real game alike). Dup'd as the first thing in main, before the fork loads.
static int s_frameIn = -1;
enum class ReadStatus { kOk, kCleanEof, kFailure };
static ReadStatus ReadN(void *pv, size_t cb) {
  unsigned char *p = static_cast<unsigned char *>(pv);
  size_t total = 0;
  while (cb) {
    int n = read(s_frameIn, p, cb);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      // EOF is clean only before any byte of a new record was consumed.
      return n == 0 && total == 0 ? ReadStatus::kCleanEof : ReadStatus::kFailure;
    }
    p += n;
    cb -= n;
    total += static_cast<size_t>(n);
  }
  return ReadStatus::kOk;
}
static bool ReadRequired(void *pv, size_t cb, const char *part) {
  if (ReadN(pv, cb) == ReadStatus::kOk)
    return true;
  fprintf(stderr, "[bridge] truncated or unreadable %s on fd %d (errno %d)\n",
          part, s_frameIn, errno);
  return false;
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
// Return the complete classic command's size, 0 when its tail is incomplete,
// or -1 for an opcode Hyperion's in-game dispatcher does not accept.
static int TurnCommandSize(const unsigned char *p, unsigned left) {
  return pluto_bridge::TurnCommandSize(p, left);
}

// The bot appends classic commands to StarCraft's 512-byte TurnBuffer. Read a
// complete, valid in-game stream before clearing it. Unknown, lobby-only, or
// truncated data is a host error, not a prefix to ship while silently
// discarding the rest.
static bool FlushReply(unsigned frame) {
  unsigned char *tb = Sc(0x654880);
  unsigned end = 0;
  while (end < 0x200 && tb[end] != 0) {
    const int size = TurnCommandSize(tb + end, 0x200 - end);
    if (size < 0) {
      fprintf(stderr, "[bridge] unknown turn opcode %02x at %u in frame %u\n",
              tb[end], end, frame);
      return false;
    }
    if (size == 0) {
      fprintf(stderr, "[bridge] truncated turn opcode %02x at %u in frame %u\n",
              tb[end], end, frame);
      return false;
    }
    end += static_cast<unsigned>(size);
  }
  if (end == 0x200 && tb[end - 1] != 0) {
    // A full buffer is valid only when the loop consumed it exactly. sendTurn's
    // full-queue behavior is exercised separately; never scan past the native
    // 512-byte buffer as the old 64 KiB scan did.
    fprintf(stderr, "[bridge] turn buffer is full in frame %u\n", frame);
  }
  unsigned out[3] = {0x50525848, frame, end};  // 'HXRP'
  if (!WriteN(out, sizeof out) || !WriteN(tb, end))
    return false;
  unsigned win = 0;
  if (!WriteN(&win, 4))
    return false;
  memset(tb, 0, 0x200);
  *ScT<unsigned>(0x654AA0) = 0;
  fflush(stdout);
  if (frame == 0) {
    // The console detach lands at the end of frame 0. Move input to the named
    // FIFO now, while frame 1 can still be read; switching at frame 1 was
    // already too late in a real game. Hyperion opens its writer before waiting
    // for this reply, so this O_RDONLY open cannot see a writerless EOF.
    const int fd = open("frames.fifo", O_RDONLY | O_BINARY);
    if (fd >= 0) {
      _setmode(fd, _O_BINARY);
      s_frameIn = fd;
      fprintf(stderr, "[bridge] frame input -> frames.fifo (fd %d)\n", fd);
    }
  }
  return true;
}

// Hash bytes from the same open handle that is held with write/delete sharing
// denied through LoadLibrary.  This does not hash a pathname or the loaded image.
static bool HashOpenFileSha256(HANDLE file, char hex[65]) {
  LARGE_INTEGER zero;
  zero.QuadPart = 0;
  if (!SetFilePointerEx(file, zero, NULL, FILE_BEGIN))
    return false;
  pluto_bridge::Sha256 hash;
  unsigned char block[65536];
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(file, block, sizeof block, &got, NULL))
      return false;
    if (got == 0)
      break;
    hash.Update(block, got);
  }
  hash.Finish(hex);
  return true;
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
  const bool testSyncClient = getenv("HX_TEST_SYNC_CLIENT") != NULL;
  const bool testAsyncClient = getenv("HX_TEST_ASYNC_CLIENT") != NULL;
  if (testSyncClient && testAsyncClient) {
    fprintf(stderr, "[bridge] choose only one test Client mode\n");
    return 1;
  }
  s_testAsyncClient = testAsyncClient;
  const int scenario = argc > 1 ? atoi(argv[1]) : 1;
  if (!ReserveScMemory())
    return 1;
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
  g->latency = Latency::Enum::LanLow;
  g->latencyFrames = 4;
  g->latencyTime = 167;
  g->remainingLatencyFrames = 4;
  g->remainingLatencyTime = 167;
  g->hasLatCom = true;
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
  if (getenv("HYPERION_PLUTO_MAP_FILE") != NULL && !ApplyMapIdentity())
    return 1;
  FillPlayer(0, Races::Enum::Zerg, "Alpha");
  FillPlayer(1, Races::Enum::Terran, "Bravo");
  if (g->startLocationCount == 0) {
    g->startLocationCount = 2;
    g->startLocations[0].x = 512;  g->startLocations[0].y = 1024;
    g->startLocations[1].x = 3072; g->startLocations[1].y = 3072;
    g->players[0].startLocationX = 512;  g->players[0].startLocationY = 1024;
    g->players[1].startLocationX = 3072; g->players[1].startLocationY = 3072;
  }

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
  if (bDebug)
    fprintf(stderr, "[bridge] pipe name %s\n", name);

  // 4. Open the explicit official path with read sharing only.  Win32 sharing
  // rules then reject existing writers and deny new write/delete opens while
  // this handle remains held.  Hash bytes from that handle before LoadLibrary,
  // and keep it open through the load to prevent pathname replacement in that
  // interval.  The post-load pathname hash below is consistency evidence only,
  // not an in-memory hash or a second race-safe execution check.
  char dllPath[512];
  const DWORD dllPathLength = GetFullPathNameA("pluto.dll", sizeof dllPath, dllPath, NULL);
  if (dllPathLength == 0 || dllPathLength >= sizeof dllPath) {
    fprintf(stderr, "[bridge] cannot resolve official pluto.dll path (%lu)\n", GetLastError());
    return 1;
  }
  HANDLE officialFile = CreateFileA(dllPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (officialFile == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "[bridge] cannot open official pluto.dll with write/delete denied (%lu)\n",
            GetLastError());
    return 1;
  }
  char officialHash[65];
  if (!HashOpenFileSha256(officialFile, officialHash)) {
    fprintf(stderr, "[bridge] cannot hash official pluto.dll handle (%lu)\n", GetLastError());
    CloseHandle(officialFile);
    return 1;
  }
  fprintf(stderr, "[bridge] official file: %s\n[bridge] official handle SHA-256: %s\n",
          dllPath, officialHash);
  if (strcmp(officialHash, pluto_bridge::OfficialPlutoSha256()) != 0) {
    fprintf(stderr, "[bridge] refusing non-official pluto.dll; expected %s\n",
            pluto_bridge::OfficialPlutoSha256());
    CloseHandle(officialFile);
    return 1;
  }
  HMODULE dll = LoadLibraryA(dllPath);
  if (!dll) {
    fprintf(stderr, "[bridge] official pluto.dll LoadLibrary failed (error %lu)\n", GetLastError());
    CloseHandle(officialFile);
    return 1;
  }
  char loadedPath[512];
  char loadedHash[65];
  const DWORD loadedPathLength = GetModuleFileNameA(dll, loadedPath, sizeof loadedPath);
  if (loadedPathLength == 0 || loadedPathLength >= sizeof loadedPath ||
      !pluto_bridge::HashFileSha256(loadedPath, loadedHash) ||
      strcmp(loadedHash, pluto_bridge::OfficialPlutoSha256()) != 0) {
    fprintf(stderr, "[bridge] loaded module path failed official consistency check: %s\n",
            loadedPathLength != 0 && loadedPathLength < sizeof loadedPath ? loadedPath : "(unknown)");
    CloseHandle(officialFile);
    return 1;
  }
  fprintf(stderr,
          "[bridge] LoadLibrary module path: %s\n"
          "[bridge] loaded pathname SHA-256 (consistency only): %s\n",
          loadedPath, loadedHash);
  CloseHandle(officialFile);
  FnNewAIModule newAIModule = reinterpret_cast<FnNewAIModule>(GetProcAddress(dll, "newAIModule"));
  FnGameInit gameInit = reinterpret_cast<FnGameInit>(GetProcAddress(dll, "gameInit"));
  if (!newAIModule || !gameInit) {
    fprintf(stderr, "[bridge] exports missing\n");
    return 1;
  }
  // The pinned official PE declares .bss at RVA 0x72000 with virtual size
  // 0x2678, ending at RVA 0x74678.  Wine maps that tail read-only, although a
  // real game's loader permits its stores.  Protect the complete page-rounded
  // RVA 0x72000..0x75000 extent, then query both pages and every later store:
  // 0x745E0, 0x74608, and the eight bytes at 0x7466C..0x74674.
  {
    unsigned char *image = reinterpret_cast<unsigned char *>(dll);
    const uintptr_t dllBase = reinterpret_cast<uintptr_t>(dll);
    const uintptr_t bssRva = 0x72000;
    const SIZE_T bssVirtualSize = 0x2678;
    const SIZE_T bssProtectSize = 0x3000;
    static_assert(bssRva + bssVirtualSize == 0x74678, "pinned BSS end");
    void *bss = image + bssRva;
    DWORD old;
    if (!VirtualProtect(bss, bssProtectSize, PAGE_READWRITE, &old)) {
      fprintf(stderr, "[bridge] complete bss protect failed (%lu)\n", GetLastError());
      return 1;
    }
    struct BssCheck {
      uintptr_t rva;
      SIZE_T size;
      const char *name;
    };
    static const BssCheck checks[] = {
        {0x72000, 0x1000, "first-page"},
        {0x74000, 0x1000, "last-page"},
        {0x745E0, 4, "store-745e0"},
        {0x74608, 4, "store-74608"},
        {0x7466C, 8, "stores-7466c-74674"},
    };
    for (const BssCheck &check : checks) {
      MEMORY_BASIC_INFORMATION info;
      if (!QueryCommittedImageRange(dllBase + check.rva, check.size, dllBase, false, &info))
        return 1;
      fprintf(stderr, "[bridge] DLL bss %s: RVA %lx+%lx protect=%lx region=%p+%lx\n",
              check.name, check.rva, check.size, info.Protect,
              info.BaseAddress, info.RegionSize);
    }
    fprintf(stderr,
            "[bridge] DLL bss pinned extent RVA 72000..74678 (size 2678); "
            "protected complete RVA 72000..75000\n");
    fprintf(stderr, "[bridge] image bytes at +0x153f: %02x %02x %02x %02x %02x %02x %02x\n",
            image[0x153f], image[0x1540], image[0x1541], image[0x1542],
            image[0x1543], image[0x1544], image[0x1545]);
    fflush(stdout);
  }
  BWAPI::BWAPIClient.data = g;  // connect() would set this; the impls read it directly
  // Client::connect() deletes the existing BroodwarPtr before installing its
  // own GameImpl. Ownership therefore has to start on the heap, not the stack.
  GameImpl *handoff = new GameImpl(g);
  BWAPI::BroodwarPtr = handoff;  // the GameWrapper forwards through this

  // Install PlayerRace4 at the official MSVC BWAPI race slot; see the ABI note
  // above the shim.  This remains a shared PlayerImpl patch because the bot's
  // opponent Player is owned by the handoff's playerVector.
  {
    PlayerImpl probe(0);
    void **vt = *reinterpret_cast<void ***>(&probe);
    DWORD old;
    VirtualProtect(vt, 0x100, PAGE_READWRITE, &old);
    vt[4] = reinterpret_cast<void *>(&PlayerRace4);
    VirtualProtect(vt, 0x100, old, &old);
    fprintf(stderr, "[bridge] PlayerImpl vtable slot 4 -> %p\\n", reinterpret_cast<void *>(&PlayerRace4));
  }

  // Official DLL RVA 0x15D0 calls [Broodwar vtable + 0xE0] with
  // (toAllies, format, va_list): official MSVC vSendTextEx.  The extra Itanium
  // destructor makes that the same dynamic index as this host's vPrintf slot.
  // Resolve it from the actual handoff object and install the matching ret-12
  // chat shim.  There is no separate obs-walk stub and no absolute vtable patch.
  {
    void (GameImpl::*pmf)(const char *, va_list) = &GameImpl::vPrintf;
    const unsigned p = *reinterpret_cast<unsigned *>(&pmf);
    const int slot = static_cast<int>(p - 1) / 4;
    void **vt = *reinterpret_cast<void ***>(handoff);
    if (slot != 0xE0 / 4) {
      fprintf(stderr, "[bridge] unexpected official chat slot: host vPrintf=%d, vtable=%p\n",
              slot, static_cast<void *>(vt));
      return 1;
    }
    DWORD old;
    if (!VirtualProtect(vt, 0x200, PAGE_READWRITE, &old)) {
      fprintf(stderr, "[bridge] GameImpl chat vtable protect failed (%lu)\n", GetLastError());
      return 1;
    }
    vt[slot] = reinterpret_cast<void *>(&GamePrint3);
    VirtualProtect(vt, 0x200, old, &old);
    fprintf(stderr,
            "[bridge] official Game vSendTextEx [vtable %p + 0xE0] -> %p "
            "(resolved from GameImpl host vPrintf slot %d)\n",
            static_cast<void *>(vt), reinterpret_cast<void *>(&GamePrint3), slot);
  }

  // Pluto's onStart thunk reads enemy() through the official MSVC BWAPI Game
  // vtable slot 0x118/4 = 70.  In this Itanium host, self() is slot 70 and
  // enemy() is slot 71 because of the extra destructor entry.  Capture the real
  // enemy slot now; frame zero temporarily remaps the shared vtable while the
  // bot reads its opponent, then restores host self() before onFrame.
  {
    auto slotOf = [](auto pmf) -> int {
      return static_cast<int>((*reinterpret_cast<unsigned *>(&pmf) - 1) / 4);
    };
    const int selfSlot = slotOf(static_cast<Player (GameImpl::*)() const>(&GameImpl::self));
    s_hostEnemyVslot = slotOf(static_cast<Player (GameImpl::*)() const>(&GameImpl::enemy));
    if (selfSlot != 70 || s_hostEnemyVslot != 71) {
      fprintf(stderr, "[bridge] unexpected GameImpl vtable: self=%d enemy=%d\n",
              selfSlot, s_hostEnemyVslot);
      return 1;
    }
    fprintf(stderr, "[bridge] GameImpl official enemy slot 70 <- host enemy slot %d\n",
            s_hostEnemyVslot);
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
  fprintf(stderr, "[bridge] pluto.dll base %p, newAIModule %p, gameInit %p\n",
          static_cast<void *>(dll), reinterpret_cast<void *>(newAIModule),
          reinterpret_cast<void *>(gameInit));
  fflush(stdout);
  // BWAPI 4.4 initializes the module's Game pointer before constructing it.
  gameInit(handoff);
  s_testGameInit = gameInit;
  fprintf(stderr, "[bridge] gameInit handed over\n");
  AIModule *mod = newAIModule();
  fprintf(stderr, "[bridge] module %p\n", static_cast<void *>(mod));
  fflush(stdout);
  // MatchStart is driven after the first real frame has replaced the rig's
  // scripted units, not before it.

  // 5. The bot's thread now connects; grant it, then serve its updates.
  fprintf(stderr, "[bridge] waiting for the bot to connect\n");
  fflush(stdout);
  // Direct official-DLL replay can finish onFrame without opening BWAPI's pipe,
  // so connection is optional and cannot gate host progress by itself.  Once a
  // real client connects, ServePipe and PublicationState apply backpressure
  // until each delivered generation has been consumed and acknowledged.
  InitializeCriticalSection(&g_stateLock);
  InitializeConditionVariable(&g_stateChanged);
  s_pipeThread = CreateThread(NULL, 0, ServePipe, hPipe, 0, NULL);
  if (s_pipeThread == NULL) {
    fprintf(stderr, "[bridge] pipe thread creation failed (%lu)\n", GetLastError());
    return 1;
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
  memcpy(s_bigview, reinterpret_cast<unsigned char *>(dll) + 0x54000, 0x20000);
  memset(reinterpret_cast<char *>(s_bigview) + 0x20000, 0, 0xE0000);
  {
    unsigned *client = reinterpret_cast<unsigned *>(reinterpret_cast<unsigned char *>(dll) + 0x54160);
    fprintf(stderr, "[bridge] client: view-slot %08x flag %02x -> setting view=%p+flag\n",
           client[6], *reinterpret_cast<unsigned char *>(client + 7), s_bigview);
    fflush(stdout);
    client[6] = reinterpret_cast<unsigned>(s_bigview);
    *reinterpret_cast<unsigned char *>(client + 7) = 1;
  }
  int nFramesDone = 0;
  unsigned expectedFrame = 0;
  unsigned endWinner = 0;
  bool hasMatchResult = false;
  bool runtimeFailed = false;
  for (;;) {
    {
      StateGuard pipeStatusGuard;
      if (s_pipeUnexpectedFailure)
        runtimeFailed = true;
    }
    if (runtimeFailed)
      break;
    DWORD t0 = GetTickCount();
    unsigned hdr[3];
    const ReadStatus headerStatus = ReadN(hdr, sizeof hdr);
    if (headerStatus != ReadStatus::kOk) {
      if (headerStatus == ReadStatus::kCleanEof) {
        fprintf(stderr, "[bridge] stdin closed after %d frames\n", nFramesDone);
      } else {
        fprintf(stderr, "[bridge] truncated frame/HXPM header after %d frames\n", nFramesDone);
        runtimeFailed = true;
      }
      break;
    }
    if (hdr[0] == pluto_hxpm::kMagic) {
      if (nFramesDone != 0 ||
          s_hxpmStream.state() != pluto_hxpm::StreamMetadataState::kWaiting) {
        fprintf(stderr, "[bridge] duplicate or late HXPM metadata rejected\n");
        runtimeFailed = true;
        break;
      }
      unsigned char fixed[pluto_hxpm::kHeaderBytes];
      memcpy(fixed, hdr, sizeof hdr);
      if (!ReadRequired(fixed + sizeof hdr, sizeof fixed - sizeof hdr,
                        "HXPM fixed header")) {
        runtimeFailed = true;
        break;
      }
      const size_t payload = pluto_hxpm::Read32(fixed + 28);
      if (pluto_hxpm::Read16(fixed + 4) != pluto_hxpm::kVersion ||
          pluto_hxpm::Read16(fixed + 6) != pluto_hxpm::kHeaderBytes ||
          payload > pluto_hxpm::kMaxPayloadBytes) {
        fprintf(stderr, "[bridge] unsupported or oversized HXPM header rejected\n");
        runtimeFailed = true;
        break;
      }
      std::vector<unsigned char> bytes(pluto_hxpm::kHeaderBytes + payload);
      memcpy(bytes.data(), fixed, sizeof fixed);
      if (!ReadRequired(bytes.data() + sizeof fixed, payload, "HXPM payload")) {
        runtimeFailed = true;
        break;
      }
      pluto_hxpm::Metadata metadata;
      std::string error;
      const pluto_hxpm::ParseResult parsed = pluto_hxpm::Parse(bytes, metadata, &error);
      if (parsed != pluto_hxpm::ParseResult::kOk) {
        fprintf(stderr, "[bridge] invalid HXPM rejected: %s\n", error.c_str());
        runtimeFailed = true;
        break;
      }
      bool metadataCommitted = false;
      {
        // HXPM mutates the same GameData and SC pathing pointers as a frame.
        // Keep its commit in the publication critical section as well, even
        // though normal clients are gated until frame zero has been published.
        StateGuard metadataGuard;
        metadataCommitted = ApplyStaticMapMetadata(metadata) &&
            s_hxpmStream.AcceptMetadata(metadata.map_width, metadata.map_height);
      }
      if (!metadataCommitted) {
        fprintf(stderr, "[bridge] HXPM allocation/state commit failed\n");
        runtimeFailed = true;
        break;
      }
      fprintf(stderr, "[bridge] HXPM complete: %ux%u, %zu regions, %zu splits\n",
              metadata.map_width, metadata.map_height, metadata.regions.size(),
              metadata.split_tiles.size());
      continue;
    }
    if (hdr[0] == 0x45505848) {  // 'HXPE': the actual match result
      endWinner = hdr[1] != 0;
      hasMatchResult = true;
      break;
    }
    if (hdr[0] != pluto_late::kHxpf && hdr[0] != pluto_late::kHxp2 &&
        hdr[0] != pluto_late::kHxp3 && hdr[0] != pluto_late::kHxp4) {
      fprintf(stderr, "[bridge] bad magic %08x after %d frames\n", hdr[0], nFramesDone);
      runtimeFailed = true;
      break;
    }
    const unsigned frame = hdr[1];
    const unsigned nUnits = hdr[2];
    unsigned mapWH[2];
    if (!ReadRequired(mapWH, sizeof mapWH, "frame map dimensions")) {
      runtimeFailed = true;
      break;
    }
    unsigned nPlayers = 2;
    if (!ReadRequired(&nPlayers, sizeof nPlayers, "frame player count")) {
      runtimeFailed = true;
      break;
    }
    if (hdr[0] == pluto_late::kHxp4) {
      unsigned sizeWords[2];
      if (!ReadRequired(sizeWords, sizeof sizeWords, "HXP4 section sizes")) {
        runtimeFailed = true;
        break;
      }
      if (sizeWords[0] != pluto_late::kHxp4ExtraBytes ||
          sizeWords[1] != pluto_late::kPlayerTechBytes) {
        fprintf(stderr, "[bridge] HXP4 section sizes %u/%u do not equal 78/119\n",
                sizeWords[0], sizeWords[1]);
        runtimeFailed = true;
        break;
      }
    }
    if (nUnits > 9999 || nPlayers > 12) {
      fprintf(stderr, "[bridge] invalid declared counts: units=%u players=%u\n",
              nUnits, nPlayers);
      runtimeFailed = true;
      break;
    }
    if (mapWH[0] == 0 || mapWH[1] == 0 || mapWH[0] > 256 || mapWH[1] > 256) {
      // GameData's tile arrays are 256x256 (walkability is 1024x1024).
      // Never accept a frame whose dimensions cannot be published there.
      fprintf(stderr, "[bridge] unsupported map dimensions %ux%u (maximum 256x256)\n",
              mapWH[0], mapWH[1]);
      runtimeFailed = true;
      break;
    }
    if (frame != expectedFrame) {
      // In particular, reject a nonzero first frame before onStart can touch
      // the handoff, and reject a repeated zero after the original was used.
      fprintf(stderr, "[bridge] frame sequence error: expected %u, got %u\n",
              expectedFrame, frame);
      runtimeFailed = true;
      break;
    }
    const bool firstFrame = !s_hxpmStream.saw_frame();
    if (!s_hxpmStream.AcceptFrame(static_cast<uint16_t>(mapWH[0]),
                                  static_cast<uint16_t>(mapWH[1]))) {
      fprintf(stderr, "[bridge] frame map does not match HXPM/earlier frames; stale metadata rejected\n");
      runtimeFailed = true;
      break;
    }
    if (firstFrame &&
        s_hxpmStream.state() == pluto_hxpm::StreamMetadataState::kLegacyIncomplete) {
      ClearStaticMapMetadata();
      fprintf(stderr, "[bridge] HXPM absent: legacy region metadata is INCOMPLETE\n");
    }
    static HxUnit units[9999];
    static HxUnitExtra extras[9999];
    static pluto_late::UnitExtra extras4[9999];
    static pluto_late::PlayerTech playerTech[12];
    static HxPlayer players[12];
    if (!ReadRequired(units, nUnits * sizeof(HxUnit), "frame unit array")) {
      runtimeFailed = true;
      break;
    }
    const HxUnitExtra *unitExtras = NULL;
    const pluto_late::UnitExtra *unitExtras4 = NULL;
    const pluto_late::PlayerTech *unitPlayerTech = NULL;
    if (hdr[0] == pluto_late::kHxp3) {
      if (!ReadRequired(extras, nUnits * sizeof(HxUnitExtra), "HXP3 unit extras")) {
        runtimeFailed = true;
        break;
      }
      unitExtras = extras;
    } else if (hdr[0] == pluto_late::kHxp4) {
      if (!ReadRequired(extras4, nUnits * sizeof(pluto_late::UnitExtra), "HXP4 unit extras")) {
        runtimeFailed = true;
        break;
      }
      unitExtras4 = extras4;
    }
    if (!ReadRequired(players, nPlayers * sizeof(HxPlayer), "frame player array")) {
      runtimeFailed = true;
      break;
    }
    if (hdr[0] == pluto_late::kHxp4) {
      if (!ReadRequired(playerTech, nPlayers * sizeof(pluto_late::PlayerTech),
                        "HXP4 player-tech records")) {
        runtimeFailed = true;
        break;
      }
      unitPlayerTech = playerTech;
      for (unsigned i = 0; i < nUnits; ++i) {
        pluto_late::Unit validationUnit;
        validationUnit.order = units[i].order;
        std::string error;
        if (!pluto_late::ValidateHxp4Unit(validationUnit, extras4[i], &error)) {
          fprintf(stderr, "[bridge] HXP4 unit %u rejected: %s\n", i, error.c_str());
          runtimeFailed = true;
          break;
        }
      }
      if (runtimeFailed)
        break;
    }
    static std::vector<unsigned char> seen, explored, rawTiles;
    const unsigned nPlaneBytes = (mapWH[0] * mapWH[1] + 7) / 8;
    const unsigned char *raw = NULL;
    if (hdr[0] == pluto_late::kHxp2 || hdr[0] == pluto_late::kHxp3 ||
        hdr[0] == pluto_late::kHxp4) {
      rawTiles.resize(mapWH[0] * mapWH[1] * 4);
      if (!ReadRequired(rawTiles.data(), rawTiles.size(), "frame tile array")) {
        runtimeFailed = true;
        break;
      }
      raw = rawTiles.data();
    } else {
      seen.resize(nPlaneBytes);
      explored.resize(nPlaneBytes);
      if (!ReadRequired(seen.data(), nPlaneBytes, "legacy visibility plane") ||
          !ReadRequired(explored.data(), nPlaneBytes, "legacy explored plane")) {
        runtimeFailed = true;
        break;
      }
    }
    DWORD t1 = GetTickCount();
    StateGuard stateGuard;
    if (bDebug && !s_publication.CanWriteNext())
      fprintf(stderr, "[bridge] f%u waiting for client acknowledgement\n", frame);
    while (!s_shutdown && !s_pipeUnexpectedFailure && !s_pipeGracefulDisconnect &&
           !s_publication.CanWriteNext())
      SleepConditionVariableCS(&g_stateChanged, &g_stateLock, INFINITE);
    if (s_shutdown || s_pipeUnexpectedFailure) {
      runtimeFailed = true;
      break;
    }
    if (s_pipeGracefulDisconnect) {
      // Reaching another complete input frame proves that the client's earlier
      // close was not the normal post-final-generation disconnect.
      fprintf(stderr,
              "[bridge] early client disconnect before frame %u "
              "(generation %llu, error %lu)\n",
              frame, s_pipeDisconnectGeneration, s_pipeDisconnectError);
      runtimeFailed = true;
      break;
    }
    if (!ApplyImageState(units, unitExtras, unitExtras4, unitPlayerTech,
                         nUnits, players, nPlayers, frame,
                         mapWH[0], mapWH[1], raw, seen.data(), explored.data(), nPlaneBytes)) {
      runtimeFailed = true;
      break;
    }
    int f = static_cast<int>(frame);
    g->frameCount = f;
    g->elapsedTime = f / 24;
  g->latency = Latency::Enum::LanLow;
  g->latencyFrames = 4;
  g->latencyTime = 167;
  g->remainingLatencyFrames = 4;
  g->remainingLatencyTime = 167;
  g->hasLatCom = true;
    static int packedOfId[1700];
    static lifecycle::Tracker lifecycleTracker;
    static lifecycle::Event lifecycleEvents[GameData::MAX_EVENTS - 1];
    static std::vector<lifecycle::UnitInput> lifecycleInputs;
    lifecycleInputs.clear();
    lifecycleInputs.reserve(nUnits);
    if (f == 0) {
      lifecycleTracker.Reset();
      for (int &index : packedOfId)
        index = -1;
    }
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
      const HxUnit &src = units[i];
      if (src.id >= 1700)
        continue;
      const pluto_bridge::UnitVisibility visibility =
          UnitVisibilityForFrame(src, unitExtras, unitExtras4, i);
      lifecycle::UnitInput input;
      input.slot = src.id;
      input.present = true;
      input.generation = src.pad;
      input.visible = visibility.visible;
      input.detected = visibility.detected;
      input.completed = src.completed != 0;
      input.type = src.type;
      input.player = src.player;
      lifecycleInputs.push_back(input);
    }
    const lifecycle::UpdateReport lifecycleReport = lifecycleTracker.Update(
        lifecycleInputs.data(), lifecycleInputs.size(), f == 0,
        lifecycleEvents, GameData::MAX_EVENTS - 1);
    if (f == 0) {
      // BWAPI's initial GameData must describe this match, not the rig's eight
      // scripted units. The obs-state converter updates its own state from the
      // SC image, but onStart also reads GameData directly.
      memset(g->units, 0, sizeof g->units);
      memset(g->unitArray, 0xFF, sizeof g->unitArray);
      g->initialUnitCount = 0;
    }
    // Initialize each logical identity once.  A replacement generation receives
    // a fresh packed id and UnitData record; it never inherits the old record.
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
      const HxUnit &src = units[i];
      if (src.id >= 1700)
        continue;
      const pluto_bridge::UnitVisibility visibility =
          UnitVisibilityForFrame(src, unitExtras, unitExtras4, i);
      const int index = lifecycleTracker.PackedId(src.id);
      if (index < 0 || index >= 10000)
        continue;
      const int oldIndex = packedOfId[src.id];
      packedOfId[src.id] = index;
      g->unitArray[src.id] = (visibility.visible || (f == 0 && src.player == g->neutral)) ? index : -1;
      if (oldIndex >= 0 && oldIndex < 10000) {
        UnitData &old = g->units[oldIndex];
        old.exists = false;
        old.isDetected = false;
        for (int k = 0; k < 9; ++k)
          old.isVisible[k] = false;
      }
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
      u.exists = visibility.visible || (f == 0 && src.player == g->neutral);
      u.isDetected = (f == 0 && src.player == g->neutral) || visibility.detected;
      for (int k = 0; k < 9; ++k)
        u.isVisible[k] = (k == 0 && visibility.visible);
      u.order = static_cast<int>(src.order);
      if (unitExtras4 != NULL) {
        const pluto_late::UnitExtra &e = unitExtras4[i];
        const pluto_late::ProductionProjection production =
            pluto_late::ProjectHxp4Production(src.order, e);
        u.buildType = static_cast<int>(e.buildType);
        u.trainingQueueCount = production.trainingQueueCount;
        for (int q = 0; q < 5; ++q)
          u.trainingQueue[q] = production.trainingQueue[q];
        u.tech = production.tech;
        u.upgrade = production.upgrade;
        u.remainingResearchTime = production.remainingResearchTime;
        u.remainingUpgradeTime = production.remainingUpgradeTime;
        u.remainingBuildTime = static_cast<int>(e.remainingBuildTime);
        u.groundWeaponCooldown = e.groundCooldown;
        u.airWeaponCooldown = e.airCooldown;
        u.spellCooldown = e.spellCooldown;
      } else if (unitExtras != NULL) {
        const HxUnitExtra &e = unitExtras[i];
        u.buildType = static_cast<int>(e.buildType);
        u.trainingQueueCount = e.buildType != UnitTypes::Enum::None ? 1 : 0;
        u.trainingQueue[0] = static_cast<int>(e.buildType);
        u.remainingBuildTime = static_cast<int>(e.remainingBuildTime);
        u.groundWeaponCooldown = e.groundCooldown;
        u.airWeaponCooldown = e.airCooldown;
        u.spellCooldown = e.spellCooldown;
      }
      FilterUnitData(src, visibility, u, f == 0);
      if (f == 0 && index >= g->initialUnitCount)
        g->initialUnitCount = index + 1;
    }
    bool accessibleNow[1700] = {};
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
      const HxUnit &src = units[i];
      if (src.id < 1700 &&
          (UnitVisibilityForFrame(src, unitExtras, unitExtras4, i).visible ||
           (f == 0 && src.player == g->neutral)))
        accessibleNow[src.id] = true;
    }
    for (int slot = 0; slot < 1700; ++slot) {
      if (accessibleNow[slot])
        continue;
      g->unitArray[slot] = -1;
      const int old = packedOfId[slot];
      if (old >= 0 && old < 10000) {
        UnitData &u = g->units[old];
        u.exists = false;
        u.isDetected = false;
        for (int k = 0; k < 9; ++k)
          u.isVisible[k] = false;
      }
    }
    // The client performs onMatchStart/onMatchFrame while consuming this
    // published frame; no raw handoff is dereferenced before that handshake.
    // Resolve links only after all current packed ids are known. Targets and
    // build units often appear later in discovery order than their owners.
    if (unitExtras != NULL || unitExtras4 != NULL) {
      for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
        const HxUnit &src = units[i];
        if (src.id >= 1700)
          continue;
        HxUnitExtra common;
        const HxUnitExtra *e = unitExtras != NULL ? &unitExtras[i] : &common;
        if (unitExtras4 != NULL) {
          static_assert(sizeof common == offsetof(pluto_late::UnitExtra, buildQueue),
                        "HXP4 common extra prefix size");
          memcpy(&common, &unitExtras4[i], sizeof common);
        }
        const pluto_bridge::UnitVisibility visibility = UnitVisibilityFor(src, e);
        const int packed = packedOfId[src.id];
        if (packed < 0 || packed >= 10000)
          continue;
        UnitData &u = g->units[packed];
        if (visibility.visible && src.player == 0)
          u.buildUnit = e->currentBuildUnitId < 1700 ? packedOfId[e->currentBuildUnitId] : -1;
        if (visibility.detected)
          u.orderTarget = e->orderTargetId < 1700 ? packedOfId[e->orderTargetId] : -1;
      }
    }
    // Resolve larva links after every packed id is known; a larva may precede
    // its hatchery in the used list, and new larvae appear throughout a match.
    for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
      const HxUnit &src = units[i];
      if (src.type != UnitTypes::Enum::Zerg_Larva || src.id >= 1700 ||
          !UnitVisibilityForFrame(src, unitExtras, unitExtras4, i).visible)
        continue;
      int hatchery = -1;
      if (unitExtras4 != NULL && unitExtras4[i].connectedId < 1700)
        hatchery = static_cast<int>(unitExtras4[i].connectedId);
      else if (unitExtras != NULL && unitExtras[i].connectedId < 1700)
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
      if (hatchery >= 0 && packedOfId[src.id] >= 0 && packedOfId[hatchery] >= 0)
        g->units[packedOfId[src.id]].hatchery = packedOfId[hatchery];
    }
    g->eventCount = 1;
    g->events[0].type = f == 0 ? EventType::Enum::MatchStart : EventType::Enum::MatchFrame;
    g->events[0].v1 = f;
    g->events[0].v2 = 0;
    // GameImpl fills each PlayerImpl's unit set from these events. Without
    // them the observation has units but self()->getUnits() stays empty.  The
    // standalone tracker supplies BWAPI's complete lifecycle, not just the old
    // Discover/Evade approximation, with permanent slot+generation identities.
    for (std::size_t i = 0; i < lifecycleReport.emitted; ++i) {
      const lifecycle::Event &event = lifecycleEvents[i];
      BWAPIC::Event &out = g->events[g->eventCount++];
      out.type = static_cast<EventType::Enum>(static_cast<int>(event.type));
      out.v1 = event.unit_id;
      out.v2 = 0;
    }
    if (lifecycleReport.dropped != 0 || lifecycleReport.invalid_inputs != 0 ||
        lifecycleReport.packed_id_overflow != 0)
      fprintf(stderr,
              "[bridge] lifecycle f%d: dropped=%zu invalid=%zu packed-overflow=%zu\n",
              f, lifecycleReport.dropped, lifecycleReport.invalid_inputs,
              lifecycleReport.packed_id_overflow);
    // Publication is complete only after the SC image, dynamic UnitData, and
    // the entire event array are all in place.  The pipe thread may now release
    // exactly this generation to a waiting Client::update.
    if (getenv("HX_TEST_LATE_STATE_DUMP") != NULL &&
        !DumpLateStateEvidence(getenv("HX_TEST_LATE_STATE_DUMP"))) {
      fprintf(stderr, "[bridge] late-state evidence write failed\n");
      runtimeFailed = true;
      break;
    }
    s_publication.Publish();
    WakeAllConditionVariable(&g_stateChanged);
    stateGuard.Release();
    if (f == 0) {
      // Pluto reads its opponent from the initial GameImpl during onStart,
      // before its client thread replaces that object. Populate the handoff's
      // players and enemies from the first real frame, as BWAPI's rig does;
      // otherwise opponent lookup fails even though PlayerData is populated.
      handoff->onMatchStart();
      void **gameVtable = *reinterpret_cast<void ***>(handoff);
      void *const originalSelfSlot = gameVtable[70];
      DWORD oldVtableProtection = 0;
      if (!VirtualProtect(gameVtable, 0x200, PAGE_READWRITE, &oldVtableProtection)) {
        fprintf(stderr, "[bridge] cannot install official enemy vtable slot (%lu)\n",
                GetLastError());
        runtimeFailed = true;
        break;
      }
      // The official enemy mapping lives in the SHARED vtable, so a synchronous
      // or asynchronous Client::connect sees it in its replacement GameImpl too.
      gameVtable[70] = gameVtable[s_hostEnemyVslot];
      s_testHandoffIdentity = handoff;
      handoff = NULL;  // last possible host reference before deletion is enabled
      {
        StateGuard replacementGuard;
        s_clientReplacementEnabled = true;
        WakeAllConditionVariable(&g_stateChanged);
      }
      if (testSyncClient && !TestConnectReplacement())
        runtimeFailed = true;
      if (testAsyncClient) {
        s_testClientReady = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (s_testClientReady == NULL) {
          runtimeFailed = true;
        } else {
          s_testClientThread = CreateThread(NULL, 0, TestClientUpdates, NULL, 0, NULL);
          if (s_testClientThread == NULL ||
              WaitForSingleObject(s_testClientReady, 5000) != WAIT_OBJECT_0 ||
              InterlockedCompareExchange(&s_testClientError, 0, 0) != 0)
            runtimeFailed = true;
        }
      }
      if (!runtimeFailed && (testSyncClient || testAsyncClient))
        TestPrepareReplacement(gameVtable, originalSelfSlot);
      // ServePipe is now allowed to release a synchronous Client::connect from
      // inside this call.  No handoff dereference occurs after this point.
      if (!runtimeFailed)
        onStart(mod);
      // Restore through the shared vtable address, which is independent of the
      // handoff object's lifetime.  Publication updates stay gated until the
      // restoration completes, so replacement GameImpl self() sees host slot 70.
      gameVtable[70] = originalSelfSlot;
      VirtualProtect(gameVtable, 0x200, oldVtableProtection, &oldVtableProtection);
      {
        StateGuard startCompleteGuard;
        s_onStartComplete = true;
        WakeAllConditionVariable(&g_stateChanged);
      }
      if ((testSyncClient || testAsyncClient) &&
          InterlockedCompareExchange(&s_testClientError, 0, 0) != 0)
        runtimeFailed = true;
      if (runtimeFailed)
        break;
      if (testSyncClient) {
        s_testClientThread = CreateThread(NULL, 0, TestClientUpdates, NULL, 0, NULL);
        if (s_testClientThread == NULL) {
          runtimeFailed = true;
          break;
        }
      }
    }
    // The bot's client processes MatchFrame events through its own update.
    // Calling onMatchFrame here as well duplicates discovery events and can
    // dereference the GameImpl that its connect() has already replaced.
    onFrame(mod);
    {
      StateGuard pipeStatusGuard;
      if (s_pipeUnexpectedFailure)
        runtimeFailed = true;
    }
    if (runtimeFailed)
      break;
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
    if (!FlushReply(f)) {
      runtimeFailed = true;
      break;
    }
    ++expectedFrame;
    if (bDebug && f % 100 == 0)
      fprintf(stderr, "[bridge] frame %d\n", f);
  }
  // Let the ownership-regression updates finish before closing the pipe on a
  // clean run.  On failure, quiescence comes first so a waiting update is woken.
  bool testThreadJoined = false;
  if (s_testClientThread != NULL && !runtimeFailed) {
    testThreadJoined = WaitForSingleObject(s_testClientThread, 5000) == WAIT_OBJECT_0;
    if (!testThreadJoined)
      runtimeFailed = true;
  }
  // No final reply or onEnd may race an unsettled in-session pipe failure.
  // FinalizePipe releases at most the already-accepted current publication,
  // cancels and joins everything else, and inspects the committed final status.
  FinalizePipe(runtimeFailed);
  if (s_testClientThread != NULL) {
    if (!testThreadJoined)
      testThreadJoined = WaitForSingleObject(s_testClientThread, 2000) == WAIT_OBJECT_0;
    if (!testThreadJoined ||
        InterlockedCompareExchange(&s_testClientError, 0, 0) != 0)
      runtimeFailed = true;
  }
  // A clean EOF (or HXPE) retains the historical final late-command flush.
  if (!runtimeFailed && !FlushReply(static_cast<unsigned>(nFramesDone)))
    runtimeFailed = true;
  if (!runtimeFailed && hasMatchResult) {
    if (bDebug)
      fprintf(stderr, "[bridge] onEnd winner=%u after pipe quiescence\n", endWinner);
    onEnd(mod, endWinner != 0);
  }
  if ((testSyncClient || testAsyncClient) && testThreadJoined &&
      BWAPIClient.isConnected()) {
    BWAPIClient.disconnect();  // bot/onEnd is finished with the replacement too
    fprintf(stderr, "[bridge] test Client disconnected after quiescence\n");
  }
  if (s_testClientThread != NULL) {
    CloseHandle(s_testClientThread);
    s_testClientThread = NULL;
  }
  if (s_testClientReady != NULL) {
    CloseHandle(s_testClientReady);
    s_testClientReady = NULL;
  }
  fprintf(stderr, "[bridge] done (%s)\n", runtimeFailed ? "failed" : "clean EOF");
  // The client thread may outlive its pipe. Exit at the process boundary rather
  // than running CRT teardown with that thread still touching shared state.
  ExitProcess(runtimeFailed ? 1 : 0);
}
