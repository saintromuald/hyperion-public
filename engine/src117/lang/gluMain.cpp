// gluMain.cpp -- the main menu: the campaign-presence predicates the menu grays
// its buttons with, the game-mode chooser, the StarEdit launcher, and the
// screen's activate and event handlers. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>            // memset, for launch_staredit's STARTUPINFO

#include "../h/gluMain.h"
#include "../h/SystemSurvey.h"    // SetSurveyProduct
#include "../h/Storm.h"     // SFileGetInstallPath / SStrCopy / SStrPack / SMem*
#include "../h/dialog.h"    // ctrl_t, dlgevt_t, PFN_CTRL_EVT
#include "../h/dlg_ctrl.h"  // ctrl_find_ndx / ctrl_gray / ctrl_ungray / ctrl_dlg
#include "../h/dlg_dlg.h"   // dlg_load / dlg_run_modal / dlg_set_font
#include "../h/dlg_draw.h"  // font_strlen / font_set
#include "../h/gamedata.h"  // MemFileLoad / alloc_load_bmp
#include "../h/gametype.h"  // GameHeader / TGAMEPARAMS / gametype_load_ums
#include "../h/gds_local.h" // psnprintf
#include "../h/glues.h"     // get_cd_archive / glues_get_str / glues_clear_all
#include "../h/gluPopup.h"  // do_select_gamemode_dlg / do_pok_dlg / do_pok_cancel_dlg
#include "../h/gluRdy.h"    // stop_music / snd_play_music / glues_transition_out
#include "../h/grid.h"      // IsBroodwar
#include "../h/init.h"      // FileExists / FindCDArchive / close_cd_archive
#include "../h/net_cmds.h"  // error_get_str
#include "../h/netdesc.h"   // RetrieveKey / cleanup_key
#include "../h/snd.h"       // snd_end_streamed / snd_music_enabled
#include "../h/victory.h"   // snd_end_streamed
#include "../gen/globals117.h"
#include "../../hyperion/version.h"  // Hyperion: HYPERION_VERSION_TEXT -- gluMain_dlg_activate
#include "../port/front/port_front.h"  // Hyperion
#include "../port/platform/port_paths.h"  // Hyperion: PortCdOptional

// The globals this TU reaches, spelled both ways, one guard per name.

// gnDlgReturnVal @0x6CE6DC, registered but not yet emitted.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x536720, the expansion gate. Only ever read and written as a byte; the
// spelling four other TUs already carry.
#ifdef OC_DLL_MODE
  #ifndef gbIsExpansion
  #define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
  #endif
#else
extern BYTE gbIsExpansion;
#endif

// The `.rdata` strings this TU hands on as addresses. Each is an object whose
// address is taken, not a pointer variable: a pointer would cost a load where
// the oracle has a 5-byte `mov reg, imm32`.
#ifdef OC_DLL_MODE
  #ifndef sgCmpgnBinPath
  #define sgCmpgnBinPath (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDB6Cu)))
  #endif
  #ifndef sgExpCmpgnBinPath
  #define sgExpCmpgnBinPath (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDB80u)))
  #endif
  #ifndef sgStarProductCode
  #define sgStarProductCode (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDB94u)))
  #endif
  #ifndef sgSexpProductCode
  #define sgSexpProductCode (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDB9Cu)))
  #endif
  #ifndef sgStarEditExe
  #define sgStarEditExe (*reinterpret_cast<char (*)[1]>(OC_VA(0x4EDBA4u)))
  #endif
  #ifndef gszVersionNumber
  #define gszVersionNumber (*reinterpret_cast<char (*)[0x104]>(OC_VA(0x508620u)))
  #endif
#else
extern char sgCmpgnBinPath[1];
extern char sgExpCmpgnBinPath[1];
extern char sgStarProductCode[1];
extern char sgSexpProductCode[1];
extern char sgStarEditExe[1];
extern char gszVersionNumber[0x104];
#endif
#ifdef OC_DLL_MODE
  #ifndef gGameHeader
  #define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
  #endif
