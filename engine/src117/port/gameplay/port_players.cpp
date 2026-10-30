// port_players.cpp -- Hyperion's player slots past 1.17's.
//
// The flag that turns the player tables' extra entries on, and the clearing of them; a
// map's Hyperion players section; the computers past 7 the command line asks for.
#include <windows.h>
#include <string.h>

#include "../../h/playertables.h"
#include "port_eud.h"
#include "../../h/chunk.h"  // ReadChunk
#include "../replays/port_snap.h"  // PORT_SNAP_KEEP
#include "../../h/net_cmds.h"  // PlayerData
#include "../../h/point16.h"
#include "../../h/saveload.h"  // compress, decompress
#include "../render/port_teamcolor.h"  // PortTeamColorSet
#include "port_session.h"  // Session
#include "../../h/glues.h"        // GameLoadImage
#include "../../h/Teams.h"        // IsTeamGame
#include "../front/port_frontscreens.h"  // FrontLog
#include "../../gen/globals117.h" // g_LocalNationID
#include "../platform/port_globals117.h"
#include "../../h/typelimits.h"  // TECH_TYPES_MAX, g_hxTypes

bool g_hxPlayers = false;
int g_hxSlots = PLAYER_SLOTS_MAX;

// HXPL, version 1: a DWORD version, a DWORD count of the slots the game uses (9 to
// PLAYER_SLOTS_MAX), then for each slot from 8 on four bytes, its owner, race, force and
// colour; slot 11's entry is neutral's, and not read.
static PlayerMapSlot g_mapSlots[PLAYER_SLOTS_MAX];
// HXRG, version 1 (stage G): a DWORD version, a DWORD count of slots as HXPL's, then for
// each slot from 8 on four bytes, 1 where the slot has an RGB colour and its red, green and
// blue; slot 11's entry is neutral's, and not read. Presentation only.
static BYTE g_mapRgb[PLAYER_SLOTS_MAX][4];

const PlayerMapSlot *PlayersMapSlot(int nSlot) {
  return &g_mapSlots[nSlot];
}

void PlayersMapBegin() {
  TriggersHxMapBegin();
  EudMapBegin();
  g_hxPlayers = false;
  g_hxSlots = PLAYER_SLOTS_MAX;
  memset(g_mapSlots, 0, sizeof g_mapSlots);
  memset(g_mapRgb, 0, sizeof g_mapRgb);
}

// The game's slots: one past the last the map gives an owner, never fewer than 1.17's 12.
// The map's slots, which a save keeps, give them, so a save need not.
static void SetSlots() {
  const int n = PlayersMapSlotCount();
  g_hxSlots = n < PLAYER_SLOTS_117 ? PLAYER_SLOTS_117 : n;
}

int PlayersMapSlotCount() {
  for (int s = PLAYER_SLOTS_MAX - 1; s >= PLAYER_LOBBY_117; s--)
    if (g_mapSlots[s].bOwner != 0)
      return s + 1;
  return PLAYER_LOBBY_117;
}

int __stdcall PlayersMapHxpl(void *pFile, int nSize, void *) {
  BYTE ab[8 + 4 * PLAYER_SLOTS_MAX];
  if (nSize < 8 || nSize > static_cast<int>(sizeof ab) || !ReadChunk(pFile, ab))
    return FALSE;
  DWORD dwVersion, dwSlots;
  memcpy(&dwVersion, ab, 4);
  memcpy(&dwSlots, ab + 4, 4);
  if (dwVersion != 1 || dwSlots <= PLAYER_LOBBY_117 || dwSlots > PLAYER_SLOTS_MAX ||
      nSize != static_cast<int>(8 + 4 * (dwSlots - PLAYER_LOBBY_117)))
    return FALSE;
  memcpy(&g_mapSlots[PLAYER_LOBBY_117], ab + 8, 4 * (dwSlots - PLAYER_LOBBY_117));
  g_hxPlayers = true;
  SetSlots();
  return TRUE;
}

