// port_canvas.cpp -- Debug Tools' canvas (docs/modules/tools.md): a map Hyperion makes
// on the spot and plays, to see new work in the game as it really draws, and map_push's, at the
// size and tiles the push gives (PortCanvasPushNext), its starts and armies placed by its size.
//
// It is a Use Map Settings scenario at Brood War's map version, 205, written into
// mods\debug-tools\canvas.scx in the data folder and played as any map (PortLaunchScenario),
// with the sections 1.17's loader requires of that version (maphdr.cpp's handler tables):
//   pass 1:   VER DIM ERA OWNR SIDE STR SPRP FORC VCOD
//   the list: STR MBRF
//   pass 2:   STR MTXM THG2 MASK UNIx UPGx TECx PUNI PUPx PTEx UNIT UPRP MRGN TRIG COLR
// The rules keep 1.17's defaults, every unit and tech available and researched and the
// upgrades' maxima from upgrades.dat; VCOD is the player's exe's own (resource 202), which the
// loader checks a map's against. The battle (PortCanvasBattle, HYPERION_CANVAS_BATTLE=1) has a
// trigger that sends a wave to each side, orders every unit at the other camp and runs again 25
// seconds later. The map is made of numbers alone and stays in the data folder, never committed.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../platform/port_paths.h"  // PortDataFolder
#include "../platform/port_standalone.h"  // PortGameVcod
#include "../platform/port_log.h"  // PortLogger
#include "port_canvas.h"
#include "oc_data.h"  // OC_VA
#include "Storm.h"    // SMemFree, SStrCopy
#include "snd.h"      // load_game_data, load_array_data
#include "gamedata.h" // array_data_t
#include "../data/port_stormlib.h"  // StormLibWriteArchive
#include "../platform/port_globals117.h"

// 1.17's globals, by name: the tileset names and upgrades.dat's table hold pointers, so
// the 64-bit build keeps them apart from 1.17's data block (gen/wide117.h).
extern BYTE upg_repeats[64] OC_UNALIGNED;               // 0x524D8C, upgrades.dat's maximum levels

namespace {

const int SIZE = 64;           // tiles a side
const int TILE = 32;           // pixels a tile
const int VERSION = 205;       // Brood War's map version
const int PLAYERS = 12;
const int UNIT_TYPES = 228;
const int UPGRADES = 61;
const int TECHS = 44;
const int LOCATIONS = 255;
const int ANYWHERE = 64;       // the location every map has, the whole map

bool g_bBattleNext;            // PortCanvasBattle: the next canvas has the battle
bool g_bPushNext;              // PortCanvasPushNext: the next canvas is g_push
PortCanvasMap g_push;
const WORD ANY_UNIT = 229;
const WORD START_LOCATION = 214;

// Slot types and races as a map file gives them (OWNR, SIDE).
const BYTE SLOT_INACTIVE = 0, SLOT_COMPUTER = 5, SLOT_HUMAN = 6, SLOT_NEUTRAL = 7;
const BYTE RACE_ZERG = 0, RACE_TERRAN = 1, RACE_NEUTRAL = 4, RACE_INACTIVE = 7;

// Trigger conditions and actions by their number in the map format.
const BYTE COND_ALWAYS = 22;
const BYTE ACT_PRESERVE = 3, ACT_WAIT = 4, ACT_CREATE_UNIT = 44, ACT_ORDER = 46;
const BYTE ORDER_ATTACK = 2;

// The canvas's own locations, 1-based as triggers name them.
const int LOC_WEST = 1, LOC_EAST = 2, LOC_CENTRE = 3;

struct Unit {
  WORD wType;
  int nCount;
};

// The armies placed at the start, and the waves the trigger sends.
const Unit kWestArmy[] = {{0, 8},  {32, 4}, {34, 2}, {5, 2},  {3, 2},  {65, 4},
                          {66, 4}, {68, 1}, {8, 2},  {12, 1}, {83, 1}, {67, 2}};
const Unit kEastArmy[] = {{37, 16}, {38, 8}, {39, 2}, {103, 2}, {43, 6},
                          {44, 2},  {62, 2}, {46, 1}, {45, 1}};
const Unit kWestWave[] = {{0, 6}, {32, 2}, {34, 1}, {65, 2}, {66, 2}, {5, 1}, {8, 1}};
const Unit kEastWave[] = {{37, 10}, {38, 4}, {43, 3}, {39, 1}};

// hyperion_canvas.log, beside the exe as the other logs are: what was made, or why not.
const PortLogger CanvasLog("hyperion_canvas.log");

class Chk {
public:
  std::vector<BYTE> data;

