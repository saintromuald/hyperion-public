// port_observe.cpp -- oc_observe, the game state the replay harness's observer
// logs, handed over by the engine (port_observe.h).
// The observer reads the oracle's from 1.17's addresses; reading the engine's
// here, by name, keeps its log the same when the engine's layout is not.
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "CUnit.h"
#include "gamemap.h"      // gamemap_center_on_mtx
#include "dlg_ctrl.h"     // ctrl_evt
#include "saveload.h"     // saveload_write_save_data
#include "oc_data.h"
#include "port_observe.h"
#include "../tools/port_bench.h"  // PortBenchWanted, PortBenchSeed
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../render/port_present.h"  // PortPresenterReport
#include "../platform/port_standalone.h"  // PortDataAddress, PortOriginalAddress
#include "../../h/poollimits.h"
#include "playerlimits.h"   // PlayerSlots
#include "port_hxreplay.h"  // ReplayHxSync
#include "port_synchash.h"  // SyncHashCompute, SyncSlotRead
#include "../gameplay/port_trainer.h"   // PortTrainerSeed
#include "../../h/playertables.h"  // gPlayerMinerals, gPlayerGas
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

// The game's globals by the registry's names (re/globals117.json), bound by
// abs_globals.py --bind-only.
extern DWORD sgdwCurFrame_g;                // 0x665D58
extern DWORD sgdwCurNextTick_g;             // 0x665D5C
extern ctrl_t *sgpGlueDlg;                  // 0x66A318
extern HWND ghWnd;                          // 0x66FD28
// Two fields of the replay's packed header, unaligned and unnamed in the registry, which
// replay.cpp reads by address as well.
#define oc_dwReplayTotalFrames (*reinterpret_cast<unsigned int *>(OC_VA(0x67CCA1u)))
#define sgdwReplaySeed_g (*reinterpret_cast<int *>(OC_VA(0x67CCA8u)))

#define UNIT_COUNT g_pools.units  // the unit table's size in the game being played

static void __cdecl ObserveState(OcObserveState *pState)
{
  memset(pState, 0, sizeof *pState);
  pState->dwGameMode = gwGameMode;
  pState->nGlueScreen = static_cast<int32_t>(glGluesMode);
  pState->dwFrame = oc_dwReplayFrame;
  pState->dwPlayback = gPlayback;
  pState->dwReplayFrames = oc_dwReplayTotalFrames;
  pState->dwReplaySeed = static_cast<uint32_t>(sgdwReplaySeed_g);
  pState->dwGameSpeed = gOptions;
  pState->dwMsPerFrame = gOptions < 7 ? sgTicksPerFrame_g[gOptions] : 0;
  pState->dwRandState = sgNetRandState_g;
  pState->dwRandCount = static_cast<uint32_t>(sgNetRandCount_g);
  for (int i = 0; i < 12; ++i) {
    pState->anMinerals[i] = gPlayerMinerals[i];
    pState->anGas[i] = gPlayerGas[i];
  }
}

// A unit's slot in the table, from 1; 0 for none or for a pointer outside it.
static uint32_t ObserveSlot(const CUnit *pUnit)
{
  const CUnit *pHead = CUnit::Head();
  if (pUnit < pHead || pUnit >= pHead + UNIT_COUNT)
    return 0;
  return static_cast<uint32_t>(pUnit - pHead) + 1;
}

static uint32_t __cdecl ObserveUnits(OcObserveUnit *pUnits, uint32_t nMax, uint32_t *pbBroken)
{
  uint32_t n = 0;
  *pbBroken = 0;
  for (const CUnit *p = CUnit::GetUsedList(); p != NULL && n < nMax; p = p->pNext) {
    const uint32_t dwSlot = ObserveSlot(p);
    if (dwSlot == 0) {
      *pbBroken = 1;
      break;
    }
    OcObserveUnit &u = pUnits[n++];
    u.dwSlot = dwSlot;
    u.dwTypePlayer = p->wType | static_cast<uint32_t>(p->bPlayer) << 16;
    u.dwHitPoints = static_cast<uint32_t>(p->lHitPoints);
    u.dwShields = p->lShields;
    u.dwEnergy = p->wEnergy;
    u.dwPosition = p->wPositionX | static_cast<uint32_t>(p->wPositionY) << 16;
    u.dwOrder = p->bOrderId | static_cast<uint32_t>(p->bOrderState) << 8;
    u.dwOrderTarget = ObserveSlot(p->pOrderTarget);
    u.dwFlags = p->dwFlags;
  }
  return n;
}

static const uint8_t *__cdecl ObservePalette(void) { return sgGamePalette_g; }

static void __cdecl ObserveControl(uint32_t bKeepActive, uint32_t dwUpdates)
{
  if (bKeepActive)
    sgbWindowActive_g = 1;
  if (dwUpdates)
    sgdwUpdatesPerFrame_g = dwUpdates;
}

static void __cdecl ObserveHoldCursor(uint32_t dwNextTick)
{
  sgdwCurFrame_g = 0;
  sgdwCurNextTick_g = dwNextTick;
}

static void __cdecl ObserveCentre(int32_t nTileX, int32_t nTileY) { gamemap_center_on_mtx(nTileX, nTileY); }