int __stdcall PlayersMapHxrg(void *pFile, int nSize, void *) {
  BYTE ab[8 + 4 * PLAYER_SLOTS_MAX];
  if (nSize < 8 || nSize > static_cast<int>(sizeof ab) || !ReadChunk(pFile, ab))
    return FALSE;
  DWORD dwVersion, dwSlots;
  memcpy(&dwVersion, ab, 4);
  memcpy(&dwSlots, ab + 4, 4);
  if (dwVersion != 1 || dwSlots <= PLAYER_LOBBY_117 || dwSlots > PLAYER_SLOTS_MAX ||
      nSize != static_cast<int>(8 + 4 * (dwSlots - PLAYER_LOBBY_117)))
    return FALSE;
  memcpy(&g_mapRgb[PLAYER_LOBBY_117], ab + 8, 4 * (dwSlots - PLAYER_LOBBY_117));
  return TRUE;
}

// Where the set-up's computers went (F10).
static int g_nSeats;                      // computers seated
static BYTE g_abSeat[PLAYER_SLOTS_MAX];   // the k-th one's lobby slot
static BYTE g_abFrom[PLAYER_SLOTS_MAX];   // a player's lobby slot before the shuffle

void PlayersSeatsBegin() {
  g_nSeats = 0;
  for (int s = 0; s < PLAYER_SLOTS_MAX; s++)
    g_abFrom[s] = static_cast<BYTE>(s);
}

void PlayersSeatNote(int nSlot) {
  if (g_nSeats < PLAYER_SLOTS_MAX)
    g_abSeat[g_nSeats++] = static_cast<BYTE>(nSlot);
}

void PlayersShuffleNote(int nSlot, int nFrom) {
  if (nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX)
    g_abFrom[nSlot] = static_cast<BYTE>(nFrom);
}

int PlayersSeatedComputer(int nPlayer) {
  // a multiplayer team game seats its players by team (PrepareTeamSlots), which is not followed
  if (nPlayer < 0 || nPlayer >= PLAYER_SLOTS_MAX || IsTeamGame())
    return -1;
  for (int k = 0; k < g_nSeats; k++)
    if (g_abSeat[k] == g_abFrom[nPlayer])
      return k;
  return -1;
}

int PlayersSeatSlot(int k) {
  return k >= 0 && k < g_nSeats ? g_abSeat[k] : -1;
}

int PlayersCameFrom(int nPlayer) {
  return nPlayer >= 0 && nPlayer < PLAYER_SLOTS_MAX ? g_abFrom[nPlayer] : nPlayer;
}

void PlayersChooseColour(int nRow, int nColour) {
  if (nRow < 0 || nRow > PLAYER_SLOTS_MAX)
    return;
  SessionRow &row = Session().aRow[nRow];
  Session().bColours = true;
  row.nColour = nColour >= 0 && nColour < PLAYER_COLOURS ? nColour : -1;
  row.bRgb = false;
}

void PlayersChooseColourRgb(int nRow, unsigned char r, unsigned char g, unsigned char b) {
  if (nRow < 0 || nRow > PLAYER_SLOTS_MAX)
    return;
  PlayersChooseColour(nRow, -1);
  SessionRow &row = Session().aRow[nRow];
  row.bRgb = true;
  row.abRgb[0] = r;
  row.abRgb[1] = g;
  row.abRgb[2] = b;
}

