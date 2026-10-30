// port_eud.cpp -- EUD maps (port_eud.h): the test
// of a map's triggers, the death counts of an EUD game, and the map of 1.16.1's addresses onto
// Hyperion's data that they go through.
//
// The map is a sorted list of items, each a range of 1.16.1's addresses. Most are twins: a range
// whose 1.17 global has 1.16.1's layout, named by its 1.17 address. A twin in 1.17's data block
// is served as its bytes (OC_VA); one the 64-bit build keeps in storage of its own, with
// pointers and widened fields, field by field through
// PortDataField, a pointer read as the 1.16.1 address of what it points at and written back
// from one. The rest hold bytes of their own: the trigger lists, their nodes and the string
// section, 1.16.1's as an EUD map's program expects them, which its triggers run from,
// and stat_txt.tbl. Remastered serves more (its adapters for the structures and their lists);
// until Hyperion does, the log says which addresses a map reaches that no item serves.
#include <windows.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <set>
#include <vector>
#include <SDL3/SDL.h>

#include "port_eud.h"
#include "oc_data.h"
#include "playerlimits.h"  // PlayerIsLobby, PLAYER_SLOTS_117
#include "TrigAct.h"       // map_msg_default
#include "victory.h"       // TRIGGERNODE
#include "Storm.h"         // SMemAlloc, SMemFree
#include "grp.h"           // GROUP, FRAME
#include "gamedata.h"      // array_data_t
#include "CImage.h"
#include "CSprite.h"
#include "CBullet.h"
#include "COrder.h"
#include "CLink.h"
#include "poollimits.h"     // g_pools, POOL_*_MAX
#include "statcmd.h"        // StatcmdEudButtons
#include "../platform/port_globals117.h"  // oc_dwReplayFrame
#include "../platform/port_log.h"
#include "../platform/port_posix.h"       // PortNativePath
#include "../platform/port_standalone.h"  // PortDataBytes, PortDataField, PortOriginalAddress
#include "../front/port_ui.h"             // PortCascAvailable, PortCascFolder
#include "../data/port_stormbase.h"        // PortMemSize
#include "../data/port_language.h"         // PortLanguageGame
#include "../data/port_remastered.h"       // PortRemasteredStatTxtEnglish
#include "../data/port_stormapi.h"         // SFileUnloadFile
#include "../../gen/globals117.h"
#include "../../gen/wide117.h"  // WIDE_*

extern DWORD sgdwTriggerPlayer;  // 0x6CA754, the trigger player (victory.cpp)
// The map's string table (maphdr_STR, maphdr_STRx). A pointer, so in the storage of its own
// the build gives such globals, not in the data block.
extern void *gpMapStrs_g;      // 0x666440
extern DWORD gdwMapStrSize_g;  // 0x666444
extern void *sgpStatStrs;      // 0x6AB4F8, stat_txt.tbl (status.cpp)
extern char MapFileName[0x104];  // 0x52701C, the map's path (map_load.cpp)
// The .dat files' load tables: each field's array, its element size and its count, in the
// order the file holds them (load_array_data, gamedata.cpp).
extern array_data_t gUnitsArrayDataTbl[55];      // 0x5014F0
extern array_data_t gWEAPONSArrayDataTbl_g[25];  // 0x501828
extern array_data_t gFLINGYArrayDataTbl_g[8];    // 0x501148
extern array_data_t gSPRITESArrayDataTbl_g[7];   // 0x501408
extern array_data_t gIMAGESArrayDataTbl_g[15];   // 0x5011A8
extern array_data_t gUPGRADESArrayDataTbl[13];   // 0x501788
extern array_data_t gTECHDATAArrayDataTbl_g[12]; // 0x501460
extern array_data_t gSFXDATAArrayDataTbl_g[6];   // 0x5013C0
extern array_data_t gPORTDATAArrayDataTbl_g[7];  // 0x501368
extern array_data_t gORDERSArrayDataTbl_g[20];   // 0x501278
// The wireframes: of a unit in a transport, of a selection, of the one unit selected
// (statdata.cpp, statwire.cpp).
extern GROUP *sgpTransWires;    // 0x6AAB28
extern GROUP *sgpMultSelWires;  // 0x6AAB2C
extern GROUP *sgpWireFrames;    // 0x6AB524
extern BYTE *sgpRpMap;          // 0x67CF60, the repulse counts (repulse.cpp)
extern BYTE *gpAIScript_g;      // 0x6A2E74, aiscript.bin as loaded (SAI_Scripts.cpp)
extern BYTE *gpBWScript_g;      // 0x6A2E78, bwscript.bin
extern BYTE *gfpMtxSet;         // 0x543238, the tileset's megatiles (gamemap.cpp)
extern WORD *gfpCellMap;        // 0x54323C, the tiles as drawn (gamemap.cpp)
extern WORD *gpMtxInfo;         // 0x6CE650, the tileset's .cv5 (gamemap.cpp)
extern DWORD gdwMIEntries;      // its groups
// The pools of images, sprites, bullets and orders, and their free lists (CImage.cpp,
// CSprite.cpp, CBullet.cpp, COrder.cpp).
extern CImage *images_sgpFreeHead_g;  // 0x544694
extern CImage *images_sgpFreeTail;    // 0x592898
extern CSprite sgSprites[POOL_SPRITES_STORE];
extern CSprite *sprites_sgpFreeHead;  // 0x5C51C8
extern CSprite *sprites_sgpFreeTail;  // 0x5C51CC
extern CBullet sgBullets[POOL_BULLETS_STORE] OC_UNALIGNED;
extern CLink *sgBulletsFreeHead_g;    // 0x540758
extern CLink *sgBulletsFreeTail_g;    // 0x54075C
extern COrder sgOrders[POOL_ORDERS_STORE] OC_UNALIGNED;
extern COrder *sgpOrderPoolHead_g;    // 0x5AEF00
extern COrder *sgpOrderPoolTail_g;    // 0x5AEF04

bool g_eudHalted = false;
bool g_dumpMap = false;  // HYPERION_EUD=map

