// gluGameMode.cpp -- the cinematic flic chooser: one body that maps a campaign
// id to its intro and outro flics through a .rdata pair table.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluGameMode.h"
#include "../gen/globals117.h"

// 0x4EDAD8 gFlicPretextMapping_g -- ten {trigger, pretext} byte pairs, named by
// sc-refs/sheets/data_naming_1.17.csv. Twenty bytes: the next .rdata object
// starts at 0x4EDAEC. Scale-2 addressing with a byte load is what proves the
// pair, and the +1 displacement the second member.
#ifdef OC_DLL_MODE
#define gFlicPretextMapping_g (*reinterpret_cast<BYTE (*)[10][2]>(OC_VA(0x4EDAD8u)))
#else
extern BYTE gFlicPretextMapping_g[10][2] OC_UNALIGNED;
#endif

// A flic that has a pretext plays it first, and the pretext table is searched
// from the top down. gwGameMode 2 suppresses the flic itself but not its
// pretext.
void play_cinematic_flic(int nFlicId) {
  int i = 10;

  do {
    if (i-- == 0)
      goto no_pretext;
  } while (gFlicPretextMapping_g[i][0] != nFlicId);
  play_flic(gFlicPretextMapping_g[i][1]);
no_pretext:;
  if (gwGameMode != 2)
    play_flic(nFlicId);
}
