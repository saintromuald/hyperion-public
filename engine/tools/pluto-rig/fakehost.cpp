// fakehost.cpp -- run pluto.dll (a BWAPI 4.4.0 AI module) against a scripted game state.
//
// Pluto is binary-only and self-contained (it imports no BWAPI.dll). It links the BWAPI
// client code into itself, so it expects the same environment a real game provides:
//   - "Local\\bwapi_shared_memory_game_list"  a GameTable naming this process
//   - "Local\\bwapi_shared_memory_<pid>"      the GameData block (this file fills it)
//   - "\\\\.\\pipe\\bwapi_pipe_<pid>"          a pipe: the client writes 1 each update,
//                                              this host advances the frame and writes 2
// The bot then launches pluto/pluto_infer.exe itself and speaks to it over BWRL_SHM, which
// this host dumps every frame for reverse engineering the observation schema.
//
// Build with llvm-mingw i686 (build.sh); run under Wine from the rig folder, which holds
// pluto.dll beside this exe and pluto/ (the engine and weights).

#include <BWAPI/AIModule.h>
#include <BWAPI/Client/GameData.h>
#include <BWAPI/Client/Client.h>
#include <BWAPI/Client/GameImpl.h>
#include <BWAPI/Client/PlayerData.h>
#include <BWAPI/Client/UnitData.h>
#include <BWAPI/Client/Event.h>
#include <BWAPI/Client/GameTable.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

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
  u.order = Orders::Enum::PlayerGuard;
  g->unitArray[n] = n;
  ++g->initialUnitCount;
  ++g->players[player].allUnitCount[t];
  ++g->players[player].completedUnitCount[t];
}

typedef AIModule *(WINAPI *FnNewAIModule)();
typedef void(WINAPI *FnGameInit)(Game *);

static DWORD WINAPI ServePipe(LPVOID p) {
  HANDLE hPipe = static_cast<HANDLE>(p);
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

static void DumpShm(int frame) {
  if (!s_hShm) {
    s_hShm = OpenFileMappingA(FILE_MAP_READ, FALSE, "BWRL_SHM");
    if (!s_hShm)
      return;
    s_view = MapViewOfFile(s_hShm, FILE_MAP_READ, 0, 0, 0);
    if (!s_view)
      return;
  }
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(s_view, &mbi, sizeof mbi) == 0)
    return;
  char path[64];
  snprintf(path, sizeof path, "dump/f%05d.bin", frame);
  FILE *f = fopen(path, "wb");
  if (f) {
    fwrite(s_view, 1, static_cast<size_t>(mbi.RegionSize), f);
    fclose(f);
  }
}

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
  printf("[fakehost] SC stand-in: low %p, data %p\n", low, sc);
  fflush(stdout);
}