namespace {

// 1.16.1's code addresses the panel and button tables hold, each with 1.17's address of the same
// function, and 1.16.1's button arrays with 1.17's (tools/eud_code116.py).
struct EudCodePair {
  DWORD dwVa116, dwVa117;
};
struct EudButtons116 {
  DWORD dwVa116, dwVa117, nButtons;
};
#include "../../gen/eud_code116.inc"

// hyperion_eud.log: each EUD game, and every address one reached that no item serves
const PortLogger Log("hyperion_eud.log");

// 1.16.1's death table, glCounts' fourth plane.
const DWORD kDeaths116 = 0x58A364;
// The unit codes past the types: any unit, men, buildings and factories.
const WORD kAnyUnit = 229, kMen = 230, kBuildings = 231, kFactories = 232;

// 1.16.1's eight trigger lists, Storm's TSList heads of 12 bytes: a dword 0, the last node
// (the head's own link when empty) and the first (the complement of that link at the end).
const DWORD kLists116 = 0x51A280;
const DWORD kLists = 8;
// The nodes, 1.16.1's TRIGGERNODE of 0x968 bytes, where Remastered's buffers start and at its
// 16-byte alignment. A map reads their addresses from the lists, never names one.
const DWORD kNodes116 = 0x6DE000;
const DWORD kNodeSize = 0x968;
const DWORD kNodeStride = 0x970;
// The string section and 1.16.1's pointer to it. Remastered keeps the section at 0x191943C8,
// where euddraft's maps put their program (eudplib, payload_init.py).
const DWORD kStrings116 = 0x191943C8;
const DWORD kStringsPtr116 = 0x5993D4;
// stat_txt.tbl and 1.16.1's pointer to it. Remastered's address for the table, which eudplib
// notes beside the pointer it reads (tblprint.py).
const DWORD kStatTxt116 = 0x19184660;
const DWORD kStatTxtPtr116 = 0x6D5A30;
// 1.16.1's pointers to the three wireframe GRPs. A GRP as 1.16.1 keeps it is its file, the frames
// eight bytes each with the data's offset, which a map rewrites to show another unit's wireframe
// (eudplib, wireframe.py). The engine's own frames (PortGroupNative) follow each write.
const DWORD kTranWirePtr116 = 0x68C1F4;
const DWORD kGrpWirePtr116 = 0x68C1FC;
const DWORD kWireframPtr116 = 0x68C204;

// A node runs in place, through a TRIGGERNODE kLinkPad bytes before it: the 64-bit link is
// that much wider than 1.16.1's, and the fields after it are 1.16.1's.
const size_t kLinkPad = offsetof(TRIGGERNODE, conditions) - 8;
static_assert(offsetof(TRIGGERNODE, actions) - kLinkPad == 0x148, "1.16.1's actions");
static_assert(offsetof(TRIGGERNODE, dwFlags) - kLinkPad == 0x948, "1.16.1's flags");
static_assert(offsetof(TRIGGERNODE, bCurrentAction) - kLinkPad == 0x967, "1.16.1's action index");

// A range of 1.16.1's addresses: the bytes that stand for it, or for a twin in storage of its
// own its 1.17 address, pHost NULL. A node may run from one with bNodes, cbBefore the host bytes
// it has before its start.
struct Item {
  DWORD dwBase;
  DWORD dwSize;
  BYTE *pHost;
  bool bReadOnly;
  const char *pszName;
  bool bNodes;
  size_t cbBefore;
  DWORD dwBase117;  // a twin's 1.17 address, else 0
  GROUP *pGroup;    // a GRP's file, whose frames the engine's follow, else NULL
  bool bStandIn;    // bytes Hyperion does not have, served as zeros or empty: a read is logged
  int nPool;        // an end of a pool's free list, its index in g_pools116 (bTail the end), else -1
  bool bTail;
  bool bCards;      // the command cards, whose buttons a map may point at its own
};

Item MakeItem(DWORD dwBase, DWORD dwSize, void *pHost, const char *pszName, bool bReadOnly = false,
              bool bNodes = false, size_t cbBefore = 0) {
  Item it = {dwBase, dwSize, static_cast<BYTE *>(pHost), bReadOnly, pszName, bNodes, cbBefore, 0, NULL, false, -1,
             false, false};
  return it;
}

// The map, sorted by base, built as a game starts; the trigger items join at its first pass.
std::vector<Item> g_items;
const Item *g_pLast = NULL;  // the item the last access found

DWORD g_lists[kLists * 3];
DWORD g_dwStringsPtr = kStrings116;
DWORD g_dwStatTxtPtr = kStatTxt116;
// 1.16.1's import of storm.dll's ordinal 506 (0x4FE544, in its import table), which the loader
// fills with the function's address in storm.dll, loaded at 0x15000000: the one constant Remastered
// serves (0x1402ea040), and how a map finds storm.dll's data, the archive list among it.
const DWORD kStormImport116 = 0x4FE544;
DWORD g_dwStormImport = 0x15021A00;

// Storm's archive list, a TSList at 0x1505ADF8 in 1.16.1's storm.dll: its link offset, 0, then the
// last archive and the first, ~(head + 4) at the end. Each archive is Storm's 0x144-byte record:
// its link, then the file's name (left empty, as Remastered leaves it), the sector size at +0x120,
// the archive's offset in its file at +0x12C, and its header, block table and hash table at +0x130,
// +0x134 and +0x138, each a buffer of its own, the tables decrypted as Storm keeps them. Remastered
// lists 1.16.1's four archives, patch_rt.mpq, BrooDat.mpq, StarDat.mpq and the CD's, from a table
// it carries (0x140d896e8), then the map's, read from its file (0x1402e39c0). Hyperion has the four
// headers and serves their tables empty, logging a read of them; the map's are its own.
// The .dat tables as 1.16.1 laid them out: its load tables (units.dat's at 0x513C30, and so on),
// read from its exe, give each field's address, element size and count. 1.17's tables list the
// same fields in the same order, so each field of 1.16.1's is Hyperion's array at the same place
// in Hyperion's table.
struct DatField116 {
  DWORD dwAddr, dwElem, dwCount;
};
// units.dat, 54 fields (1.16.1 0x513c30)
const DatField116 kDat116_units[] = {
    {0x6644F8, 1, 228}, {0x6607C0, 2, 228}, {0x660C38, 2, 228}, {0x664980, 2, 96},
    {0x6610B0, 4, 228}, {0x6605F0, 1, 228}, {0x6647B0, 1, 228}, {0x660E00, 2, 228},
    {0x662350, 4, 228}, {0x663150, 1, 228}, {0x660FC8, 1, 228}, {0x663DD0, 1, 228},
    {0x662EA0, 1, 228}, {0x662268, 1, 228}, {0x664898, 1, 228}, {0x663320, 1, 228},
    {0x663A50, 1, 228}, {0x6636B8, 1, 228}, {0x6645E0, 1, 228}, {0x6616E0, 1, 228},
    {0x65FC18, 1, 228}, {0x660178, 1, 228}, {0x664080, 4, 228}, {0x662DB8, 1, 228},
    {0x663238, 1, 228}, {0x6635D0, 1, 228}, {0x662180, 1, 228}, {0x65FEC8, 1, 228},
    {0x662098, 1, 228}, {0x661FC0, 2, 106}, {0x65FFB0, 2, 228}, {0x662BF0, 2, 228},
    {0x663B38, 2, 106}, {0x661EE8, 2, 106}, {0x663C10, 2, 106}, {0x661440, 2, 106},
    {0x662860, 2, 456}, {0x6626E0, 2, 192}, {0x6617C8, 2, 912}, {0x662F88, 2, 228},
    {0x663888, 2, 228}, {0x65FD00, 2, 228}, {0x660428, 2, 228}, {0x660A70, 2, 228},
    {0x6637A0, 1, 228}, {0x6646C8, 1, 228}, {0x663CE8, 1, 228}, {0x664410, 1, 228},
    {0x660988, 1, 228}, {0x663408, 2, 228}, {0x663EB8, 2, 228}, {0x660260, 2, 228},
    {0x6606D8, 1, 228}, {0x661518, 2, 228},
};
// weapons.dat, 24 fields (1.16.1 0x513868)
const DatField116 kDat116_weapons[] = {
    {0x6572E0, 2, 130}, {0x656CA8, 4, 130}, {0x6573E8, 1, 130}, {0x657998, 2, 130},
    {0x656A18, 4, 130}, {0x657470, 4, 130}, {0x6571D0, 1, 130}, {0x657258, 1, 130},
    {0x656670, 1, 130}, {0x657040, 1, 130}, {0x6566F8, 1, 130}, {0x656888, 2, 130},
    {0x6570C8, 2, 130}, {0x657780, 2, 130}, {0x656EB0, 2, 130}, {0x657678, 2, 130},
    {0x656FB8, 1, 130}, {0x6564E0, 1, 130}, {0x656990, 1, 130}, {0x657888, 1, 130},
    {0x657910, 1, 130}, {0x656C20, 1, 130}, {0x656568, 2, 130}, {0x656780, 2, 130},
};
// flingy.dat, 7 fields (1.16.1 0x515a38)
const DatField116 kDat116_flingy[] = {
    {0x6CA318, 2, 209}, {0x6C9EF8, 4, 209}, {0x6C9C78, 2, 209}, {0x6C9930, 4, 209},
    {0x6C9E20, 1, 209}, {0x6CA240, 1, 209}, {0x6C9858, 1, 209},
};
// sprites.dat, 6 fields (1.16.1 0x513fb8)
const DatField116 kDat116_sprites[] = {
    {0x666160, 2, 517}, {0x665E50, 1, 387}, {0x666570, 1, 517}, {0x665C48, 1, 517},
    {0x665AC0, 1, 387}, {0x665FD8, 1, 387},
};
// images.dat, 14 fields (1.16.1 0x514010)
const DatField116 kDat116_images[] = {
    {0x668AA0, 4, 999}, {0x66E860, 1, 999}, {0x66C150, 1, 999}, {0x66D4D8, 1, 999},
    {0x667718, 1, 999}, {0x669E28, 1, 999}, {0x669A40, 1, 999}, {0x66EC48, 4, 999},
    {0x66C538, 4, 999}, {0x66B1B0, 4, 999}, {0x66A210, 4, 999}, {0x667B00, 4, 999},
    {0x666778, 4, 999}, {0x66D8C0, 4, 999},
};
// upgrades.dat, 12 fields (1.16.1 0x5136e0)
const DatField116 kDat116_upgrades[] = {
    {0x655740, 2, 61}, {0x6559C0, 2, 61}, {0x655840, 2, 61}, {0x6557C0, 2, 61}, {0x655B80, 2, 61},
    {0x655940, 2, 61}, {0x6558C0, 2, 61}, {0x655AC0, 2, 61}, {0x655A40, 2, 61}, {0x655BFC, 1, 61},
    {0x655700, 1, 61}, {0x655B3C, 1, 61},
};
// techdata.dat, 11 fields (1.16.1 0x5137d8)
const DatField116 kDat116_techdata[] = {
    {0x656248, 2, 44}, {0x6561F0, 2, 44}, {0x6563D8, 2, 44}, {0x656380, 2, 44}, {0x656198, 2, 44},
    {0x6562F8, 2, 44}, {0x656430, 2, 44}, {0x6562A0, 2, 44}, {0x656488, 1, 44}, {0x656350, 1, 44},
    {0x6564B4, 1, 44},
};
// sfxdata.dat, 5 fields (1.16.1 0x515498)
const DatField116 kDat116_sfxdata[] = {
    {0x68DAA0, 4, 1144}, {0x68D628, 1, 1144}, {0x68C8C0, 1, 1144}, {0x68CD38, 2, 1144},
    {0x68C448, 1, 1144},
};
// portdata.dat, 6 fields (1.16.1 0x513780)
const DatField116 kDat116_portdata[] = {
    {0x655C58, 4, 110}, {0x655E80, 4, 110}, {0x655E10, 1, 110}, {0x6560A8, 1, 110},
    {0x656038, 1, 110}, {0x656118, 1, 110},
};
// orders.dat, 19 fields (1.16.1 0x513ec8)
const DatField116 kDat116_orders[] = {
    {0x665280, 2, 189}, {0x664B00, 1, 189}, {0x665940, 1, 189}, {0x665A00, 1, 189},
    {0x664A40, 1, 189}, {0x6657C0, 1, 189}, {0x665040, 1, 189}, {0x665100, 1, 189},
    {0x665700, 1, 189}, {0x6651C0, 1, 189}, {0x6654C0, 1, 189}, {0x664C80, 1, 189},
    {0x664BC0, 1, 189}, {0x665880, 1, 189}, {0x664E00, 1, 189}, {0x664D40, 1, 189},
    {0x664EC0, 2, 189}, {0x665580, 2, 189}, {0x665400, 1, 189},
};

struct Dat116 {
  const char *pszName;
  const DatField116 *pFields;
  size_t nFields;
  array_data_t *pTable;
};
#define DAT116(name, table) {#name ".dat", kDat116_##name, sizeof(kDat116_##name) / sizeof(DatField116), table}
const Dat116 kDats116[] = {
    DAT116(units, gUnitsArrayDataTbl),         DAT116(weapons, gWEAPONSArrayDataTbl_g),
    DAT116(flingy, gFLINGYArrayDataTbl_g),     DAT116(sprites, gSPRITESArrayDataTbl_g),
    DAT116(images, gIMAGESArrayDataTbl_g),     DAT116(upgrades, gUPGRADESArrayDataTbl),
    DAT116(techdata, gTECHDATAArrayDataTbl_g), DAT116(sfxdata, gSFXDATAArrayDataTbl_g),
    DAT116(portdata, gPORTDATAArrayDataTbl_g), DAT116(orders, gORDERSArrayDataTbl_g),
};
#undef DAT116
// The bytes between two of 1.16.1's arrays, which Remastered serves too: a dword written over an
// array's last byte reaches them.
BYTE g_abDatGaps[192][16];
// The fields whose writes Remastered drops (flags 0x2): images.dat's GRP and its five overlays,
// sprites.dat's selection circle, its offset and the bar beside them, which the game holds in
// other forms once loaded.
const DWORD kDatReadOnly116[] = {0x668AA0, 0x666778, 0x667B00, 0x66A210, 0x66B1B0, 0x66C538,
                                 0x66D8C0, 0x665AC0, 0x665E50, 0x665FD8};

const DWORD kArchives116 = 0x1505ADF8;
const DWORD kArchiveRecord = 0x144;
DWORD g_adwArchiveHead[3];
struct Archive {
  BYTE abRecord[kArchiveRecord];
  BYTE abHeader[0x20];
  std::vector<BYTE> block, hash;
};
std::vector<Archive> g_archives;
// 1.16.1's four archive headers: the size, the sector shift, the tables' offsets and counts.
struct ArchiveHeader {
  DWORD dwSize;
  WORD wShift;
  DWORD dwHashOffset, dwBlockOffset, dwHashCount, dwBlockCount;
};
const ArchiveHeader kArchiveHeaders116[4] = {
    {1803889, 3, 0x177B21, 0x1B7B21, 16384, 181},        // patch_rt.mpq
    {21786162, 3, 0x14B3402, 0x14C3402, 4096, 931},      // BrooDat.mpq
    {66927148, 3, 0x3FB835C, 0x3FC835C, 4096, 2925},     // StarDat.mpq
    {571552776, 3, 0x221065C8, 0x2210E5C8, 2048, 1252},  // the CD's install archive
};
DWORD g_adwWirePtrs[3];   // the wireframe GRPs' 1.16.1 addresses
const DWORD kRepulsePtr116 = 0x6D5CD8;  // 1.16.1's pointer to the repulse counts
const DWORD kRepulseSize = 171 * 171;
DWORD g_dwRepulsePtr = 0;
DWORD g_adwTilePtrs[3];  // the tiles', their flags' and the tileset groups' 1.16.1 addresses
// iscript.bin's, aiscript.bin's, bwscript.bin's, the cell map's, the tile cells', the megatiles'
// and the scroll ring's
DWORD g_adwFilePtrs[7];
// The dialogs a map reaches through 1.16.1's pointers to them, as Remastered stands them in
// (kind 0x2C, its table at 0x140aa98e0, built by 0x1402e9390): a pointer, the size of its
// dialog's blocks and how many there are, one for the dialog and each of its controls.
const struct {
  DWORD dwPtr116, dwSize, nBlocks;
} kDialogs116[] = {
    {0x68C234, 0x31A, 8}, {0x68C1F0, 0x1388, 9}, {0x68AC8C, 0x10E, 6},
    {0x59CB5C, 0x1B6, 13}, {0x68C224, 0xB8, 11}, {0x68C140, 0x2DE, 12},
};
const DWORD kDialogBitmap = 0x109A0;  // the bitmap Remastered hangs on each block but the last
DWORD g_adwDialogPtrs[sizeof(kDialogs116) / sizeof(kDialogs116[0])];
std::vector<std::vector<BYTE>> g_dialogBlocks;
DWORD g_dwAfterUnitTree = 0;  // 0x5145BC, a dword past the unit requirements, which Remastered serves
// The ranges Remastered serves from its record of 1.16.1's memory as a game starts (kind 7 in its
// table): a dword of 1.16.1's code or read-only data a map reads to tell the game's version, tables
// 1.16.1 kept in another form, values Remastered makes up (the program's path at 0x51BAF8, a game
// password at 0x6D6394), 0x200000 to 0x207000, which starts with its credits, and three in 1.16.1's
// heap. A map's writes are kept, as Remastered keeps them. The bytes are Remastered's, read from the
// player's install (StreamRead); without them each is served as zeros, read only, a read logged.
// One another item serves is left to it.
const struct {
  DWORD dwAddr, dwSize;
} kBytes116[] = {
    {0x4887C0, 0x4},   {0x45134C, 0x4},   {0x451350, 0x4},   {0x4BD5EC, 0x4},  {0x492F90, 0x4},   {0x41E0D0, 0x4},
    {0x426918, 0x4},   {0x427900, 0x4},   {0x427CDC, 0x4},   {0x46044C, 0x4},  {0x489204, 0x4},   {0x4B995C, 0x4},
    {0x4BD5F0, 0x4},   {0x4D68F4, 0x4},   {0x4D98BC, 0x4},   {0x4F3288, 0x4},  {0x4FDFBC, 0x4},   {0x4F3360, 0x4},
    {0x4A3898, 0x4},   {0x4A389C, 0x4},   {0x4FF900, 0xC},   {0x51521C, 0x8},  {0x5128F8, 0xA0},  {0x51ABF0, 0xC},
    {0x51AC74, 0x10},  {0x51ABB0, 0x10},  {0x51ABC0, 0x14},  {0x6D6394, 0x18}, {0x655C58, 0x1B8}, {0x515300, 0x48},
    {0x51BAF8, 0x100}, {0x515348, 0x3C},  {0x515388, 0x60},  {0x5152A8, 0x54}, {0x68C208, 0x18},  {0x655E80, 0x1B8},
    {0x5973A0, 0x2000}, {0x657AA0, 0x1000}, {0x6CE000, 0xC0}, {0x6CA550, 0xFC}, {0x6CA64C, 0x18},  {0x6CE320, 0x400},
    {0x6CE720, 0x400}, {0x6CDFD4, 0x24},  {0x59C6C0, 0x400}, {0x6CEB20, 0x1A}, {0x59CB60, 0x100}, {0x51AAB0, 0x17},
    {0x51AAC8, 0x17},  {0x51AAE0, 0x17},  {0x51AB94, 0x19},  {0x51ABD4, 0x1A}, {0x51AC10, 0x19},  {0x51A440, 0x17},
    {0x51AC2C, 0x18},  {0x51AC48, 0x12},  {0x51AC5C, 0x17},  {0x51AD18, 0x16}, {0x51A424, 0x16},  {0x200000, 0x1000},
    {0x201000, 0x6000}, {0x19046138, 0x100}, {0x19018F38, 0x4}, {0x190452C8, 0x80},
};
std::vector<BYTE> g_bytes116;  // their bytes, back to back
BYTE g_abZeros[0x6000];
// Remastered's record of 1.16.1's memory as a game starts, which its EUD table's builder copies into
// the ranges of kind 7 (0x1402eaa4c, from 0x140aa9a00 in the 12409 dump, 0x7A99 bytes). It is in the
// read-only data of the install's x86_64\StarCraft.exe as the file holds it: a list of records, each
// a 1.16.1 address and then, the address's top bit clear, a dword; set, a size and that many bytes,
// or, the size's top bit set too, a buffer of that size, how many bytes it starts with and those
// bytes, zeros after. Its fourth record, at 0x2C, is 0x200000's (kAnchor), and it ends at an address
// of 0.
struct Record116 {
  DWORD dwAddr;
  std::vector<BYTE> bytes;
};
std::vector<Record116> g_stream116;
bool g_streamRead = false;
const BYTE kAnchor[] = {0x00, 0x00, 0x20, 0x80, 0x00, 0x10, 0x00, 0x80};
const size_t kAnchorAt = 0x2C;
// The dwords Remastered serves from buffers of its own that nothing else touches (its items from
// 0x1402f13af on whose host is in 0x141030d40 to 0x14103aafc), 1.16.1's command button sets'
// spare dwords among them.
const DWORD kSpare116[] = {
    0x515D64, 0x517354, 0x5177D4, 0x5176A4, 0x516ADC, 0x516BAC, 0x515E34, 0x516814, 0x517D14, 0x51626C,
    0x51659C, 0x516FA4, 0x51850C, 0x515F54, 0x5171E4, 0x516164, 0x5172EC, 0x51842C, 0x51863C, 0x5186CC,
    0x518144, 0x517DA4, 0x517B6C, 0x66FD0C, 0x5167AC, 0x517034, 0x5178DC, 0x640B5C, 0x5170C4, 0x515384,
    0x5152FC, 0x51639C, 0x51662C, 0x5168CC, 0x516984, 0x516D04, 0x516DBC, 0x516E74, 0x517154, 0x517994,
    0x64169C, 0x515E9C, 0x5160D4, 0x51642C, 0x5164E4, 0x516B44, 0x517A4C, 0x517AB4, 0x51763C, 0x6D1234,
    0x52E4C0, 0x68C1E0, 0x597238, 0x6D5E1C, 0x190913D8, 0x19046134,
};
DWORD g_adwSpare116[sizeof(kSpare116) / sizeof(kSpare116[0])];
// Ranges Remastered serves from state of its own that Hyperion has no counterpart for, kept for the
// map the same way, zeros as a game starts: 1.16.1's player records 9 to 12 (1.17 has 8; with more
// players Hyperion's are not in 1.16.1's layout); a flag 1.16.1 alone has, which turns a sound down
// to a fifth (0x48D10C), and the text a dialog of its alone formats a player's name into (0x4F2FA4);
// the invalidation map and the console mask, which Hyperion keeps in forms of its own for its view
// (port_view.cpp, PortMask); the cursors' file names, whose strings have no 1.16.1 address; and
// 0x400 bytes of storm.dll's data, which Remastered serves read only.
const struct {
  DWORD dwAddr, dwSize;
  const char *pszName;
  bool bReadOnly;
} kOwn116[] = {
    {0x57F000, 4 * 36, "players 9 to 12", false},  {0x64086C, 4, "a sound flag 1.16.1 alone has", false},
    {0x68C10C, 0x32, "a dialog text 1.16.1 alone keeps", false}, {0x6CEFF8, 0x4B0, "invalidation map", false},
    {0x6D5E14, 4, "console mask", false},          {0x512708, 0x4C, "cursor files", false},
    {0x1505E670, 0x400, "storm.dll's data", true},
};
std::vector<BYTE> g_own116;
// The tileset's megatiles in 1.16.1's form, a WORD a cell where Hyperion keeps a DWORD
// (PortMegatilesLoad): the cells 1.17 has are the same numbers in both.
std::vector<WORD> g_megatiles116;
// The command cards, 1.16.1's 250 of 12 bytes, and the 1.16.1 address of the buttons a map has
// pointed each at in memory of its own, 0 for a card with 1.17's; such a card shows the engine's
// copy of those buttons (StatcmdEudButtons), built again as each trigger pass ends, as a map may
// write the pointer a byte at a time over several triggers.
const DWORD kCards116 = 0x5187E8;
const DWORD kCards = 250;
DWORD g_adwCardButtons116[kCards];
// The tables 1.17 keeps for the program's run that an EUD map writes in place, the requirement
// trees, the cards, their buttons and the panels: their 1.17 ranges, and the engine's bytes behind
// them as the EUD game started, field by field, put back as the next game starts.
std::vector<std::pair<DWORD, DWORD>> g_staticRanges;
std::vector<std::pair<BYTE *, std::vector<BYTE>>> g_static;
// And every .dat table, which 1.17 loads once for the program's run: a map's writes would
// otherwise reach the next game, a melee game's units with the map's stats, or a unit made as
// the map's next game starts with an image whose draw function the map had left past the table.
// And iscript.bin, also loaded once, whose edits a map makes again in its next game: on top of the
// last game's, they can play a frame past an image's GRP. The others the map is served are loaded
// for each game. A buffer replaced since (a mod's scripts added) is not written.
struct StaticDat {
  const char *pszName;
  BYTE *p;
  std::vector<BYTE> bytes;
  void *const *ppOwner;  // the global that holds a loaded file, NULL for a .dat table
};
std::vector<StaticDat> g_staticDat;
// The code map by engine function (PortCodeMap), for a function pointer's 1.16.1 address.
std::vector<std::pair<UINT_PTR, DWORD>> g_codeFns;

// The pools whose free lists a map may build again of nodes of its own, as euddraft's unlimiter
// does as a program starts (plugins/unlimiter.py): a write to an end of the list that stands for
// nothing in the pool is held until the trigger pass has run, as Remastered defers it, then the list
// the map wrote is walked by its next links and the pool grows by as many objects, Remastered's
// up to the list's length (0x1402ec4c0) and Hyperion's up to the storage its pools have
// (poollimits.h), the new slots at the free list's tail. A grown slot's 1.16.1 address is the
// map's node it stands for, when the nodes lie a stride apart, as the unlimiter's do; else it
// has none, and reads as NULL.
struct Pool116 {
  const char *pszName;
  DWORD dwHead116, dwTail116;  // 1.16.1's free list
  DWORD dwStride116;
  DWORD dwHead117, dwTail117;
  BYTE *pBase;  // Hyperion's pool, cbHost a slot, *pnCount slots in this game, nMax at most
  size_t cbHost;
  int *pnCount;
  int nMax;
  CLink **ppHead, **ppTail;
  bool bSprites;  // a sprite's slot holds its index
  // the ends the map wrote, until the trigger has run
  DWORD adwWritten[2];
  bool bWritten;
  int nFrom;          // the first slot grown, 0 for none
  DWORD dwGrown116;   // its 1.16.1 address, 0 for none
};
Pool116 g_pools116[4];
DWORD g_dwHeapNext = 0;   // the next 1.16.1 address a buffer of the game's takes, past the nodes
std::vector<BYTE> g_nodes;  // kLinkPad bytes, then a node every kNodeStride
DWORD g_nNodes = 0;
bool g_triggersBuilt = false;

unsigned g_mapKind = 0;  // EudMapKind
bool g_masked = false;
DWORD g_dwMask = 0;

// Every unserved access logged, by its dword's address and whether it was a write.
std::set<unsigned long long> g_logged;
unsigned g_unserved = 0;
bool g_continue = false;  // HYPERION_EUD=continue
bool g_trace = false;     // HYPERION_EUD=trace: continue, and log the nodes run
unsigned g_traced = 0;
DWORD g_nPassList = 0;    // the list being walked, and the nodes it has run
unsigned g_nPassNodes = 0;
DWORD g_dwNode = 0;              // the node running, by its 1.16.1 address, and its bytes
const BYTE *g_pNode = NULL;
// The last accesses served, a ring, which the log shows before the first one not served.
struct Access {
  DWORD dwNode, dwAddr, dwValue;
  bool bWrite;
};
Access g_recent[256];
unsigned g_nRecent = 0;
unsigned g_nRecentShown = 0;  // the first four logged accesses show the ones before them

void Recent(DWORD dwAddr, DWORD dwValue, bool bWrite) {
  Access &a = g_recent[g_nRecent++ % 256];
  a.dwNode = g_dwNode;
  a.dwAddr = dwAddr;
  a.dwValue = dwValue;
  a.bWrite = bWrite;
}

// A 1.17 global with 1.16.1's layout, as an item of 1.16.1's address: its bytes when it is in
// the block, else a twin served field by field. Every twin keeps its 1.17 address, by which a
// pointer to it is turned back into 1.16.1's.
Item Twin(DWORD dwAddr116, DWORD dwAddr117, DWORD dwSize, const char *pszName, bool bReadOnly = false) {
  Item it = MakeItem(dwAddr116, dwSize, NULL, pszName, bReadOnly);
  if (PortDataPlain(dwAddr117, dwSize))
    it.pHost = reinterpret_cast<BYTE *>(OC_VA(dwAddr117));
  it.dwBase117 = dwAddr117;
  return it;
}

void PoolsBuild() {
  const Pool116 pools[4] = {
      {"images", 0x57EB68, 0x57EB70, 0x40, 0x544694, 0x592898, reinterpret_cast<BYTE *>(sgImages), sizeof(CImage),
       &g_pools.images, POOL_IMAGES_MAX, reinterpret_cast<CLink **>(&images_sgpFreeHead_g),
       reinterpret_cast<CLink **>(&images_sgpFreeTail), false},
      {"sprites", 0x63FE30, 0x63FE34, 0x24, 0x5C51C8, 0x5C51CC, reinterpret_cast<BYTE *>(sgSprites),
       sizeof(CSprite), &g_pools.sprites, POOL_SPRITES_MAX, reinterpret_cast<CLink **>(&sprites_sgpFreeHead),
       reinterpret_cast<CLink **>(&sprites_sgpFreeTail), true},
      {"bullets", 0x64EED8, 0x64EEDC, 0x70, 0x540758, 0x54075C, reinterpret_cast<BYTE *>(sgBullets),
       sizeof(CBullet), &g_pools.bullets, POOL_BULLETS_MAX, &sgBulletsFreeHead_g, &sgBulletsFreeTail_g, false},
      {"orders", 0x64B2E0, 0x64B2E4, 0x14, 0x5AEF00, 0x5AEF04, reinterpret_cast<BYTE *>(sgOrders), sizeof(COrder),
       &g_pools.orders, POOL_ORDERS_MAX, reinterpret_cast<CLink **>(&sgpOrderPoolHead_g),
       reinterpret_cast<CLink **>(&sgpOrderPoolTail_g), false},
  };
  for (int i = 0; i < 4; i++) {
    g_pools116[i] = pools[i];
    for (int nEnd = 0; nEnd < 2; nEnd++) {
      Pool116 &pool = g_pools116[i];
      Item it = Twin(nEnd ? pool.dwTail116 : pool.dwHead116, nEnd ? pool.dwTail117 : pool.dwHead117, 4, pool.pszName);
      it.nPool = i;
      it.bTail = nEnd != 0;
      g_items.push_back(it);
    }
  }
}

// Whether a host pointer is a slot of a pool in this game.
bool PoolHolds(const Pool116 &pool, UINT_PTR q) {
  const UINT_PTR base = reinterpret_cast<UINT_PTR>(pool.pBase);
  return q - base < static_cast<UINT_PTR>(*pool.pnCount) * pool.cbHost && (q - base) % pool.cbHost == 0;
}

Item StaticTwin(DWORD dwAddr116, DWORD dwAddr117, DWORD dwSize, const char *pszName) {
  g_staticRanges.push_back(std::make_pair(dwAddr117, dwSize));
  return Twin(dwAddr116, dwAddr117, dwSize, pszName);
}

void SortMap() {
  std::sort(g_items.begin(), g_items.end(), [](const Item &a, const Item &b) { return a.dwBase < b.dwBase; });
  g_pLast = NULL;
  for (size_t i = 0; i + 1 < g_items.size(); i++)
    if (g_items[i].dwBase + g_items[i].dwSize > g_items[i + 1].dwBase)
      Log("items overlap: %s at 0x%08X and %s at 0x%08X", g_items[i].pszName, g_items[i].dwBase,
          g_items[i + 1].pszName, g_items[i + 1].dwBase);
}

// Each .dat field as an item over Hyperion's array, as many bytes as both hold, then the gaps of
// up to 16 bytes between one array and the next. A column of ids the build keeps wider than
// 1.17's byte (h/typelimits.h) is a twin, served element by element as its low byte.
void DatItems() {
  std::vector<std::pair<DWORD, DWORD>> spans;
  for (const Dat116 &dat : kDats116) {
    for (size_t i = 0; i < dat.nFields && dat.pTable[i].pDest != NULL; i++) {
      const DatField116 &f = dat.pFields[i];
      const DWORD cb116 = f.dwElem * f.dwCount;
      const DWORD cbHost = dat.pTable[i].dwCount * dat.pTable[i].dwElemSize;
      if (cbHost != cb116)
        Log("%s field %u: %u bytes in 1.16.1, %u in Hyperion", dat.pszName, static_cast<unsigned>(i), cb116, cbHost);
      const bool bReadOnly = std::find(std::begin(kDatReadOnly116), std::end(kDatReadOnly116), f.dwAddr) !=
                             std::end(kDatReadOnly116);
      const DWORD cb = cb116 < cbHost ? cb116 : cbHost;
      if (PortDataBytes(dat.pTable[i].pDest, cb) != cb)
        g_items.push_back(Twin(f.dwAddr, PortOriginalAddress(reinterpret_cast<UINT_PTR>(dat.pTable[i].pDest)), cb,
                               dat.pszName, bReadOnly));
      else
        g_items.push_back(MakeItem(f.dwAddr, cb, dat.pTable[i].pDest, dat.pszName, bReadOnly));
      spans.push_back(std::make_pair(f.dwAddr, f.dwAddr + cb116));
    }
  }
  std::sort(spans.begin(), spans.end());
  size_t nGaps = 0;
  memset(g_abDatGaps, 0, sizeof(g_abDatGaps));
  for (size_t i = 0; i + 1 < spans.size() && nGaps < 192; i++) {
    const DWORD dwGap = spans[i + 1].first - spans[i].second;
    if (spans[i + 1].first > spans[i].second && dwGap <= 16)
      g_items.push_back(MakeItem(spans[i].second, dwGap, g_abDatGaps[nGaps++], "between .dat arrays"));
  }
}

// The stream's records from pb on, to an address of 0; false should one not be a record of its.
bool StreamParse(const BYTE *pb, const BYTE *pbEnd, std::vector<Record116> *pRecords) {
  pRecords->clear();
  while (pbEnd - pb >= 4) {
    DWORD dwAddr, n;
    memcpy(&dwAddr, pb, 4);
    if (dwAddr == 0)
      return !pRecords->empty();
    const DWORD a = dwAddr & 0x7FFFFFFF;
    if (a < 0x200000 || a >= 0x20000000 || pbEnd - pb < 12 || pRecords->size() >= 0x10000)
      return false;
    memcpy(&n, pb + 4, 4);
    Record116 r = {a, std::vector<BYTE>()};
    if (!(dwAddr & 0x80000000)) {
      r.bytes.assign(pb + 4, pb + 8);
      pb += 8;
    } else if (!(n & 0x80000000)) {
      if (n > 0x10000 || static_cast<size_t>(pbEnd - pb - 8) < n)
        return false;
      r.bytes.assign(pb + 8, pb + 8 + n);
      pb += 8 + n;
    } else {
      DWORD nHas;
      memcpy(&nHas, pb + 8, 4);
      n &= 0x7FFFFFFF;
      if (n > 0x10000 || nHas > n || static_cast<size_t>(pbEnd - pb - 12) < nHas)
        return false;
      r.bytes.assign(pb + 12, pb + 12 + nHas);
      r.bytes.resize(n, 0);
      pb += 12 + nHas;
    }
    pRecords->push_back(r);
  }
  return false;
}

// The stream, out of the Remastered install Hyperion reads the game's files from, once.
void StreamRead() {
  if (g_streamRead)
    return;
  g_streamRead = true;
  if (!PortCascAvailable())
    return;
  char szExe[MAX_PATH];
  SDL_snprintf(szExe, sizeof szExe, "%s\\x86_64\\StarCraft.exe", PortCascFolder());
  size_t nSize = 0;
  BYTE *pFile = static_cast<BYTE *>(SDL_LoadFile(PortNativePath(szExe), &nSize));
  if (pFile == NULL) {
    Log("1.16.1's memory as a game starts: %s not read", szExe);
    return;
  }
  const BYTE *pEnd = pFile + nSize;
  const BYTE *p = pFile + kAnchorAt;
  while ((p = std::search(p, pEnd, kAnchor, kAnchor + sizeof kAnchor)) != pEnd) {
    if (StreamParse(p - kAnchorAt, pEnd, &g_stream116))
      break;
    p++;
  }
  if (p == pEnd)
    g_stream116.clear();
  size_t cb = 0;
  for (const Record116 &r : g_stream116)
    cb += r.bytes.size();
  Log("1.16.1's memory as a game starts: %u records, %u bytes, from %s", static_cast<unsigned>(g_stream116.size()),
      static_cast<unsigned>(cb), g_stream116.empty() ? "nowhere" : szExe);
  SDL_free(pFile);
}

void BuildMap() {
  if (!g_items.empty())
    return;
  // The game's data, 1.16.1's from 0x57F0F0 to 0x5967F0 and 1.17's from 0x5263D0, in the same
  // layout: the resources, the map's size, shared vision, the frame count, the units each player
  // may make, the scores and counts, the death table at 0x58A364, tech and upgrades, forces,
  // alliances, the game's seconds, the locations, and the unused bytes at its end. The matched
  // globals inside it agree (the map's size, shared vision, the countdown, Brood War's flag),
  // and so does every global the two have named. The frame count is read only: a replay plays
  // by it.
  const DWORD kGame116 = 0x57F0F0, kGame117 = 0x5263D0, kGameEnd116 = 0x5967F0;
  const DWORD kFrames116 = 0x57F23C;
  g_items.push_back(Twin(kGame116, kGame117, kFrames116 - kGame116, "game data"));
  g_items.push_back(Twin(kFrames116, kGame117 + (kFrames116 - kGame116), 4, "frames", true));
  g_items.push_back(Twin(kFrames116 + 4, kGame117 + (kFrames116 + 4 - kGame116), kGameEnd116 - kFrames116 - 4,
                         "game data"));
  // The current player, which Set Deaths for player 13 reads: 1.17's trigger player.
  g_items.push_back(Twin(0x6509B0, 0x6CA754, 4, "current player"));
  // The trigger timer: 0 runs the triggers each frame, as EUD maps keep it. A WORD in a dword's
  // place in 1.17.
  g_items.push_back(Twin(0x6509A0, 0x6CA748, 4, "trigger timer"));
  // The local player, and the game text's 13 lines of 218 bytes (matched in both).
  g_items.push_back(Twin(0x512684, 0x508D50, 4, "local player", true));
  // Whether a replay plays (gPlayback, matched in both).
  g_items.push_back(Twin(0x6D0F14, 0x67CC90, 4, "in replay", true));
  g_items.push_back(Twin(0x640B60, 0x66FF00, 13 * 218, "game text"));
  g_items.push_back(MakeItem(kStormImport116, 4, &g_dwStormImport, "storm.dll import", true));
  // The units, CUnit[1700] of 0x150 bytes in both, and their lists, each a head and a tail.
  // 1.16.1 keeps them in another order; its unlinks (0x49DEA0 the used list, 0x49DE00 the free,
  // 0x49DE50 the units inside others, 0x49E3F0 the scanner sweeps and the dead) set a head to the
  // unit's next and a tail to its previous, as CLink::Unlink does.
  g_items.push_back(Twin(0x59CCA8, 0x5DA3A0, 1700 * 0x150, "units"));
  const struct {
    DWORD dwHead116, dwTail116, dwHead117;
    const char *pszName;
  } kUnitLists[] = {
      {0x628430, 0x59CC9C, 0x665AE0, "units in play"},  {0x62842C, 0x59CC98, 0x665AE8, "dying units"},
      {0x628438, 0x62843C, 0x665AF0, "free units"},     {0x6283EC, 0x628428, 0x665AF8, "units inside others"},
      {0x6283F4, 0x628434, 0x665B00, "scanner sweeps"},
  };
  for (const auto &l : kUnitLists) {
    g_items.push_back(Twin(l.dwHead116, l.dwHead117, 4, l.pszName));
    g_items.push_back(Twin(l.dwTail116, l.dwHead117 + 4, 4, l.pszName));
  }
  // Each player's units (EUDLoopPlayerUnit), and the count of units in play, which only the
  // game changes (Remastered serves it read only).
  g_items.push_back(Twin(0x6283F8, 0x665B08, 12 * 4, "player units"));
  g_items.push_back(Twin(0x6283F0, 0x665B38, 4, "unit count", true));
  // The images, CImage[5000] of 0x40 bytes; the sprites, CSprite[2500] of 0x24, and the lists of
  // each row of the map, heads at 0x629688 and tails at 0x629288 (0x497978); the orders,
  // COrder[2000] of 0x14; the bullets, CBullet[100] of 0x70, those in flight, and their count,
  // which 1.16.1's allocator tests for weapon 0x67 (0x48C273), as CBullet.cpp's does. Their free
  // lists are PoolsBuild's: the images' at 0x57EB68 (0x4D4D29 puts one back at the head), the
  // sprites' at 0x63FE30, the bullets' at 0x64EED8 (the allocator at 0x48C264) and the orders' at
  // 0x64B2E0 (0x474336). Remastered serves each head 0x08 and tail 0x10.
  g_items.push_back(Twin(0x52F568, 0x544698, 5000 * 0x40, "images"));
  g_items.push_back(Twin(0x629D98, 0x5AF238, 2500 * 0x24, "sprites"));
  g_items.push_back(Twin(0x629688, 0x5C51D0, 256 * 4, "sprites by row"));
  g_items.push_back(Twin(0x629288, 0x5C55D0, 256 * 4, "sprites by row"));
  g_items.push_back(Twin(0x6416A0, 0x5A52C0, 2000 * 0x14, "orders"));
  g_items.push_back(Twin(0x64B2E8, 0x53DB90, 100 * 0x70, "bullets"));
  g_items.push_back(Twin(0x64DEC4, 0x540750, 4, "bullets in flight"));
  g_items.push_back(Twin(0x64DEC8, 0x540754, 4, "bullets in flight"));
  g_items.push_back(Twin(0x64DEBC, 0x53DB8C, 4, "bullet count"));
  // The players' structures, PlayerData of 36 bytes: 1.16.1 has twelve, 1.17's gPlayerData eight
  // (matched in both), so the last four are not served.
  g_items.push_back(Twin(0x57EEE0, 0x6726C8, 8 * 36, "players"));
  // What the local player sees and does, which a map reads for its own interface: the view's
  // position in pixels (matched in both), the keys held (1.16.1 sets 0x596A18 + key on a key
  // pressed as event_Win.cpp sets gKeyTbl), the mouse buttons and the cursor's position.
  g_items.push_back(Twin(0x62848C, 0x543218, 4, "view x", true));
  g_items.push_back(Twin(0x6284A8, 0x54321C, 4, "view y", true));
  g_items.push_back(Twin(0x596A18, 0x665E58, 256, "keys", true));
  g_items.push_back(Twin(0x6CDDC0, 0x6D315C, 2, "mouse buttons", true));
  g_items.push_back(Twin(0x6CDDC4, 0x6D3154, 8, "cursor", true));
  // The requirement trees of units, upgrades, research, tech use and orders, 1.16.1's byte for
  // byte in 1.17, which pads each to the next table's start. The indexes into them are built
  // as the game starts, in 1.16.1 and Remastered as here, so a map that rewrites a tree keeps
  // its runs where they were.
  g_staticRanges.clear();
  g_items.push_back(StaticTwin(0x514178, 0x50DA88, 0x442, "unit requirements"));
  g_items.push_back(MakeItem(0x5145BC, 4, &g_dwAfterUnitTree, "after the unit requirements"));
  g_items.push_back(StaticTwin(0x5145C0, 0x501958, 0x346, "upgrade requirements"));
  g_items.push_back(StaticTwin(0x514908, 0x501CA0, 0x13E, "research requirements"));
  g_items.push_back(StaticTwin(0x514A48, 0x501DE0, 0x2B0, "tech use requirements"));
  g_items.push_back(StaticTwin(0x514CF8, 0x502090, 0x524, "order requirements"));
  // The command cards (gCards; 250 of 12 bytes, the count, the buttons, the link), the buttons
  // they point at, and each unit type's status panel functions (gTypeFcns): the same in both,
  // their functions read and written as 1.16.1's addresses (Code116).
  for (const EudButtons116 &b : kEudButtons116)
    g_items.push_back(StaticTwin(b.dwVa116, b.dwVa117, 20 * b.nButtons, "buttons"));
  Item cards = StaticTwin(kCards116, 0x50AE98, kCards * 12, "command cards");
  cards.bCards = true;
  g_items.push_back(cards);
  g_items.push_back(StaticTwin(0x5193A0, 0x50BA50, 228 * 12, "unit panels"));
  // The game text's next line; the largest command packet (read only in Remastered); the fog's
  // bit mask (mask_init).
  g_items.push_back(Twin(0x640B58, 0x670A12, 1, "next game text line"));
  g_items.push_back(Twin(0x57F0D8, 0x67265C, 4, "largest packet", true));
  // The commands the local player sends this turn and their bytes, which a map adds to as
  // euddraft's QueueGameCommand does, and Remastered serves both written (netmgr_queue_cmd's).
  g_items.push_back(Twin(0x654880, 0x6729D8, 0x200, "commands queued"));
  g_items.push_back(Twin(0x654AA0, 0x6729D4, 4, "bytes of commands queued"));
  g_items.push_back(Twin(0x657A9C, 0x671A80, 1, "fog mask"));
  // The local player's selection, which a map reads for its own interface: 1.17's gpNewUnits (the
  // box selection's shift merge reads it in both, 0x46FA97 and 0x4BC31C). Remastered serves it as
  // unit pointers; a write would change one player's selection only, and is dropped.
  g_items.push_back(Twin(0x6284B8, 0x6A6070, 12 * 4, "selection", true));
  // Whether the local player is typing a message (gbInMsgMode, which textbox_mode_switch sets
  // in both; euddraft's NotTyping reads it), the card the command card shows (sgCard), whether
  // the game is a multiplayer one, and the scroll speeds by the keys' and the edge's directions
  // (scroll_update's table), first among Remastered's ranges.
  g_items.push_back(Twin(0x68C144, 0x6BE658, 1, "typing", true));
  g_items.push_back(Twin(0x68C14C, 0x6AAAF0, 2, "card shown", true));
  g_items.push_back(Twin(0x57F0B4, 0x672664, 1, "multiplayer", true));
  g_items.push_back(StaticTwin(0x513B68, 0x509390, 0x31, "scroll speeds"));
  // The milliseconds a frame takes at each game speed (sgTicksPerFrame_g, the same seven values),
  // which a map shortens to play faster than Fastest.
  g_items.push_back(StaticTwin(0x5124D8, 0x503360, 0x1C, "frame times"));
  // The game loop's own: the frames a turn takes at each speed (filled from the frame times in
  // both, 0x4D932E), the scroll timer RunGameLoop counts to 10 (sgdwLastScrollUpdateTime_g), and
  // the dword beside it, paired the same way (dword_6663E4).
  g_items.push_back(Twin(0x51CE70, 0x666404, 0x1C, "frames a turn"));
  g_items.push_back(Twin(0x51CE8C, 0x6663F4, 4, "scroll timer"));
  g_items.push_back(Twin(0x51CE98, 0x6663E4, 4, "game loop"));
  // Paired by the global references of paired functions (tools/eud_pair116.py), read only
  // where Remastered drops a write or a write would reach one machine only:
  // each player's selection (gSelected, 12 a player), the local player's selection group
  // (gpStatUnits), the active and local player numbers beside g_LocalNationID's,
  g_items.push_back(Twin(0x6284E8, 0x6A5EF0, 0x180, "selections", true));
  g_items.push_back(Twin(0x597208, 0x6AB4C8, 0x30, "selection group", true));
  g_items.push_back(Twin(0x512678, 0x508D44, 4, "active player", true));
  g_items.push_back(Twin(0x51267C, 0x508D48, 4, "active human", true));
  g_items.push_back(Twin(0x512688, 0x508D54, 4, "local human", true));
  // the screen's last position in pixels and in tiles (Remastered drops writes),
  g_items.push_back(Twin(0x628448, 0x543210, 4, "screen x", true));
  g_items.push_back(Twin(0x628470, 0x543214, 4, "screen y", true));
  g_items.push_back(Twin(0x628498, 0x543208, 2, "screen tile x", true));
  g_items.push_back(Twin(0x6284AC, 0x54320C, 2, "screen tile y", true));
  // the game's random number state, which every machine runs alike,
  g_items.push_back(Twin(0x51CA14, 0x67393C, 4, "random state"));
  // the game's header (map name, size, speed and the like) and the network's turn and time
  // values (Remastered drops writes),
  g_items.push_back(Twin(0x5967F8, 0x6728D0, 0x8D, "game header", true));
  g_items.push_back(Twin(0x57F090, 0x672660, 4, "turns in transit", true));
  g_items.push_back(Twin(0x51CEA0, 0x6663FC, 4, "turn timer", true));
  g_items.push_back(Twin(0x6556E4, 0x672990, 4, "latency", true));
  g_items.push_back(Twin(0x59CC78, 0x67502C, 4, "drop window time", true));
  g_items.push_back(Twin(0x59CC80, 0x675020, 4, "drop timer", true));
  // the races' sounds loaded ahead, the unit finder's marks, each image's damage overlays and
  // the last palette.
  g_items.push_back(Twin(0x63FF78, 0x666C10, 0x33C, "Terran sounds loaded", true));
  g_items.push_back(Twin(0x6402B8, 0x666980, 0x290, "Zerg sounds loaded", true));
  g_items.push_back(Twin(0x640550, 0x666F50, 0x31A, "Protoss sounds loaded", true));
  g_items.push_back(Twin(0x6BD3D0, 0x5D6E60, 0x1A94, "unit finder marks", true));
  g_items.push_back(Twin(0x5240D0, 0x543310, 0x3E7, "damage overlays", true));
  g_items.push_back(Twin(0x50E064, 0x503E88, 4, "last palette", true));
  // The replay's header, 633 bytes laid out alike in both (Remastered serves its own, read only),
  g_items.push_back(Twin(0x6D0F30, 0x67CCA0, 0x279, "replay header", true));
  // and what Remastered serves from buffers of its own that nothing fills, so zeros, writes
  // dropped: the images' GRP pointers and their overlay tables, the game screen's buffer and two
  // single values (its plain items, 0x1402f21a9 to 0x1402f2311).
  const struct {
    DWORD dwAddr, dwSize;
    const char *pszName;
  } kZeros116[] = {
      {0x51CED0, 0xF9C, "image GRP pointers"}, {0x51F2A8, 0x4E0C, "image overlay pointers"},
      {0x52E5C8, 0xF9C, "a pointer for each image"}, {0x6CEFF0, 8, "game screen buffer"},
      {0x68C238, 4, "a value Remastered keeps as 0"}, {0x6D0F24, 4, "a value Remastered keeps as 0"},
  };
  static_assert(sizeof(g_abZeros) >= 0x4E0C, "the largest of them");
  for (const auto &z : kZeros116)
    g_items.push_back(MakeItem(z.dwAddr, z.dwSize, g_abZeros, z.pszName, true));
  // Globals Remastered reads and writes in place at its own copy of them (its plain items whose
  // flags come from a register), paired by tools/eud_pair116.py or by their bytes: the cheat
  // flags, the AI's threat bases, the pathing bins, their edges, count and index (1.16.1's
  // ResetBins, 0x422A50, stores 1, 0 and -1 to them as 1.17's does), the costs the banker stages
  // (supply, gas, ore: 1.17 keeps them in the other order), the sight hash, the hit points the
  // unit panel last drew (0x6CA94C, an entry after 0x1402f70b4), the movement step
  // staged, the unit finder's X order (Remastered drops writes), and constant tables of the
  // contour search, the cursor and the sprites, which a map may rewrite as it may in Remastered.
  g_items.push_back(Twin(0x6D5A6C, 0x5432FC, 4, "cheat flags"));
  g_items.push_back(Twin(0x6BB210, 0x6AC530, 0x720, "AI threat bases"));
  g_items.push_back(Twin(0x6CAFA0, 0x69F018, 0x2A00, "path bins"));
  g_items.push_back(Twin(0x6CD9A0, 0x69EC08, 0x204, "path bin edges"));
  g_items.push_back(Twin(0x6CAF98, 0x69EE0C, 4, "path bin count"));
  g_items.push_back(Twin(0x6CDBA8, 0x69EE10, 0x204, "path bin index"));
  g_items.push_back(Twin(0x6CA4BC, 0x53DB30, 0x30, "supply cost staged"));
  g_items.push_back(Twin(0x6CA4EC, 0x53DB00, 0x30, "gas cost staged"));
  g_items.push_back(Twin(0x6CA51C, 0x53DAD0, 0x30, "ore cost staged"));
  g_items.push_back(Twin(0x629C90, 0x5AF138, 0x100, "sight hash"));
  g_items.push_back(Twin(0x6CA94C, 0x6AAB68, 0x30, "unit panel's hit points"));
  g_items.push_back(Twin(0x63FED8, 0x543290, 4, "step x staged"));
  g_items.push_back(Twin(0x63FF40, 0x543294, 4, "step y staged"));
  g_items.push_back(Twin(0x66FF78, 0x5C99B8, 0x6A40, "unit order x", true));
  const struct {
    DWORD dwAddr116, dwAddr117, dwSize;
  } kTables116[] = {
      {0x515AF8, 0x508EE8, 0x10}, {0x515B08, 0x508EF8, 0x10}, {0x515B18, 0x508F08, 0x20},
      {0x515B38, 0x508F28, 0x20}, {0x515B58, 0x508F48, 8},    {0x515B60, 0x508F50, 8},
      {0x515B68, 0x508F58, 8},    {0x515B70, 0x503228, 0x78}, {0x51ABFC, 0x5031AC, 0x14},
  };
  for (const auto &t : kTables116)
    g_items.push_back(StaticTwin(t.dwAddr116, t.dwAddr117, t.dwSize, "a constant table"));
  // Dwords Remastered keeps in buffers of its own that nothing else reads or writes (zeros in
  // its running image, no reference but the table's): a map's writes are kept for it to read
  // back, and do nothing else.
  memset(g_adwSpare116, 0, sizeof(g_adwSpare116));
  for (size_t i = 0; i < sizeof(kSpare116) / sizeof(kSpare116[0]); i++)
    g_items.push_back(MakeItem(kSpare116[i], 4, &g_adwSpare116[i], "a dword of the map's"));
  size_t cbOwn = 0;
  for (const auto &o : kOwn116)
    cbOwn += o.dwSize;
  g_own116.assign(cbOwn, 0);
  BYTE *pbOwn = g_own116.data();
  for (const auto &o : kOwn116) {
    g_items.push_back(MakeItem(o.dwAddr, o.dwSize, pbOwn, o.pszName, o.bReadOnly));
    pbOwn += o.dwSize;
  }
  // The status panel's flags 1.16.1 sets where 1.17 calls a request (statport.cpp, status.cpp), and
  // the blitters' colour remap, which 1.16.1 reads at 0x50CDC1 where 1.17 reads _gbRemapColorTbl.
  g_items.push_back(Twin(0x59723C, 0x6AB4FC, 1, "selection changed"));
  g_items.push_back(Twin(0x68AC74, 0x6AB3A0, 1, "portrait update"));
  g_items.push_back(Twin(0x50CDC0, 0x501020, 0x101, "colour remap"));
  PoolsBuild();
  DatItems();
  StreamRead();
  size_t cbBytes = 0;
  for (const auto &r : kBytes116)
    cbBytes += r.dwSize;
  g_bytes116.assign(cbBytes, 0);
  BYTE *pb = g_bytes116.data();
  for (const auto &r : kBytes116) {
    bool bServed = false;
    for (const Item &it : g_items)
      bServed = bServed || (r.dwAddr < it.dwBase + it.dwSize && it.dwBase < r.dwAddr + r.dwSize);
    DWORD nHave = 0;
    for (const Record116 &s : g_stream116) {
      const DWORD lo = (std::max)(r.dwAddr, s.dwAddr);
      const DWORD hi = (std::min)(r.dwAddr + r.dwSize, s.dwAddr + static_cast<DWORD>(s.bytes.size()));
      if (lo < hi) {
        memcpy(pb + (lo - r.dwAddr), &s.bytes[lo - s.dwAddr], hi - lo);
        nHave += hi - lo;
      }
    }
    if (!bServed) {
      const bool bHave = nHave >= r.dwSize;
      Item it = MakeItem(r.dwAddr, r.dwSize, bHave ? pb : g_abZeros,
                         bHave ? "1.16.1's memory as a game starts" : "1.16.1's own bytes", !bHave);
      it.bStandIn = !bHave;
      g_items.push_back(it);
    }
    pb += r.dwSize;
  }
  SortMap();
}

const Item *Find(DWORD dwAddr) {
  if (g_pLast != NULL && dwAddr - g_pLast->dwBase < g_pLast->dwSize)
    return g_pLast;
  BuildMap();
  auto it = std::upper_bound(g_items.begin(), g_items.end(), dwAddr,
                             [](DWORD a, const Item &item) { return a < item.dwBase; });
  if (it == g_items.begin())
    return NULL;
  --it;
  if (dwAddr - it->dwBase >= it->dwSize)
    return NULL;
  g_pLast = &*it;
  return g_pLast;
}

void UnservedPointer(DWORD dwAddr, bool bWrite);

// The 1.16.1 address of what an engine pointer points at, through the twin whose 1.17 range
// holds its 1.17 address; 0 for NULL, and for one no twin serves, logged.
DWORD Address116(UINT_PTR p) {
  if (p == 0)
    return 0;
  for (const Pool116 &pool : g_pools116) {
    const UINT_PTR first = reinterpret_cast<UINT_PTR>(pool.pBase) + pool.nFrom * pool.cbHost;
    if (pool.nFrom == 0 || p - first >= (*pool.pnCount - pool.nFrom) * pool.cbHost)
      continue;
    if (pool.dwGrown116 == 0 || (p - first) % pool.cbHost != 0) {
      if (g_logged.insert((3ull << 40) | pool.dwHead116).second)
        Log("frame %u: a pointer read to one of the %s past 1.16.1's, which has no 1.16.1 address",
            oc_dwReplayFrame, pool.pszName);
      return 0;
    }
    return pool.dwGrown116 + static_cast<DWORD>((p - first) / pool.cbHost) * pool.dwStride116;
  }
  const DWORD a = PortOriginalAddress(p);
  for (const Item &it : g_items)
    if (it.dwBase117 != 0 && a - it.dwBase117 < it.dwSize)
      return it.dwBase + (a - it.dwBase117);
  UnservedPointer(a, false);
  return 0;
}

// The reverse: the engine pointer a 1.16.1 address stands for, false for none.
bool Pointer116(DWORD dwAddr, UINT_PTR *pp) {
  if (dwAddr == 0) {
    *pp = 0;
    return true;
  }
  for (const Pool116 &pool : g_pools116) {
    if (pool.dwGrown116 == 0 || (dwAddr - pool.dwGrown116) % pool.dwStride116 != 0)
      continue;
    const DWORD k = (dwAddr - pool.dwGrown116) / pool.dwStride116;
    if (k < static_cast<DWORD>(*pool.pnCount - pool.nFrom)) {
      *pp = reinterpret_cast<UINT_PTR>(pool.pBase) + (pool.nFrom + k) * pool.cbHost;
      return true;
    }
  }
  for (const Item &it : g_items) {
    if (it.dwBase117 == 0 || dwAddr - it.dwBase >= it.dwSize)
      continue;
    const DWORD a = it.dwBase117 + (dwAddr - it.dwBase);
    PortField f;
    if (!PortDataField(a, &f) || (f.kind != WIDE_COPY && f.va != a))
      return false;
    *pp = f.p + (f.kind == WIDE_COPY ? a - f.va : 0);
    return true;
  }
  return false;
}

// The 1.16.1 address of an engine function the panel and button tables hold, 0 for another.
DWORD Code116(UINT_PTR fn) {
  if (g_codeFns.empty()) {
    ULONG n = 0;
    const PortCodeRow *pRows = PortCodeMap(&n);
    for (ULONG i = 0; i < n; i++)
      g_codeFns.push_back(std::make_pair(reinterpret_cast<UINT_PTR>(pRows[i].fn), static_cast<DWORD>(pRows[i].va)));
    std::sort(g_codeFns.begin(), g_codeFns.end());
  }
  auto it = std::lower_bound(g_codeFns.begin(), g_codeFns.end(), std::make_pair(fn, static_cast<DWORD>(0)));
  for (; it != g_codeFns.end() && it->first == fn; ++it)
    for (const EudCodePair &c : kEudCode116)
      if (c.dwVa117 == it->second)
        return c.dwVa116;
  return 0;
}

// The engine function for a 1.16.1 code address, false for one the tables do not hold.
bool CodeHost(DWORD dwVa116, UINT_PTR *pfn) {
  const EudCodePair *pEnd = kEudCode116 + sizeof(kEudCode116) / sizeof(kEudCode116[0]);
  const EudCodePair *c = std::lower_bound(kEudCode116, pEnd, dwVa116,
                                          [](const EudCodePair &a, DWORD v) { return a.dwVa116 < v; });
  if (c == pEnd || c->dwVa116 != dwVa116)
    return false;
  ULONG n = 0;
  const PortCodeRow *pRows = PortCodeMap(&n);
  for (ULONG i = 0; i < n; i++) {
    if (pRows[i].va == c->dwVa117) {
      *pfn = reinterpret_cast<UINT_PTR>(pRows[i].fn);
      return true;
    }
  }
  return false;
}

// A field that is not copied, as 1.16.1's memory holds it: a pointer as a 1.16.1 address (a
// function's as 1.16.1's address of it), a widened integer as its 32 bits, a widened 16-bit field
// as its 16, a widened id as its byte (the EUD layout rule).
DWORD FieldGet(const PortField &f) {
  switch (f.kind) {
  case WIDE_POINTER: {
    const UINT_PTR p = *reinterpret_cast<const UINT_PTR *>(f.p);
    if (const DWORD dwCode = Code116(p))
      return dwCode;
    return Address116(p);
  }
  case WIDE_SIGNED:
  case WIDE_UNSIGNED:
    return static_cast<DWORD>(*reinterpret_cast<const UINT_PTR *>(f.p));
  case WIDE_UNSIGNED8:
    return *reinterpret_cast<const WORD *>(f.p) & 0xFF;
  default:  // WIDE_UNSIGNED16, WIDE_SIGNED16
    return *reinterpret_cast<const DWORD *>(f.p) & 0xFFFF;
  }
}

bool FieldSet(const PortField &f, DWORD v) {
  switch (f.kind) {
  case WIDE_POINTER: {
    UINT_PTR p;
    if (!CodeHost(v, &p) && !Pointer116(v, &p)) {
      UnservedPointer(v, true);
      return false;
    }
    *reinterpret_cast<UINT_PTR *>(f.p) = p;
    return true;
  }
  case WIDE_SIGNED:
    *reinterpret_cast<INT_PTR *>(f.p) = static_cast<LONG>(v);
    return true;
  case WIDE_UNSIGNED:
    *reinterpret_cast<UINT_PTR *>(f.p) = v;
    return true;
  case WIDE_UNSIGNED16:
    *reinterpret_cast<DWORD *>(f.p) = static_cast<WORD>(v);
    return true;
  case WIDE_UNSIGNED8:
    *reinterpret_cast<WORD *>(f.p) = static_cast<BYTE>(v);
    return true;
  default:  // WIDE_SIGNED16
    *reinterpret_cast<int *>(f.p) = static_cast<short>(v);
    return true;
  }
}

// A byte of a twin by its 1.17 address; padding reads 0.
BYTE TwinByte(DWORD a) {
  PortField f;
  if (!PortDataField(a, &f))
    return 0;
  if (f.kind == WIDE_COPY)
    return reinterpret_cast<const BYTE *>(f.p)[a - f.va];
  return static_cast<BYTE>(FieldGet(f) >> (8 * (a - f.va)));
}

// Bytes written to a twin by its 1.17 address: a copied run's in place, a field that is not
// copied as its whole value with the bytes put in; padding takes them and drops them.
bool TwinWrite(DWORD a, const BYTE *pb, int n) {
  for (int i = 0; i < n;) {
    PortField f;
    if (!PortDataField(a + i, &f)) {
      i++;
      continue;
    }
    if (f.kind == WIDE_COPY) {
      reinterpret_cast<BYTE *>(f.p)[a + i - f.va] = pb[i];
      i++;
      continue;
    }
    DWORD v = FieldGet(f);
    for (; i < n && a + i - f.va < f.len; i++) {
      const DWORD sh = 8 * (a + i - f.va);
      v = (v & ~(0xFFu << sh)) | (static_cast<DWORD>(pb[i]) << sh);
    }
    if (!FieldSet(f, v))
      return false;
  }
  return true;
}

// The engine's frames of a GRP from its file's, those whose eight bytes from dwOff on cover
// (PortGroupNative's frame from the file's: four bytes, the offset, the data it names).
void GroupSync(const Item &it, DWORD dwOff, DWORD n) {
  const DWORD header = offsetof(GROUP, frames);
  const DWORD frames = *reinterpret_cast<const WORD *>(it.pHost) & 0x7FFF;
  for (DWORD k = dwOff < header + 8 ? 0 : (dwOff - header) / 8 - 1; k < frames && header + 8 * k < dwOff + n; k++) {
    if (header + 8 * k + 8 > it.dwSize)
      break;
    const BYTE *from = it.pHost + header + 8 * k;
    FRAME *pFrame = &it.pGroup->frames[k];
    memcpy(pFrame, from, 4);
    DWORD dwData;
    memcpy(&dwData, from + 4, 4);
#if defined(_WIN64) || defined(__LP64__)
    pFrame->dwData = dwData;  // the file's dword beside the pointer (h/grp.h)
#endif
    pFrame->pData = it.pHost + (dwData & 0x7FFFFFFF);
  }
}

// A read of bytes Hyperion does not have, logged once an item.
void StandInRead(const Item &it, DWORD dwAddr) {
  if (g_logged.insert((2ull << 40) | it.dwBase).second)
    Log("frame %u player %u read 0x%08X in %s, which Hyperion serves empty", oc_dwReplayFrame,
        sgdwTriggerPlayer, dwAddr, it.pszName);
}

// A dword at a multiple of 4 as Remastered reads one (0x1402E1650): from the item that holds its
// first byte, little-endian, the bytes past the item's end 0; a twin's in storage of its own
// field by field.
bool ReadAligned(DWORD dwAddr, DWORD *pValue) {
  const Item *p = Find(dwAddr);
  if (p == NULL)
    return false;
  if (p->nPool >= 0 && dwAddr == p->dwBase && g_pools116[p->nPool].bWritten) {
    *pValue = g_pools116[p->nPool].adwWritten[p->bTail];
    return true;
  }
  if (p->bStandIn)
    StandInRead(*p, dwAddr);
  const DWORD dwOff = dwAddr - p->dwBase;
  if (p->bCards && dwOff % 12 == 4 && g_adwCardButtons116[dwOff / 12] != 0) {
    *pValue = g_adwCardButtons116[dwOff / 12];
    return true;
  }
  const DWORD n = p->dwSize - dwOff < 4 ? p->dwSize - dwOff : 4;
  DWORD v = 0;
  if (p->pHost != NULL) {
    memcpy(&v, p->pHost + dwOff, n);
  } else {
    for (DWORD i = 0; i < n; i++)
      v |= static_cast<DWORD>(TwinByte(p->dwBase117 + dwOff + i)) << (8 * i);
  }
  *pValue = v;
  return true;
}

// A dword as 1.16.1's memory held it; one off a multiple of 4 from the two around it, as
// Remastered reads it.
bool Read32(DWORD dwAddr, DWORD *pValue) {
  const DWORD dwLow = dwAddr & ~3u, nShift = dwAddr & 3;
  DWORD dwLo, dwHi;
  if (!ReadAligned(dwLow, &dwLo))
    return false;
  if (nShift == 0) {
    *pValue = dwLo;
    return true;
  }
  if (!ReadAligned(dwLow + 4, &dwHi))
    return false;
  *pValue = static_cast<DWORD>(((static_cast<unsigned long long>(dwHi) << 32) | dwLo) >> (8 * nShift));
  return true;
}

// Bytes nFirst to nFirst + n of the dword at a multiple of 4, written into the item that holds
// its first byte, those past the item's end dropped; a read-only item takes them and drops them,
// as Remastered's do. False for an address no item serves.
void CardBuild(DWORD nCard);
void StaticSave();
void StaticRestore();

bool WriteAligned(DWORD dwAddr, const BYTE *pb, DWORD nFirst, DWORD n) {
  const Item *p = Find(dwAddr);
  if (p == NULL)
    return false;
  if (p->bReadOnly)
    return true;
  const DWORD dwOff = dwAddr - p->dwBase;
  if (p->bCards && dwOff % 12 == 4 && nFirst == 0 && n == 4) {
    // a card's buttons: 1.16.1's own, or the map's, of which the card shows a copy
    DWORD v;
    memcpy(&v, pb, 4);
    UINT_PTR q;
    const DWORD nCard = dwOff / 12;
    if (!Pointer116(v, &q)) {
      g_adwCardButtons116[nCard] = v;
      return true;
    }
    g_adwCardButtons116[nCard] = 0;
  }
  if (p->bCards && dwOff % 12 == 0 && g_adwCardButtons116[dwOff / 12] != 0) {
    // the count of a card the map points at buttons of its own: the copy follows
    return TwinWrite(p->dwBase117 + dwOff + nFirst, pb, static_cast<int>(n));
  }
  if (dwOff + nFirst >= p->dwSize)
    return true;
  if (dwOff + nFirst + n > p->dwSize)
    n = p->dwSize - dwOff - nFirst;
  if (p->pHost != NULL) {
    memcpy(p->pHost + dwOff + nFirst, pb, n);
    if (p->pGroup)
      GroupSync(*p, dwOff + nFirst, n);
    return true;
  }
  return TwinWrite(p->dwBase117 + dwOff + nFirst, pb, static_cast<int>(n));
}

// A dword written; one off a multiple of 4 into the two around it, as Remastered writes it.
bool Write32(DWORD dwAddr, DWORD dwValue) {
  const Item *p = Find(dwAddr);
  if (p != NULL && p->nPool >= 0 && dwAddr == p->dwBase) {
    Pool116 &pool = g_pools116[p->nPool];
    UINT_PTR q;
    if (pool.bWritten || !Pointer116(dwValue, &q) || (q != 0 && !PoolHolds(pool, q))) {
      if (!pool.bWritten) {
        pool.adwWritten[0] = Address116(reinterpret_cast<UINT_PTR>(*pool.ppHead));
        pool.adwWritten[1] = Address116(reinterpret_cast<UINT_PTR>(*pool.ppTail));
        pool.bWritten = true;
      }
      pool.adwWritten[p->bTail] = dwValue;
      return true;
    }
  }
  BYTE ab[4];
  memcpy(ab, &dwValue, 4);
  const DWORD dwLow = dwAddr & ~3u, nShift = dwAddr & 3;
  if (nShift == 0)
    return WriteAligned(dwLow, ab, 0, 4);
  if (Find(dwLow) == NULL || Find(dwLow + 4) == NULL)
    return false;
  return WriteAligned(dwLow, ab, nShift, 4 - nShift) && WriteAligned(dwLow + 4, ab + 4 - nShift, 0, nShift);
}

// The node running, as its bytes stand now: its flags, and each condition and action it holds,
// beside the first 50 accesses logged.
void LogNode() {
  if (g_pNode == NULL || g_logged.size() > 50)
    return;
  DWORD dwFlags;
  memcpy(&dwFlags, g_pNode + 0x948, 4);
  Log("  in node 0x%08X, flags 0x%X, action %u", g_dwNode, dwFlags, g_pNode[0x967]);
  for (int i = 0; i < 16; i++) {
    const BYTE *c = g_pNode + 8 + 20 * i;
    if (c[0xF] == 0)
      break;
    DWORD dwLoc, dwPlayer, dwAmount;
    WORD wUnit;
    memcpy(&dwLoc, c, 4);
    memcpy(&dwPlayer, c + 4, 4);
    memcpy(&dwAmount, c + 8, 4);
    memcpy(&wUnit, c + 0xC, 2);
    Log("    c%d type %u player 0x%08X unit %u cmp %u amount 0x%08X%s 0x%08X", i, c[0xF], dwPlayer, wUnit,
        c[0xE], dwAmount, c[0x12] == 'S' && c[0x13] == 'C' ? " mask" : " loc", dwLoc);
  }
  for (int i = 0; i < 64; i++) {
    const BYTE *a = g_pNode + 0x148 + 32 * i;
    if (a[0x1A] == 0)
      break;
    DWORD dwLoc, dwPlayer, dwValue;
    WORD wUnit;
    memcpy(&dwLoc, a, 4);
    memcpy(&dwPlayer, a + 0x10, 4);
    memcpy(&dwValue, a + 0x14, 4);
    memcpy(&wUnit, a + 0x18, 2);
    Log("    a%d type %u player 0x%08X unit %u mod %u value 0x%08X%s 0x%08X", i, a[0x1A], dwPlayer, wUnit,
        a[0x1B], dwValue, a[0x1E] == 'S' && a[0x1F] == 'C' ? " mask" : " loc", dwLoc);
  }
}

// The 64 accesses before the first not served.
void LogRecent() {
  if (g_nRecentShown >= 4)
    return;
  g_nRecentShown++;
  const unsigned n = g_nRecent < 64 ? g_nRecent : 64;
  Log("  the %u accesses before it:", n);
  for (unsigned i = g_nRecent - n; i < g_nRecent; i++) {
    const Access &a = g_recent[i % 256];
    Log("    node 0x%08X %s 0x%08X = 0x%08X", a.dwNode, a.bWrite ? "write" : "read ", a.dwAddr, a.dwValue);
  }
}

// An access no item serves: logged once an address and kind, and the triggers stopped unless
// HYPERION_EUD=continue.
void Unserved(DWORD dwAddr, bool bWrite) {
  g_unserved++;
  const unsigned long long key = (static_cast<unsigned long long>(dwAddr & ~3u) << 1) | (bWrite ? 1 : 0);
  if (g_logged.insert(key).second && g_logged.size() <= 100000) {
    Log("frame %u player %u %s 0x%08X not served", oc_dwReplayFrame, sgdwTriggerPlayer,
        bWrite ? "write" : "read", dwAddr);
    LogRecent();
    LogNode();
  }
  // a program that has read zeros for long enough has left its course: stop it there too
  if ((g_continue && g_logged.size() < 10000) || g_eudHalted)
    return;
  g_eudHalted = true;
  Log("frame %u: the triggers stop", oc_dwReplayFrame);
  map_msg_default("This map uses memory Hyperion does not support yet. Its triggers have stopped.", 10000);
}

// A pointer field read whose target no twin serves (bWrite false, dwAddr its 1.17 address, 0
// for one outside 1.17's data), or written with a 1.16.1 address that stands for nothing: logged
// and stopping the triggers as an address not served does.
void UnservedPointer(DWORD dwAddr, bool bWrite) {
  const unsigned long long key = (1ull << 40) | (static_cast<unsigned long long>(dwAddr) << 1) | (bWrite ? 1 : 0);
  if (g_logged.insert(key).second && g_logged.size() <= 100000) {
    Log(bWrite ? "frame %u player %u pointer written as 0x%08X, which stands for nothing served"
               : "frame %u player %u pointer read to 1.17's 0x%08X, which no item serves",
        oc_dwReplayFrame, sgdwTriggerPlayer, dwAddr);
    LogRecent();
    LogNode();
  }
  if ((g_continue && g_logged.size() < 10000) || g_eudHalted)
    return;
  g_eudHalted = true;
  Log("frame %u: the triggers stop", oc_dwReplayFrame);
  map_msg_default("This map uses memory Hyperion does not support yet. Its triggers have stopped.", 10000);
}

// Set Deaths' player and unit to their 1.16.1 address, wrapping at 32 bits as 1.16.1 did.
DWORD DeathsAddr(DWORD nPlayer, WORD wUnit) {
  return kDeaths116 + 4 * (nPlayer + 12 * static_cast<DWORD>(wUnit));
}

// A player's men, buildings or factories as 1.17 keeps them (glGameCounts' planes 3, 10, 16).
DWORD *GameCount(DWORD nPlayer, WORD wUnit) {
  const int nPlane = wUnit == kMen ? 3 : wUnit == kBuildings ? 10 : 16;
  return &glGameCounts_g[nPlane][nPlayer];
}

// Remastered's test of one player and unit: a player past 26 or 12, or a unit past 232.
bool Reaches(DWORD dwPlayer, WORD wUnit) {
  return dwPlayer >= 27 || wUnit >= 233 || dwPlayer == 12;
}

}  // namespace