void PlayersChosenColoursApply() {
  const PortSession &session = Session();
  if (session.bColours) {
    for (int p = 0; p < PLAYER_SLOTS_MAX; p++) {
      if (!g_hxPlayers && p >= PLAYER_SLOTS_117)
        break;  // 1.17's colour tables end at its 12 slots
      if (p == PLAYER_NEUTRAL)
        continue;
      const int k = PlayersSeatedComputer(p);
      const int nRow = p == static_cast<int>(g_LocalNationID) ? 0 : k >= 0 ? 1 + k : -1;
      if (nRow < 0)
        continue;
      const SessionRow &row = session.aRow[nRow];
      if (row.bRgb) {
        PortTeamColorSet(p, row.abRgb[0], row.abRgb[1], row.abRgb[2], 1);
        FrontKit::FrontLog("colours: player %d, the set-up's row %d, #%02x%02x%02x\n", p, nRow, row.abRgb[0],
                           row.abRgb[1], row.abRgb[2]);
        continue;
      }
      const int c = row.nColour;
      BYTE abRamp[8], bMinimap;
      if (c < 0 || !PlayersColourRamp(c, abRamp, &bMinimap))
        continue;
      memcpy(gdwTeamColorTbl_g[p], abRamp, 8);
      gbMinimapColor_g[p] = bMinimap;
      FrontKit::FrontLog("colours: player %d, the set-up's row %d, colour %d\n", p, nRow, c);
    }
  }
  g_nSeats = 0;
}

// The computers past 7: as many as the launch asks for (port_session.h), of its races; or,
// when it asks for none (a game from 1.17's menus, whose Play Custom has 8 rows), one in each
// slot past 7 the map opens to a player, of the race the map gives that slot.
void PlayersSetExtraComputers(int nCount, unsigned char bRace) {
  PortSession &session = Session();
  session.nExtraComputers = nCount;
  session.bExtraRace = bRace;
  memset(session.abExtraRace, 0xFF, sizeof session.abExtraRace);
}

void PlayersSetExtraComputerRace(int i, unsigned char bRace) {
  if (i >= 0 && i < PLAYER_SLOTS_MAX)
    Session().abExtraRace[i] = bRace;
}

static bool MapSlotOpen(int nSlot) {
  return nSlot != PLAYER_NEUTRAL && (g_mapSlots[nSlot].bOwner == 5 || g_mapSlots[nSlot].bOwner == 6);
}

int PlayersExtraComputers() {
  if (Session().nExtraComputers >= 0)
    return Session().nExtraComputers;
  int n = 0;
  for (int s = PLAYER_LOBBY_117; s < PLAYER_SLOTS_MAX; s++)
    n += MapSlotOpen(s);
  return n;
}

unsigned char PlayersExtraComputerRace(int i) {
  const PortSession &session = Session();
  if (session.nExtraComputers >= 0)
    return i >= 0 && i < PLAYER_SLOTS_MAX && session.abExtraRace[i] != 0xFF ? session.abExtraRace[i]
                                                                              : session.bExtraRace;
  for (int s = PLAYER_LOBBY_117; s < PLAYER_SLOTS_MAX; s++) {
    if (MapSlotOpen(s) && i-- == 0) {
      const BYTE bRace = g_mapSlots[s].bRace;
      return bRace <= 2 ? bRace : 6;  // zerg, terran or protoss; any other choice random
    }
  }
  return 6;
}

template <class Table>
static void ClearExtra() {
  memset(Table::Extra(), 0, sizeof(*Table::Extra()) * Table::kExtra);
}

static PlayerTechRecord g_playerTech[PLAYER_EXTRA_SLOTS];
PORT_SNAP_KEEP(g_playerTech);  // the trainer's snapshots hold it (port_snap.cpp)

// A slot outside the extra ones writes to a record of its own that nothing reads, as 1.17's
// block took a write past its end.
PlayerTechRecord *PlayerTech(int nSlot) {
  static PlayerTechRecord s_none;
  return nSlot >= PLAYER_EXTRA_FIRST && nSlot < PLAYER_SLOTS_MAX ? &g_playerTech[nSlot - PLAYER_EXTRA_FIRST]
                                                                  : &s_none;
}

