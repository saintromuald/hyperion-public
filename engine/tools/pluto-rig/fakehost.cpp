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

// The bot's Player vtable slot 4 is a race getter that writes the race id to *out
// (when non-null) and returns it in eax (Races: Zerg 0, Terran 1, Protoss 2, Random 6,
// Unknown 8). BWAPI 4.4.0 puts getType at that slot, so the bot's interface is a fork.
static int __attribute__((thiscall)) PlayerRace4(void *self, int *out) {
  const int race = static_cast<PlayerImpl *>(self)->getRace().getID();
  if (out)
    *out = race;
  { static int s_n; if (s_n < 20) { FILE *tf = fopen("c:\\rigtrace.log", "a"); if (tf) { fprintf(tf, "RACE4 self=%p race=%d caller=%p\n", self, race, (void*)__builtin_return_address(0)); fclose(tf); } s_n++; } }
  return race;
}

// The bot's chat announcement calls this slot as (int code, const char *text); BWAPI
// 4.4.0's vPrintf is (const char *format, va_list), so the shim just logs the text.
static void __attribute__((thiscall)) GamePrint2(void *self, int code, const char *text) {
  (void)self;
  (void)code;
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
  { static int s_n; if (s_n < 10) { FILE *tf = fopen("c:\\rigtrace.log", "a"); if (tf) { fprintf(tf, "EMPTYSET sret=%p caller=%p\\n", sret, (void*)__builtin_return_address(0)); fclose(tf); } s_n++; } }
  if (sret) {
    memset(sret, 0, 12);  // an empty {begin,end,cap}: all null
  }
  return sret;
}

// Guard-page taps: a PAGE_GUARD on the page of each interesting entry raises
// STATUS_GUARD_PAGE_VIOLATION on the next execution there; the handler logs the
// context (the this/arg registers), clears the guard and lets the instruction run.
// Re-armed every frame (ArmTaps).
struct Tap {
  void *page;     // page to guard
  void *at;       // exact address whose execution is logged
  const char *name;
  int hits;
};
static Tap g_taps[] = {
  {reinterpret_cast<void *>(0x7b19d000), reinterpret_cast<void *>(0x7b19d01b), "driver-gate", 0},
  {reinterpret_cast<void *>(0x7b1b0000), reinterpret_cast<void *>(0x7b1b0130), "walk", 0},
  {reinterpret_cast<void *>(0x7b1c4000), reinterpret_cast<void *>(0x7b1c4960), "writer", 0},
};

static void ArmTaps() {
  for (Tap &t : g_taps) {
    DWORD old;
    VirtualProtect(t.page, 0x1000, PAGE_EXECUTE_READ | PAGE_GUARD, &old);
  }
}

static void TapLog(const Tap &t, EXCEPTION_POINTERS *e) {
  FILE *f = fopen("c:\\taps.log", "a");
  if (!f)
    return;
  DWORD esp = e->ContextRecord->Esp;
  fprintf(f, "%s #%d eip=%p ecx=%p esi=%p edi=%p ebx=%p eax=%p edx=%p ebp=%p esp=%p ret=%p a1=%p a2=%p\\n",
          t.name, t.hits, e->ExceptionRecord->ExceptionAddress,
          reinterpret_cast<void *>(e->ContextRecord->Ecx),
          reinterpret_cast<void *>(e->ContextRecord->Esi),
          reinterpret_cast<void *>(e->ContextRecord->Edi),
          reinterpret_cast<void *>(e->ContextRecord->Ebx),
          reinterpret_cast<void *>(e->ContextRecord->Eax),
          reinterpret_cast<void *>(e->ContextRecord->Edx),
          reinterpret_cast<void *>(e->ContextRecord->Ebp),
          reinterpret_cast<void *>(esp),
          reinterpret_cast<void *>(*reinterpret_cast<DWORD *>(esp)),
          reinterpret_cast<void *>(*reinterpret_cast<DWORD *>(esp + 4)),
          reinterpret_cast<void *>(*reinterpret_cast<DWORD *>(esp + 8)));
  fclose(f);
  // Only for a hit exactly at the walk's entry: dump its this and the record head.
  if (e->ExceptionRecord->ExceptionAddress != t.at)
    return;
  f = fopen("c:\\taps.log", "a");
  if (!f)
    return;
  MEMORY_BASIC_INFORMATION mbi;
  unsigned char *c = reinterpret_cast<unsigned char *>(e->ContextRecord->Ecx);
  if (VirtualQuery(c, &mbi, sizeof mbi) == sizeof mbi && mbi.State == MEM_COMMIT &&
      !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    unsigned head;
    memcpy(&head, c + 0x1EC, 4);
    fprintf(f, "  walk this=%p head=%08x a134=%08x a2a4=%08x\\n", c, head,
            *reinterpret_cast<unsigned *>(c + 0x134),
            *reinterpret_cast<unsigned *>(c + 0x2A4));
  }
  fclose(f);
}
__attribute__((naked)) static void NullStub(void) {
  __asm__("ret");
}