  void Section(const char *pszTag, const std::vector<BYTE> &body) {
    data.insert(data.end(), pszTag, pszTag + 4);
    Dword(static_cast<DWORD>(body.size()));
    data.insert(data.end(), body.begin(), body.end());
  }

private:
  void Dword(DWORD dw) {
    for (int i = 0; i < 4; ++i)
      data.push_back(static_cast<BYTE>(dw >> (8 * i)));
  }
};

void Put16(std::vector<BYTE> &v, size_t at, WORD w) {
  v[at] = static_cast<BYTE>(w);
  v[at + 1] = static_cast<BYTE>(w >> 8);
}

void Put32(std::vector<BYTE> &v, size_t at, DWORD dw) {
  for (int i = 0; i < 4; ++i)
    v[at + i] = static_cast<BYTE>(dw >> (8 * i));
}

std::vector<BYTE> Bytes(size_t n, BYTE b = 0) {
  return std::vector<BYTE>(n, b);
}

std::vector<BYTE> Word(WORD w) {
  std::vector<BYTE> v = Bytes(2);
  Put16(v, 0, w);
  return v;
}

// The tileset by name ("badlands", "jungle", ...), or its number; badlands when neither.
int TilesetOf(const char *pszName) {
  if (pszName != NULL && pszName[0] >= '0' && pszName[0] <= '7' && pszName[1] == '\0')
    return pszName[0] - '0';
  for (int i = 0; pszName != NULL && i < 8; ++i)
    if (lstrcmpiA(pszName, gpszTilesetNames_g[i]) == 0)
      return i;
  return 0;
}

// The tileset's grass, where it has one, by cv5's terrain type (a group's first word):
// Badlands' Grass, Jungle's Jungle and Ice's Grass; 0 for none.
const WORD kGrass[8] = {6, 0, 0, 0, 9, 0, 11, 0};

// The tiles of the tileset's grass, else of its first group that is plain ground: buildable,
// not creep (cv5's 0x0040; every tileset's first group is), and every minitile of its
// megatiles walkable on low ground. Each cell takes one of the group's variations, as an
// editor's brush does. A tileset with no such group (Installation's floors are not all low
// ground) takes the first buildable group walkable at any height, then the first walkable one.
bool GroundTiles(int nTileset, std::vector<WORD> *pTiles) {
  char szPath[MAX_PATH];
  DWORD cbCv5 = 0, cbVf4 = 0;
  wsprintfA(szPath, "Tileset\\%s.cv5", gpszTilesetNames_g[nTileset]);
  WORD *pCv5 = static_cast<WORD *>(load_game_data(szPath, NULL, &cbCv5, "port_canvas.cpp", __LINE__));
  wsprintfA(szPath, "Tileset\\%s.vf4", gpszTilesetNames_g[nTileset]);
  WORD *pVf4 = static_cast<WORD *>(load_game_data(szPath, NULL, &cbVf4, "port_canvas.cpp", __LINE__));
  const DWORD nGroups = cbCv5 / 52, nMegatiles = cbVf4 / 32;
  pTiles->clear();
  // -1: the grass; 0: buildable, walkable low ground; 1: buildable, walkable at one height;
  // 2: walkable
  const WORD wGrass = nTileset >= 0 && nTileset < 8 ? kGrass[nTileset] : 0;
  for (int nTier = wGrass != 0 ? -1 : 0; nTier < 3 && pTiles->empty(); ++nTier) {
    for (DWORD g = 1; pCv5 != NULL && pVf4 != NULL && g < nGroups && g < 1024 && pTiles->empty(); ++g) {
      const WORD *pGroup = pCv5 + g * 26;
      if ((nTier < 0 && pGroup[0] != wGrass) || (pGroup[1] & 0x0040) != 0)  // not the grass; creep
        continue;
      if (nTier < 2 && (pGroup[1] & 0x0080) != 0)  // unbuildable
        continue;
      for (int sub = 0; sub < 16; ++sub) {
        const WORD wMega = pGroup[10 + sub];
        if (wMega == 0 || wMega >= nMegatiles)
          continue;
        const WORD wFirst = pVf4[wMega * 16];
        bool bFlat = true;
        for (int k = 0; k < 16 && bFlat; ++k) {
          const WORD w = pVf4[wMega * 16 + k];
          bFlat = nTier <= 0 ? (w & 0x0F) == 0x01  // walkable; not mid, high or ramp
                  : (w & 0x01) != 0 && (w & 0x16) == (wFirst & 0x16);  // walkable, one height, no ramp
        }
        if (bFlat)
          pTiles->push_back(static_cast<WORD>(g * 16 + sub));
      }
      if (pTiles->size() < 2)
        pTiles->clear();
    }
  }
  if (pCv5 != NULL)
    SMemFree(pCv5, "port_canvas.cpp", __LINE__, 0);
  if (pVf4 != NULL)
    SMemFree(pVf4, "port_canvas.cpp", __LINE__, 0);
  return !pTiles->empty();
}

std::vector<BYTE> Strings(const char *const *ppsz, int n) {
  std::vector<BYTE> v = Bytes(2 + 2 * n);
  Put16(v, 0, static_cast<WORD>(n));
  for (int i = 0; i < n; ++i) {
    Put16(v, 2 + 2 * i, static_cast<WORD>(v.size()));
    v.insert(v.end(), ppsz[i], ppsz[i] + strlen(ppsz[i]) + 1);
  }
  return v;
}

// Units in rows of eight, centred on (x, y) in pixels, wider apart for the bigger ones.
void Place(std::vector<BYTE> &v, DWORD *pSerial, BYTE bOwner, const Unit *pUnits, int n, int x, int y) {
  int nRow = 0, nTotal = 0;
  for (int i = 0; i < n; ++i)
    nTotal += pUnits[i].nCount;
  const int nRows = (nTotal + 7) / 8;
  for (int i = 0; i < n; ++i) {
    for (int k = 0; k < pUnits[i].nCount; ++k, ++nRow) {
      std::vector<BYTE> u = Bytes(36);
      const int col = nRow % 8, row = nRow / 8;
      Put32(u, 0x00, (*pSerial)++);
      Put16(u, 0x04, static_cast<WORD>(x + (row - nRows / 2) * 48));
      Put16(u, 0x06, static_cast<WORD>(y + (col - 4) * 40));
      Put16(u, 0x08, pUnits[i].wType);
      u[0x10] = bOwner;
      u[0x11] = u[0x12] = u[0x13] = 100;  // hit points, shields and energy, as percentages
      v.insert(v.end(), u.begin(), u.end());
    }
  }
}

void StartLocation(std::vector<BYTE> &v, DWORD *pSerial, BYTE bOwner, int x, int y) {
  std::vector<BYTE> u = Bytes(36);
  Put32(u, 0x00, (*pSerial)++);
  Put16(u, 0x04, static_cast<WORD>(x));
  Put16(u, 0x06, static_cast<WORD>(y));
  Put16(u, 0x08, START_LOCATION);
  u[0x10] = bOwner;
  v.insert(v.end(), u.begin(), u.end());
}

// A trigger action (32 bytes): its location, the player it acts for, a number, a unit
// type, the action and a count (for Order, the order).
void Action(std::vector<BYTE> &t, int nAt, int nLocation, DWORD dwGroup, DWORD dwValue, WORD wUnit,
            BYTE bAction, BYTE bCount, DWORD dwTime = 0) {
  const size_t at = 16 * 20 + nAt * 32;
  Put32(t, at + 0x00, static_cast<DWORD>(nLocation));
  Put32(t, at + 0x0C, dwTime);
  Put32(t, at + 0x10, dwGroup);
  Put32(t, at + 0x14, dwValue);
  Put16(t, at + 0x18, wUnit);
  t[at + 0x1A] = bAction;
  t[at + 0x1B] = bCount;
  t[at + 0x1C] = wUnit != 0 ? 0x10 : 0;  // the unit type is used, as an editor marks it
}

std::vector<BYTE> WaveTrigger() {
  std::vector<BYTE> t = Bytes(0x960);
  t[0x0F] = COND_ALWAYS;  // the first condition's type
  int n = 0;
  for (const Unit &u : kWestWave)
    Action(t, n++, LOC_WEST, 0, 0, u.wType, ACT_CREATE_UNIT, static_cast<BYTE>(u.nCount));
  for (const Unit &u : kEastWave)
    Action(t, n++, LOC_EAST, 1, 0, u.wType, ACT_CREATE_UNIT, static_cast<BYTE>(u.nCount));
  Action(t, n++, ANYWHERE, 0, LOC_EAST, ANY_UNIT, ACT_ORDER, ORDER_ATTACK);
  Action(t, n++, ANYWHERE, 1, LOC_WEST, ANY_UNIT, ACT_ORDER, ORDER_ATTACK);
  Action(t, n++, 0, 0, 0, 0, ACT_WAIT, 0, 25000);
  Action(t, n++, 0, 0, 0, 0, ACT_PRESERVE, 0);
  t[0x944] = 1;  // run for player 1
  return t;
}

void Location(std::vector<BYTE> &v, int nLocation, int left, int top, int right, int bottom) {
  const size_t at = (nLocation - 1) * 20;
  Put32(v, at + 0x00, static_cast<DWORD>(left));
  Put32(v, at + 0x04, static_cast<DWORD>(top));
  Put32(v, at + 0x08, static_cast<DWORD>(right));
  Put32(v, at + 0x0C, static_cast<DWORD>(bottom));
}

bool Vcod(std::vector<BYTE> *pVcod) {
  ULONG cb = 0;
  const BYTE *p = static_cast<const BYTE *>(PortGameVcod(&cb));
  pVcod->assign(p, p + cb);
  return true;
}

// The number of tile groups in the tileset's cv5; 0 when it cannot be read.
DWORD TileGroups(int nTileset) {
  char szPath[MAX_PATH];
  DWORD cbCv5 = 0;
  wsprintfA(szPath, "Tileset\\%s.cv5", gpszTilesetNames_g[nTileset]);
  void *pCv5 = load_game_data(szPath, NULL, &cbCv5, "port_canvas.cpp", __LINE__);
  if (pCv5 == NULL)
    return 0;
  SMemFree(pCv5, "port_canvas.cpp", __LINE__, 0);
  return cbCv5 / 52;
}

bool BuildChk(int nTileset, bool bBattle, const PortCanvasMap *pPush, std::vector<BYTE> *pChk) {
  std::vector<WORD> tiles;
  std::vector<BYTE> vcod;
  const bool bTiles = pPush != NULL && !pPush->tiles.empty();
  if ((!bTiles && !GroundTiles(nTileset, &tiles)) || !Vcod(&vcod))
    return false;
  const int W = pPush != NULL ? pPush->nWidth : SIZE, H = pPush != NULL ? pPush->nHeight : SIZE;
  load_array_data("arr\\upgrades.dat", gUPGRADESArrayDataTbl);

  Chk chk;
  chk.Section("TYPE", {'R', 'A', 'W', 'B'});
  chk.Section("VER ", Word(VERSION));
  chk.Section("IVE2", Word(11));
  chk.Section("VCOD", vcod);

  std::vector<BYTE> ownr = Bytes(PLAYERS, SLOT_INACTIVE), side = Bytes(PLAYERS, RACE_INACTIVE);
  ownr[0] = SLOT_HUMAN;
  side[0] = RACE_TERRAN;
  ownr[1] = SLOT_COMPUTER;
  side[1] = RACE_ZERG;
  ownr[11] = SLOT_NEUTRAL;
  side[11] = RACE_NEUTRAL;
  chk.Section("IOWN", ownr);
  chk.Section("OWNR", ownr);
  chk.Section("ERA ", Word(static_cast<WORD>(nTileset)));
  std::vector<BYTE> dim = Bytes(4);
  Put16(dim, 0, static_cast<WORD>(W));
  Put16(dim, 2, static_cast<WORD>(H));
  chk.Section("DIM ", dim);
  chk.Section("SIDE", side);

  std::vector<BYTE> mtxm = Bytes(static_cast<size_t>(W) * H * 2);
  DWORD dwSeed = 0x2545F491;
  for (int i = 0; i < W * H; ++i) {
    dwSeed = dwSeed * 1103515245 + 12345;
    Put16(mtxm, 2 * i, bTiles ? pPush->tiles[i] : tiles[(dwSeed >> 16) % tiles.size()]);
  }
  for (size_t r = 0; pPush != NULL && r < pPush->rects.size(); ++r) {
    const PortCanvasMap::Rect &rc = pPush->rects[r];
    for (int y = rc.y < 0 ? 0 : rc.y; y < rc.y + rc.h && y < H; ++y)
      for (int x = rc.x < 0 ? 0 : rc.x; x < rc.x + rc.w && x < W; ++x)
        Put16(mtxm, 2 * (static_cast<size_t>(y) * W + x), rc.tile);
  }
  chk.Section("MTXM", mtxm);

  // Rules: 1.17's own, everything available, every tech researched.
  std::vector<BYTE> puni = Bytes(UNIT_TYPES * PLAYERS * 2 + UNIT_TYPES, 1);
  chk.Section("PUNI", puni);
  std::vector<BYTE> pupx = Bytes(UPGRADES * PLAYERS * 3 + UPGRADES * 2);
  for (int i = 0; i < UPGRADES; ++i)
    pupx[UPGRADES * PLAYERS * 2 + i] = upg_repeats[i];       // the maximum level
  memset(&pupx[UPGRADES * PLAYERS * 2 + UPGRADES * 2], 1, UPGRADES * PLAYERS);  // use these
  chk.Section("PUPx", pupx);
  std::vector<BYTE> ptex = Bytes(TECHS * PLAYERS * 3 + TECHS * 2, 1);
  chk.Section("PTEx", ptex);
  std::vector<BYTE> unix = Bytes(0x1048);
  memset(&unix[0], 1, UNIT_TYPES);
  chk.Section("UNIx", unix);
  std::vector<BYTE> upgx = Bytes(0x31A);
  memset(&upgx[0], 1, UPGRADES);
  chk.Section("UPGx", upgx);
  std::vector<BYTE> tecx = Bytes(0x18C);
  memset(&tecx[0], 1, TECHS);
  chk.Section("TECx", tecx);

  const int west = 18 * TILE, east = (W - 18) * TILE, mid = H * TILE / 2;
  const int centre = W * TILE / 2;
  std::vector<BYTE> units;
  DWORD dwSerial = 1;
  StartLocation(units, &dwSerial, 0, centre - 4 * TILE, mid);
  StartLocation(units, &dwSerial, 1, east, mid);
  if (bBattle) {
    Place(units, &dwSerial, 0, kWestArmy, ARRAYSIZE(kWestArmy), west + 6 * TILE, mid);
    Place(units, &dwSerial, 1, kEastArmy, ARRAYSIZE(kEastArmy), east - 6 * TILE, mid);
  }
  chk.Section("UNIT", units);
  chk.Section("THG2", {});
  chk.Section("MASK", Bytes(static_cast<size_t>(W) * H, 0));  // explored by everyone

  static const char *const kStrings[] = {
      "Debug canvas",
      bBattle ? "Hyperion's Debug Tools: flat ground and two armies that keep fighting."
              : "Hyperion's Debug Tools: flat ground to place units on.",
      "Hyperion", "Opponent", "West camp", "East camp", "Centre"};
  chk.Section("STR ", Strings(kStrings, ARRAYSIZE(kStrings)));
  std::vector<BYTE> sprp = Bytes(4);
  Put16(sprp, 0, 1);
  Put16(sprp, 2, 2);
  chk.Section("SPRP", sprp);
  std::vector<BYTE> forc = Bytes(20);
  forc[1] = 1;          // player 2 in force 2, enemies of player 1's
  Put16(forc, 8, 3);    // the forces' names
  Put16(forc, 10, 4);
  chk.Section("FORC", forc);

  std::vector<BYTE> mrgn = Bytes(LOCATIONS * 20);
  Location(mrgn, LOC_WEST, west - 4 * TILE, mid - 4 * TILE, west + 4 * TILE, mid + 4 * TILE);
  Location(mrgn, LOC_EAST, east - 4 * TILE, mid - 4 * TILE, east + 4 * TILE, mid + 4 * TILE);
  Location(mrgn, LOC_CENTRE, centre - 4 * TILE, mid - 4 * TILE, centre + 4 * TILE, mid + 4 * TILE);
  Location(mrgn, ANYWHERE, 0, 0, W * TILE, H * TILE);
  for (int i = 0; i < 3; ++i)
    Put16(mrgn, i * 20 + 0x10, static_cast<WORD>(5 + i));
  chk.Section("MRGN", mrgn);
  chk.Section("UPRP", Bytes(0x500));
  chk.Section("UPUS", Bytes(64));
  chk.Section("TRIG", bBattle ? WaveTrigger() : std::vector<BYTE>());
  chk.Section("MBRF", {});
  std::vector<BYTE> colr = Bytes(8);
  for (int i = 0; i < 8; ++i)
    colr[i] = static_cast<BYTE>(i);
  chk.Section("COLR", colr);

  pChk->swap(chk.data);
  return true;
}

}  // namespace

