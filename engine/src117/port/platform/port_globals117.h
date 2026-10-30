// port_globals117.h -- 1.17's globals that several port files read and gen/globals117.h does
// not declare (it carries those the game's own files name), declared once. The 1.17 address is
// beside each; the pools of units, sprites, thingies, bullets and the AI, which need the game's
// class headers, stay declared in the save, hash and snapshot files that walk them.
#ifndef PORT_GLOBALS117_H
#define PORT_GLOBALS117_H

#include "../../h/platform.h"      // BYTE, DWORD, OC_UNALIGNED
#include "../../h/gamedata.h"      // array_data_t
#include "../../h/playertables.h"  // the per-player tables

class CNetRecord;
struct ctrl_t;

// The game's frame and its random numbers.
extern unsigned int oc_dwReplayFrame;  // 0x52651C, the frame counter
extern unsigned int sgNetRandState_g;  // 0x67393C, the generator's state
extern int sgNetRandCount_g;           // 0x673938, the draws since net_srand
extern DWORD sgdwUpdatesPerFrame_g;    // 0x50335C, the game frames a pass runs (a replay's multiple)
extern DWORD sgbWindowActive_g;        // 0x66FD24, the game's window has the focus
extern CNetRecord *gpNetRecord_g;      // 0x673D40, the replay being played or recorded

// Per player.
extern PlayerVictoryStateTable gbVictoryState;                     // 0x5349E0
extern PlayerTeamColorTable gdwTeamColorTbl_g OC_UNALIGNED;        // 0x529056, the team colours
extern PlayerGasGatheredTable gPlayerGasGathered_g;                // the gas gathered
extern PlayerMineralsGatheredTable gPlayerMineralsGathered_g;      // the minerals gathered
extern PlayerCachedCompletedCountsGrid sglCachedCompletedCounts_g;  // the completed units by type

// The races Play Custom records (InitSinglePlayerCustom): whether it did, the local player's, and
// the computers' in order.
extern BYTE byte_5264C3_g;     // 0x5264C3
extern BYTE byte_526546_g;     // 0x526546
extern BYTE byte_526547_g[8];  // 0x526547

// The DAT tables' descriptions (units.dat's 55 fields, upgrades.dat's 13) and the tilesets' names.
extern array_data_t gUnitsArrayDataTbl[55] OC_UNALIGNED;     // 0x5014F0
extern array_data_t gUPGRADESArrayDataTbl[13] OC_UNALIGNED;  // 0x501788
extern const char *gpszTilesetNames_g[8] OC_UNALIGNED;       // 0x503398

// The console's dialogs and the map's draw buffers.
extern ctrl_t *sgpPortDlg;           // 0x6AB398, the portrait
extern ctrl_t *sgpStatCmdDlg;        // 0x6AAAE8, the command card
extern ctrl_t *sgpTextBoxDlg;        // 0x6BE65C, the message entry
extern ctrl_t *sgpStatResDlg;        // 0x6AB3F4, the resources at the top (statres.cpp)
extern intptr_t sgConsoleArtMask_g;  // 0x6AB500, the console's mask node
extern BYTE *_gfpScrollBuf;          // 0x543240, the scroll ring
extern BYTE *gfpCellBuf;             // 0x543244, its 8x8 cells, 64 bytes each

#endif