#else
extern GameHeader gGameHeader;
#endif

// 0x5081D0, the main menu's 11-entry handler table; 11 from the case map's
// own span and the table's end at 0x5081FC.
#ifdef OC_DLL_MODE
  #ifndef sMainMenuEvtFcnTbl_g
  #define sMainMenuEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5081D0u)))
  #endif
#else
extern PFN_CTRL_EVT sMainMenuEvtFcnTbl_g[11];
#endif

// dlgevt_t +0x08 is a per-event-id payload; the 0x0F arm reads its low word.
#define evt_key(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped08[0]))

// ---------------------------------------------------------------------------
// The bodies, in oracle VA order.

// glumain_file_exists leaves gnDlgReturnVal clobbered, so the caller saves and
// restores it around the call. EAX carries the callee's result across the
// restore untouched, so the save is a plain local.
int HasCampaign() {
  LONG nSavedReturnVal = gnDlgReturnVal;
  int bExists = glumain_file_exists(0xA7, reinterpret_cast<intptr_t>(sgCmpgnBinPath));
  gnDlgReturnVal = nSavedReturnVal;
  return bExists;
}

// Whichever campaign the expansion gate selects. Each arm carries its own
// `return 0`, which is what puts the merged FileExists tail after both of them
// rather than inside the first; a single shared `return 0` at the end merges
// the tail into the expansion arm instead.
// Hyperion: where the CD archive may be missing and is (PortCdOptional), as with a
// StarCraft: Remastered install alone, whose files have both campaigns, a campaign's layout is
// looked for in the game's files and not on the CD. HasEitherCampaign,
// HasExpansionCampaign and glumain_file_exists (which HasCampaign calls) take this first.
static bool CampaignWithoutCd(intptr_t nPath, BOOL *pbHas)
{
  if (!PortCdOptional() || get_cd_archive() != 0)
    return false;
  *pbHas = FileExists(nPath, 0) != 0;
  return true;
}

BOOL HasEitherCampaign() {
  BOOL bHas;
  if (CampaignWithoutCd(reinterpret_cast<intptr_t>(gbIsExpansion != 0 ? sgExpCmpgnBinPath : sgCmpgnBinPath), &bHas))
    return bHas;
  if (gbIsExpansion != 0) {
    if (get_cd_archive() &&
        FileExists(reinterpret_cast<intptr_t>(sgExpCmpgnBinPath), get_cd_archive()))
      return 1;
    return 0;
  }

  if (get_cd_archive() &&
      FileExists(reinterpret_cast<intptr_t>(sgCmpgnBinPath), get_cd_archive()))
    return 1;
  return 0;
}

// get_cd_archive is called twice rather than held in a local: the oracle tests
// the first result, branches, then calls again for the handle it passes on.
// FileExists takes the path in ECX and the archive handle in EDX, which is the
// opposite of the order the name suggests -- `mov edx, eax / mov ecx, 4EDB80h`.
BOOL HasExpansionCampaign() {
  BOOL bHas;
  if (CampaignWithoutCd(reinterpret_cast<intptr_t>(sgExpCmpgnBinPath), &bHas))
    return bHas;
  if (get_cd_archive() &&
      FileExists(reinterpret_cast<intptr_t>(sgExpCmpgnBinPath), get_cd_archive()))
    return 1;
  return 0;
}

// The dialog loader this screen hands dlg_load. Only the first stack word is
// read; the other two and both register arguments are forwarded untouched.
// Hyperion: returns the block, which dlg_load reads as the dialog; the original
// left it in EAX from MemFileLoad's tail, which a 64-bit build does not keep
//.
void *glumain_load_fn(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3, int nArg4) {
  return MemFileLoad(reinterpret_cast<const char *>(nArg0), nArg1,
              reinterpret_cast<DWORD *>(nArg2), 1, 0, "gluMain.cpp", 0x23D);
}

