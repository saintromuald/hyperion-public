// construction_blockers_test.cpp -- focused checks for arrived-worker site clearing.
//
// Include the implementation, as smartcast_test does, and link with unused sections
// discarded. The small stubs below make blocker orders observable without a game.
#include <stdio.h>
#include <string.h>

#include "../../src117/lang/CUnitBuild.cpp"

rect16_t gUnitBoundBox[228] OC_UNALIGNED = {};
point16_t gUnitPlaceboxSize[228] OC_UNALIGNED = {};
BYTE gubUnitPriorityPlane[228] OC_UNALIGNED = {};
upos_t gwMapPixWidth = 4096;
upos_t gwMapPixHeight = 4096;
int g_posShift = 1;
int g_posMaskU = 0x7FFFFFFF;

static CUnit *gTestUnits;
unsigned g_hxRuleOptions = HX_OPT_CONSTRUCTION_CLEAR;
bool g_hxRulesGame = true;
bool HxLimits() { return false; }
static bool gTestAffordable = true;
static bool gTestFindDestination = true;
static bool gTestReachable = true;
static int gTestNormalPlacement;
static int gTestRelaxedPlacement;
static int gTestNormalQueries;
static int gTestRelaxedQueries;
static int gTestCostChecks;
static int gTestMoveOrders;
static int gTestDeburrows;
static int gTestMakeUnburrowed;
static int gTestMineDispatches;
static int gTestBuildErrors;
static CUnit *gTestQueryBlocker;
static CUnit *gTestNoDestinationUnit;
static CUnit *gTestUsedList;
static mpoint_t gTestLastDestination;

static void ResetActions()
{
  gTestMoveOrders = 0;
  gTestDeburrows = 0;
  gTestMakeUnburrowed = 0;
  gTestMineDispatches = 0;
  gTestBuildErrors = 0;
  gTestLastDestination.x = 0;
  gTestLastDestination.y = 0;
}

CUnit *CUnit::Head() { return gTestUnits; }
CUnit *CUnit::GetUsedList() { return gTestUsedList; }
BOOL CUnit::CanMove() { return wType != 0x7F; }
BOOL CUnit::IsDisabled() { return bLockdownTimer != 0; }
void CUnit::SetOrder_xy(order_id_t nOrder, int x, int y)
{
  if (nOrder == 6) {
    ++gTestMoveOrders;
    gTestLastDestination.x = x;
    gTestLastDestination.y = y;
  }
  bOrderId = nOrder;
  wOrderTargetX = PosS(x);
  wOrderTargetY = PosS(y);
}
void CUnit::SetOrder(order_id_t nOrder) { bOrderId = nOrder; }
void CUnit::SetBackgroundOrder(order_id_t nOrder) { (void)nOrder; }
void CUnit::Deburrow()
{
  ++gTestDeburrows;
  bOrderId = 0x76;
}
void CUnit::MakeUnburrowed()
{
  ++gTestMakeUnburrowed;
  dwFlags &= ~(0x810 | UF_Burrowed);
}
void CUnit::DispatchVultureMine() { ++gTestMineDispatches; }
void CSprite::SetAnimation(BYTE, int) {}

BOOL __fastcall FindPlaceForUnit(CUnit *pUnit, mpoint_t *pInput, mpoint_t *pOutput,
                                 mrect_t *, int, int)
{
  if (!gTestFindDestination || pUnit == gTestNoDestinationUnit)
    return FALSE;
  *pOutput = *pInput;
  return TRUE;
}

BOOL __fastcall sai_path_can_reach_target(CUnit *, upos_t, upos_t)
{
  return gTestReachable;
}

extern "C" BOOL __fastcall bldg_get_unit_cost(BYTE, int, BOOL, BOOL)
{
  ++gTestCostChecks;
  return gTestAffordable;
}

void __fastcall gamesnd_build_error(CUnit *, int) { ++gTestBuildErrors; }

extern "C" int mtx_query_place_bldg(CUnit *, BYTE, int, int, int, int, int,
                                    int, int)
{
  ++gTestNormalQueries;
  return gTestNormalPlacement;
}

extern "C" int mtx_query_place_bldg_worker(
    order_id_t nOrder, CUnit *, BYTE, int, int, int, int, int, int, int,
    ConstructionBlockerVisitor pfnVisitor, void *pContext)
{
  ++gTestRelaxedQueries;
  if (nOrder != 0x19 && nOrder != 0x1E && nOrder != 0x1F)
    return gTestNormalPlacement;
  if (gTestRelaxedPlacement == 0 && pfnVisitor != NULL &&
      gTestQueryBlocker != NULL) {
    pfnVisitor(gTestQueryBlocker, pContext);
  }
  return gTestRelaxedPlacement;
}

static int Check(bool bCondition, const char *pszWhat)
{
  if (bCondition)
    return 0;
  fprintf(stderr, "FAILED: %s\n", pszWhat);
  return 1;
}

