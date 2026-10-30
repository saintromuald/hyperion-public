// BNetGW.cpp -- the Battle.net gateway list: load it from the registry or from
// rez\\gateways.txt, remember which one the player picked, and write it back.
// Bodies are in oracle VA order.
//
// The list is one flat NUL-separated blob of three strings per gateway, so
// every accessor walks it with SStrLen rather than indexing; Nth is that walk
// and Name and PickClosestZone are built on it.

#include <windows.h>
#include <ctype.h>
#include <stdlib.h>
#include "../h/BNetGW.h"
#include "../h/gluConn.h"        // BNGatewayAccess
#include "../h/Storm.h"          // SStrLen, the SReg pair, SMem*
#include "../h/gds_local.h"      // psnprintf

// 0x408860. Read one registry value into pszText, sized by a first call that
// asks only for the length. dwVersion is the number the blob starts with, and
// is left 0 when strtoul stopped on the first character.
void BNGatewayAccess::GetGatewayList(const char *pszValue)
{
  DWORD cbData = 0;
  if (!SRegLoadData("Configuration", pszValue, 0x82, 0, 0, &cbData))
    return;
  if (cbData == 0)
    return;

  nTextSize = cbData;
  pszText = (char *)SMemAlloc(cbData, "BNetGW.cpp", 0xFF, 0);
  if (!SRegLoadData("Configuration", pszValue, 0x82, pszText, nTextSize, 0)) {
    SMemFree(pszText, "BNetGW.cpp", 0x107, 0);
    pszText = 0;
    nTextSize = 0;
  }

  dwVersion = 0;
  if (pszText != 0) {
    char *pszEnd = pszText;
    DWORD dwVer = strtoul(pszText, &pszEnd, 10);
    if (pszText == pszEnd)
      dwVer = 0;
    dwVersion = dwVer;
  }
}

// 0x408930. Read the list, preferring the override value, replace it when it
// is older than build 1000, count the records, and fall back on the machine's
// time zone when the stored gateway is out of range.
void BNGatewayAccess::Load(void)
{
  TIME_ZONE_INFORMATION tzi;

  nCount = 0;
  nCurGateway = 0;
  pszText = 0;
  nTextSize = 0;
  dwVersion = 0;
  szEmpty[0] = 0;
  nOverride = 0;

  GetGatewayList("Override Battle.net gateways");
  if (pszText != 0)
    nOverride = 1;
  else
    GetGatewayList("Battle.net gateways");

  if (pszText != 0) {
    if (dwVersion < 1000) {
      nDirty = 0;
      Unload();
      SRegDeleteValue("Configuration", "Battle.net gateways", 2);
    }
  }
  if (pszText == 0)
    WriteDefaultGatewayList();
  if (dwVersion < 1000) {
    nCount = 0;
    nCurGateway = 0;
    return;
  }

  int nStrings = -2;
  char *psz = pszText;
  int nOff = 0;
  while (nOff < nTextSize) {
    int n = SStrLen(psz) + 1;
    nOff += n;
    nStrings++;
    psz += n;
  }
  psz = pszText;
  nCount = nStrings / 3;

  char *pszEnd;
  char *pszNum = psz + SStrLen(psz) + 1;
  nCurGateway = strtoul(pszNum, &pszEnd, 10);
  nDirty = 0;
  if (nCurGateway < 1 || nCurGateway > nCount) {
    SMemZero(&tzi, sizeof(tzi));
    GetTimeZoneInformation(&tzi);
    PickClosestZone(tzi.Bias);
  }
}

// 0x408A90. The gateway's display name: the second of its three strings, or the
// empty one when the record is short.
char *BNGatewayAccess::Name(int nIndex)
{
  char *psz = Nth(nIndex);
  if (*psz != 0) {
    char *pszB = psz + SStrLen(psz) + 1;
    if (*pszB != 0)
      return pszB + SStrLen(pszB) + 1;
    return pszB;
  }
  return psz;
}

// 0x408AD0. Walk to the first string of record nIndex, one-based. Returns the
// object's own empty string when the walk runs off the end of the blob.
// The NULL test returns on its own before the range checks, and the bound stays
// inline in the `for`: both are 1.17's code shape.
char *BNGatewayAccess::Nth(int nIndex)
{
  char *psz = pszText;
  if (psz == NULL)
    return szEmpty;
  if (nIndex < 1 || nIndex > nCount)
    return szEmpty;
  int nOff = 0;
  int i;
  for (i = 1; i < nIndex * 3; i++) {
    if (nOff >= nTextSize)
      break;
    int n = SStrLen(psz) + 1;
    psz += n;
    nOff += n;
  }
  if (nOff < nTextSize && i <= nIndex * 3)
    return psz;
  return szEmpty;
}

