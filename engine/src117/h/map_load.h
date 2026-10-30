// map_load.h -- declarations for src117/lang/map_load.cpp (0x46B0B0 to 0x46B910):
// the saved-map payload's read and write, and the current map's file name.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_MAP_LOAD_H
#define OC117_MAP_LOAD_H

#include "platform.h"

// Checks a map file's Blizzard signature. Second argument is an out status.
BOOL map_validate_signature(const char *pszName, int *pnStatus);

// Reads the saved-map payload; wVersion arrives in EDX and is read as 16 bits.
int read_save_mapdata(void *pFile, WORD wVersion);

BOOL PreloadCampaignMap(WORD wMission, void *pOut);

// Writes the saved-map payload.
int write_save_mapdata(void *pFile);


// 0x46B0B0, 31 B. sub_469DB0 passes 1. net_player.cpp declares it too, with
// this spelling.
void *__fastcall GetCurrentMap(int nWhich);

#endif  // OC117_MAP_LOAD_H
