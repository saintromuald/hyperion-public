// rclick_morph_rally_test.cpp -- focused checks for rally clicks from morphing larvae.
//
// Include the implementation so the test exercises the same local resolution and received-command
// path as the game. Function/data sections and --gc-sections leave unrelated UI queries unlinked.
#include <stdio.h>
#include <string.h>

#include "../../src117/lang/rclick.cpp"

static bool g_testLimits;
static CUnit *g_testSelection[8];
static int g_testSelectionCount;
static int g_testSelectionAt;
static int g_testSent;
static int g_testSentX;
static int g_testSentY;
static CUnit *g_testSentTarget;
static CUnit *g_testQueryUnits[8];
static int g_testQueryCount;

bool g_hxRulesGame;
unsigned g_hxRuleOptions;
bool HxLimits() { return g_testLimits; }
int g_posShift = 16;
int g_posMaskU = 0xFFFF;

int g_nSelectionMax = 8;
CUnit **g_ppStatUnits = g_testSelection;
CUnit *gpStatUnit;
DWORD g_LocalNationID;
DWORD g_ActiveNationID;
DWORD *gpSquareMap;
WORD gwMapWidth = 1;
BYTE byte_665E68;
BYTE byte_67CC68;
BYTE byte_6AB4C0;
DWORD gPlayback;
DWORD gulUnitTypeFlags[228] = {};
order_id_t gubUnitOrderAttackUnit[228] = {};

rclick_query_fn query_click[7] = {};

static void Select(CUnit **ppUnits, int nCount)
{
  memset(g_testSelection, 0, sizeof g_testSelection);
  for (int i = 0; i < nCount; ++i)
    g_testSelection[i] = ppUnits[i];
  g_testSelectionCount = nCount;
}

void SelectOnlySet(CUnit *) {}
bool SelectOnlyActive() { return false; }
CUnit *SelectSmartCaster(int, int, order_id_t, int) { return NULL; }

CUnit *__fastcall select_get_first()
{
  g_testSelectionAt = 0;
  return g_testSelectionCount != 0 ? g_testSelection[0] : NULL;
}

CUnit *__fastcall select_get_next()
{
  ++g_testSelectionAt;
  return g_testSelectionAt < g_testSelectionCount ? g_testSelection[g_testSelectionAt] : NULL;
}

void __fastcall select_form_calc_offset(select_form_t *, BYTE) {}
CUnit *__fastcall GetRClickTarget(int, int, int) { return NULL; }
int __fastcall OrderAllowed(WORD, int, CUnit *) { return 1; }
int __fastcall get_allowed_err() { return 0; }
void __fastcall AllowedErr(int) {}
void __fastcall sub_46CFD0(LONG *, LONG *) {}
void __fastcall gamesnd_ack(CUnit *) {}
void __fastcall AllocBlackX(int, int) {}
int console_collision(int, int) { return 0; }
BYTE __fastcall GetSelectedOwner() { return 0; }
CUnit *__fastcall get_map_target(int, int) { return NULL; }
CThingy *__fastcall sub_417F70(int, int) { return NULL; }
void PortViewScreenToMap(int nX, int nY, int *pnMapX, int *pnMapY)
{
  *pnMapX = nX;
  *pnMapY = nY;
}
int __fastcall gamesnd_play(int, CUnit *, int, int) { return 0; }
char *__fastcall status_get_str(WORD) { return NULL; }
void __fastcall map_msg_target_self(char *, DWORD) {}

void __fastcall send_net_cmd_RightClick(int nX, int nY, CUnit *pTarget, int, int)
{
  ++g_testSent;
  g_testSentX = nX;
  g_testSentY = nY;
  g_testSentTarget = pTarget;
}

BYTE CUnit::GetRClickAction()
{
  return wType == 0x83 || wType == 0x84 || wType == 0x85 ? 2 : 0;
}

BOOL CUnit::IsRallyer()
{
  return wType == 0x83 || wType == 0x84 || wType == 0x85;
}

