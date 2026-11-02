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
#include <io.h>
#include <fcntl.h>
#include <unistd.h>
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
  DWORD fp = e->ContextRecord->Ebp;
  fprintf(f, "  frames:");
  for (int i = 0; i < 8 && fp > 0x10000 && fp < 0x80000000; ++i) {
    DWORD old = *reinterpret_cast<DWORD *>(fp);
    DWORD ret = *reinterpret_cast<DWORD *>(fp + 4);
    fprintf(f, " %08x->%08x", fp, ret);
    if (old <= fp)
      break;
    fp = old;
  }
  fprintf(f, "\\n");
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
__attribute__((naked)) static void RetOneStub(void) {
  __asm__("movl $1, %eax\n\tret");
}

static LONG CALLBACK VecHandler(EXCEPTION_POINTERS *e) {
  // The bot calls through garbage pointers (an exec fault at e.g. 0x7B1A2000):
  // answer "ready" (eax=1) and continue, so the engine-poll loop settles.
  if (e->ExceptionRecord->ExceptionCode == 0xC0000005 &&
      e->ExceptionRecord->ExceptionInformation[0] == 8) {
    static int s_exec;
    FILE *f = fopen("c:\\fault.log", "a");
    if (f) {
      fprintf(f, "exec fault #%d at eip=%p\\n", ++s_exec,
              e->ExceptionRecord->ExceptionAddress);
      fclose(f);
    }
    if (s_exec < 500) {
      e->ContextRecord->Eip = reinterpret_cast<DWORD>(&RetOneStub);
      return EXCEPTION_CONTINUE_EXECUTION;
    }
  }
  // Reads/writes at near-null addresses: answer 0 and continue.
  if (e->ExceptionRecord->ExceptionCode == 0xC0000005 &&
      e->ExceptionRecord->ExceptionInformation[1] < 0x10000) {
    static int s_low;
    FILE *f = fopen("c:\\fault.log", "a");
    if (f) {
      fprintf(f, "low-addr fault #%d at eip=%p addr=%p\\n", ++s_low,
              e->ExceptionRecord->ExceptionAddress,
              reinterpret_cast<void *>(e->ExceptionRecord->ExceptionInformation[1]));
      fclose(f);
    }
    if (s_low < 500) {
      e->ContextRecord->Eip = reinterpret_cast<DWORD>(&NullStub);
      return EXCEPTION_CONTINUE_EXECUTION;
    }
  }
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
  uint8_t  _p14[0x28 - 0x14];
  uint16_t posX;               // 0x28
  uint16_t posY;               // 0x2A
  uint8_t  _p2c[0x4C - 0x2C];
  uint8_t  playerID;           // 0x4C
  uint8_t  orderID;            // 0x4D
  uint8_t  orderState;         // 0x4E
  uint8_t  orderSignal;        // 0x4F
  uint16_t orderUnitType;      // 0x50
  uint8_t  _p52[0x58 - 0x52];
  uint32_t orderTargetX;       // 0x58
  uint32_t orderTargetY;       // 0x5C
  uint32_t shieldPoints;       // 0x60
  uint16_t unitType;           // 0x64
  uint8_t  _p66[0x68 - 0x66];
  uint32_t prevPlayerUnit;     // 0x68
  uint32_t nextPlayerUnit;     // 0x6C
  uint8_t  _p70[0xA2 - 0x70];
  uint16_t energy;             // 0xA2
  uint8_t  _pA4[0xDC - 0xA4];
  uint32_t statusFlags;        // 0xDC
  uint8_t  _pE0[0xE4 - 0xE0];
  int32_t  visibilityStatus;   // 0xE4
  uint8_t  _pE8[0x150 - 0xE8];
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
  unsigned char visible, completed, pad[2];
};
struct HxPlayer {
  unsigned race, minerals, gas, supplyTotal, supplyUsed;
  unsigned startX, startY;
};

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
static void ApplyImageState(const HxUnit *units, unsigned nUnits, const HxPlayer *players,
                            unsigned nPlayers, unsigned frame, unsigned mapW, unsigned mapH,
                            const unsigned char *seen, const unsigned char *explored,
                            unsigned nPlaneBytes) {
  // ActiveTileArray: the same one-time setup UpdateScMemory does.
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
    // The fog planes, one bit a tile in the map's row order (the fork indexes
    // the array by the map's width): byte 0 = the seen bit (the bot's player is
    // 0), byte 1 = the explored bit, byte 2 = walkable.
    for (unsigned ti = 0; ti < nPlaneBytes * 8u; ++ti) {
      const bool bSeen = (seen[ti >> 3] >> (ti & 7)) & 1;
      const bool bExplored = (explored[ti >> 3] >> (ti & 7)) & 1;
      t[ti * 4 + 0] = bSeen ? 1 : 0;
      t[ti * 4 + 1] = bExplored ? 1 : 0;
      t[ti * 4 + 2] = 1;
    }
    *ScT<void *>(0x6D1260) = s_tiles;
  }

  ScUnit *nodes = ScT<ScUnit>(SC_UNITS);
  // The node table is set up once at start-up by UpdateScMemory and is not
  // wiped here: the fork's engine reads it concurrently, and a torn walk of a
  // half-memset table would leave its model empty.

  // Slots: Hyperion sends position-based ids (0..n-1) every frame, so the slot
  // IS the id. The fork's server tracks units by their node addresses, so a
  // fixed slot keeps the fork's model index equal to the Hyperion list position,
  // and the bot's commands (9363 + index) map straight back. The id table only
  // serves ids beyond 1700 (the test harness's legacy ids).
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
  // Direct slots: position-based ids.
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
  // 3. The dead slots are marked gone in place; the live ones are filled in
  // place (a 0x150-byte window per node, not a 1700-node one).
  for (int sl = 0; sl < 1700; ++sl)
    if (s_idOfSlot[sl] == ~0u && nodes[sl].playerID != 0xFF) {
      nodes[sl].unitType = 228;
      nodes[sl].playerID = 0xFF;
      nodes[sl].statusFlags = 0;
    }
  for (unsigned i = 0; i < nUnits && i < 9999; ++i) {
    const HxUnit &u = units[i];
    const unsigned slot = u.id < 0x20000 ? s_slotOfId[u.id] : ~0u;
    if (slot == ~0u || slot >= 1700)
      continue;
    ScUnit &su = nodes[slot];
    memset(&su, 0, sizeof su);
    su.hitPoints = static_cast<int32_t>(u.hp) * 256;
    su.posX = static_cast<uint16_t>(u.x);
    su.posY = static_cast<uint16_t>(u.y);
    su.playerID = static_cast<uint8_t>(u.player);
    su.orderID = static_cast<uint8_t>(u.order);
    su.shieldPoints = static_cast<int32_t>(u.shields) * 256;
    su.unitType = static_cast<uint16_t>(u.type);
    su.energy = static_cast<int32_t>(u.energy) * 256;
    su.statusFlags = 1;   // Completed
    su.visibilityStatus = u.visible ? 1 : 0;
  }
  // 4. Link the slots in ascending order through the visible-unit list. The
  // list is the local player's view: only the units seen by the bot are in it,
  // and the fork's engine keeps the rest as hidden last-seen entries.
  unsigned linkOrder[1700];
  unsigned nLinked = 0;
  for (int s = 0; s < 1700; ++s) {
    if (s_idOfSlot[s] == ~0u)
      continue;
    if (nodes[s].visibilityStatus == 0)
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

  // Players, from the stream.
  for (int i = 0; i < 12; ++i) {
    unsigned char *p = Sc(SC_PLAYERS) + i * 0x24;
    if (i < static_cast<int>(nPlayers)) {
      *reinterpret_cast<unsigned *>(p + 0x00) = i;   // dwPlayerID
      *reinterpret_cast<unsigned *>(p + 0x04) = i;   // dwStormId
      p[0x08] = 2;                                   // nType: human
      p[0x09] = static_cast<unsigned char>(players[i].race);  // nRace
      p[0x0A] = 0;                                   // nTeam
      snprintf(reinterpret_cast<char *>(p + 0x0B), 25, "%s", i == 0 ? "Pluto" : "Bravo");
    } else {
      p[0x08] = 0;
      p[0x0B] = '\0';
    }
  }
  for (int i = 0; i < 8; ++i)
    ScT<unsigned>(SC_STATUS)[i] = 1;  // playing

  // Game state, from the stream.
  unsigned char *gg = Sc(SC_GAME);
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
  for (int r = 0; r < 3; ++r)
    for (int i = 0; i < 12; ++i) {
      unsigned char *s = gg + 0x3054 + r * 0x90 + i * 4;
      *reinterpret_cast<int *>(s) = 0;
      *reinterpret_cast<int *>(s + 0x30) = 0;
      *reinterpret_cast<int *>(s + 0x60) = 0;
    }
  for (unsigned i = 0; i < nPlayers && i < 12; ++i) {
    *reinterpret_cast<int *>(gg + 0x3054 + 0 * 0x90 + i * 4) = players[i].supplyTotal;
    *reinterpret_cast<int *>(gg + 0x3054 + 0 * 0x90 + 0x30 + i * 4) = players[i].supplyUsed;
  }
  // Unit counts: all[type][player] from the stream (the fork reads these for its
  // opening choice).
  for (int i = 0; i < 12; ++i)
    for (int t = 0; t < 234; ++t)
      *reinterpret_cast<int *>(gg + 0x3234 + t * 0x30 + i * 4) = 0;
  for (unsigned i = 0; i < nUnits && i < 9999; ++i)
    if (units[i].player < 12 && units[i].type < 234)
      ++*reinterpret_cast<int *>(gg + 0x3234 + units[i].type * 0x30 + units[i].player * 4);
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
  fprintf(stderr, "[bridge] SC stand-in: low %p, data %p\n", low, sc);
  fflush(stdout);
}