// A push button's end (ctrl_flc_evn): its index published as the dialog's result,
// then the dialog's event 2.
static void __cdecl ObserveEndDialog(int32_t nResult)
{
  ctrl_t *pDlg = sgpGlueDlg;
  dlgevt_t evt;

  if (pDlg == NULL)
    return;
  memset(&evt, 0, sizeof evt);
  evt.dwId = 2;
  evt.wTag = 0x0E;
  gnDlgReturnVal = nResult;
  ctrl_evt(pDlg, &evt);
}

// Where the engine keeps what 1.17 kept at 0x4EB000.
static uintptr_t __cdecl ObserveDataBase(void) { return OC_VA(OC_DATA_VA); }

static void *__cdecl ObserveWindow(void) { return ghWnd; }
static const void *__cdecl ObserveGlueDialog(void) { return sgpGlueDlg; }
static uintptr_t __cdecl ObserveAddress(uint32_t va) { return PortDataAddress(va); }
static uint32_t __cdecl ObserveOriginal(uintptr_t p) { return PortOriginalAddress(p); }
static int32_t __cdecl ObserveSave(const char *pszName, uint32_t dwTimestamp)
{
  return saveload_write_save_data(dwTimestamp, pszName, 0) ? 1 : 0;
}

static bool s_bSeedPinned;
static int s_nPinnedSeed;
static void __cdecl ObservePinSeed(uint32_t dwSeed)
{
  s_nPinnedSeed = static_cast<int>(dwSeed);
  s_bSeedPinned = true;
}

static void __cdecl ObservePresenter(char *pszName, uint32_t nCap, uint32_t *pnChecked, uint32_t *pnMismatched)
{
  ULONG nChecked = 0;
  ULONG nMismatched = 0;
  PortPresenterReport(pszName, nCap, &nChecked, &nMismatched);
  *pnChecked = nChecked;
  *pnMismatched = nMismatched;
}

// new_game's seed: the clock's, or the one the harness pinned, or --seed's.
int __cdecl PortObserveSeed(int nClock) {
  if (s_bSeedPinned)
    return s_nPinnedSeed;
  int nTrainer;
  if (PortTrainerSeed(&nTrainer))
    return nTrainer;  // a saved session's game, made again
  const char *pszSeed = PortCommandOptions()->szSeed;
  if (!pszSeed[0] && PortBenchWanted())
    return PortBenchSeed();  // a benchmark's own, so two builds play the same game
  return pszSeed[0] ? static_cast<int>(strtoul(pszSeed, NULL, 0)) : nClock;
}

static uint32_t __cdecl ObserveSlots(void) { return static_cast<uint32_t>(PlayerSlots()); }

static_assert(sizeof(SyncSlot) == sizeof(OcObserveSlot), "OcObserveSlot is SyncSlot's layout");
static void __cdecl ObserveSlotNumbers(uint32_t nSlot, OcObserveSlot *pSlot)
{
  SyncSlot s;
  SyncSlotRead(static_cast<int>(nSlot), &s);
  memcpy(pSlot, &s, sizeof s);
}

static uint32_t __cdecl ObserveUnitCap(void) { return UNIT_COUNT; }

static void __cdecl ObserveSync(OcObserveSync *pSync)
{
  const ReplayHxSyncStatus *p = ReplayHxSync();
  pSync->dwState = static_cast<uint32_t>(p->nState);
  pSync->dwRecorded = p->dwRecorded;
  pSync->dwChecked = p->dwChecked;
  pSync->dwFirstBad = p->dwFirstBad;
  pSync->dwBadDomains = p->dwBadDomains;
  memcpy(pSync->szReason, p->szReason, sizeof pSync->szReason);
}

static_assert(sizeof(SyncHash) == sizeof(OcObserveHash), "OcObserveHash is SyncHash's layout");
static void __cdecl ObserveHash(OcObserveHash *pHash)
{
  SyncHash h;
  SyncHashCompute(&h);
  memcpy(pHash, &h, sizeof h);
}

static void(__cdecl *g_pfnFrameHook)(void);

static void __cdecl ObserveSetFrameHook(void(__cdecl *pfnFrame)(void))
{
  g_pfnFrameHook = pfnFrame;
}

// After a Hyperion game's frame and its check (port_hxreplay.cpp, ReplayHxFrame).
void ObserveFrameDone()
{
  if (g_pfnFrameHook)
    g_pfnFrameHook();
}

static const OcObserveApi s_api = {OC_OBSERVE_VERSION, ObserveState,      ObserveUnits,   ObservePalette,
                                   ObserveControl,     ObserveHoldCursor, ObserveCentre,  ObserveEndDialog,
                                   ObserveDataBase,    ObserveWindow,     ObserveGlueDialog, ObserveAddress,
                                   ObserveOriginal,    ObserveSave,       ObservePinSeed,
                                   ObservePresenter,   ObserveSlots,      ObserveSlotNumbers, ObserveUnitCap,
                                   ObserveSync,        ObserveHash,       ObserveSetFrameHook};

extern "C" const OcObserveApi *__stdcall oc_observe(void) { return &s_api; }