// A mod's techs and upgrades: a row an id from 0, so that a snapshot holds the game's count of
// them (g_hxTypes); 1.17's rows here go unused.
struct PlayerModRow {
  unsigned char ab[3][PLAYER_SLOTS_MAX];
};
static PlayerModRow g_modTechs[TECH_TYPES_MAX];
static PlayerModRow g_modUpgrades[UPGRADE_TYPES_MAX];
PORT_SNAP_KEEP_POOL(g_modTechs, g_hxTypes.techs);
PORT_SNAP_KEEP_POOL(g_modUpgrades, g_hxTypes.upgrades);

void PlayerTechInit(const unsigned char *pbMaxLevels) {
  for (PlayerTechRecord &r : g_playerTech) {
    memset(r.abTechAvailable, 1, sizeof r.abTechAvailable);
    memset(r.abTechResearched, 0, sizeof r.abTechResearched);
    memset(r.abUpgradeLevel, 0, sizeof r.abUpgradeLevel);
    memcpy(r.abMaxUpgradeLevel, pbMaxLevels, sizeof r.abMaxUpgradeLevel);
  }
  ClearExtra<PlayerTechInProgressTable>();
  ClearExtra<PlayerUpgradeInProgressTable>();
  for (PlayerModRow &r : g_modTechs) {
    memset(r.ab, 0, sizeof r.ab);
    memset(r.ab[PLAYER_MOD_AVAILABLE], 1, sizeof r.ab[0]);
  }
  memset(g_modUpgrades, 0, sizeof g_modUpgrades);
}

unsigned char &PlayerModTech(int nSlot, unsigned nTech, int nField) {
  static unsigned char s_none;
  return nTech < TECH_TYPES_MAX && static_cast<unsigned>(nSlot) < PLAYER_SLOTS_MAX ? g_modTechs[nTech].ab[nField][nSlot]
                                                                                  : s_none;
}

unsigned char &PlayerModUpgrade(int nSlot, unsigned nUpgrade, int nField) {
  static unsigned char s_none;
  return nUpgrade < UPGRADE_TYPES_MAX && static_cast<unsigned>(nSlot) < PLAYER_SLOTS_MAX
             ? g_modUpgrades[nUpgrade].ab[nField][nSlot]
             : s_none;
}

// The rows from nFirst to nCount, a mod's, to f as one block; none when the game has none.
template <class F>
static bool PlayerModRows(PlayerModRow *pRows, unsigned nFirst, unsigned nCount, F f) {
  return nCount <= nFirst || f(pRows + nFirst, static_cast<int>(sizeof(PlayerModRow) * (nCount - nFirst)));
}

int PlayerModTechSave(void *pFile) {
  const DWORD adwCounts[2] = {static_cast<DWORD>(g_hxTypes.techs), static_cast<DWORD>(g_hxTypes.upgrades)};
  auto put = [pFile](void *pv, int cb) { return compress(pv, cb, pFile) != 0; };
  return pfwrite(adwCounts, sizeof adwCounts, pFile) && PlayerModRows(g_modTechs, TECH_TYPE_MOD_FIRST, adwCounts[0], put)
         && PlayerModRows(g_modUpgrades, UPGRADE_TYPE_MOD_FIRST, adwCounts[1], put);
}

int PlayerModTechLoad(void *pFile) {
  DWORD adwCounts[2];
  if (!pfread(adwCounts, sizeof adwCounts, pFile) || adwCounts[0] > TECH_TYPES_MAX || adwCounts[1] > UPGRADE_TYPES_MAX)
    return 0;
  auto get = [pFile](void *pv, int cb) { return decompress(pv, cb, pFile) != 0; };
  return PlayerModRows(g_modTechs, TECH_TYPE_MOD_FIRST, adwCounts[0], get)
         && PlayerModRows(g_modUpgrades, UPGRADE_TYPE_MOD_FIRST, adwCounts[1], get);
}