void EudMapBegin() {
  g_mapKind = 0;
}

void EudMapTrigger(const BYTE *pTrigger) {
  // 16 conditions of 20 bytes: player +4, unit +0xC, comparison +0xE, condition +0xF. Remastered
  // tests them all, a disabled one too, and so does this.
  for (int i = 0; i < 16; i++) {
    const BYTE *c = pTrigger + i * 0x14;
    DWORD dwPlayer;
    WORD wUnit;
    memcpy(&dwPlayer, c + 4, 4);
    memcpy(&wUnit, c + 0xC, 2);
    if (c[0xF] == 15 && c[0xE] < 12 && Reaches(dwPlayer, wUnit))
      g_mapKind |= 1;
  }
  // 64 actions of 32 bytes from +0x140: player +0x10, unit +0x18, action +0x1A, modifier +0x1B.
  for (int i = 0; i < 64; i++) {
    const BYTE *a = pTrigger + 0x140 + i * 0x20;
    DWORD dwPlayer;
    WORD wUnit;
    memcpy(&dwPlayer, a + 0x10, 4);
    memcpy(&wUnit, a + 0x18, 2);
    if (a[0x1A] == 45 && a[0x1B] < 12 && Reaches(dwPlayer, wUnit))
      g_mapKind |= 2;
  }
}