BOOL CUnit::IsATransport() { return false; }
BOOL CUnit::CanEnterTransport(CUnit *) { return false; }
BOOL CUnit::ErrorRClick(order_id_t, CUnit *, int, int, WORD *) { return false; }
BOOL CUnit::sub_434130(WORD, CUnit *, int, int, int, WORD *) { return false; }
BOOL CUnit::IsEnemyUnit(CUnit *) { return false; }
BOOL CUnit::IsCircleOfPower() { return false; }
BOOL CUnit::CanTarget(CUnit *, int) { return true; }
BOOL CUnit::sub_420AB0(CUnit *) { return false; }
BOOL CUnit::sub_420B50(CUnit *) { return true; }
BYTE CUnit::GetRace() { return 0; }

void CUnit::SetRallyTarget_xy(spos_t x, spos_t y)
{
  wRallyX = x;
  wRallyY = y;
  pRallyUnit = NULL;
}

void CUnit::SetRallyTarget_CUnit(CUnit *pTarget)
{
  pRallyUnit = pTarget != NULL ? pTarget : this;
}

void CUnit::sub_42A880(int, order_id_t, CUnit *) {}
void CUnit::sub_42A850(int, order_id_t, int, int, int) {}
void CUnit::sub_49C5A0(select_form_t *) {}

static int __fastcall TestRallyQuery(CUnit *pUnit, int nX, int nY, CUnit **ppTarget,
                                     WORD wUnitType)
{
  if (g_testQueryCount < 8)
    g_testQueryUnits[g_testQueryCount] = pUnit;
  ++g_testQueryCount;
  return rclick_query_mover(pUnit, nX, nY, ppTarget, wUnitType);
}

static int Check(bool bOK, const char *pszWhat)
{
  if (bOK)
    return 0;
  fprintf(stderr, "%s\n", pszWhat);
  return 1;
}

static void MakeTownhall(CUnit *pUnit, WORD wType)
{
  pUnit->wType = wType;
  pUnit->dwFlags = 2;
  pUnit->wRallyX = 11;
  pUnit->wRallyY = 12;
  pUnit->pRallyUnit = pUnit;
}