// Colours for the slots past 1.17's: a ramp of eight palette entries for the team pixels and
// a minimap entry (tunit.pcx's ramps use their first, the others kMinimap). 0 to 15 are
// tunit.pcx's 16 ramps from the player's copy; 1.17 reads 12, Remastered names 12 to 15 (pale
// green, bluish grey, a second pale yellow, cyan). Remastered's 16 to 22, HD colours with no
// ramp, and Hyperion's from 23 are each the nearest ramp of palette entries the same in every
// tileset (none pink or magenta). The numbers are tools/hxmap/names.py's. A slot from 8 whose
// map names no colour takes kDefaultColors in order, each farthest in OKLab from 1.17's 8 lobby
// colours and those before it, the second pale yellow left out. Presentation only.
static const BYTE kRamps[][8] = {
    {0x71, 0x71, 0x6C, 0x6C, 0x64, 0x64, 0x5F, 0x58},  // 16 coral (Remastered's pink)
    {0x6D, 0x68, 0x68, 0x65, 0x61, 0x60, 0x5C, 0x10},  // 17 olive (Remastered's olive)
    {0x75, 0x75, 0x75, 0xBA, 0xB9, 0xB8, 0xB8, 0xB6},  // 18 lime (Remastered's lime)
    {0x2D, 0x79, 0x79, 0x79, 0x29, 0x29, 0x76, 0x28},  // 19 navy (Remastered's navy)
    {0xA3, 0xA3, 0xA1, 0xA1, 0xA1, 0x78, 0x0F, 0x28},  // 20 plum (Remastered's magenta)
    {0x96, 0x94, 0x49, 0x49, 0x8F, 0x8E, 0x8D, 0x8B},  // 21 grey (Remastered's grey)
    {0x2E, 0x2E, 0x2C, 0x2B, 0x77, 0xC0, 0xC0, 0x8A},  // 22 slate (Remastered's black)
    {0x35, 0x35, 0x7B, 0x7B, 0x7B, 0x2A, 0x2A, 0x29},  // 23 electric_blue
    {0xB7, 0xB7, 0xB6, 0xB6, 0xB6, 0xB5, 0xB5, 0x8A},  // 24 forest
    {0x3F, 0x3F, 0x52, 0x4F, 0x4B, 0x4B, 0x48, 0x44},  // 25 cream
    {0x39, 0x38, 0x38, 0x37, 0x32, 0x2F, 0x2C, 0x43},  // 26 sky
    {0x21, 0x84, 0x83, 0x81, 0x65, 0x61, 0x60, 0x59},  // 27 yellow_hx
    {0x34, 0x34, 0x34, 0x34, 0x9D, 0x2C, 0x45, 0x42},  // 28 teal_hx
    {0x62, 0x62, 0x5E, 0x5E, 0xAB, 0xAA, 0xAA, 0xA8},  // 29 maroon
    {0x4B, 0x4B, 0x4B, 0x48, 0x46, 0x44, 0x8D, 0x41},  // 30 moss
    {0x7F, 0xC6, 0xC5, 0xC4, 0xC2, 0x2E, 0xC1, 0x77},  // 31 cornflower
    {0x74, 0x74, 0x6D, 0x6D, 0x68, 0x65, 0x61, 0x5C},  // 32 yellow_2
    {0x3D, 0xA7, 0xA7, 0xA6, 0x6D, 0x6A, 0x65, 0x5C},  // 33 pale_yellow_hx
    {0xBC, 0xBC, 0x26, 0x25, 0x23, 0x23, 0xB6, 0xB5},  // 34 green_hx
    {0xB1, 0x1B, 0x1B, 0x67, 0xAD, 0xAC, 0xAC, 0x58},  // 35 orange_hx
    {0x6F, 0xB0, 0xAE, 0x17, 0x62, 0x5E, 0x5E, 0xAA},  // 36 red_hx
    {0xB4, 0x1F, 0x1F, 0x68, 0x18, 0x16, 0x60, 0x59},  // 37 amber
    {0x25, 0x23, 0x23, 0x23, 0x22, 0x22, 0xB5, 0xB5},  // 38 green_2
    {0x6C, 0x6C, 0x64, 0x64, 0x5F, 0x5F, 0x58, 0xA9},  // 39 red_2
    {0x52, 0x4F, 0x4F, 0x4B, 0x24, 0x49, 0x90, 0x8D},  // 40 dusty_mint
    {0x3B, 0x3A, 0x3A, 0x38, 0x4B, 0x4A, 0x49, 0x8E},  // 41 pale_dusty_aqua
    {0x65, 0x61, 0x61, 0x60, 0x5C, 0x59, 0x59, 0x41},  // 42 yellow_3
    {0xC7, 0x36, 0x36, 0x32, 0x2F, 0x2E, 0x2C, 0x77},  // 43 blue_hx
    {0x66, 0x66, 0x66, 0x8F, 0x5B, 0x5B, 0x56, 0xA8},  // 44 mauve
    {0xB8, 0xB8, 0xB7, 0xB7, 0xB6, 0xB6, 0xB6, 0xB5},  // 45 deep_green
    {0x33, 0x33, 0x33, 0x2D, 0x79, 0x79, 0x78, 0x76},  // 46 indigo
    {0x73, 0x70, 0x70, 0x70, 0x69, 0x14, 0x14, 0x59},  // 47 orange_2
    {0xC4, 0xC3, 0xC3, 0xC2, 0xC1, 0x2B, 0x2B, 0xC0},  // 48 blue_2
    {0x1B, 0x67, 0xAD, 0xAD, 0x13, 0xAC, 0x11, 0xA9},  // 49 orange_3
    {0x17, 0x62, 0x62, 0x62, 0x5E, 0xAB, 0xAA, 0xA9},  // 50 deep_red
    {0x6E, 0x69, 0x19, 0x19, 0x63, 0x5D, 0x5B, 0x56},  // 51 orange_4
    {0x83, 0x82, 0x82, 0x93, 0x93, 0x60, 0x5D, 0x10},  // 52 yellow_4
    {0x32, 0x30, 0x30, 0x2F, 0x2C, 0x2B, 0x2B, 0xC0},  // 53 blue_3
    {0x72, 0x72, 0x6E, 0x6B, 0x63, 0x63, 0x46, 0x57},  // 54 orange_5
    {0x24, 0x24, 0x22, 0x22, 0x22, 0xB5, 0xB5, 0x40},  // 55 dusty_green
    {0x1C, 0x1A, 0x1A, 0x15, 0x12, 0x12, 0x11, 0x57},  // 56 orange_6
    {0xFF, 0x55, 0x53, 0x51, 0x4E, 0x4A, 0x49, 0x8E},  // 57 white_hx
    {0x60, 0x5C, 0x5C, 0x5C, 0x10, 0x57, 0x56, 0x8A},  // 58 deep_amber
    {0x97, 0x97, 0x95, 0x92, 0x91, 0x45, 0x43, 0x42},  // 59 dusty_indigo
    {0x86, 0x86, 0x98, 0x98, 0x94, 0x94, 0x8F, 0x5B},  // 60 dusty_orange
    {0x98, 0x98, 0x94, 0x94, 0x48, 0x8F, 0x5B, 0x41},  // 61 dusty_orange_2
    {0x64, 0x64, 0x5F, 0x5F, 0x58, 0x58, 0xA9, 0xA8},  // 62 deep_red_2
    {0x3E, 0x85, 0x83, 0x82, 0x93, 0x93, 0x48, 0x44},  // 63 yellow_5
    {0xA6, 0xA6, 0x81, 0x6A, 0x65, 0x61, 0x60, 0x10},  // 64 yellow_6
};
// The minimap's entry of each colour from 16 on: one of its ramp's entries, the one
// farthest from every other colour's among those no other colour has and no darker than
// 1.17's darkest dot (brown's, OKLab lightness 0.39; the lightest where none is), so no two
// colours share a dot and none is lost on a dark map (1.17's tunit.pcx has two identical
// ramps, 9 and 14, which do share one).
static const BYTE kMinimap[] = {
    0x71, 0x6D, 0x75, 0x2D, 0xA3, 0x49, 0x2E, 0x35, 0xB7, 0x4F,
    0x39, 0x61, 0x9D, 0x62, 0x4B, 0xC6, 0x74, 0x6A, 0xBC, 0xB1,
    0xAE, 0xB4, 0x25, 0x6C, 0x52, 0x3B, 0x65, 0xC7, 0x66, 0xB8,
    0x79, 0x69, 0xC3, 0xAD, 0x17, 0x6E, 0x83, 0x32, 0x72, 0x24,
    0x1C, 0xFF, 0x60, 0x95, 0x94, 0x98, 0x64, 0x3E, 0xA6,
};