static void InitialiseUnit(CUnit *pUnit, CSprite *pSprite, WORD wType)
{
  memset(pUnit, 0, sizeof(*pUnit));
  memset(pSprite, 0, sizeof(*pSprite));
  pUnit->wType = wType;
  pUnit->pSprite = pSprite;
  pUnit->bOrderId = 0x17;
  pUnit->lHitPoints = 100;
  pSprite->wPosX = 256;
  pSprite->wPosY = 256;
}

int main()
{
  int nFailed = 0;
  CUnit units[3];
  CSprite sprites[3];
  gTestUnits = units;
  for (int i = 0; i < 3; ++i)
    InitialiseUnit(&units[i], &sprites[i], 1);

  gUnitPlaceboxSize[1].x = 64;
  gUnitPlaceboxSize[1].y = 64;
  for (int i = 0; i < 3; ++i) {
    gUnitBoundBox[i + 1].left = 8;
    gUnitBoundBox[i + 1].right = 8;
    gUnitBoundBox[i + 1].top = 8;
    gUnitBoundBox[i + 1].bottom = 8;
  }
  gUnitBoundBox[0x0D] = gUnitBoundBox[1];
  gUnitBoundBox[0x29] = gUnitBoundBox[1];
  gUnitBoundBox[0x7F] = gUnitBoundBox[1];

  const WorkerSiteBox site = {128, 128, 192, 192};
  mpoint_t inside = {160, 160};
  mpoint_t overlapping = {121, 160};
  mpoint_t outside = {120, 160};
  nFailed += Check(!WorkerDestinationOutside(&units[0], inside, site),
                   "centre inside the footprint is not a clear destination");
  nFailed += Check(!WorkerDestinationOutside(&units[0], overlapping, site),
                   "collision bounds, not just the centre, must leave the footprint");
  nFailed += Check(WorkerDestinationOutside(&units[0], outside, site),
                   "a fully clear destination is accepted");

  // Two blockers get deterministic, non-overlapping reservations. Existing move
  // orders to those reservations are reused rather than restarted every frame.
  ResetActions();
  std::vector<CUnit *> blockers;
  blockers.push_back(&units[0]);
  blockers.push_back(&units[1]);
  std::vector<WorkerBlockerPlan> plans;
  nFailed += Check(WorkerPlanBlockers(&units[2], blockers, site, &plans),
                   "two movable blockers can be planned");
  nFailed += Check(gTestMoveOrders == 2, "both blockers receive move orders");
  nFailed += Check(plans.size() == 2 && plans[0].bHasDestination &&
                       plans[1].bHasDestination,
                   "both plans reserve a destination");
  if (plans.size() == 2) {
    nFailed += Check(plans[0].destination.x != plans[1].destination.x ||
                         plans[0].destination.y != plans[1].destination.y,
                     "multiple blockers do not share one destination");
    nFailed += Check(WorkerDestinationReserved(&units[2], plans,
                                               plans[0].destination),
                     "a reserved destination rejects another blocker's overlap");
    mpoint_t farAway = {800, 800};
    nFailed += Check(!WorkerDestinationReserved(&units[2], plans, farAway),
                     "an unrelated destination remains available");
    mpoint_t first = plans[0].destination;
    mpoint_t second = plans[1].destination;
    for (int i = 0; i < 2; ++i) {
      units[i].bOrderId = 6;
      units[i].wOrderTargetX = plans[i].destination.x;
      units[i].wOrderTargetY = plans[i].destination.y;
    }
    ResetActions();
    std::vector<WorkerBlockerPlan> reused;
    nFailed += Check(WorkerPlanBlockers(&units[2], blockers, site, &reused),
                     "existing clear moves remain plannable");
    nFailed += Check(gTestMoveOrders == 0, "existing clear moves are not restarted");
    nFailed += Check(reused.size() == 2 && reused[0].destination.x == first.x &&
                         reused[0].destination.y == first.y &&
                         reused[1].destination.x == second.x &&
                         reused[1].destination.y == second.y,
                     "reservations are deterministic across retries");

    // A unit that already left the site still owns its in-progress destination.
    units[0].pNext = NULL;
    gTestUsedList = &units[0];
    units[1].bOrderId = 0x17;
    blockers.assign(1, &units[1]);
    plans.clear();
    nFailed += Check(WorkerPlanBlockers(&units[2], blockers, site, &plans) &&
                         plans.size() == 1,
                     "a later blocker can be planned after an earlier one leaves");
    if (plans.size() == 1) {
      std::vector<WorkerBlockerPlan> earlier(1, reused[0]);
      nFailed += Check(!WorkerDestinationReserved(&units[1], earlier,
                                                  plans[0].destination),
                       "an earlier blocker's in-progress destination stays reserved");
    }
    gTestUsedList = NULL;
  }

  // Burrowed units finish Deburrow before receiving their move order.
  ResetActions();
  units[0].wType = 0x29;
  units[0].bOrderId = 0x75;
  units[0].dwFlags = CUnit::UF_Burrowed;
  blockers.assign(1, &units[0]);
  plans.clear();
  nFailed += Check(WorkerPlanBlockers(&units[2], blockers, site, &plans),
                   "a burrowed blocker can be cleared");
  nFailed += Check(gTestDeburrows == 1 && gTestMoveOrders == 0,
                   "Deburrow precedes the move");
  plans.clear();
  nFailed += Check(WorkerPlanBlockers(&units[2], blockers, site, &plans) &&
                       gTestDeburrows == 1 && gTestMoveOrders == 0,
                   "an in-progress Deburrow is awaited");
  units[0].bOrderId = 0x17;
  units[0].dwFlags = 0;
  plans.clear();
  nFailed += Check(WorkerPlanBlockers(&units[2], blockers, site, &plans) &&
                       gTestMoveOrders == 1,
                   "the unburrowed blocker then moves");

  // Mines enter DispatchVultureMine's real detonation state, not Deburrow/move.
  ResetActions();
  InitialiseUnit(&units[0], &sprites[0], 0x0D);
  units[0].bOrderId = 0x14;
  units[0].bOrderState = 3;
  units[0].dwFlags = CUnit::UF_Burrowed;
  plans.clear();
  nFailed += Check(WorkerPlanBlockers(&units[2], blockers, site, &plans),
                   "a Spider Mine can be cleared");
  nFailed += Check(gTestMineDispatches == 1 && gTestMakeUnburrowed == 1 &&
                       gTestDeburrows == 0 && gTestMoveOrders == 0 &&
                       units[0].bOrderState == 6,
                   "mine handling uses unburrow plus the real detonation dispatcher");

  // Permanent blockers fail without partially applying earlier plans.
  ResetActions();
  InitialiseUnit(&units[0], &sprites[0], 1);
  InitialiseUnit(&units[1], &sprites[1], 0x7F);
  blockers.assign(1, &units[1]);
  plans.clear();
  nFailed += Check(!WorkerPlanBlockers(&units[2], blockers, site, &plans) &&
                       gTestMoveOrders == 0,
                   "an immovable blocker fails instead of retrying forever");
  units[1].wType = 1;
  units[1].bLockdownTimer = 1;
  nFailed += Check(!WorkerPlanBlockers(&units[2], blockers, site, &plans) &&
                       gTestMoveOrders == 0,
                   "a disabled blocker fails instead of retrying forever");
  units[1].bLockdownTimer = 0;
  gTestFindDestination = false;
  nFailed += Check(!WorkerPlanBlockers(&units[2], blockers, site, &plans) &&
                       gTestMoveOrders == 0,
                   "no destination fails instead of retrying forever");
  gTestFindDestination = true;
  ResetActions();
  blockers.assign(1, &units[0]);
  blockers.push_back(&units[1]);
  gTestNoDestinationUnit = &units[1];
  plans.clear();
  nFailed += Check(!WorkerPlanBlockers(&units[2], blockers, site, &plans) &&
                       gTestMoveOrders == 0,
                   "a later destination failure applies no earlier move orders");
  gTestNoDestinationUnit = NULL;

  // Affordability is checked before either query or blocker side effects. A
  // clear normal query does no relaxed work; a blocked one waits without paying.
  InitialiseUnit(&units[0], &sprites[0], 1);
  units[0].bPlayer = 0;
  units[0].wBuildQueue[0] = 1;
  units[0].bBuildQueueSlot = 0;
  units[0].wOrderTargetX = 160;
  units[0].wOrderTargetY = 160;
  gTestQueryBlocker = &units[1];
  gTestAffordable = false;
  gTestNormalQueries = 0;
  gTestRelaxedQueries = 0;
  gTestCostChecks = 0;
  ResetActions();
  nFailed += Check(PrepareWorkerConstructionSite(&units[0], 0x1E) ==
                       WORKER_SITE_FAILED &&
                       gTestCostChecks == 1 && gTestNormalQueries == 0 &&
                       gTestRelaxedQueries == 0 && gTestMoveOrders == 0,
                   "an unaffordable attempt has no blocker side effects");
  gTestAffordable = true;
  gTestNormalPlacement = 0;
  nFailed += Check(PrepareWorkerConstructionSite(&units[0], 0x1E) ==
                       WORKER_SITE_READY &&
                       gTestNormalQueries == 1 && gTestRelaxedQueries == 0,
                   "a clear site is ready without blocker work");
  gTestNormalPlacement = 4;
  gTestRelaxedPlacement = 0;
  nFailed += Check(PrepareWorkerConstructionSite(&units[0], 0x1E) ==
                       WORKER_SITE_WAIT && gTestMoveOrders == 1,
                   "a blocked site waits after issuing the deterministic move");

  if (nFailed == 0)
    puts("construction-blocker checks passed");
  return nFailed != 0;
}