// 0x408B40. Choose the gateway whose stored UTC offset is nearest the machine's
// own, and install it. Nth and SetCurGateway are both inlined here by /Ob2.
void BNGatewayAccess::PickClosestZone(int nBias)
{
  int nBest = 0x17;
  int nBestIndex = 1;
  // nStop is never read here: it is the nIndex*3 accumulator the inlined Nth
  // compares against, so deleting it loses the strength reduction.
  int nStop = 3;
  for (int nIndex = 1; nIndex <= nCount; nIndex++, nStop += 3) {
    {
      char *psz = Nth(nIndex);
      if (*psz != 0)
        psz += SStrLen(psz) + 1;
      char *pszEnd;
      long lHours = strtol(psz, &pszEnd, 10);
      int nDiff = abs(lHours * 60 - nBias);
      if (nDiff < nBest) {
        nBest = nDiff;
        nBestIndex = nIndex;
      }
    }
  }
  SetCurGateway(nBestIndex);
}

// 0x408C50. Install a gateway, clamped to 0x63 and ignored when the list is
// empty or the index is out of range.
void BNGatewayAccess::SetCurGateway(int nIndex)
{
  if (pszText == 0)
    return;
  if (nIndex < 1 || nIndex > nCount)
    return;
  nDirty = 1;
  if (nIndex > 0x63)
    nIndex = 0x63;
  nCurGateway = nIndex;
}

// 0x408C80. Write the blob back under whichever value it came from, patching
// the current gateway number into the two digits the first line reserves for
// it, then release the buffer.
void BNGatewayAccess::Unload(void)
{
  if (nDirty != 0) {
    char *psz = pszText;
    if (psz != 0) {
      char *pszNum = psz + SStrLen(psz);
      pszNum[1] = (char)(nCurGateway / 10 + '0');
      pszNum[2] = (char)(nCurGateway % 10 + '0');
      SRegSaveData("Configuration",
                   nOverride != 0 ? "Override Battle.net gateways"
                                  : "Battle.net gateways",
                   0x82, pszText, nTextSize);
    }
  }
  if (pszText != 0) {
    SMemFree(pszText, "BNetGW.cpp", 0x90, 0);
    pszText = 0;
    nTextSize = 0;
  }
}

// Skips a run of CR and LF, stopping at a NUL or at pEnd.
static __forceinline const char *gw_skip_eol(const char *pSrc, const char *pEnd)
{
  while (pSrc < pEnd) {
    char c = *pSrc;
    if (c == 0)
      break;
    if (c != 13 && c != 10)
      break;
    pSrc++;
  }
  return pSrc;
}

// Skips to the end of a comment line: a NUL, CR or LF, or pEnd.
static __forceinline const char *gw_skip_comment(const char *pSrc, const char *pEnd)
{
  while (pSrc < pEnd) {
    char c = *pSrc;
    if (c == 0 || c == 13 || c == 10)
      break;
    pSrc++;
  }
  return pSrc;
}

// 0x408D20. Build the list from rez\gateways.txt when the registry has none:
// three space-separated fields per line become three NUL-separated strings,
// '#' starts a comment, and the blob is prefixed with the version and the two
// digits Unload later overwrites with the current gateway. The file buffer and
// its size are filled in a block of their own and copied out.
void BNGatewayAccess::WriteDefaultGatewayList(void)
{
  void *pv;
  DWORD cbSize;
  {
    void *pvFile;
    DWORD cbFile = 0;
    SFileLoadFile("rez\\gateways.txt", &pvFile, &cbFile, 0, 0);
    pv = pvFile;
    cbSize = cbFile;
  }
  if (pv == 0)
    return;

  DWORD cbOut = cbSize * 2;
  char *pszOut = (char *)SMemAlloc(cbOut, "BNetGW.cpp", 0x15F, 0);
  const char *pSrc = (const char *)pv;
  const char *pEnd = pSrc + cbSize;

  psnprintf(pszOut, cbOut, "%d", 1000);
  char *pDst = pszOut + SStrLen(pszOut);
  pDst++;
  *pDst++ = '0';
  *pDst++ = '0';
  *pDst++ = 0;

  while (pSrc < pEnd) {
    if (pSrc < pEnd && *pSrc == '#') {
      pSrc = gw_skip_comment(pSrc, pEnd);
      pSrc = gw_skip_eol(pSrc, pEnd);
      continue;
    }

    while (pSrc < pEnd) {
      if (isspace(*pSrc))
        break;
      *pDst++ = *pSrc++;
    }
    *pDst++ = 0;

    while (pSrc < pEnd) {
      if (!isspace(*pSrc))
        break;
      pSrc++;
    }
    while (pSrc < pEnd) {
      if (isspace(*pSrc))
        break;
      *pDst++ = *pSrc++;
    }
    *pDst++ = 0;

    while (pSrc < pEnd) {
      if (!isspace(*pSrc))
        break;
      pSrc++;
    }
    while (pSrc < pEnd) {
      char c = *pSrc;
      if (c == 13 || c == 10)
        break;
      *pDst++ = c;
      pSrc++;
    }
    *pDst++ = 0;

    pSrc = gw_skip_eol(pSrc, pEnd);
  }

  *pDst = 0;
  int cbBlob = (int)(pDst - pszOut) + 1;
  SRegSaveData("Configuration", "Battle.net gateways", 0x82, pszOut, cbBlob);
  SMemFree(pv, "BNetGW.cpp", 0x1A6, 0);
  nTextSize = cbBlob;
  pszText = pszOut;
  dwVersion = 1000;
}
