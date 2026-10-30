// net_error.cpp -- the deferred network-error report: one pending error is
// latched with its strings, actualised later on the UI thread, and its two
// heap strings freed. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/net_error.h"
#include "../h/net_cmds.h"      // gameloop_init
#include "../h/net_player.h"    // leave_game
#include "../h/glues.h"         // CloseAllWindows
#include "../h/Storm.h"         // SMemFree, SErrGetLastError
#include "../h/dlgFatal.h"      // do_dlgfatal
#include "../h/gluError.h"      // do_err_ok_statement_dlg
#include "../h/error.h"         // error_get_str
#include "../h/gds_local.h"     // psnprintf
#include <string.h>              // memcpy
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name. 0x6728B8
// sgbHasNetError_g comes from gen/globals117.h.

// 0x6728BC -- the pending error's string id, and the latch: actualize returns
// immediately when it is zero. sub_4748D0 is what writes it.
#ifdef OC_DLL_MODE
#define sgNetErrId_g (*reinterpret_cast<int *>(OC_VA(0x6728BCu)))
#else
extern int sgNetErrId_g;
#endif

// 0x6728C0 -- the pending message text, a heap copy actualize frees.
#ifdef OC_DLL_MODE
#define sgpNetErrText_g (*reinterpret_cast<char **>(OC_VA(0x6728C0u)))
#else
extern char *sgpNetErrText_g;
#endif

// 0x6728C4 -- the third argument to net_error_show_error, also a heap copy
// actualize frees, though that function spells the slot as an integer.
#ifdef OC_DLL_MODE
#define sgpNetErrArg2_g (*reinterpret_cast<char **>(OC_VA(0x6728C4u)))
#else
extern char *sgpNetErrArg2_g;
#endif

// 0x6728C8 -- the fourth argument. Never freed, so it stays an int.
#ifdef OC_DLL_MODE
#define sgNetErrArg3_g (*reinterpret_cast<int *>(OC_VA(0x6728C8u)))
#else
extern int sgNetErrArg3_g;
#endif

// 0x50F0F0 gbFadedOut, a BYTE, spelled as dlgFatal.cpp and gds_image.cpp spell
// it.
#ifdef OC_DLL_MODE
#ifndef gbFadedOut
#define gbFadedOut (*reinterpret_cast<BYTE *>(OC_VA(0x50F0F0u)))
#endif
#else
extern BYTE gbFadedOut;
#endif

// Storm ordinal 462, spelled as net_cmds.cpp and gds_sys_Win.cpp spell it; no
// header of ours seats it.
extern "C" {
BOOL  __stdcall SErrGetErrorStr(DWORD dwErrCode, char *pszBuffer,
                                unsigned int nBufferSize);
}

// The bodies, in oracle VA order.

// One heap copy of a latched string, or NULL; inlined at both call sites in
// sub_4748D0. The length is tested before its increment, as in 1.17.
static char *net_error_dup(const char *pszText)
{
  DWORD nSize;
  char *pszCopy;

  if (pszText == NULL)
    return NULL;

  nSize = SStrLen(pszText);
  if (nSize++ == 0)
    return NULL;

  pszCopy = static_cast<char *>(SMemAlloc(nSize, "net_error.cpp", 36, 0));
  memcpy(pszCopy, pszText, nSize);
  return pszCopy;
}

// 0x4748D0 (169 B). Latches one pending error for net_error_actualize, which
// frees both strings.
void sub_4748D0(int nArg0, const char *pszText, intptr_t nArg2, int nArg3)
{
  sgNetErrId_g = nArg0;
  sgpNetErrText_g = net_error_dup(pszText);
  sgpNetErrArg2_g = net_error_dup(reinterpret_cast<const char *>(nArg2));
  sgNetErrArg3_g = nArg3;
}