// Run the game-mode chooser and confirm the chosen product's campaign file is
// present. nArg0 non-zero skips the original-campaign check.
int do_gamemode_select_dlg(int nArg0) {
  LONG nSavedReturnVal = gnDlgReturnVal;
  int bOk = 0;

  switch (do_select_gamemode_dlg()) {
    case 6:
      SetSurveyProduct(sgStarProductCode);
      if (nArg0 == 0 &&
          !glumain_file_exists(0xA7, reinterpret_cast<intptr_t>(sgCmpgnBinPath)))
        break;
      gbIsExpansion = 0;
      bOk = 1;
      break;
    case 7:
      SetSurveyProduct(sgSexpProductCode);
      if (glumain_file_exists(0xA8, reinterpret_cast<intptr_t>(sgExpCmpgnBinPath))) {
        bOk = 1;
        gbIsExpansion = 1;
      }
      break;
  }

  gnDlgReturnVal = nSavedReturnVal;
  return bOk;
}

// Is nPath present on the CD, asking the user to insert it until they give up.
// The three retry sites all tail-merge onto one `return TRUE`.
BOOL glumain_file_exists(int nStrId, intptr_t nPath) {
  BOOL bHas;
  if (CampaignWithoutCd(nPath, &bHas))
    return bHas;
  if (get_cd_archive() && FileExists(nPath, get_cd_archive()))
    return TRUE;

  snd_end_streamed();
  stop_music();
  close_cd_archive();

  if (FindCDArchive(0, nPath))
    return TRUE;
  while (do_pok_cancel_dlg(reinterpret_cast<intptr_t>(glues_get_str(nStrId)))) {
    if (FindCDArchive(0, nPath))
      return TRUE;
  }

  while (!FindCDArchive(0, 0))
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0xA9)));

  if (get_cd_archive() && FileExists(nPath, get_cd_archive()))
    return TRUE;
  return FALSE;
}

// Hyperion: what gluMain_dlg_fcn does before Single Player (3) or Multiplayer (4)
// leaves the menu, for Hyperion's home screen, which chose the game (the active mod)
// already, in place of the game mode dialog's question. False keeps the menu. Hyperion's
// campaign screen calls it too, for an episode of the other game (port_front_campaign.cpp).
int glumain_choose_product(int nChoice, int bExpansion) {
  if (nChoice == 3 && IsSpawnedCopy()) {
    do_pok_dlg(reinterpret_cast<intptr_t>(error_get_str(0x67)));
    return 0;
  }
  if (!IsBroodwar()) {
    if (!IsSpawnedCopy() && !HasCampaign())
      return 0;
    gbIsExpansion = 0;
    return 1;
  }
  if (bExpansion) {
    SetSurveyProduct(sgSexpProductCode);
    if (!glumain_file_exists(0xA8, reinterpret_cast<intptr_t>(sgExpCmpgnBinPath)))
      return 0;
    gbIsExpansion = 1;
  } else {
    SetSurveyProduct(sgStarProductCode);
    if (nChoice == 3 && !glumain_file_exists(0xA7, reinterpret_cast<intptr_t>(sgCmpgnBinPath)))
      return 0;
    gbIsExpansion = 0;
  }
  return 1;
}