int main(int argc, char **argv) {
  const int scenario = argc > 1 ? atoi(argv[1]) : 1;
  ReserveScMemory();
  printf("[fakehost] scenario %d\n", scenario);
  fflush(stdout);

  // 1. The shared GameData block.
  const DWORD pid = GetCurrentProcessId();
  char name[128];
  snprintf(name, sizeof name, "Local\\bwapi_shared_memory_%lu", pid);
  HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                   sizeof(GameData), name);
  if (!hMap) {
    printf("[fakehost] GameData mapping failed\n");
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
  printf("[fakehost] %d units\n", g->initialUnitCount);
  fflush(stdout);

  // 2. The game list the client's connect() opens.
  HANDLE hList = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                    sizeof(GameTable), "Local\\bwapi_shared_memory_game_list");
  if (!hList) {
    printf("[fakehost] game list mapping failed\n");
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
    printf("[fakehost] pipe failed (%lu)\n", GetLastError());
    return 1;
  }

  // 4. Load the bot and hand it the game, as the real framework does at injection.
  HMODULE dll = LoadLibraryA("pluto.patched.dll");
  if (!dll) {
    printf("[fakehost] no pluto.dll (error %lu)\n", GetLastError());
    return 1;
  }
  FnNewAIModule newAIModule = reinterpret_cast<FnNewAIModule>(GetProcAddress(dll, "newAIModule"));
  FnGameInit gameInit = reinterpret_cast<FnGameInit>(GetProcAddress(dll, "gameInit"));
  if (!newAIModule || !gameInit) {
    printf("[fakehost] exports missing\n");
    return 1;
  }
  {
    unsigned char *img = reinterpret_cast<unsigned char *>(GetModuleHandleA("pluto.patched.dll"));
    printf("[fakehost] image bytes at +0x153f: %02x %02x %02x %02x %02x %02x %02x\n",
           img[0x153f], img[0x1540], img[0x1541], img[0x1542], img[0x1543], img[0x1544], img[0x1545]);
    char path[512];
    GetModuleFileNameA(GetModuleHandleA("pluto.patched.dll"), path, sizeof path);
    printf("[fakehost] loaded: %s\n", path);
    fflush(stdout);
  }
  BWAPI::BWAPIClient.data = g;  // connect() would set this; the impls read it directly
  GameImpl handoff(g);  // what the bot's own connect() later replaces with its own GameImpl
  BWAPI::BroodwarPtr = &handoff;  // the GameWrapper forwards through this
  {
    HMODULE hDll = GetModuleHandleA("pluto.patched.dll");
    printf("[fakehost] pluto.dll base %p, newAIModule %p, gameInit %p\n",
           static_cast<void *>(hDll), reinterpret_cast<void *>(newAIModule),
           reinterpret_cast<void *>(gameInit));
    fflush(stdout);
  }
  AIModule *mod = newAIModule();
  printf("[fakehost] module %p\n", static_cast<void *>(mod));
  gameInit(&handoff);
  printf("[fakehost] gameInit handed over\n");
  fflush(stdout);
  // The bridge runs these when its update() sees the events; the rig drives them itself.
  handoff.onMatchStart();

  // 5. The bot's thread now connects; grant it, then serve its updates.
  printf("[fakehost] waiting for the bot to connect\n");
  fflush(stdout);
  HANDLE hConnect = CreateThread(NULL, 0, ServePipe, hPipe, 0, NULL);
  (void)hConnect;

  // 6. Drive the module through its own vtable, as the bridge does: its layout is not
  // BWAPI 4.4.0's AIModule (no virtual destructor; slots read from the disassembly):
  //   [1] onStart(), [2] onEnd(bool), [3] onFrame().
  typedef void(__thiscall *V0)(void *self);
  typedef void(__thiscall *V1)(void *self, unsigned char b);
  void **vt = *reinterpret_cast<void ***>(mod);
  V0 onStart = reinterpret_cast<V0>(vt[1]);
  V1 onEnd = reinterpret_cast<V1>(vt[2]);
  V0 onFrame = reinterpret_cast<V0>(vt[3]);
  printf("[fakehost] driving the module (vt[1]=%p vt[2]=%p vt[3]=%p)\n", vt[1], vt[2], vt[3]);
  fflush(stdout);
  for (int f = 0; f < 1200; ++f) {
    g->frameCount = f;
    g->elapsedTime = f * 42;
    g->eventCount = 1;
    g->events[0].type = f == 0 ? EventType::Enum::MatchStart : EventType::Enum::MatchFrame;
    g->events[0].v1 = f;
    g->events[0].v2 = 0;
    if (f == 0)
      onStart(mod);
    handoff.onMatchFrame();
    onFrame(mod);
    if (scenario == 2 && f >= 600) {
      UnitData &m = g->units[7];
      m.positionY -= 1;
      m.isMoving = true;
      m.order = Orders::Enum::Move;
      m.orderTargetPositionX = m.positionX;
      m.orderTargetPositionY = m.positionY - 200;
    }
    DumpShm(f);
    if (f % 100 == 0)
      printf("[fakehost] frame %d\n", f);
    Sleep(16);
  }
  onEnd(mod, 1);
  printf("[fakehost] done\n");
  return 0;
}
