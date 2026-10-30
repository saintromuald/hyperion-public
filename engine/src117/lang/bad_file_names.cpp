// bad_file_names.cpp -- screen a name the player typed before it becomes a file
// on disk: the characters the error table forbids, a drive letter, and the DOS
// device names that are still reserved whatever the extension.

#include <ctype.h>
#include <string.h>
#include "../h/bad_file_names.h"
#include "../h/net_cmds.h"       // is_bad_filename, get_valid_str_len, error_get_str

// The reserved DOS device names, 1.17's table at 0x5025B8, local to this file.
static const char *sgapszReserved[25] = {
  "nul",  "con",  "prn",  "aux",  "conin$", "conout$", "clock$",
  "com1", "com2", "com3", "com4", "com5",   "com6",    "com7", "com8", "com9",
  "lpt1", "lpt2", "lpt3", "lpt4", "lpt5",   "lpt6",    "lpt7", "lpt8", "lpt9"
};

// 0x408300. Non-zero refuses the name. nFlags asks for the path separators to
// be allowed and the name to be reduced to its last component.
int is_bad_filename(const char *pszStr, DWORD dwLen, int nFlags)
{
  char szForbidden[0x40];

  if (!get_valid_str_len(pszStr, dwLen, dwLen, 0, 0))
    return 1;

  const char *pszBad = error_get_str(0x11);
  char *pDst = szForbidden;
  char c = *pszBad;
  if (c != 0) {
    DWORD n = 0;
    do {
      if (n >= 0x3F)
        break;
      if (!nFlags || (c != '/' && c != '\\')) {
        *pDst++ = c;
        n++;
      }
      pszBad++;
      c = *pszBad;
    } while (c != 0);
  }
  *pDst = 0;

  if (isalpha(pszStr[0]) && pszStr[1] == ':')
    pszStr += 2;
  if (strpbrk(pszStr, szForbidden) != 0)
    return 1;

  if (nFlags) {
    const char *pszBack = strrchr(pszStr, '\\');
    const char *pszFwd = strrchr(pszStr, '/');
    if (pszFwd != 0 || pszBack != 0)
      pszStr = (pszFwd > pszBack ? pszFwd : pszBack) + 1;
  }
  // The two identical arms are 1.17's shape: they load the byte into AL before
  // the test instead of comparing memory.
  char c0;
  if (nFlags)
    c0 = *pszStr;
  else
    c0 = *pszStr;
  if (c0 == 0)
    return 1;

  const char *pszDot = strchr(pszStr, '.');
  size_t nStem = pszDot == 0 ? strlen(pszStr) : (size_t)(pszDot - pszStr);

  for (DWORD i = 0; i < 25; i++) {
    const char *pszRes = sgapszReserved[i];
    if (_strnicmp(pszStr, pszRes,
                  nStem > strlen(pszRes) ? nStem : strlen(pszRes)) == 0)
      return 1;
  }
  return 0;
}