// Load rez\gluMain.bin, run the menu modally, and translate its result into
// the glue state machine's next screen. Cases 5 and 0xFFF0 leave every global
// alone; 6 and 7 are holes the dense table fills with the default.
// Hyperion: Hyperion's home screen (port_front.cpp, docs/front-end.md) runs in the
// menu's place when it is on, and its choice goes through the same switch.
void do_main_dlg() {
  TGAMEPARAMS *pParams;
  int nChoice;
  int bExpansion = 0;

  gbMultiPlayerMode = 0;
  if (!snd_music_enabled())
    snd_play_music(0x16);
  nChoice = PortFrontMainMenu(&bExpansion);
  if ((nChoice == 3 || nChoice == 4) && !glumain_choose_product(nChoice, bExpansion))
    nChoice = 0xFFF0;
  // Hyperion: the active mod's own mode: its game set to start, with the
  // main menu after it, or the menu again when it cannot start.
  if (nChoice == PORT_FRONT_MODE)
    nChoice = PortFrontStartMode() ? 0 : 0xFFF0;
  // Hyperion: a LAN room's match, made or joined: its chat room next
  if (nChoice == PORT_FRONT_LAN) {
    gbMultiPlayerMode = 1;
    glGluesMode = 3;
    glues_clear_all();
    return;
  }
  if (nChoice < 0) {
    ctrl_t *pDlg = dlg_load("rez\\gluMain.bin",
                            reinterpret_cast<PFN_DLGLOADER>(glumain_load_fn),
                            reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                            "gluMain.cpp", 0x270);
    if (pDlg == 0)
      return;
    nChoice = dlg_run_modal(pDlg, 0, reinterpret_cast<PFN_CTRL_EVT>(gluMain_dlg_fcn));
  }

  switch (nChoice) {
    case 4:
      gbMultiPlayerMode = 1;
      glGluesMode = 2;
      pParams = gametype_load_ums();
      *reinterpret_cast<TGAMEPARAMS *>(&gGameHeader.bGameType) = *pParams;
      SMemFree(pParams, "gluMain.cpp", 0x27B, 0);
      break;
    case 3:
      gbMultiPlayerMode = 0;
      glGluesMode = 5;
      pParams = gametype_load_ums();
      *reinterpret_cast<TGAMEPARAMS *>(&gGameHeader.bGameType) = *pParams;
      SMemFree(pParams, "gluMain.cpp", 0x286, 0);
      break;
    case 8:
      gwGameMode = 0;
      break;
    case 9:
      gwGameMode = 8;
      break;
    case 2:
      gwGameMode = 2;
      break;
    case 5:
    case 0xFFF0:
      break;
    default:
      glGluesMode = 0;
      break;
  }
  glues_clear_all();
}

// Start StarEdit.exe out of the install directory and gray the button that
// launched it until the new process has an input queue.
BOOL launch_staredit(intptr_t nArg0) {
  char szCommandLine[0x104];
  char szInstallPath[0x104];
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ctrl_t *pCtrl;

  SFileGetInstallPath(szInstallPath, 0x104, 1);
  SStrCopy(szCommandLine, szInstallPath, 0x104);
  SStrPack(szCommandLine, sgStarEditExe, 0x104);

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  if (CreateProcessA(0, szCommandLine, 0, 0, 0, 0x20, 0, szInstallPath,
                     &si, &pi)) {
    SetForegroundWindow(GetDesktopWindow());
    pCtrl = ctrl_find_ndx(reinterpret_cast<ctrl_t *>(nArg0), 5);
    ctrl_gray(pCtrl);
    WaitForInputIdle(pi.hProcess, 0xFFFFFFFF);
    ctrl_ungray(pCtrl);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
  }

  do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(1)));
  return 0;
}

