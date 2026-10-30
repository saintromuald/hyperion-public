// dlg_str.h -- declarations for src117/lang/dlg_str.cpp: the string-table loader.
// The free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_DLG_STR_H
#define OC117_DLG_STR_H

#include "platform.h"

// The generic parameter names are the ones glues.h and snd.h first gave these.

// 0x4C13B0. Returns string wArg1 (1-based) of the loaded table pArg0: 0 for id
// 0, the shared empty string past the end. The id is 16-bit.
intptr_t str_get_string(void *pArg0, WORD wArg1);

// 0x4C13E0. Loads a .tbl through the loader callback in EDX, called as
// loader(file, 0, 0, tag, line). ECX is forwarded untouched.
intptr_t str_load_table(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3);

// 0x4C1400. Loads a .tbl through pfnLoad, then replaces each nonzero string id
// in pTable[0..nCount) with that string's address.
void *str_load_table_ex(const char *pszTblFile, void *pTable, int nCount,
                        void *pfnLoad, const char *pszFile, int nLine);
#if !defined(_MSC_VER)
// clang and gcc: a string literal or a function as the void * (h/compiler.h, OcVoidPtr)
template <typename T, typename F>
inline void *str_load_table_ex(const char *pszTblFile, T *pTable, int nCount, F *pfnLoad, const char *pszFile,
                               int nLine)
{
  return str_load_table_ex(pszTblFile, OcVoidPtr(pTable), nCount, OcVoidPtr(pfnLoad), pszFile, nLine);
}
#endif

#endif  // OC117_DLG_STR_H