int main()
{
  static_assert((HX_OPT_KNOWN & HX_OPT_MORPH_RALLY) != 0,
                "morph-rally games must be replayable");
  int nFailed = 0;
  CUnit hatchery = {}, lair = {}, hive = {}, other = {}, target = {};
  CUnit larva = {}, secondLarva = {}, egg = {};
  MakeTownhall(&hatchery, 0x83);
  MakeTownhall(&lair, 0x84);
  MakeTownhall(&hive, 0x85);
  other.wType = 0x00;
  larva.wType = 0x23;
  larva.bOrderId = 0x2A;
  larva.pUnion80 = &hatchery;
  secondLarva = larva;
  egg.wType = 0x24;
  egg.dwFlags = 0x10;  // an inert egg must not hide its parent's eligibility
  egg.pUnion80 = &lair;
  target.wType = 0x00;

  g_hxRulesGame = true;
  g_hxRuleOptions = HX_OPT_MORPH_RALLY;
  query_click[0] = rclick_query_none;
  query_click[2] = TestRallyQuery;

  nFailed += Check(rclick_morph_rally_unit(&larva) == &hatchery,
                   "a morphing larva must resolve to its Hatchery");
  egg.pUnion80 = &hive;
  nFailed += Check(rclick_morph_rally_unit(&egg) == &hive,
                   "an egg must resolve to its Hive");
  egg.pUnion80 = &lair;
  nFailed += Check(rclick_unit_action(&egg) == 2 && unit_has_rclick_action(&egg),
                   "the local world/minimap action scans must admit an egg as its Lair");
  nFailed += Check(rclick_unit_action(&larva) == 2,
                   "the local action scan must admit a larva already given morph");

  g_hxRuleOptions = 0;
  nFailed += Check(rclick_morph_rally_unit(&larva) == &larva &&
                       rclick_unit_action(&egg) == 0 && !unit_has_rclick_action(&egg),
                   "an old replay without the option must retain the inert larva/egg rules");
  g_hxRuleOptions = HX_OPT_MORPH_RALLY;
  g_hxRulesGame = false;
  nFailed += Check(rclick_morph_rally_unit(&larva) == &larva,
                   "the option alone must not change classic rules");
  g_hxRulesGame = true;
  larva.bOrderId = 0;
  nFailed += Check(rclick_morph_rally_unit(&larva) == &larva,
                   "an idle larva must not resolve to its parent");
  larva.bOrderId = 0x2A;
  egg.pUnion80 = &other;
  nFailed += Check(rclick_morph_rally_unit(&egg) == &egg,
                   "an egg without a Zerg town hall parent must not gain a rally action");
  egg.pUnion80 = &lair;

  // One ordinary received minimap-style click updates every parent represented in the selection,
  // including duplicates and a separately selected parent, without rewriting the selection.
  CUnit *selection[] = {&larva, &secondLarva, &egg, &lair};
  Select(selection, 4);
  g_testQueryCount = 0;
  sub_47F680(640, 480, NULL, 0xE4, 0);
  nFailed += Check(hatchery.wRallyX == 640 && hatchery.wRallyY == 480 &&
                       hatchery.pRallyUnit == NULL,
                   "both larvae must update their existing Hatchery rally to the clicked point");
  nFailed += Check(lair.wRallyX == 640 && lair.wRallyY == 480 && lair.pRallyUnit == NULL,
                   "an egg and its selected parent must update the same Lair rally");
  nFailed += Check(g_testQueryCount == 4 && g_testQueryUnits[0] == &hatchery &&
                       g_testQueryUnits[1] == &hatchery && g_testQueryUnits[2] == &lair &&
                       g_testQueryUnits[3] == &lair,
                   "the ordinary right-click query must run on each resolved parent");
  nFailed += Check(g_testSelection[0] == &larva && g_testSelection[1] == &secondLarva &&
                       g_testSelection[2] == &egg && g_testSelection[3] == &lair,
                   "resolving a click must not replace selected units");

  // A world click with a unit target follows the same received-command path and sets a unit rally.
  CUnit *onlyEgg[] = {&egg};
  Select(onlyEgg, 1);
  sub_47F680(700, 500, &target, 0xE4, 0);
  nFailed += Check(lair.pRallyUnit == &target,
                   "a targeted world click must set the parent's unit rally");

  // The minimap's own eligibility/action loops must emit the click with no ordinary unit selected.
  gpStatUnit = &egg;
  egg.bPlayer = 0;
  g_LocalNationID = 0;
  g_testSent = 0;
  dlgevt_t event = {};
  event.nX = 100;
  event.nY = 80;
  rclick_action(&event);
  nFailed += Check(g_testSent == 1 && g_testSentX == 100 && g_testSentY == 80,
                   "a morphing egg alone must be eligible for a minimap right-click");

  // The world map's two eligibility/action scans must likewise emit an empty-map click.
  byte_6AB4C0 = 1;
  g_testSent = 0;
  g_testSentTarget = &target;
  gamemap_rclick_default(&event);
  nFailed += Check(g_testSent == 1 && g_testSentTarget == NULL,
                   "a morphing egg alone must be eligible for a world right-click");
  byte_6AB4C0 = 0;

  g_hxRuleOptions = 0;
  MakeTownhall(&lair, 0x84);
  g_testSent = 0;
  rclick_action(&event);
  gamemap_rclick_default(&event);
  sub_47F680(900, 700, NULL, 0xE4, 0);
  nFailed += Check(g_testSent == 0 && lair.wRallyX == 11 && lair.wRallyY == 12 &&
                       lair.pRallyUnit == &lair,
                   "old replay playback must neither emit nor apply the new morph rally click");

  if (nFailed == 0)
    puts("morphing-larva rally right-click checks passed");
  return nFailed != 0;
}
