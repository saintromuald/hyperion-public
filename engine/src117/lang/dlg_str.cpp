// dlg_str.cpp -- the .tbl string-table loader and lookup. Oracle
// 0x4C13B0..0x4C1461, bodies in oracle VA order.
//
// A table is a WORD count followed by WORD offsets from the table's own start;
// string ids are 1-based.

#include <string.h>
#include "../h/dlg_str.h"
#include "../gen/globals117.h"

// The loader both str_load_table entry points call through: the file, a null
// buffer, a null size slot, then Storm's owner tag pair.
typedef void *(__fastcall *PFN_TBLLOAD)(const char *pszFile, void *pBuffer,
                                        DWORD *pdwSize, const char *pszTag,
                                        int nLine);

// 0x4C13B0 (39 B). The post-decrement in the test computes the index before the
// zero check, as 1.17 does.
intptr_t str_get_string(void *pArg0, WORD wArg1)
{
  if (!wArg1--)
    return 0;
  WORD *pTbl = static_cast<WORD *>(pArg0);
  // Hyperion: a table past 64 KB, a translation's, in a wide form: 0xFFFF, then a 32-bit
  // count and 32-bit offsets (port_remastered.cpp's TblMake)
  if (pTbl[0] == 0xFFFF) {
    DWORD dwCount, dwOffset;
    memcpy(&dwCount, pTbl + 1, 4);
    if (wArg1 >= dwCount)
      return (intptr_t)"";
    memcpy(&dwOffset, reinterpret_cast<BYTE *>(pTbl) + 6 + 4 * wArg1, 4);
    return (intptr_t)pTbl + dwOffset;
  }
  if (wArg1 >= pTbl[0])
    return (intptr_t)"";
  return (intptr_t)pTbl + pTbl[wArg1 + 1];
}

// 0x4C13E0 (21 B). ECX passes through to the loader untouched.
intptr_t str_load_table(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3)
{
  return (intptr_t)reinterpret_cast<PFN_TBLLOAD>(nArg1)(
      reinterpret_cast<const char *>(nArg0), 0, 0,
      reinterpret_cast<const char *>(nArg2), nArg3);
}

// 0x4C1400 (97 B). 1.17 inlines str_get_string here.
void *str_load_table_ex(const char *pszTblFile, void *pTable, int nCount,
                        void *pfnLoad, const char *pszFile, int nLine)
{
  void *pTbl = reinterpret_cast<PFN_TBLLOAD>(pfnLoad)(pszTblFile, 0, 0,
                                                     pszFile, nLine);
  DWORD_PTR *p = static_cast<DWORD_PTR *>(pTable);
  while (nCount) {
    --nCount;
    if (*p)
      *p = str_get_string(pTbl, (WORD)*p);
    ++p;
  }
  return pTbl;
}
