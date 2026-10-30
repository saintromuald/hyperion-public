// sai_ContoursUtil.h -- declarations for src117/lang/sai_ContoursUtil.cpp, the
// contour table searches. The contours_t searches and point tests the TU
// defines (sub_48B780 to sub_48C5E0) are members, declared in sai_Contours.h.

#ifndef OC117_SAI_CONTOURSUTIL_H
#define OC117_SAI_CONTOURSUTIL_H

#include "platform.h"
#include "sai_Contours.h"

// 0x48B720. The list in ECX and its count in EDX; returns an index.
int find_contour(contour_t *pList, int nCount, int nCoord, int bFlag);

#endif  // OC117_SAI_CONTOURSUTIL_H
