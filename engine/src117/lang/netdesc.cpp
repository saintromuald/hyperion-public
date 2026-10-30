// netdesc.cpp -- the SNet descriptors and the text form of a game description.
// Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>   // memset -- both SNETPROGRAMDATA bodies zero the record
#include <ctype.h>    // tolower -- Txt2Number folds each hex digit through it

#include "../h/netdesc.h"
#include "../h/glues.h"          // IsSpawnedCopy
#include "../h/net_mgr.h"        // ProgressNetwork, SendTurns
#include "../h/net_time.h"       // sub_47A630
#include "../h/pause.h"          // get_lobby_state
#include "../h/fd_user.h"        // sub_440EE0
#include "../h/gluChat.h"        // on_chrm_kicked, sub_451790
#include "../h/net_chat_cmds.h"  // create_net_cmd_download_progress, sub_470460, sub_470930
#include "../h/net_cmds.h"       // get_valid_str_len
#include "../h/victory.h"        // psnprintf
#include "../gen/globals117.h"

// GetTickCount, imported from KERNEL32 at IAT slot 0x4EB2FC, read off the
// oracle's import table. The dllimport spelling emits the six-byte indirect
// call rather than a direct call through a linker thunk.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// gLocalPlayerName (0x672690) comes from gen/globals117.h.

// 0x536720 -- the expansion gate, only ever compared against zero as a byte.
// Spelled as src117/lang/gametype.cpp and CUnitUpgrade.cpp spell it.
#ifndef OC_GB_IS_EXPANSION
#define OC_GB_IS_EXPANSION
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif
#endif

// 0x672590 -- the GetTickCount stamp the heartbeat measures against; DWORD,
// because every compare against it is unsigned.
#ifndef OC_DWORD_672590
#define OC_DWORD_672590
#ifdef OC_DLL_MODE
#define dword_672590 (*reinterpret_cast<DWORD *>(OC_VA(0x672590u)))
#else
extern DWORD dword_672590;
#endif
#endif

// The bodies, in oracle VA order.

// Release the CD key pair and blank the record.
void DestroySNetProgramData(SNETPROGRAMDATA *pData)
{
  cleanup_key(pData->dwKeyLo, pData->dwKeyHi);
  memset(pData, 0, sizeof(*pData));
}

// The local player's SNet descriptor: the name buffer and an empty comment.
void InitSNetPlayerData(SNETPLAYERDATA *pData)
{
  memset(pData, 0, sizeof(*pData));
  pData->dwSize = sizeof(*pData);
  pData->pszName = gLocalPlayerName;
  pData->pszDescription = "";
}

// The program descriptor SNet identifies us by. The product name and id are the
// only things the expansion changes.
void InitSNetProgramData(SNETPROGRAMDATA *pData)
{
  DWORD_PTR dwKeyLo;
  DWORD_PTR dwKeyHi;

  memset(pData, 0, sizeof(*pData));
  pData->dwSize = sizeof(*pData);
  if (gbIsExpansion != 0) {
    pData->dwProgramId = 'SEXP';
    pData->pszName = "Brood War";
  } else {
    pData->dwProgramId = 'STAR';
    pData->pszName = "Starcraft";
  }
  pData->pszVersion = "internal version unknown";
  pData->dwVersionId = 0xD5;
  pData->dwMaxPlayers = 8;
  pData->dwField28 = 0xFF;
  RetrieveKey(&dwKeyLo, &dwKeyHi);
  pData->dwKeyLo = dwKeyLo;
  pData->dwKeyHi = dwKeyHi;
  pData->dwIsSpawn = IsSpawnedCopy();
  pData->dwLangId = GetLocale();
}