static bool ReadN(void *pv, size_t cb) {
  unsigned char *p = static_cast<unsigned char *>(pv);
  while (cb) {
    int n = read(0, p, cb);
    if (n <= 0)
      return false;
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
      sz = 12;
    } else if (op == 0x16) {
      sz = 1;
    } else {
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
  s_nSent = end;
  fflush(stdout);
}


int main(int argc, char **argv) {
  const bool bDebug = getenv("HX_DEBUG") != NULL;
  AddVectoredExceptionHandler(1, VecHandler);
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
  // so the bot's own stores and the rig's gate forcing can land.
  {
    void *bss = reinterpret_cast<void *>(0x7b1a2000);
    DWORD old;
    if (VirtualProtect(bss, 0x2000, PAGE_READWRITE, &old))
      fprintf(stderr, "[bridge] bss 0x7b1a2000..0x7b1a4000 protected RW\n");
    else
      fprintf(stderr, "[bridge] bss protect failed (%lu)\n", GetLastError());
    fflush(stdout);
  }
  MeasureTypes();
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
    fprintf(stderr, "[bridge] PlayerImpl vtable slot 4 -> %p\\n", reinterpret_cast<void *>(&PlayerRace4));
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
    fprintf(stderr, "[bridge] GameImpl vPrintf slot %d -> %p\\n", slot,
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
    if (getenv("HX_NO_SETS") == NULL || *getenv("HX_NO_SETS") == '2')
      for (int s : {64, 66, 68})
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
  AIModule *mod = newAIModule();
  fprintf(stderr, "[bridge] module %p\n", static_cast<void *>(mod));
  gameInit(&handoff);
  fprintf(stderr, "[bridge] gameInit handed over\n");
  fflush(stdout);
  // The bridge runs these when its update() sees the events; the rig drives them itself.
  handoff.onMatchStart();

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
  ArmTaps();
  int nFramesDone = 0;
  for (;;) {
    DWORD t0 = GetTickCount();
    unsigned hdr[3];
    if (!ReadN(hdr, sizeof hdr)) {
      fprintf(stderr, "[bridge] stdin closed after %d frames\n", nFramesDone);
      break;
    }
    if (hdr[0] != 0x50465848) {  // 'HXPF'
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
    static HxUnit units[9999];
    static HxPlayer players[12];
    ReadN(units, nUnits * sizeof(HxUnit));
    ReadN(players, nPlayers * sizeof(HxPlayer));
    static unsigned char seen[0x8000], explored[0x8000];  // 256x256 bits max
    const unsigned nPlaneBytes = (mapWH[0] * mapWH[1] + 7) / 8;
    ReadN(seen, nPlaneBytes);
    ReadN(explored, nPlaneBytes);
    DWORD t1 = GetTickCount();
    if (getenv("HX_NOAPPLY") == NULL)
      ApplyImageState(units, nUnits, players, nPlayers, frame, mapWH[0], mapWH[1],
                      seen, explored, nPlaneBytes);
    if (bDebug && frame == 0) {
      fprintf(stderr, "[bridge] sizes: HxUnit=%u HxPlayer=%u\n",
              static_cast<unsigned>(sizeof(HxUnit)),
              static_cast<unsigned>(sizeof(HxPlayer)));
      fprintf(stderr, "[bridge] p0: race=%u min=%u gas=%u sup=%u/%u sx=%u sy=%u\n",
              players[0].race, players[0].minerals, players[0].gas,
              players[0].supplyTotal, players[0].supplyUsed,
              players[0].startX, players[0].startY);
      fprintf(stderr, "[bridge] offs: race=%u minerals=%u gas=%u supply=%u size=%u\n",
              static_cast<unsigned>(offsetof(PlayerData, race)),
              static_cast<unsigned>(offsetof(PlayerData, minerals)),
              static_cast<unsigned>(offsetof(PlayerData, gas)),
              static_cast<unsigned>(offsetof(PlayerData, supplyTotal)),
              static_cast<unsigned>(sizeof(PlayerData)));
      {
        unsigned vis = *ScT<unsigned>(0x628430);
        unsigned iu = *ScT<unsigned>(0x59CCA8) & 0xFFFF;  // unit 0's type
        unsigned ip = *ScT<unsigned char>(0x59CCA8 + 0x4C) & 0xFF;  // unit 0's player
        unsigned it = *ScT<unsigned short>(0x59CCA8 + 0x64) & 0xFFFF;  // unit 0's type
        fprintf(stderr, "[bridge] sc: vislist=%08x u0type=%u u0player=%u u0type64=%u\n",
                vis, iu, ip, it);
      }
      FILE *pf = fopen("dump/pdata0.bin", "wb");
      if (pf) {
        fwrite(&g->players[0], 1, 0x169C, pf);
        fclose(pf);
      }
    }
    int f = static_cast<int>(frame);
    g->frameCount = f;
    g->elapsedTime = f * 42;
    if (getenv("HX_NOAPPLY") != NULL)
      UpdateScMemory();  // the scripted debug path; the production path applied the image above
    if (bDebug && frame == 0) {
      const char *tag = getenv("HX_NOAPPLY") != NULL ? "na" : "p";
      char path[64];
      snprintf(path, sizeof path, "dump/sc-img-%s.bin", tag);
      FILE *pf = fopen(path, "wb");
      if (pf) {
        fwrite(reinterpret_cast<void *>(0x21400000), 1, 0x400000, pf);
        fclose(pf);
      }
    }
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
    if (bDebug)
      ArmTaps();
    handoff.onMatchFrame();
    onFrame(mod);
    if (bDebug && frame == 1) {
      fprintf(stderr, "[bridge] GameData units after onFrame f1 (initialUnitCount=%d):\n", g->initialUnitCount);
      for (int i = 0; i < 14; ++i) {
        const UnitData &u = g->units[i];
        fprintf(stderr, "  [%d] ex=%d id=%u pl=%d ty=%u vis0=%d idle=%d ord=%u hp=%d\n",
                i, u.exists, u.id, u.player, u.type, u.isVisible[0], u.isIdle,
                static_cast<unsigned>(u.order), u.hitPoints);
      }
      unsigned *cnts = reinterpret_cast<unsigned *>(reinterpret_cast<char *>(g) + 0xC0 + 0x88);
      fprintf(stderr, "  p0 counts: %u %u %u | p1 counts: %u %u %u\n",
              cnts[0], cnts[1], cnts[2],
              reinterpret_cast<unsigned *>(reinterpret_cast<char *>(g) + 0xC0 + sizeof(PlayerData) + 0x88)[0],
              reinterpret_cast<unsigned *>(reinterpret_cast<char *>(g) + 0xC0 + sizeof(PlayerData) + 0x88)[1],
              reinterpret_cast<unsigned *>(reinterpret_cast<char *>(g) + 0xC0 + sizeof(PlayerData) + 0x88)[2]);
    }
    // Capture the live pipeline: the bot's own handshake (inside onStart) points
    // the client's view at the real BWRL_SHM mapping it shares with the engine, so
    // dump that mapping (the obs requests) and SC's TurnBuffer (the bot's issued
    // commands, 0x654880 with the byte count at 0x654AA0) on the step cadence.
    if (bDebug && f % 6 == 0) {
      MEMORY_BASIC_INFORMATION q;
      void *view = reinterpret_cast<void *>(
          *reinterpret_cast<unsigned *>(0x7b1e4160 + 0x18));
      bool ok = VirtualQuery(view, &q, sizeof q) == sizeof q;
      if (ok) {
        // copy page-wise, skipping unmapped or unreadable pages
        char path[64];
        snprintf(path, sizeof path, "dump/live-f%05d.bin", f);
        FILE *f2 = fopen(path, "wb");
        if (f2) {
          unsigned char *p = reinterpret_cast<unsigned char *>(view);
          for (unsigned off = 0; off < 0x90000; off += 0x1000) {
            MEMORY_BASIC_INFORMATION m;
            unsigned char *pg = p + off;
            if (VirtualQuery(pg, &m, sizeof m) != sizeof m || m.State != MEM_COMMIT ||
                m.Protect & (PAGE_NOACCESS | PAGE_GUARD))
              fwrite("\0\0\0\0", 1, 0x1000, f2);  // unmapped: zero-fill keeps offsets
            else
              fwrite(pg, 1, 0x1000, f2);
          }
          fclose(f2);
        }
      }
      unsigned char *turn = Sc(0x654880);
      unsigned used = *ScT<unsigned>(0x654AA0);
      if (used > 0x10000)
        used = 0;
      {
        char path[64];
        snprintf(path, sizeof path, "dump/acts-f%05d.bin", f);
        FILE *f2 = fopen(path, "wb");
        if (f2) {
          fwrite(turn, 1, used, f2);
          fclose(f2);
        }
      }
      FILE *uf = fopen("dump/unitlist.txt", f == 0 ? "w" : "a");
      if (uf) {
        unsigned char *gates = reinterpret_cast<unsigned char *>(0x7b1a2640);
        fprintf(uf, "f%d gate654=%08x gate658=%02x gate661=%02x view=%p turnused=%u\n",
                f, *reinterpret_cast<unsigned *>(gates + 0x14),
                gates[0x18], gates[0x21],
                reinterpret_cast<void *>(*reinterpret_cast<unsigned *>(0x7b1e4160 + 0x18)),
                used);
        if (ok)
          fprintf(uf, "  view region: state=%lx prot=%lx size=%lx\\n", q.State, q.Protect, q.RegionSize);
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
    ++nFramesDone;
    DWORD t2 = GetTickCount();
    if (bDebug && (frame % 6 == 0 || frame < 3))
      fprintf(stderr, "[bridge] f%d: read=%lums fill=%lums frame=%lums\n",
              static_cast<int>(frame), t1 - t0, t2 - t1, GetTickCount() - t2);
    FlushReply(f);
    if (bDebug)
      DumpShm(f);    if (bDebug)
      DumpShm(f);
    if (bDebug && f % 100 == 0)
      fprintf(stderr, "[bridge] frame %d\n", f);
    Sleep(16);
  }
  if (bDebug) {
    // The client is a static object at base+0x54160 (the onFrame loads ecx=0x7b1e4160
    // for the writer); dump it and its neighbourhood.
    unsigned char *base = reinterpret_cast<unsigned char *>(0x7b190000);
    FILE *f = fopen("dump/client.bin", "wb");
    if (f) {
      fwrite(base + 0x54100, 1, 0x200, f);
      fclose(f);
    }
  }
  FlushReply(static_cast<unsigned>(nFramesDone));  // a last command may land late
  onEnd(mod, 1);
  if (bDebug) {
    unsigned char *tb = Sc(0x654880);
    unsigned char *cn = Sc(0x654A90);
    fprintf(stderr, "[bridge] end: turn buffer: %02x %02x %02x %02x %02x %02x %02x %02x\n",
            tb[0], tb[1], tb[2], tb[3], tb[4], tb[5], tb[6], tb[7]);
    fprintf(stderr, "[bridge] end: count region: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
            cn[0], cn[1], cn[2], cn[3], cn[4], cn[5], cn[6], cn[7],
            cn[8], cn[9], cn[10], cn[11], cn[12], cn[13], cn[14], cn[15]);
  }
  {
    HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, "BWRL_SHM");
    fprintf(stderr, "[bridge] BWRL_SHM open: %p err=%lu\n", reinterpret_cast<void *>(h), GetLastError());
    if (h) {
      void *v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
      fprintf(stderr, "[bridge] BWRL_SHM view: %p\n", v);
    }
  }
  fprintf(stderr, "[bridge] done\n");
  return 0;
}