void PortCanvasBattle(bool bOn) {
  g_bBattleNext = bOn;
}

bool PortCanvasCheck(const PortCanvasMap &map, std::string *pWhy) {
  const int nTileset = TilesetOf(map.tileset.c_str());
  if (!map.tileset.empty() && lstrcmpiA(map.tileset.c_str(), gpszTilesetNames_g[nTileset]) != 0 &&
      !(map.tileset.size() == 1 && map.tileset[0] - '0' == nTileset)) {
    *pWhy = "tileset is one of badlands, platform, install, ashworld, jungle, desert, ice, twilight";
    return false;
  }
  auto sizeOk = [](int n) { return n == 64 || n == 96 || n == 128 || n == 192 || n == 256; };
  if (!sizeOk(map.nWidth) || !sizeOk(map.nHeight)) {
    *pWhy = "width and height are each 64, 96, 128, 192 or 256 tiles";
    return false;
  }
  const DWORD nGroups = TileGroups(nTileset);
  for (size_t r = 0; r < map.rects.size(); ++r)
    if (static_cast<DWORD>(map.rects[r].tile >> 4) >= nGroups) {
      *pWhy = "rect " + std::to_string(r) + "'s tile names group " + std::to_string(map.rects[r].tile >> 4) + "; " +
              gpszTilesetNames_g[nTileset] + " has " + std::to_string(nGroups);
      return false;
    }
  if (map.tiles.empty()) {
    std::vector<WORD> ground;
    if (!GroundTiles(nTileset, &ground)) {
      *pWhy = std::string(gpszTilesetNames_g[nTileset]) + " has no plain ground the canvas can use; send tiles or fill";
      return false;
    }
    return true;
  }
  if (map.tiles.size() != static_cast<size_t>(map.nWidth) * map.nHeight) {
    *pWhy = "tiles has " + std::to_string(map.tiles.size()) + " values; a " + std::to_string(map.nWidth) + "x" +
            std::to_string(map.nHeight) + " map needs " + std::to_string(map.nWidth * map.nHeight);
    return false;
  }
  for (size_t i = 0; i < map.tiles.size(); ++i)
    if (static_cast<DWORD>(map.tiles[i] >> 4) >= nGroups) {
      *pWhy = "tile " + std::to_string(i) + " (" + std::to_string(map.tiles[i]) + ") names group " +
              std::to_string(map.tiles[i] >> 4) + "; " + gpszTilesetNames_g[nTileset] + " has " +
              std::to_string(nGroups);
      return false;
    }
  return true;
}