// The network heartbeat, run once per frame while a game is being set up. It
// only does work every 250 ms; a network that has not progressed for 20 s gets
// the timeout dialog instead.
int sub_46F3C0()
{
  BYTE bPercent;
  DWORD dwNow = GetTickCount();

  if (dwNow - dword_672590 < 0xFA)
    return 0;
  if (!ProgressNetwork()) {
    if (dwNow - dword_672590 >= 0x4E20)
      sub_47A630();
    return 0;
  }
  if (get_lobby_state() == 4) {
    if (!sub_440EE0(&bPercent))
      on_chrm_kicked(6);
    else
      create_net_cmd_download_progress(bPercent);
  }
  sub_470460();
  dword_672590 = dwNow;
  SendTurns();
  sub_451790();
  sub_470930();
  return get_lobby_state() == 9;
}

// Restamp the heartbeat clock and flush the turn queue.
void sub_46F460()
{
  dword_672590 = GetTickCount();
  SendTurns();
}

// Render a game description as the comma-separated hex text a game list entry
// carries; a field holding its default prints as a bare comma. The assignment
// order and the one ternary keep 1.17's code shape.
void GameData2Txt(gamedesc_t *pData, char *pszBuf, int cchBuf)
{
  int nWritten;
  int nValue;
  const char *pszFmt;

  --cchBuf;

  nValue = pData->dwGameId;
  pszFmt = "%x,";
  if (nValue == 0)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  WORD wWidth = pData->wMapWidth;
  WORD wHeight = pData->wMapHeight;
  nValue = ((BYTE)(wWidth >> 1) & 0xF0) | (BYTE)(wHeight >> 5);
  pszFmt = "%x,";
  if (nValue == 0x88)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = (BYTE)(pData->bActivePlayers << 4) | pData->bMaxPlayers;
  pszFmt = "%x,";
  if (nValue == 0x18)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->bGameSpeed;
  pszFmt = "%x,";
  if (nValue == 4)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->bApproval;
  pszFmt = "%x,";
  if (nValue == 0)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->bGameType;
  pszFmt = "%x,";
  if (nValue == 0)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->bGameSubType;
  pszFmt = "%x,";
  if (nValue == 0)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->wField0E;
  pszFmt = "%x,";
  if (nValue == 0)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->dwField10;
  nWritten = psnprintf(pszBuf, cchBuf, nValue == 0 ? "," : "%x,", nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->wTemplate;
  pszFmt = "%x,";
  if (nValue == 0)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;
  pszBuf += nWritten;

  nValue = pData->bField16;
  pszFmt = "%x,";
  if (nValue == 0)
    pszFmt = ",";
  nWritten = psnprintf(pszBuf, cchBuf, pszFmt, nValue);
  if (nWritten == -1)
    nWritten = cchBuf;
  cchBuf -= nWritten;

  pszBuf = String2Txt(pData->szGameName, pszBuf + nWritten, &cchBuf, 0x19);
  pszBuf = String2Txt(pData->szMapName, pszBuf, &cchBuf, 0x20);
  *pszBuf = '\0';
}

// Copy one name into the text form, translating a carriage return to a line
// feed and terminating with a carriage return. The character is held as a BYTE
// and widened separately, which keeps 1.17's two cursors.
__declspec(noinline) char *String2Txt(const char *pszSrc, char *pszDest, int *pcchLeft, int cchMax)
{
  int nLen = (int)strlen(pszSrc);

  *pcchLeft -= nLen + 1;
  while (nLen != 0) {
    BYTE b = (BYTE)*pszSrc;
    ++pszSrc;
    int c = b;
    if (b == 0x0D)
      c = 0x0A;
    *pszDest = (char)c;
    ++pszDest;
    --nLen;
  }
  *pszDest = '\r';
  return pszDest + 1;
}

// Copy one CR-terminated name of at most nLeft - 1 characters and step past the
// CR; NULL for a missing, overlong or NUL-cut field. The oracle inlines it twice.
static __forceinline char *desc_copy_str(char *pDest, char *p, int nLeft) {
  if (p == 0)
    return 0;
  while (*p != '\r') {
    if (--nLeft == 0)
      return 0;
    char ch = *p++;
    *pDest++ = ch;
    if (ch == '\0')
      return 0;
  }
  *pDest = '\0';
  return p + 1;
}

// Parse the text form back into a description, then range-check it. A field
// that is missing takes the default the writer would have omitted it for. The
// two nibble splits shift unsigned, and the active count is stored first.
BOOL Txt2GameData(const char *pszText, gamedesc_t *pData)
{
  int nValue;
  char *p;

  if (!get_valid_str_len(pszText, 0x80, 0x80, 1, 1))
    return 0;

  p = Txt2Number((int *)&pData->dwGameId, const_cast<char *>(pszText), 0);

  nValue = 0;
  p = Txt2Number(&nValue, p, 0x88);
  pData->wMapWidth = (WORD)((((unsigned)nValue >> 4) & 0x0F) << 5);
  pData->wMapHeight = (WORD)((nValue & 0x0F) << 5);

  nValue = 0;
  p = Txt2Number(&nValue, p, 0x18);
  pData->bActivePlayers = (BYTE)(((unsigned)nValue >> 4) & 0x0F);
  pData->bMaxPlayers = (BYTE)(nValue & 0x0F);

  nValue = 0;
  p = Txt2Number(&nValue, p, 4);
  pData->bGameSpeed = (BYTE)nValue;

  p = Txt2Number(&nValue, p, 0);
  pData->bApproval = (BYTE)nValue;

  nValue = 0;
  p = Txt2Number(&nValue, p, 0);
  pData->bGameType = (BYTE)nValue;

  nValue = 0;
  p = Txt2Number(&nValue, p, 0);
  pData->bGameSubType = (BYTE)nValue;

  p = Txt2Number(&nValue, p, 0);
  pData->wField0E = (WORD)nValue;

  p = Txt2Number(&nValue, p, 0);
  pData->dwField10 = nValue;

  p = Txt2Number(&nValue, p, 0);
  pData->wTemplate = (WORD)nValue;

  p = Txt2Number(&nValue, p, 0);
  pData->bField16 = (BYTE)nValue;

  p = desc_copy_str(pData->szGameName, p, 0x19);
  p = desc_copy_str(pData->szMapName, p, 0x20);
  if (p == 0 || *p != '\0')
    return 0;
  return sub_46F8B0(pData);
}

// Is a parsed description in range? Every arm shares one `return 0`, which is
// what an `&&` chain emits.
BOOL sub_46F8B0(gamedesc_t *pData)
{
  if (pData->wMapWidth > 0x100)
    return 0;
  if (pData->wMapHeight > 0x100)
    return 0;
  if (pData->bActivePlayers > pData->bMaxPlayers)
    return 0;
  if (pData->bMaxPlayers > 8)
    return 0;
  if (pData->bGameSpeed > 6)
    return 0;
  if (pData->szGameName[0] == '\0')
    return 0;
  if (pData->szMapName[0] == '\0')
    return 0;
  if (get_valid_str_len(pData->szMapName, 0x20, 0x20, 0, 1) == 0)
    return 0;
  if (pData->bApproval >= 7)
    return 0;
  return pData->wTemplate < 8;
}

// Read one comma-terminated hex field. An absent field or an empty one takes
// the default; an invalid digit fails the whole parse. tolower's result is held
// as a char and both range tests are taken off it, as in 1.17.
char *Txt2Number(int *pnOut, char *pszTxt, int nDefault)
{
  if (pszTxt == 0) {
    *pnOut = nDefault;
    return 0;
  }
  if (*pszTxt == ',') {
    *pnOut = nDefault;
    return pszTxt + 1;
  }

  *pnOut = 0;
  char ch = *pszTxt;
  while (ch != ',') {
    ++pszTxt;
    char chDigit = (char)tolower(ch);
    BYTE bTest = (BYTE)(chDigit - '0');
    if (bTest <= 9) {
      chDigit = (char)bTest;
    } else {
      if ((BYTE)(chDigit - 'a') > 5)
        return 0;
      chDigit += -0x57;
    }
    *pnOut = (*pnOut << 4) + chDigit;
    ch = *pszTxt;
  }
  return pszTxt + 1;
}