static LONG CALLBACK VecHandler(EXCEPTION_POINTERS *e) {
  // Guard-page taps: log every execution on a tapped page (throttled per page per
  // frame), clear the guard and rerun. Re-armed each frame before onFrame (ArmTaps).
  if (e->ExceptionRecord->ExceptionCode == 0x80000001) {
    for (Tap &t : g_taps) {
      if ((reinterpret_cast<DWORD>(e->ExceptionRecord->ExceptionAddress) & ~0xFFFu) ==
              reinterpret_cast<DWORD>(t.page) &&
          t.hits < 400) {
        ++t.hits;
        TapLog(t, e);
      }
    }
    void *page = reinterpret_cast<void *>(
        reinterpret_cast<DWORD>(e->ExceptionRecord->ExceptionAddress) & ~0xFFFu);
    DWORD old;
    VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  if (e->ExceptionRecord->ExceptionAddress == reinterpret_cast<void *>(0)) {
    static int s_nulls;
    FILE *f = fopen("c:\\fault.log", "a");
    if (f) {
      fprintf(f, "null-call #%d ret=%p\\n", ++s_nulls, *reinterpret_cast<void **>(e->ContextRecord->Esp));
      unsigned char *img = reinterpret_cast<unsigned char *>(0x7b190000);
      fprintf(f, "d9a0:");
      for (unsigned a = 0xd9a0; a < 0xd9e0; a += 4)
        fprintf(f, " %02x%02x%02x%02x", img[a], img[a + 1], img[a + 2], img[a + 3]);
      fprintf(f, "\\n");
      fclose(f);
    }
    if (s_nulls < 200) {
      e->ContextRecord->Eip = reinterpret_cast<DWORD>(&NullStub);
      return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
  }
  FILE *f = fopen("c:\\fault.log", "a");
  if (f) {
    fprintf(f, "AV at eip=%p esp=%p ret=%p arg1=%p eax=%p ecx=%p edx=%p faddr=%p fcode=%lx\\n",
            e->ExceptionRecord->ExceptionAddress, reinterpret_cast<void *>(e->ContextRecord->Esp),
            *reinterpret_cast<void **>(e->ContextRecord->Esp),
            *reinterpret_cast<void **>(e->ContextRecord->Esp + 4),
            reinterpret_cast<void *>(e->ContextRecord->Eax),
            reinterpret_cast<void *>(e->ContextRecord->Ecx),
            reinterpret_cast<void *>(e->ContextRecord->Edx),
            reinterpret_cast<void *>(e->ExceptionRecord->ExceptionInformation[1]),
            e->ExceptionRecord->ExceptionInformation[0]);
    DWORD fp = e->ContextRecord->Ebp;
    fprintf(f, "ebp-walk:");
    for (int i = 0; i < 12 && fp > 0x10000 && fp < 0x80000000; ++i) {
      DWORD old = *reinterpret_cast<DWORD *>(fp);
      DWORD ret = *reinterpret_cast<DWORD *>(fp + 4);
      fprintf(f, " %08x->%08x", fp, ret);
      if (old <= fp)
        break;
      fp = old;
    }
    fprintf(f, "\\n");
    fprintf(f, "stack:");
    unsigned char *sp = reinterpret_cast<unsigned char *>(e->ContextRecord->Esp);
    for (int i = -16; i < 16; i += 4)
      fprintf(f, " %08x", *reinterpret_cast<unsigned *>(sp + i * 4));
    fprintf(f, "\\n");
    fclose(f);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// Hunts the bot's static type instances in its loaded data: the enum statics are
// consecutive objects whose first dword is the running enum id (0,1,2,...). The stride
// between objects is the type's size, and the other dwords are its layout.
static void MeasureTypes() {
  FILE *f = fopen("c:\\measure.log", "w");
  if (!f)
    return;
  unsigned char *base = reinterpret_cast<unsigned char *>(0x7b190000);
  // page-guarded scan of the loaded image for the enum statics: consecutive objects
  // whose id field runs 0,1,2,... at a regular stride.
  for (unsigned start = 0x1000; start < 0x75000; start += 4) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(base + start, &mbi, sizeof mbi) != sizeof mbi ||
        mbi.State != MEM_COMMIT || mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
      continue;
    for (int S : {4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 64}) {
      for (int off = 0; off + 4 <= S; off += 4) {
        int run = 0;
        for (int i = 0; i < 12; ++i) {
          unsigned v;
          memcpy(&v, base + start + off + i * S, 4);
          if (v == static_cast<unsigned>(i))
            ++run;
          else
            break;
        }
        if (run >= 8) {
          fprintf(f, "run: start=%08x stride=%d idoff=%d n=%d\n", start, S, off, run);
          for (int i = 0; i < 3; ++i) {
            fprintf(f, "  obj%d:", i);
            for (int k = 0; k < S; k += 4) {
              unsigned v;
              memcpy(&v, base + start + i * S + k, 4);
              fprintf(f, " %08x", v);
            }
            fprintf(f, "\n");
          }
          // the neighborhood: 256 dwords around the run
          for (int i = -32; i < 96; i += 8) {
            fprintf(f, "  n%+04x:", i * 4);
            for (int k = 0; k < 8; ++k) {
              unsigned v;
              memcpy(&v, base + start + i * 4 + k * 4, 4);
              fprintf(f, " %08x", v);
            }
            fprintf(f, "\n");
          }
        }
      }
    }
  }
  fclose(f);
}

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

static int s_nScan;
static void ScanForHeader(int frame) {
  // The shm header's magic, PLO1, must exist in the bot's mapped view once the
  // handshake has run. Scan the whole loaded image for it each frame; dump the region
  // around it when found.
  unsigned char *base = reinterpret_cast<unsigned char *>(0x7b190000);
  static void *s_found;
  if (!s_found) {
    for (unsigned a = 0x10000; a < 0x74000; a += 4) {
      if (memcmp(base + a, "PLO1", 4) == 0) {
        s_found = base + a;
        FILE *f = fopen("dump/header-at.txt", "w");
        if (f)
          fprintf(f, "PLO1 at %p (base+%x) frame %d\n", s_found, a, frame),
          fclose(f);
        break;
      }
    }
  }
  if (s_bigview && frame % 10 == 0) {
    char path[64];
    snprintf(path, sizeof path, "dump/shm-f%05d.bin", frame);
    FILE *f = fopen(path, "wb");
    if (f) {
      fwrite(s_bigview, 1, 0x90000, f);
      fclose(f);
    }
  }
}

static void DumpShm(int frame) {
  ScanForHeader(frame);
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
  AddVectoredExceptionHandler(1, VecHandler);
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
  // The bot's zero-fill BSS tail (its uninitialised globals: the obs gates at
  // base+0x72654/0x72661 and the obs-state object at base+0x73120) is mapped
  // read-only by Wine; a real game's loader gives it write access. Make it writable
  // so the bot's own stores and the rig's gate forcing can land.
  {
    void *bss = reinterpret_cast<void *>(0x7b1a2000);
    DWORD old;
    if (VirtualProtect(bss, 0x2000, PAGE_READWRITE, &old))
      printf("[fakehost] bss 0x7b1a2000..0x7b1a4000 protected RW\n");
    else
      printf("[fakehost] bss protect failed (%lu)\n", GetLastError());
    fflush(stdout);
  }
  MeasureTypes();
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
    printf("[fakehost] PlayerImpl vtable slot 4 -> %p\\n", reinterpret_cast<void *>(&PlayerRace4));
  }

  // Same trick for the Game's chat/printf slot: the bot calls it as (int, const char *).
  {
    void (GameImpl::*pmf)(const char *, va_list) = &GameImpl::vPrintf;
    const unsigned p = *reinterpret_cast<unsigned *>(&pmf);
    const int slot = static_cast<int>(p - 1) / 4;
    void **vt = *reinterpret_cast<void ***>(&handoff);
    DWORD old;
    VirtualProtect(vt, 0x200, PAGE_READWRITE, &old);
    vt[slot] = reinterpret_cast<void *>(&GamePrint2);
    VirtualProtect(vt, 0x200, old, &old);
    printf("[fakehost] GameImpl vPrintf slot %d -> %p\\n", slot,
           reinterpret_cast<void *>(&GamePrint2));
  }

  // The set-returning Unit slots: the bot expects an MSVC-shaped set by value; hand it
  // an empty one (slots 63 getLoadedUnits, 65 getInterceptors, 67 getLarva with the
  // inline destructor at 0).
  if (getenv("HX_NO_SETS") == NULL) {
    g_emptyHead.left = g_emptyHead.parent = g_emptyHead.right = &g_emptyHead;
    // Slot audit: the set-returning methods' real slots (PMF encoding) and neighbours.
    {
      auto slotOf = [](auto pmf) -> int {
        return static_cast<int>((*reinterpret_cast<unsigned *>(&pmf) - 1) / 4);
      };
      printf("[fakehost] slots: loadedUnits=%d interceptors=%d larva=%d hatchery=%d transport=%d\\n",
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
    if (getenv("HX_NO_SETS") == NULL || *getenv("HX_NO_SETS") == '2')
      for (int s : {64, 66, 68})
        vt[s] = reinterpret_cast<void *>(&UnitEmptySet);
    VirtualProtect(vt, 0x200, old, &old);
    printf("[fakehost] UnitImpl set slots -> %p\\n", reinterpret_cast<void *>(&UnitEmptySet));
  }
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
  // The bot's client (a static object at base+0x54160) never got its shm view
  // pointer (+0x18) or its send flag (+0x1C) from the handshake in the rig. The view
  // was set to the bot's own .data page (base+0x54000), but the writer writes the obs
  // up to view+0x85f1c: past .data (ends base+0x54678) into .rdata (read-only) and off
  // the image. Give it a big writable buffer instead, seeded with the .data page so
  // the PLO1 header the bot and engine expect is where they expect it.
  s_bigview = VirtualAlloc(NULL, 0x100000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!s_bigview) {
    printf("[fakehost] view buffer alloc failed\n");
    return 1;
  }
  memcpy(s_bigview, reinterpret_cast<void *>(0x7b1e4000), 0x20000);
  memset(reinterpret_cast<char *>(s_bigview) + 0x20000, 0, 0xE0000);
  {
    unsigned *client = reinterpret_cast<unsigned *>(0x7b1e4160);
    printf("[fakehost] client: view-slot %08x flag %02x -> setting view=%p+flag\n",
           client[6], *reinterpret_cast<unsigned char *>(client + 7), s_bigview);
    fflush(stdout);
    client[6] = reinterpret_cast<unsigned>(s_bigview);
    *reinterpret_cast<unsigned char *>(client + 7) = 1;
  }
  ArmTaps();
  for (int f = 0; f < 1200; ++f) {
    g->frameCount = f;
    g->elapsedTime = f * 42;
    g->eventCount = 1;
    g->events[0].type = f == 0 ? EventType::Enum::MatchStart : EventType::Enum::MatchFrame;
    g->events[0].v1 = f;
    g->events[0].v2 = 0;
    if (f == 0)
      onStart(mod);
    // Force the obs gates open before the frame, in case the bot's pump resets them
    // at the start of its update (they are forced again after onFrame below).
    {
      unsigned char *gates = reinterpret_cast<unsigned char *>(0x7b1a2640);
      *reinterpret_cast<unsigned *>(gates + 0x14) = 0;  // next obs frame: never future
      gates[0x18] = 0;                                  // not yet sent
      gates[0x21] = 1;                                  // engine "ready"
    }
    ArmTaps();
    handoff.onMatchFrame();
    onFrame(mod);
    // Watch the bot's per-unit record list: the obs-state object is the global at
    // base+0x73120 (the writer's this is base+0x54160, a different object), so the
    // list head is at 0x7b1a3120+0x1EC.
    if (f % 5 == 0) {
      FILE *uf = fopen("dump/unitlist.txt", f == 0 ? "w" : "a");
      if (uf) {
        unsigned char *c = reinterpret_cast<unsigned char *>(0x7b1a3120);
        unsigned char *gates = reinterpret_cast<unsigned char *>(0x7b1a2640);
        unsigned head;
        memcpy(&head, c + 0x1EC, 4);
        fprintf(uf, "f%d head=%08x gate654=%08x gate658=%02x gate661=%02x gate660=%02x a2a4=%08x\n",
                f, head, *reinterpret_cast<unsigned *>(gates + 0x14),
                gates[0x18], gates[0x21], gates[0x20],
                *reinterpret_cast<unsigned *>(c + 0x2A4));
        unsigned node = head;
        for (int i = 0; i < 8 && node > 0x10000 && node < 0x80000000; ++i) {
          MEMORY_BASIC_INFORMATION mbi;
          unsigned char *n = reinterpret_cast<unsigned char *>(node);
          if (VirtualQuery(n, &mbi, sizeof mbi) != sizeof mbi ||
              mbi.State != MEM_COMMIT || mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            break;
          fprintf(uf, "  n%d (%p):", i, reinterpret_cast<void *>(node));
          for (int k = 0; k < 28; k += 4) {
            unsigned v;
            memcpy(&v, n + k, 4);
            fprintf(uf, " %08x", v);
          }
          fprintf(uf, "\n");
          memcpy(&node, n, 4);
        }
        fclose(uf);
      }
    }
    // The obs build is gated on the engine's ready-reply, which the rig does not
    // service: the driver skips the build while gate661==0, gate658!=0 ("already
    // sent") or gate654 > frame ("next obs frame"). Force all three open so the
    // writer runs against our buffer every frame.
    {
      unsigned char *gates = reinterpret_cast<unsigned char *>(0x7b1a2640);
      *reinterpret_cast<unsigned *>(gates + 0x14) = 0;  // next obs frame: never future
      gates[0x18] = 0;                                  // not yet sent
      gates[0x21] = 1;                                  // engine "ready"
    }
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
  {
    // The client is a static object at base+0x54160 (the onFrame loads ecx=0x7b1e4160
    // for the writer); dump it and its neighbourhood.
    unsigned char *base = reinterpret_cast<unsigned char *>(0x7b190000);
    FILE *f = fopen("dump/client.bin", "wb");
    if (f) {
      fwrite(base + 0x54100, 1, 0x200, f);
      fclose(f);
    }
  }
  onEnd(mod, 1);
  {
    HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, "BWRL_SHM");
    printf("[fakehost] BWRL_SHM open: %p err=%lu\n", reinterpret_cast<void *>(h), GetLastError());
    if (h) {
      void *v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
      printf("[fakehost] BWRL_SHM view: %p\n", v);
    }
  }
  printf("[fakehost] done\n");
  return 0;
}