void EudMapTriggersRead() {
  // A replay or a save plays by its rule record (RulesForGame); a new game on an EUD map is an
  // EUD game, by Hyperion's rules so that its replay records it. Its selection is 1.17's, which
  // the map's triggers read (the selection's twins above).
  if (g_mapKind == 0 || gPlayback || gpLoadGameFile != NULL)
    return;
  g_hxRulesGame = true;
  g_hxRuleOptions = (g_hxRuleOptions | HX_OPT_EUD) & ~HX_OPT_SELECTION;
  // the game is an EUD game from here, the map read in game_init after EudGameStart, so the
  // tables are kept as they are now, before the triggers write them
  StaticSave();
}

unsigned EudMapKind() {
  return g_mapKind;
}

bool EudSavesBlocked() {
  return EudGame() && (g_mapKind & 2) != 0;
}

void EudFrameMissing(WORD wImageID, unsigned nFrame, unsigned nFrames) {
  if (EudGame() && g_logged.insert((5ull << 40) | wImageID).second)
    Log("frame %u: image %u on frame %u of a group of %u, not drawn", oc_dwReplayFrame, wImageID, nFrame, nFrames);
}

// HYPERION_EUD=dat: every .dat field as an EUD map reads it, byte by byte, held to the bytes
// of the file the game loaded it from: a column of ids the build keeps in
// 16 bits reads as its low byte, as 1.17's. A game that changes a table (a map's unit
// settings, a mod's edits, a mutator) shows those fields as differing. It runs as the
// triggers first do, the map whole and the game's tables loaded.
bool g_datCheck = false;
void DatReadBack() {
  unsigned nFields = 0, nDiffer = 0;
  DWORD cbAll = 0;
  for (const Dat116 &dat : kDats116) {
    char szFile[64];
    SDL_snprintf(szFile, sizeof szFile, "arr\\%s", dat.pszName);
    void *pFile = NULL;
    DWORD cbFile = 0;
    if (!SFileLoadFile(szFile, &pFile, &cbFile, 0, NULL)) {
      Log("dat read-back: %s does not load", szFile);
      continue;
    }
    const BYTE *pb = static_cast<const BYTE *>(pFile);
    DWORD dwAt = 0;  // the field's place in the file, where the fields follow one another
    for (size_t i = 0; i < dat.nFields; i++) {
      const DatField116 &f = dat.pFields[i];
      const DWORD cb = f.dwElem * f.dwCount;
      DWORD nBad = 0, dwFirst = 0;
      int nFirstGot = 0;
      for (DWORD o = 0; o < cb; o++) {
        const Item *p = Find(f.dwAddr + o);
        const DWORD dwOff = p != NULL ? f.dwAddr + o - p->dwBase : 0;
        const int nGot = p == NULL ? -1 : p->pHost != NULL ? p->pHost[dwOff] : TwinByte(p->dwBase117 + dwOff);
        if (dwAt + o >= cbFile || nGot != pb[dwAt + o]) {
          if (nBad++ == 0) {
            dwFirst = o;
            nFirstGot = nGot;
          }
        }
      }
      if (nBad != 0) {
        nDiffer++;
        Log("dat read-back: %s field %u at 0x%08X: %u of %u bytes differ, the first at +%u (element %u), "
            "read %d where the file has %d",
            dat.pszName, static_cast<unsigned>(i), f.dwAddr, nBad, cb, dwFirst, dwFirst / f.dwElem, nFirstGot,
            dwAt + dwFirst < cbFile ? pb[dwAt + dwFirst] : -1);
      }
      nFields++;
      cbAll += cb;
      dwAt += cb;
    }
    if (dwAt != cbFile)
      Log("dat read-back: %s has %u bytes, its fields %u", dat.pszName, cbFile, dwAt);
    SFileUnloadFile(pFile);
  }
  Log("dat read-back: %u fields, %u bytes; %u differ from their files", nFields, cbAll, nDiffer);
}