void PortCanvasPushNext(const PortCanvasMap &map) {
  g_push = map;
  g_bPushNext = true;
}

void PortCanvasPushCancel(void) {
  g_bPushNext = false;
}

bool PortCanvasWrite(const char *pszTileset, char *pszPath, size_t cchPath) {
  std::vector<BYTE> chk;
  const bool bPush = g_bPushNext;
  g_bPushNext = false;
  const int nTileset = TilesetOf(bPush && !g_push.tileset.empty()           ? g_push.tileset.c_str()
                                 : pszTileset != NULL && pszTileset[0] != 0 ? pszTileset
                                                                            : PORT_CANVAS_TILESET);
  const char *pszBattle = getenv("HYPERION_CANVAS_BATTLE");
  const bool bBattle = g_bBattleNext || (pszBattle != NULL && pszBattle[0] == '1');
  g_bBattleNext = false;
  if (!BuildChk(nTileset, bBattle, bPush ? &g_push : NULL, &chk)) {
    CanvasLog("canvas: could not make the map (tileset %d)", nTileset);
    return false;
  }
  std::string folder = std::string(PortDataFolder()) + "mods\\";
  CreateDirectoryA(folder.c_str(), NULL);
  folder += "debug-tools\\";
  CreateDirectoryA(folder.c_str(), NULL);
  const std::string path = folder + "canvas.scx";
  if (!StormLibWriteArchive(path.c_str(), "staredit\\scenario.chk", chk.data(),
                            static_cast<DWORD>(chk.size()))) {
    CanvasLog("canvas: could not write %s", path.c_str());
    return false;
  }
  SStrCopy(pszPath, path.c_str(), cchPath);
  CanvasLog("canvas: %s, %s, %s, %s, %u bytes of map", path.c_str(), gpszTilesetNames_g[nTileset],
            bBattle ? "the battle" : "no units", bPush ? "pushed" : "plain ground", static_cast<unsigned>(chk.size()));
  return true;
}
