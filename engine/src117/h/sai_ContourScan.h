// sai_ContourScan.h -- the header of src117/lang/sai_ContourScan.cpp, the
// contour scan: four directional sweeps over the workspace and the rectangle
// normaliser they share. They are contour_workspace_t members, declared in
// sai_Contours.h with the struct, so this header declares nothing.

#ifndef OC117_SAI_CONTOURSCAN_H
#define OC117_SAI_CONTOURSCAN_H

#include "platform.h"

#include "sai_Contours.h"

// contour_workspace_t::NormalizeRect (0x48A7A0) writes its out pointer at
// +0..+6 as words (rect16_t), and its coordinates compare signed (short).

#endif  // OC117_SAI_CONTOURSCAN_H