void EudGameStart() {
  g_eudHalted = false;
  g_logged.clear();
  g_unserved = 0;
  g_masked = false;
  const char *psz = getenv("HYPERION_EUD");
  g_trace = psz != NULL && strcmp(psz, "trace") == 0;
  g_continue = g_trace || (psz != NULL && strcmp(psz, "continue") == 0);
  g_traced = 0;
  // the tables the last EUD game wrote as they were, then the map anew, without the last game's
  // trigger items
  StaticRestore();
  g_items.clear();
  g_pLast = NULL;
  BuildMap();
  if (EudGame())
    StaticSave();
  g_datCheck = psz != NULL && strcmp(psz, "dat") == 0;
  g_dumpMap = psz != NULL && strcmp(psz, "map") == 0;
  g_triggersBuilt = false;
  g_pNode = NULL;
  g_nRecent = 0;
  g_nRecentShown = 0;
  std::vector<BYTE>().swap(g_nodes);
  g_nNodes = 0;
}

bool EudTriggersBuilt() {
  return g_triggersBuilt;
}

void EudTriggersBegin(DWORD nNodes) {
  g_triggersBuilt = true;
  g_nodes.assign(kLinkPad + static_cast<size_t>(nNodes) * kNodeStride, 0);
  g_nNodes = 0;
  for (DWORD n = 0; n < kLists; n++)
    EudTriggersClear(n);
}