enum { TUNIT_RAMPS = 16, RAMPS = sizeof kRamps / sizeof kRamps[0], COLORS_MAX = TUNIT_RAMPS + RAMPS };
static_assert(sizeof kMinimap == RAMPS, "a minimap entry for each ramp");
static_assert(COLORS_MAX == PLAYER_COLOURS, "the set-up offers every colour");
static const BYTE kDefaultColors[] = {
    18, 31, 15, 45, 48, 33, 20, 52, 32, 55, 28, 8, 57, 23, 47, 56, 36, 61, 24, 10, 46,
    22, 34, 44, 16, 54, 13, 37, 29, 25, 38, 21, 12, 11, 26, 39, 42, 40, 64, 63, 17, 41,
    60, 59, 58, 35, 49, 30, 50, 9, 51, 62, 19, 27, 43, 53
};

int PlayersDefaultColour(int i) {
  return kDefaultColors[(i < 0 ? 0 : i) % sizeof kDefaultColors];
}

// 1.17's colours as tunit.pcx and tminimap.pcx hold them, before any map moved them; tminimap.pcx
// has 1.17's twelve, and 12 to 15 take their ramp's first entry, as the slots past 1.17's do
bool PlayersColourRamp(int nColour, unsigned char *pRamp, unsigned char *pMinimap) {
  static BYTE s_abTunit[TUNIT_RAMPS][8], s_abMinimap[12];
  static bool s_bRead;
  if (nColour < 0 || nColour >= PLAYER_COLOURS)
    return false;
  if (!s_bRead) {
    GameLoadImage((intptr_t)"game\\tunit.pcx", 0, (intptr_t)s_abTunit, sizeof s_abTunit, 0, 0, 0);
    GameLoadImage((intptr_t)"game\\tminimap.pcx", 0, (intptr_t)s_abMinimap, sizeof s_abMinimap, 0, 0, 0);
    s_bRead = true;
  }
  const BYTE *pFrom = nColour < TUNIT_RAMPS ? s_abTunit[nColour] : kRamps[nColour - TUNIT_RAMPS];
  memcpy(pRamp, pFrom, 8);
  *pMinimap = nColour < 12 ? s_abMinimap[nColour] : nColour < TUNIT_RAMPS ? pFrom[0] : kMinimap[nColour - TUNIT_RAMPS];
  return true;
}