// 0x474980 (128 B). Reports the latched error, then releases it. Each pointer
// is read out and its slot zeroed before it is freed, so a re-entrant call
// finds nothing pending. 131 and 132 are 1.17's line numbers in the owner tag.
void net_error_actualize()
{
  if (sgNetErrId_g == 0)
    return;

  net_error_show_error(sgNetErrId_g, sgpNetErrText_g,
                       reinterpret_cast<intptr_t>(sgpNetErrArg2_g), sgNetErrArg3_g);

  {
    char *const pszText = sgpNetErrText_g;

    sgNetErrId_g = 0;
    if (pszText != NULL)
      SMemFree(pszText, "net_error.cpp", 131, 0);
  }

  {
    char *const pszArg2 = sgpNetErrArg2_g;

    sgpNetErrText_g = NULL;
    if (pszArg2 != NULL)
      SMemFree(pszArg2, "net_error.cpp", 132, 0);
  }

  sgpNetErrArg2_g = NULL;
  sgNetErrArg3_g = 0;
}

// 0x474A00 (104 B). `ret 0Ch`: two register arguments and three stack words. An
// error already latched makes this a no-op. Otherwise the quiet arm reports the
// id alone, and anything else tears the game down first and latches the error
// so actualize will not repeat it.
void err_popup(int nStrId, intptr_t nArg1, intptr_t nArg2, int nArg3, int nArg4)
{
  if (sgbHasNetError_g != 0)
    return;

  if (nArg4 == 0) {
    net_error_show_error(nStrId, NULL, 0, 0);
    return;
  }

  leave_game(3);
  sgbHasNetError_g = 1;
  net_error_show_error(nStrId, reinterpret_cast<const char *>(nArg1),
                       nArg2, nArg3);

  if (gwGameMode == 3)
    gameloop_init(4);

  CloseAllWindows();
}

// 0x474A70 (253 B). Shows a network error: the fatal dialog in game, the plain
// error dialog in the glue screens. With the screen faded out or no dialog up,
// it latches the error with sub_4748D0 instead. A file and line are appended
// only when both are given.
void __fastcall net_error_show_error(int nStringId, const char *pszText,
                                     intptr_t nArg2, int nArg3)
{
  char szBuf[0x100];

  if (gbFadedOut != 0 || gpDlgList == NULL) {
    sub_4748D0(nStringId, pszText, nArg2, nArg3);
    return;
  }

  if (pszText != NULL) {
    if (nArg2 != 0 && nArg3 != 0) {
      psnprintf(szBuf, sizeof(szBuf), "%s file: %s:%d", pszText, nArg2, nArg3);
      pszText = szBuf;
    }
  } else {
    szBuf[0] = '\0';
    pszText = szBuf;
  }

  if (gwGameMode == 3) {
    DWORD dwNation = g_ActiveNationID;
    DWORD dwHuman = g_ActiveHumanID;
    g_ActiveNationID = g_LocalNationID;
    g_ActiveHumanID = g_LocalHumanID;
    do_dlgfatal(error_get_str((WORD)nStringId), pszText);
    g_ActiveNationID = dwNation;
    g_ActiveHumanID = dwHuman;
    return;
  }

  // Hyperion: glue mode 4, Battle.net's, had its own message box
  // (BNMessageBox); the mode is gone with the screens.
  do_err_ok_statement_dlg(error_get_str((WORD)nStringId), pszText);
}

// 0x474B70 (8 B). Clears the latch.
void reset_net_error()
{
  sgbHasNetError_g = 0;
}

// 0x474B80 (105 B), `ret 8h`. Reports Storm's last error under nStrId, unless
// an error is already latched or the code is one of the three the game treats
// as expected.
void net_error_popup(int nStrId, const char *pszFile, int nLine, int nLeave)
{
  char szErr[0x100];
  DWORD dwErr;

  if (sgbHasNetError_g != 0)
    return;
  dwErr = SErrGetLastError();
  if (sgbHasNetError_g != 0)
    return;
  if (dwErr == 0x8510006A || dwErr == 0x120 || dwErr == 0x4C7)
    return;
  SErrGetErrorStr(dwErr, szErr, sizeof(szErr));
  err_popup(nStrId, reinterpret_cast<intptr_t>(szErr),
            reinterpret_cast<intptr_t>(pszFile), nLine, nLeave);
}
