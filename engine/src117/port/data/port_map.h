// port_map.h -- a map's string table and the players' colours it sets (port_map.cpp).
#ifndef PORT_MAP_H
#define PORT_MAP_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The map's string table (port_map.cpp): whether it is STRx, with a u32 count
// and offsets (maphdr_STRx), rather than STR's u16 ones; and a string of a STRx
// table by id, on str_get_string's rules.
void PortMapStringsWide(int bWide);
int PortMapStringsAreWide(void);
const char *PortMapStringWide(const void *pTable, ULONG dwSize, unsigned short wId);
const char *PortMapStringNarrow(const void *pTable, ULONG dwSize, unsigned short wId);

// The players' colours a map sets in CRGB (port_map.cpp). Begin runs as
// pass 2 of a map load starts and keeps 1.17's colour tables as they are; Set
// takes the section's 32 bytes; Apply changes the tables once the game has given
// each slot its colour.
void PortMapColorsBegin(void);
void PortMapColorsSet(const unsigned char *pCrgb);
void PortMapColorsApply(void);

#endif