void EudTriggerAdd(DWORD nList, const void *pRecord) {
  if (nList >= kLists || kLinkPad + static_cast<size_t>(g_nNodes + 1) * kNodeStride > g_nodes.size())
    return;
  const DWORD dwAddr = kNodes116 + g_nNodes * kNodeStride;
  BYTE *pNode = &g_nodes[kLinkPad + static_cast<size_t>(g_nNodes) * kNodeStride];
  g_nNodes++;
  // appended as Storm appends (NewTrigger, victory.cpp): its prev the tail, its next the end
  DWORD *pHead = &g_lists[3 * nList];
  const DWORD dwLink = kLists116 + 12 * nList + 4;
  const DWORD dwTail = pHead[1];
  const DWORD dwEnd = ~dwLink;
  memcpy(pNode, &dwTail, 4);
  memcpy(pNode + 4, &dwEnd, 4);
  memcpy(pNode + 8, pRecord, kNodeSize - 8);
  if (dwTail == dwLink)
    pHead[2] = dwAddr;
  else
    memcpy(&g_nodes[kLinkPad + (dwTail - kNodes116)] + 4, &dwAddr, 4);
  pHead[1] = dwAddr;
}

// A 1.16.1 address for a buffer of the game's, from past the trigger nodes, 16-aligned as
// Remastered's.
DWORD Heap116(DWORD dwSize) {
  const DWORD dwAddr = g_dwHeapNext;
  g_dwHeapNext = (g_dwHeapNext + dwSize + 15) & ~15u;
  return dwAddr;
}

// A GRP as an item: the file the engine keeps after its own frames (PortGroupNative), at a new
// 1.16.1 address, which the pointer to it reads.
void GroupItem(GROUP *pGroup, const char *pszName, DWORD *pdwPtr) {
  *pdwPtr = 0;
  if (pGroup == NULL)
    return;
  const size_t frames = *reinterpret_cast<const WORD *>(pGroup) & 0x7FFF;
  const size_t table = offsetof(GROUP, frames) + frames * sizeof(FRAME);
  const size_t size = PortMemSize(pGroup);
  if (size <= table)
    return;
  Item it = MakeItem(Heap116(static_cast<DWORD>(size - table)), static_cast<DWORD>(size - table),
                     reinterpret_cast<BYTE *>(pGroup) + table, pszName);
  it.pGroup = pGroup;
  g_items.push_back(it);
  *pdwPtr = it.dwBase;
}

// The MPQ crypt table and the two uses Storm makes of it here: a name's hash, and a table's
// decryption.
DWORD g_adwCrypt[0x500];

void CryptInit() {
  if (g_adwCrypt[0])
    return;
  DWORD dwSeed = 0x00100001;
  for (int i = 0; i < 0x100; i++) {
    for (int j = i, k = 0; k < 5; k++, j += 0x100) {
      dwSeed = (dwSeed * 125 + 3) % 0x2AAAAB;
      const DWORD dwHigh = (dwSeed & 0xFFFF) << 16;
      dwSeed = (dwSeed * 125 + 3) % 0x2AAAAB;
      g_adwCrypt[j] = dwHigh | (dwSeed & 0xFFFF);
    }
  }
}

DWORD CryptHash(const char *psz, DWORD dwType) {
  DWORD dwSeed1 = 0x7FED7FED, dwSeed2 = 0xEEEEEEEE;
  for (; *psz; psz++) {
    const BYTE c = static_cast<BYTE>(toupper(static_cast<BYTE>(*psz)));
    dwSeed1 = g_adwCrypt[dwType * 0x100 + c] ^ (dwSeed1 + dwSeed2);
    dwSeed2 = c + dwSeed1 + dwSeed2 + (dwSeed2 << 5) + 3;
  }
  return dwSeed1;
}

void CryptDecrypt(BYTE *pb, size_t cb, DWORD dwKey) {
  DWORD dwSeed = 0xEEEEEEEE;
  for (size_t i = 0; i + 4 <= cb; i += 4) {
    DWORD dw;
    memcpy(&dw, pb + i, 4);
    dwSeed += g_adwCrypt[0x400 + (dwKey & 0xFF)];
    dw ^= dwKey + dwSeed;
    dwKey = ((~dwKey << 21) + 0x11111111) | (dwKey >> 11);
    dwSeed = dw + dwSeed + (dwSeed << 5) + 3;
    memcpy(pb + i, &dw, 4);
  }
}

void PutDword(BYTE *pb, DWORD dwOff, DWORD dw) { memcpy(pb + dwOff, &dw, 4); }
DWORD GetDword(const BYTE *pb, DWORD dwOff) {
  DWORD dw;
  memcpy(&dw, pb + dwOff, 4);
  return dw;
}