void PlayersColorsApply() {
  if (!g_hxPlayers)
    return;
  BYTE abTunit[TUNIT_RAMPS][8];
  GameLoadImage((intptr_t)"game\\tunit.pcx", 0, (intptr_t)abTunit, sizeof abTunit, 0, 0, 0);
  for (int nSlot = PLAYER_LOBBY_117; nSlot < PLAYER_SLOTS_MAX; nSlot++) {
    if (nSlot == PLAYER_NEUTRAL)
      continue;
    int c = g_mapSlots[nSlot].bColor;
    if (c == 0xFF) {
      // slots 8 to 10 too: 1.17's pale yellow for slot 9 is all but its yellow for slot 7
      c = PlayersDefaultColour(nSlot - PLAYER_LOBBY_117 - (nSlot > PLAYER_NEUTRAL));
    }
    const BYTE *pRamp = c < TUNIT_RAMPS ? abTunit[c] : kRamps[(c - TUNIT_RAMPS) % RAMPS];  // past the last, around again
    memcpy(gdwTeamColorTbl_g[nSlot], pRamp, 8);
    gbMinimapColor_g[nSlot] = c < TUNIT_RAMPS ? pRamp[0] : kMinimap[(c - TUNIT_RAMPS) % RAMPS];
    // an RGB colour from HXRG over it, and the nearest ramp for the 8-bit drawing
    if (g_mapRgb[nSlot][0] != 0)
      PortTeamColorSet(nSlot, g_mapRgb[nSlot][1], g_mapRgb[nSlot][2], g_mapRgb[nSlot][3], TRUE);
  }
}