// Build the caption under the menu: the CD key plus the version string for a
// spawned copy, the version string alone otherwise. The key is truncated a
// character at a time until it fits the control's width.
void gluMain_dlg_activate(intptr_t nArg0) {
  char *pszKey;
  char *pszText;
  int nTextWidth;
  int nVersionWidth;
  int cchKey;
  DWORD cchBuf;
  char *pszBuf;
  // Hyperion: the caption shows Hyperion's version; the oracle shows
  // gszVersionNumber, which still goes to SNet unchanged (version.h).
  const char *pszVersion = HYPERION_VERSION_TEXT;

  ctrl_t *pCtrl = ctrl_find_ndx(reinterpret_cast<ctrl_t *>(nArg0), 10);
  RetrieveKey(0, reinterpret_cast<DWORD_PTR *>(&pszKey));

  if (IsSpawnedCopy()) {
    pszText = glues_get_str(2);
    cchKey = SStrLen(pszKey) + 1;
    dlg_set_font(pCtrl);
    nVersionWidth = font_strlen(pszVersion);
    nTextWidth = font_strlen(pszText);
    while (font_strlen(pszKey) >
           pCtrl->rc.right - pCtrl->rc.left - nTextWidth - nVersionWidth + 1) {
      --cchKey;
      pszKey[cchKey - 1] = 0;
    }
    font_set(0);
    cchBuf = SStrLen(pszText) + SStrLen(pszVersion) + (cchKey - 1);
    pszBuf = static_cast<char *>(SMemAlloc(cchBuf, "gluMain.cpp", 0x1AA, 0));
    psnprintf(pszBuf, cchBuf, pszText, pszKey, pszVersion);
  } else {
    pszText = glues_get_str(3);
    cchBuf = SStrLen(pszText) + SStrLen(pszVersion);
    pszBuf = static_cast<char *>(SMemAlloc(cchBuf, "gluMain.cpp", 0x1B4, 0));
    psnprintf(pszBuf, cchBuf, pszText, pszVersion);
  }

  pCtrl->pData = pszBuf;
  cleanup_key(0, reinterpret_cast<DWORD_PTR>(pszKey));
  ctrl_set_update(pCtrl);
}

// The menu's event handler; command 2 runs a second switch over gnDlgReturnVal.
// Case 1 breaks rather than returning ctrl_dlg (a return moves the shared tail
// out of short-jump range). Case 4 comes first, IsBroodwar negated, its
// predicate split into two arms that each return the transition: that keeps
// the oracle's two predicate copies. The 0x0F arm falls into the default so
// both exits reach the ctrl_dlg tail past its register moves.
int gluMain_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  switch (pEvent->wTag) {
    case 0x0E:
      switch (pEvent->dwId) {
        case 10:
          glue_init_screen_dlg(pCtrl, sMainMenuEvtFcnTbl_g, 0x2C, 0);
          break;
        case 0:
          gluMain_dlg_activate(nArg0);
          glue_screen_activate(pCtrl);
          ctrl_dlg(pCtrl, pEvent);
          if (!snd_music_enabled())
            snd_play_music(0x16);
          return 1;
        case 1: {
          ctrl_t *pCaption = ctrl_find_ndx(pCtrl, 10);
          SMemFree(pCaption->pData, "gluMain.cpp", 0x1C4, 0);
          break;
        }
        case 2:
          switch (gnDlgReturnVal) {
            case 4:
              if (!IsBroodwar()) {
                if (IsSpawnedCopy()) {
                  gbIsExpansion = 0;
                  return glues_transition_out(pCtrl);
                } else if (HasCampaign()) {
                  gbIsExpansion = 0;
                  return glues_transition_out(pCtrl);
                }
                return 1;
              }
              if (!do_gamemode_select_dlg(1))
                return 1;
              break;
            case 3:
              if (IsSpawnedCopy()) {
                do_pok_dlg(reinterpret_cast<intptr_t>(error_get_str(0x67)));
                return 1;
              }
              if (!IsBroodwar()) {
                if (!IsSpawnedCopy() && !HasCampaign())
                  return 1;
                gbIsExpansion = 0;
                return glues_transition_out(pCtrl);
              }
              if (!do_gamemode_select_dlg(0))
                return 1;
              break;
            case 5:
              launch_staredit(nArg0);
              return 1;
          }
          return glues_transition_out(pCtrl);
      }
      break;
    case 0x0F:
      if (evt_key(pEvent) == 0x20)
        return 1;
    default:
      return ctrl_dlg(pCtrl, pEvent);
  }

  return ctrl_dlg(pCtrl, pEvent);
}