// The map's archive as Remastered reads it (0x1402e39c0): its header found at a multiple of 0x200
// in its file, the hash table's count kept to 28 bits, each table read at the archive's offset plus
// its own, as much as the file holds, then decrypted.
bool ArchiveFromMap(Archive *pArchive) {
  if (gPlayback || !MapFileName[0])
    return false;
  HANDLE h = CreateFileA(MapFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  bool bFound = false;
  DWORD dwOffset = 0, cbRead = 0;
  for (;; dwOffset += 0x200) {
    SetFilePointer(h, static_cast<LONG>(dwOffset), NULL, FILE_BEGIN);
    if (!ReadFile(h, pArchive->abHeader, 0x20, &cbRead, NULL) || cbRead != 0x20)
      break;
    if (GetDword(pArchive->abHeader, 0) == 0x1A51504D && GetDword(pArchive->abHeader, 4) >= 0x20) {
      bFound = true;
      break;
    }
  }
  if (bFound) {
    PutDword(pArchive->abRecord, 0x120, 0x200u << (pArchive->abHeader[0xE] & 0x1F));
    PutDword(pArchive->abRecord, 0x12C, dwOffset);
    const DWORD nHash = GetDword(pArchive->abHeader, 0x18) & 0x0FFFFFFF;
    PutDword(pArchive->abHeader, 0x18, nHash);
    const DWORD nBlock = GetDword(pArchive->abHeader, 0x1C);
    struct {
      std::vector<BYTE> *pTable;
      DWORD dwCount, dwAt;
      const char *pszKey;
    } tables[2] = {{&pArchive->hash, nHash, GetDword(pArchive->abHeader, 0x10), "(hash table)"},
                   {&pArchive->block, nBlock, GetDword(pArchive->abHeader, 0x14), "(block table)"}};
    for (int i = 0; i < 2; i++) {
      // a count no file could hold is left out, as a table Hyperion cannot hold
      if (tables[i].dwCount > 0x01000000)
        continue;
      tables[i].pTable->assign(static_cast<size_t>(tables[i].dwCount) * 16, 0);
      SetFilePointer(h, static_cast<LONG>(dwOffset + tables[i].dwAt), NULL, FILE_BEGIN);
      ReadFile(h, tables[i].pTable->data(), static_cast<DWORD>(tables[i].pTable->size()), &cbRead, NULL);
      CryptDecrypt(tables[i].pTable->data(), tables[i].pTable->size(), CryptHash(tables[i].pszKey, 3));
    }
  }
  CloseHandle(h);
  return bFound;
}

// Each archive's buffers, then its record, at new 1.16.1 addresses in Remastered's order
// (0x1402e4938), the record put at the list's end.
void ArchiveAdd(Archive *pArchive, Archive *pPrev, const char *pszName) {
  const DWORD dwHeader = Heap116(0x20);
  g_items.push_back(MakeItem(dwHeader, 0x20, pArchive->abHeader, "archive header"));
  DWORD dwBlock = 0, dwHash = 0;
  if (!pArchive->block.empty()) {
    dwBlock = Heap116(static_cast<DWORD>(pArchive->block.size()));
    g_items.push_back(MakeItem(dwBlock, static_cast<DWORD>(pArchive->block.size()), pArchive->block.data(),
                               "archive block table"));
  }
  if (!pArchive->hash.empty()) {
    dwHash = Heap116(static_cast<DWORD>(pArchive->hash.size()));
    g_items.push_back(MakeItem(dwHash, static_cast<DWORD>(pArchive->hash.size()), pArchive->hash.data(),
                               "archive hash table"));
  }
  PutDword(pArchive->abRecord, 0x130, dwHeader);
  PutDword(pArchive->abRecord, 0x134, dwBlock);
  PutDword(pArchive->abRecord, 0x138, dwHash);
  const DWORD dwRecord = Heap116(kArchiveRecord);
  g_items.push_back(MakeItem(dwRecord, kArchiveRecord, pArchive->abRecord, pszName));
  // TSList's tail: the record after the last, the last's next it, the head's last it
  const DWORD dwTerminator = kArchives116 + 4;
  PutDword(pArchive->abRecord, 0, g_adwArchiveHead[1]);
  PutDword(pArchive->abRecord, 4, ~dwTerminator);
  if (g_adwArchiveHead[2] == ~dwTerminator)
    g_adwArchiveHead[2] = dwRecord;
  if (pPrev != NULL)
    PutDword(pPrev->abRecord, 4, dwRecord);
  g_adwArchiveHead[1] = dwRecord;
}

void ArchivesBuild() {
  CryptInit();
  const DWORD dwTerminator = kArchives116 + 4;
  g_adwArchiveHead[0] = 0;
  g_adwArchiveHead[1] = dwTerminator;
  g_adwArchiveHead[2] = ~dwTerminator;
  g_archives.clear();
  g_archives.resize(5);  // value-initialized: each record and header zeros
  for (int i = 0; i < 4; i++) {
    Archive &a = g_archives[i];
    const ArchiveHeader &k = kArchiveHeaders116[i];
    PutDword(a.abHeader, 0, 0x1A51504D);
    PutDword(a.abHeader, 4, 0x20);
    PutDword(a.abHeader, 8, k.dwSize);
    memcpy(a.abHeader + 0xE, &k.wShift, 2);
    PutDword(a.abHeader, 0x10, k.dwHashOffset);
    PutDword(a.abHeader, 0x14, k.dwBlockOffset);
    PutDword(a.abHeader, 0x18, k.dwHashCount);
    PutDword(a.abHeader, 0x1C, k.dwBlockCount);
    a.hash.assign(static_cast<size_t>(k.dwHashCount) * 16, 0xFF);
    a.block.assign(static_cast<size_t>(k.dwBlockCount) * 16, 0);
  }
  if (!ArchiveFromMap(&g_archives[4]))
    g_archives.resize(4);
  // the items point into g_archives, which no longer moves
  static const char *const kNames[5] = {"patch_rt.mpq", "BrooDat.mpq", "StarDat.mpq", "the CD's archive",
                                        "the map's archive"};
  for (size_t i = 0; i < g_archives.size(); i++) {
    ArchiveAdd(&g_archives[i], i > 0 ? &g_archives[i - 1] : NULL, kNames[i]);
    if (i < 4) {
      // the tables Hyperion does not have: the last two items added
      g_items[g_items.size() - 2].bStandIn = true;
      g_items[g_items.size() - 3].bStandIn = true;
    }
  }
  g_items.push_back(MakeItem(kArchives116, sizeof(g_adwArchiveHead), g_adwArchiveHead, "archive list"));
}

void EudTriggersEnd() {
  g_dwHeapNext = kNodes116 + g_nNodes * kNodeStride;
  g_items.push_back(MakeItem(kLists116, sizeof(g_lists), g_lists, "trigger lists"));
  if (g_nNodes != 0)
    g_items.push_back(MakeItem(kNodes116, g_nNodes * kNodeStride, &g_nodes[kLinkPad], "trigger nodes", false,
                               true, kLinkPad));
  // the game's own table, so that a string the map changes is the one its text shows
  if (gpMapStrs_g != NULL && gdwMapStrSize_g != 0)
    g_items.push_back(MakeItem(kStrings116, gdwMapStrSize_g, gpMapStrs_g, "string section", false, true));
  // writes dropped: Remastered moves the section where one points, which is not done yet
  g_items.push_back(MakeItem(kStringsPtr116, 4, &g_dwStringsPtr, "string section pointer", true));
  // stat_txt.tbl: a WORD count, WORD offsets, and the strings, the last ending the table. An EUD
  // map may copy a table of its own over it, Remastered's size, so that the game shows its names
  // and tooltips (eudplib, _f_initstattext). The game's table goes into one of the room
  // Remastered gives it, up to the string section, which the game then reads and frees as its
  // own (status_free). In another language the table is English's, so that a map reads the
  // same bytes in every language, and the game shows English's text.
  unsigned long cbEnglish = 0;
  void *pEnglish = SDL_strcmp(PortLanguageGame(), "enUS") != 0 ? PortRemasteredStatTxtEnglish(&cbEnglish) : NULL;
  if (const BYTE *pTbl = static_cast<const BYTE *>(pEnglish != NULL ? pEnglish : sgpStatStrs)) {
    WORD nStrings;
    memcpy(&nStrings, pTbl, 2);
    DWORD dwEnd = 2 + 2u * nStrings;
    for (WORD i = 0; i < nStrings; i++) {
      WORD wOff;
      memcpy(&wOff, pTbl + 2 + 2 * i, 2);
      if (wOff >= dwEnd)
        dwEnd = wOff + static_cast<DWORD>(strlen(reinterpret_cast<const char *>(pTbl) + wOff)) + 1;
    }
    const DWORD dwRoom = kStrings116 - kStatTxt116;
    bool bRoom = false;
    if (dwEnd <= dwRoom) {
      if (void *pRoom = SMemAlloc(dwRoom, "port_eud.cpp", __LINE__, 0)) {
        memset(pRoom, 0, dwRoom);
        memcpy(pRoom, pTbl, dwEnd);
        SMemFree(sgpStatStrs, "port_eud.cpp", __LINE__, 0);
        sgpStatStrs = pRoom;
        dwEnd = dwRoom;
        bRoom = true;
      }
    }
    // English's table reaches the map only in the room; without it the language's stays unseen
    if (pEnglish == NULL || bRoom) {
      g_items.push_back(MakeItem(kStatTxt116, dwEnd, sgpStatStrs, "stat_txt.tbl"));
      g_items.push_back(MakeItem(kStatTxtPtr116, 4, &g_dwStatTxtPtr, "stat_txt.tbl pointer", true));
    }
  }
  SDL_free(pEnglish);
  GroupItem(sgpTransWires, "tranwire.grp", &g_adwWirePtrs[0]);
  GroupItem(sgpMultSelWires, "grpwire.grp", &g_adwWirePtrs[1]);
  GroupItem(sgpWireFrames, "wirefram.grp", &g_adwWirePtrs[2]);
  g_items.push_back(MakeItem(kTranWirePtr116, 4, &g_adwWirePtrs[0], "tranwire.grp pointer", true));
  g_items.push_back(MakeItem(kGrpWirePtr116, 4, &g_adwWirePtrs[1], "grpwire.grp pointer", true));
  g_items.push_back(MakeItem(kWireframPtr116, 4, &g_adwWirePtrs[2], "wirefram.grp pointer", true));
  // The flyers' repulse counts, a byte a chunk of 48 pixels (repulse_init), rows of 171 on a map of
  // 1.17's sizes as in 1.16.1: at a new 1.16.1 address, which its pointer reads (Remastered's slot
  // item, 0x6D5CD8).
  g_dwRepulsePtr = 0;
  if (sgpRpMap != NULL && gwMapPixWidth <= 8192 && gwMapPixHeight <= 8192) {
    g_dwRepulsePtr = Heap116(kRepulseSize);
    g_items.push_back(MakeItem(g_dwRepulsePtr, kRepulseSize, sgpRpMap, "repulse counts"));
  }
  g_items.push_back(MakeItem(kRepulsePtr116, 4, &g_dwRepulsePtr, "repulse counts pointer", true));
  // The map's tiles, a WORD each (gfpMIMap), their flags, a DWORD each (gpSquareMap), rows of the
  // map's width as in 1.16.1, and the tileset's groups of 52 bytes (gpMtxInfo, the .cv5), each at
  // a new 1.16.1 address its pointer reads, as Remastered serves them (its virtual buffers); the
  // tile arrays at 1.16.1's sizes, on a map of 1.17's.
  for (DWORD &dw : g_adwTilePtrs)
    dw = 0;
  if (gfpMIMap != NULL && gpSquareMap != NULL && gwMapWidth <= 256 && gwMapHeight <= 256) {
    g_adwTilePtrs[0] = Heap116(0x20000);
    g_items.push_back(MakeItem(g_adwTilePtrs[0], 0x20000, gfpMIMap, "tiles"));
    g_adwTilePtrs[1] = Heap116(0x40000);
    g_items.push_back(MakeItem(g_adwTilePtrs[1], 0x40000, gpSquareMap, "tile flags"));
  }
  if (gpMtxInfo != NULL && gdwMIEntries != 0) {
    g_adwTilePtrs[2] = Heap116(gdwMIEntries * 52);
    g_items.push_back(MakeItem(g_adwTilePtrs[2], gdwMIEntries * 52, gpMtxInfo, "tileset groups"));
  }
  g_items.push_back(MakeItem(0x5993C4, 4, &g_adwTilePtrs[0], "tiles pointer", true));
  g_items.push_back(MakeItem(0x6D1260, 4, &g_adwTilePtrs[1], "tile flags pointer", true));
  g_items.push_back(MakeItem(0x6D5EC8, 4, &g_adwTilePtrs[2], "tileset groups pointer", true));
  // The game's scripts as loaded, iscript.bin, aiscript.bin and bwscript.bin, each as many bytes
  // as its block, and the tiles as drawn (gfpCellMap, a WORD each, rows of the map's width), at a
  // new 1.16.1 address their pointers read: Remastered's partial buffers 0x23 and 0x1B and its
  // slots 0x18 and 0x19. A map that writes a script changes what the game runs, as there. Then
  // the tileset's cells (.vr4, 64 bytes each), its megatiles and the scroll ring, which 1.16.1's
  // gamemap_free (0x4BD1A0) frees and clears in 1.17's order (Remastered's partial buffers 0x1E,
  // 0x21 and 0x1D): the megatiles in 1.16.1's form, a copy whose writes are kept and not drawn,
  // and the ring as long as 1.17's, Hyperion's being longer and in its own layout.
  g_megatiles116.clear();
  if (gfpMtxSet != NULL) {
    const ULONG *pCells = reinterpret_cast<const ULONG *>(gfpMtxSet);
    g_megatiles116.resize(PortMemSize(gfpMtxSet) / sizeof(ULONG));
    for (size_t i = 0; i < g_megatiles116.size(); i++)
      g_megatiles116[i] = static_cast<WORD>(pCells[i]);
  }
  const DWORD kScrollRing117 = 0x49800;
  const struct {
    void *p;
    DWORD dwSize, dwPtr116;
    const char *pszName, *pszPtr;
  } files[7] = {
      {sgpScripts, static_cast<DWORD>(PortMemSize(sgpScripts)), 0x6D1200, "iscript.bin", "iscript.bin pointer"},
      {gpAIScript_g, static_cast<DWORD>(PortMemSize(gpAIScript_g)), 0x68C104, "aiscript.bin", "aiscript.bin pointer"},
      {gpBWScript_g, static_cast<DWORD>(PortMemSize(gpBWScript_g)), 0x68C108, "bwscript.bin", "bwscript.bin pointer"},
      {gfpCellMap, gwMapWidth <= 256 && gwMapHeight <= 256 ? 0x20000u : 0u, 0x628494, "tiles drawn",
       "tiles drawn pointer"},
      {gfpCellBuf, static_cast<DWORD>(PortMemSize(gfpCellBuf)), 0x628444, "tile cells", "tile cells pointer"},
      {g_megatiles116.data(), static_cast<DWORD>(g_megatiles116.size() * sizeof(WORD)), 0x628458, "megatiles",
       "megatiles pointer"},
      {_gfpScrollBuf, PortMemSize(_gfpScrollBuf) >= kScrollRing117 ? kScrollRing117 : 0u, 0x628454, "scroll ring",
       "scroll ring pointer"},
  };
  for (int i = 0; i < 7; i++) {
    g_adwFilePtrs[i] = 0;
    if (files[i].p != NULL && files[i].dwSize != 0) {
      g_adwFilePtrs[i] = Heap116(files[i].dwSize);
      g_items.push_back(MakeItem(g_adwFilePtrs[i], files[i].dwSize, files[i].p, files[i].pszName));
    }
    g_items.push_back(MakeItem(files[i].dwPtr116, 4, &g_adwFilePtrs[i], files[i].pszPtr, true));
  }
  // Each dialog pointer reads a chain of zeroed blocks at new 1.16.1 addresses, as Remastered
  // builds it: every block but the last points at the next at +0 (pNext), at a bitmap of its own
  // at +0x10, and holds Remastered's markers for its text at +0x14 and its two functions at
  // +0x2A and +0x2E; a map's writes to them are kept and do nothing else.
  g_dialogBlocks.clear();
  for (size_t d = 0; d < sizeof(kDialogs116) / sizeof(kDialogs116[0]); d++) {
    const DWORD dwSize = kDialogs116[d].dwSize;
    BYTE *pPrev = NULL;  // the block before, whose data stays put as the list grows
    for (DWORD k = 0; k < kDialogs116[d].nBlocks; k++) {
      g_dialogBlocks.emplace_back(dwSize, 0);
      BYTE *pBlock = g_dialogBlocks.back().data();
      const DWORD dwBlock = Heap116(dwSize);
      g_items.push_back(MakeItem(dwBlock, dwSize, pBlock, "a dialog's block"));
      if (k == 0) {
        g_adwDialogPtrs[d] = dwBlock;
      } else {
        g_dialogBlocks.emplace_back(kDialogBitmap, 0);
        const DWORD dwBitmap = Heap116(kDialogBitmap);
        g_items.push_back(MakeItem(dwBitmap, kDialogBitmap, g_dialogBlocks.back().data(), "a dialog's bitmap"));
        const DWORD adwFields[5][2] = {
            {0, dwBlock}, {0x10, dwBitmap}, {0x2A, 0xDEADBEEF}, {0x2E, 0xDEADBEE0}, {0x14, 0xDEADBEE2}};
        for (const auto &f : adwFields)
          if (f[0] + 4 <= dwSize)
            memcpy(pPrev + f[0], &f[1], 4);
      }
      pPrev = pBlock;
    }
    g_items.push_back(MakeItem(kDialogs116[d].dwPtr116, 4, &g_adwDialogPtrs[d], "a dialog pointer", true));
  }
  ArchivesBuild();
  SortMap();
  Log("%u trigger nodes; string section of %u bytes", g_nNodes, gpMapStrs_g != NULL ? gdwMapStrSize_g : 0);
  // HYPERION_EUD=map: each item, to compare with Remastered's ranges (remastered-eud-table.tsv)
  if (g_dumpMap) {
    for (const Item &it : g_items) {
      // a twin read field by field: the bytes no field of Hyperion's data covers, which read as 0
      DWORD nUnknown = 0;
      if (it.pHost == NULL && it.dwBase117 != 0)
        for (DWORD i = 0; i < it.dwSize; i++) {
          PortField f;
          nUnknown += PortDataField(it.dwBase117 + i, &f) ? 0 : 1;
        }
      Log("item 0x%08X 0x%X %s%s%s%s", it.dwBase, it.dwSize, it.pszName, it.bReadOnly ? ", read only" : "",
          it.bStandIn ? ", stand-in" : "", nUnknown != 0 ? ", bytes not known" : "");
      if (nUnknown != 0)
        Log("  %u of its bytes at 1.17's 0x%08X not in Hyperion's data", nUnknown, it.dwBase117);
    }
  }
  if (g_datCheck)
    DatReadBack();
}

DWORD EudTriggersFirst(DWORD nList) {
  g_nPassList = nList;
  g_nPassNodes = 0;
  return nList < kLists ? g_lists[3 * nList + 2] : 0;
}

TRIGGERNODE *EudTriggerAt(DWORD dwAddr) {
  const Item *p = Find(dwAddr);
  const DWORD dwOff = p != NULL ? dwAddr - p->dwBase : 0;
  if (p == NULL || !p->bNodes || p->dwSize < kNodeSize || dwOff > p->dwSize - kNodeSize ||
      dwOff + p->cbBefore < kLinkPad) {
    Unserved(dwAddr, false);
    return NULL;
  }
  if (g_trace && g_traced < 2000000) {
    g_traced++;
    Log("frame %u list %u node 0x%08X", oc_dwReplayFrame, g_nPassList, dwAddr);
  }
  if (++g_nPassNodes % 10000000 == 0)
    Log("frame %u list %u: %u million nodes in this pass, now 0x%08X", oc_dwReplayFrame, g_nPassList,
        g_nPassNodes / 1000000, dwAddr);
  g_dwNode = dwAddr;
  g_pNode = p->pHost + dwOff;
  return reinterpret_cast<TRIGGERNODE *>(p->pHost + dwOff - kLinkPad);
}

namespace {

// A button of a map's own whose functions are not 1.16.1's that Hyperion has: never shown.
int __fastcall ButtonHidden(int, int, CUnit *) {
  return 0;
}
void __fastcall ButtonNone(WORD, BYTE) {}

// The engine's copy of the buttons a card's map points at, from the bytes there now, as many as
// the card's count: each read as 1.16.1's memory holds it, its functions the engine's.
void CardBuild(DWORD nCard) {
  const DWORD dwButtons = g_adwCardButtons116[nCard];
  DWORD dwCount = 0;
  ReadAligned(kCards116 + 12 * nCard, &dwCount);
  dwCount &= 0xFFFF;
  std::vector<BYTE> bytes(20 * static_cast<size_t>(dwCount));
  std::vector<void *> fns(2 * static_cast<size_t>(dwCount));
  for (DWORD i = 0; i < dwCount; i++) {
    BYTE *pb = &bytes[20 * i];
    bool bRead = true;
    for (DWORD k = 0; k < 5 && bRead; k++) {
      DWORD v = 0;
      bRead = Read32(dwButtons + 20 * i + 4 * k, &v);
      memcpy(pb + 4 * k, &v, 4);
    }
    UINT_PTR fnCondition = 0, fnAction = 0;
    if (!bRead || !CodeHost(GetDword(pb, 4), &fnCondition) || !CodeHost(GetDword(pb, 8), &fnAction)) {
      if (g_logged.insert((4ull << 40) | (static_cast<unsigned long long>(nCard) << 16) | i).second) {
        Log("frame %u: card %u's button %u at 0x%08X: %s", oc_dwReplayFrame, nCard, i, dwButtons + 20 * i,
            bRead ? "functions not ones Hyperion has, not shown" : "not served, not shown");
        LogRecent();
        LogNode();
      }
      fnCondition = reinterpret_cast<UINT_PTR>(&ButtonHidden);
      fnAction = reinterpret_cast<UINT_PTR>(&ButtonNone);
    }
    fns[2 * i] = reinterpret_cast<void *>(fnCondition);
    fns[2 * i + 1] = reinterpret_cast<void *>(fnAction);
  }
  StatcmdEudButtons(static_cast<WORD>(nCard), bytes.data(), static_cast<int>(dwCount), fns.data());
}

// The engine's bytes behind each static range, field by field: a run copied as it is, a widened
// field as the engine's whole field.
void StaticSave() {
  // a save not yet put back is kept: the skirmish screen's preview reads a map's triggers too,
  // after the game that wrote the tables has ended
  if (!g_static.empty() || !g_staticDat.empty())
    return;
  for (const auto &r : g_staticRanges) {
    if (PortDataPlain(r.first, r.second)) {
      BYTE *p = reinterpret_cast<BYTE *>(OC_VA(r.first));
      g_static.push_back(std::make_pair(p, std::vector<BYTE>(p, p + r.second)));
      continue;
    }
    for (DWORD a = r.first; a < r.first + r.second;) {
      PortField f;
      if (!PortDataField(a, &f)) {
        a++;
        continue;
      }
      BYTE *p = reinterpret_cast<BYTE *>(f.p);
      size_t cb;
      if (f.kind == WIDE_COPY) {
        p += a - f.va;
        cb = f.va + f.len - a;
        if (cb > r.first + r.second - a)
          cb = r.first + r.second - a;
        a += static_cast<DWORD>(cb);
      } else {
        cb = f.kind == WIDE_UNSIGNED8 ? 2 : f.kind == WIDE_UNSIGNED16 || f.kind == WIDE_SIGNED16 ? 4
                                                                                 : sizeof(UINT_PTR);
        a = f.va + f.len;
      }
      g_static.push_back(std::make_pair(p, std::vector<BYTE>(p, p + cb)));
    }
  }
  for (const Dat116 &dat : kDats116)
    for (const array_data_t *t = dat.pTable; t->pDest != NULL && t - dat.pTable < 64; t++) {
      BYTE *p = static_cast<BYTE *>(t->pDest);
      const size_t cb = PortDataBytes(p, t->dwCount * t->dwElemSize);  // a wide column's whole storage
      g_staticDat.push_back({dat.pszName, p, std::vector<BYTE>(p, p + cb), NULL});
    }
  if (BYTE *p = reinterpret_cast<BYTE *>(sgpScripts))
    g_staticDat.push_back({"iscript.bin", p, std::vector<BYTE>(p, p + PortMemSize(p)),
                           reinterpret_cast<void *const *>(&sgpScripts)});
}

void StaticRestore() {
  for (const auto &s : g_static)
    memcpy(s.first, s.second.data(), s.second.size());
  g_static.clear();
  std::vector<std::pair<const char *, size_t>> changed;
  for (const StaticDat &d : g_staticDat) {
    if (d.ppOwner != NULL && *d.ppOwner != d.p)
      continue;
    size_t n = 0;
    for (size_t i = 0; i < d.bytes.size(); i++)
      n += d.p[i] != d.bytes[i];
    if (n != 0) {
      if (changed.empty() || strcmp(changed.back().first, d.pszName) != 0)
        changed.push_back(std::make_pair(d.pszName, size_t(0)));
      changed.back().second += n;
    }
    memcpy(d.p, d.bytes.data(), d.bytes.size());
  }
  g_staticDat.clear();
  if (!changed.empty()) {
    std::string s;
    for (const auto &c : changed)
      s += (s.empty() ? "" : ", ") + std::string(c.first) + " " + std::to_string(c.second);
    Log("the bytes the last EUD game changed in the .dat tables and iscript.bin, put back: %s", s.c_str());
  }
  StatcmdEudButtonsClear();
  memset(g_adwCardButtons116, 0, sizeof(g_adwCardButtons116));
}

// A free list the map wrote, once the trigger that wrote it has run: walked from its head by the
// next links, and the pool grown by the nodes past those free now.
void PoolApply(Pool116 &pool) {
  pool.bWritten = false;
  const DWORD dwFirst = pool.adwWritten[0];
  DWORD n = 0;
  bool bEven = true;
  DWORD dwNode = dwFirst;
  while (dwNode != 0 && n < (1u << 20)) {
    if (dwNode != dwFirst + n * pool.dwStride116)
      bEven = false;
    DWORD dwNext;
    if (!Read32(dwNode + 4, &dwNext)) {
      Unserved(dwNode + 4, false);
      return;
    }
    dwNode = dwNext;
    n++;
  }
  int nFree = 0;
  for (CLink *q = *pool.ppHead; q != NULL && nFree <= pool.nMax; q = q->pNext)
    nFree++;
  const int nOld = *pool.pnCount;
  int nNew = nOld + (static_cast<int>(n) > nFree ? static_cast<int>(n) - nFree : 0);
  if (nNew > pool.nMax)
    nNew = pool.nMax;
  for (int i = nOld; i < nNew; i++) {
    BYTE *pSlot = pool.pBase + i * pool.cbHost;
    memset(pSlot, 0, pool.cbHost);
    if (pool.bSprites)
      reinterpret_cast<CSprite *>(pSlot)->wIndex = static_cast<sprite_index_t>(i);
    CLink *pLink = reinterpret_cast<CLink *>(pSlot);
    if (*pool.ppTail != NULL) {
      (*pool.ppTail)->AppendLink(pLink, pool.ppTail);
    } else {
      *pool.ppHead = pLink;
      *pool.ppTail = pLink;
    }
  }
  if (nNew > nOld && pool.nFrom == 0) {
    pool.nFrom = nOld;
    pool.dwGrown116 = bEven ? dwFirst : 0;
  }
  *pool.pnCount = nNew;
  Log("frame %u: the map built the free %s again, %u nodes of its own; %d slots now, %d at most", oc_dwReplayFrame,
      pool.pszName, n, nNew, pool.nMax);
}

}  // namespace

void EudTriggersDone() {
  for (Pool116 &pool : g_pools116)
    if (pool.bWritten)
      PoolApply(pool);
  for (DWORD c = 0; c < kCards; c++)
    if (g_adwCardButtons116[c] != 0)
      CardBuild(c);
}

DWORD EudTriggerNext(DWORD dwAddr) {
  DWORD v;
  if (!Read32(dwAddr + 4, &v)) {
    Unserved(dwAddr + 4, false);
    return 0;
  }
  return v;
}

void EudTriggersClear(DWORD nList) {
  if (nList >= kLists)
    return;
  const DWORD dwLink = kLists116 + 12 * nList + 4;
  g_lists[3 * nList] = 0;
  g_lists[3 * nList + 1] = dwLink;
  g_lists[3 * nList + 2] = ~dwLink;
}

namespace {

void Mask(const BYTE *pRecord, int nMarkAt) {
  if (!EudGame())
    return;
  g_masked = pRecord[nMarkAt] == 'S' && pRecord[nMarkAt + 1] == 'C';
  if (g_masked)
    memcpy(&g_dwMask, pRecord, 4);
}

}  // namespace

void EudMaskCondition(const void *pCondition) {
  Mask(static_cast<const BYTE *>(pCondition), 0x12);
}

void EudMaskAction(const void *pAction) {
  Mask(static_cast<const BYTE *>(pAction), 0x1E);
}

void EudMaskClear() {
  g_masked = false;
}

bool EudRead(DWORD dwAddr, DWORD *pValue) {
  DWORD v;
  if (!Read32(dwAddr, &v)) {
    Unserved(dwAddr, false);
    *pValue = 0;
    return false;
  }
  *pValue = g_masked ? v & g_dwMask : v;
  Recent(dwAddr, *pValue, false);
  return true;
}

bool EudWrite(DWORD dwAddr, DWORD dwValue, EudMode mode) {
  DWORD dwOld = 0;
  if ((mode != EUD_SET || g_masked) && !Read32(dwAddr, &dwOld)) {
    Unserved(dwAddr, true);
    return false;
  }
  const DWORD dwMask = g_masked ? g_dwMask : 0xFFFFFFFFu;
  const DWORD dwCur = dwOld & dwMask;
  const DWORD dwArg = dwValue & dwMask;
  DWORD dwNew = dwArg;
  if (mode == EUD_ADD)
    dwNew = dwCur + dwArg;
  else if (mode == EUD_SUB)
    dwNew = dwArg < dwCur ? dwCur - dwArg : 0;
  dwNew = (dwOld & ~dwMask) | (dwNew & dwMask);
  if (!Write32(dwAddr, dwNew)) {
    Unserved(dwAddr, true);
    return false;
  }
  Recent(dwAddr, dwNew, true);
  return true;
}

DWORD EudDeaths(DWORD nPlayer, WORD wUnit) {
  if (wUnit == kAnyUnit)
    return nPlayer < PLAYER_SLOTS_117 ? *GameCount(nPlayer, kMen) + *GameCount(nPlayer, kBuildings) : 0;
  if (wUnit >= kMen && wUnit <= kFactories)
    return nPlayer < PLAYER_SLOTS_117 ? *GameCount(nPlayer, wUnit) : 0;
  DWORD v;
  EudRead(DeathsAddr(nPlayer, wUnit), &v);
  return v;
}

int EudSetDeaths(DWORD nPlayer, WORD wUnit, DWORD dwValue, EudMode mode) {
  if (wUnit == kAnyUnit)
    return 0;
  if (wUnit >= kMen && wUnit <= kFactories) {
    // as 1.17 sets them, for a player of the lobby
    if (PlayerIsLobby(nPlayer)) {
      DWORD *p = GameCount(nPlayer, wUnit);
      if (mode == EUD_SET)
        *p = dwValue;
      else if (mode == EUD_ADD)
        *p += dwValue;
      else
        *p = *p > dwValue ? *p - dwValue : 0;
    }
    return 0;
  }
  EudWrite(DeathsAddr(nPlayer, wUnit), dwValue, mode);
  return 0;
}