// A save of a Hyperion game (port_hxsave.cpp). Its set-up, read before the game starts:
// the map's slots and the player records past 1.17's twelve; reading it turns the extra
// entries on. A save without it is 1.17's, and turns them off, whatever the last map was.
typedef PlayerTable<PlayerData, PlayerTagData> PlayerDataTable;

int PlayersSaveSetUp(void *pFile) {
  return compress(g_mapSlots, sizeof g_mapSlots, pFile)
      && compress(PlayerDataTable::Extra(), sizeof(PlayerData) * PlayerDataTable::kExtraCount, pFile);
}

int PlayersLoadSetUp(void *pFile) {
  g_hxPlayers = true;
  if (!decompress(g_mapSlots, sizeof g_mapSlots, pFile)
      || !decompress(PlayerDataTable::Extra(), sizeof(PlayerData) * PlayerDataTable::kExtraCount, pFile))
    return 0;
  SetSlots();
  return 1;
}

void PlayersLoadNone() {
  PlayersMapBegin();
}

// The extra entries of every table the simulation keeps and 1.17's save keeps the rest of
// (its GAME section), and the tech of the slots past 11. Tables of pointers (the unit
// lists, the AI's) and the trigger lists are saved where 1.17 saves theirs, and the
// trigger counts with the trigger state (port_hxsave.cpp); the victory codes and the
// delayed actions, which a game sets again, are not saved.
typedef PlayerTable<point16_t, PlayerTagStartLocations, 8> PlayerStartLocationsTable;
typedef PlayerTable<unsigned char, PlayerTagParticipants, 8> PlayerParticipantsTable;
#define PLAYER_SAVED_TABLES(X)                                                                        \
  X(PlayerMineralsTable) X(PlayerGasTable) X(PlayerGasGatheredTable) X(PlayerMineralsGatheredTable)   \
  X(PlayerGameCountsGrid) X(PlayerUnitCountsGrid) X(PlayerAlliancesMatrix) X(PlayerSharedVisionTable) \
  X(PlayerUnitsAvailableTable) X(PlayerTechInProgressTable) X(PlayerUpgradeInProgressTable)           \
  X(PlayerVictoryStateTable) X(PlayerStartLocationsTable) X(PlayerParticipantsTable)                  \
  X(PlayerTeamColorTable) X(PlayerMinimapColorTable) X(PlayerObjectivesTable)

#define PLAYER_SAVE_ONE(T) && compress(T::Extra(), static_cast<int>(sizeof(*T::Extra()) * T::kExtraCount), pFile)
#define PLAYER_LOAD_ONE(T) && decompress(T::Extra(), static_cast<int>(sizeof(*T::Extra()) * T::kExtraCount), pFile)

int PlayerTablesSave(void *pFile) {
  return compress(g_playerTech, sizeof g_playerTech, pFile) PLAYER_SAVED_TABLES(PLAYER_SAVE_ONE);
}

int PlayerTablesLoad(void *pFile) {
  return decompress(g_playerTech, sizeof g_playerTech, pFile) PLAYER_SAVED_TABLES(PLAYER_LOAD_ONE);
}
