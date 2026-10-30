// los.h -- declarations for src117/lang/los.cpp, the line-of-sight mask tables.
// The free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_LOS_H
#define OC117_LOS_H

#include "platform.h"
#include "mask.h"

// Builds the sight patterns. ECX is the table array, EDX the element count, and
// the one stack word the map width.
void sub_466E40(mask_tbl_t *pTbls, int nCount, int nMapWidth);

// Releases each table's cell array.
void los_free(mask_tbl_t *pTbls, int nCount);

// Rasterises one sight pattern and returns how many cells it lit. ECX is the
// table, EDX the scratch array sub_466E40 sizes at nWidth * nHeight * 6 and
// this body clears.
int sub_466990(mask_tbl_t *pTbl, void *pScratch);

// Walks the rasterised scratch into the table's cell array. ECX the table, EDX
// the same scratch, one stack word for the map width.
void sub_466BD0(mask_tbl_t *pTbl, void *pScratch, int nMapWidth);

#endif  // OC117_LOS_H
