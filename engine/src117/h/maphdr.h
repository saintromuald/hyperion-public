// maphdr.h -- the CHK chunk handlers and the map loader's helpers
// (src117/lang/maphdr.cpp).
//
// The maphdr_* handlers are __stdcall with three stack arguments and no register
// input (`ret 0Ch`): the chunk's file cursor, the chunk's size, and a third that
// most of them never read. The free functions are __fastcall under /Gr.
#pragma once

#include "platform.h"
#include "chunk.h"

struct chk_unit_t;                    // 36-byte CHK UNIT entry (defined in the TU)
struct maplink_t;                     // 20-byte nydus/addon link node (defined in the TU)
struct colorcycle_t;                  // 8-byte palette cycle record (defined in the TU)
struct mapver_ctx_t;                  // 8-byte map-version probe context (defined in the TU)
struct mapauth_t;                     // 8-byte map-authentication context (defined in the TU)
struct mapheader_t;                   // 0x20-byte CHK map header (defined in the TU)
struct point16_t;

class CUnit;

BOOL __stdcall maphdr_TYPE(void *pFile, int nSize, int a3);   // 0x468A20, 8 B
BOOL __stdcall maphdr_SIDE(void *pFile, int nSize, int a3);   // 0x468A60, 79 B
BOOL __stdcall maphdr_OWNR(void *pFile, int nSize, int a3);   // 0x468AB0, 79 B
BOOL __stdcall maphdr_ERA(void *pFile, int nSize, int a3);    // 0x468B00, 85 B
BOOL __stdcall maphdr_STR(void *pFile, int nSize, int a3);    // 0x468CE0, 107 B
BOOL __fastcall is_invalid_preplaced_unit(chk_unit_t *pUnit); // 0x469600, 79 B
BOOL __fastcall is_color_cycled(int nColor);                  // 0x4697C0, 65 B
void __fastcall free_map_strs(void);                          // 0x46A610, 38 B

// Some of these return a value that looks unused: the ReadChunk wrappers end in
// `return expr != 0;`, and sub_46A320 returns the list head it may have
// advanced. maphdr_PUNI zeroes ECX for IsScenarioGame; it takes no register
// argument.
BOOL __stdcall maphdr_VER(void *pFile, int nSize, void *pDest);      // 0x468A30, 39 B
BOOL __stdcall maphdr_SPRP(void *pFile, int nSize, void *pDest);     // 0x4692F0, 36 B
BOOL __stdcall maphdr_MRGN(void *pFile, int nSize, int a3);          // 0x468D50, 43 B
BOOL __stdcall maphdr_MRGN_Ext(void *pFile, int nSize, int a3);      // 0x468D80, 43 B
BOOL __stdcall maphdr_UPRP(void *pFile, int nSize, int a3);          // 0x468DB0, 43 B
BOOL __stdcall maphdr_FORC(void *pFile, int nSize, void *pDest);     // 0x469320, 53 B
BOOL __stdcall maphdr_DIM(void *pFile, int nSize, int a3);           // 0x469130, 59 B
BOOL __stdcall maphdr_COLR(void *pFile, int nSize, int a3);          // 0x468B60, 59 B
int  __stdcall maphdr_TRIG(void *pFile, int nSize, int a3);          // 0x468DE0, 158 B
BOOL __stdcall maphdr_PUNI(void *pFile, int nSize, int a3);          // 0x4685D0, 137 B
BOOL __stdcall maphdr_MASK(void *pFile, int nSize, int a3);          // 0x469260, 138 B
int  __stdcall maphdr_MBRF(void *pFile, int nSize, int a3);          // 0x468E80, 166 B
BOOL __stdcall maphdr_MTXM(void *pFile, int nSize, int a3);          // 0x469170, 240 B
BOOL __stdcall maphdr_VCOD(void *pFile, int nSize, int a3);          // 0x467E50, 229 B
BOOL __stdcall maphdr_PTEC(void *pFile, int nSize, int a3);          // 0x468930, 226 B
BOOL __stdcall maphdr_PTEC_Bwar(void *pFile, int nSize, int a3);     // 0x468840, 226 B
BOOL __stdcall maphdr_UNIS(void *pFile, int nSize, int a3);          // 0x4680D0, 381 B
// maphdr_UNIS_Bwar is big enough to call _chkstk, whose `push ecx` is not a
// register argument.
BOOL __stdcall maphdr_UNIS_Bwar(void *pFile, int nSize, int a3);     // 0x467F40, 397 B
BOOL __stdcall maphdr_UNIT(void *pFile, int nSize, int a3);          // 0x468F30, 501 B
BOOL __stdcall maphdr_UPGS(void *pFile, int nSize, int a3);          // 0x468340, 231 B
BOOL __stdcall maphdr_UPGS_Bwar(void *pFile, int nSize, int a3);     // 0x468250, 231 B
BOOL __stdcall maphdr_TECS(void *pFile, int nSize, int a3);          // 0x468500, 196 B
BOOL __stdcall maphdr_TECS_Bwar(void *pFile, int nSize, int a3);     // 0x468430, 199 B
BOOL __stdcall maphdr_UPGR(void *pFile, int nSize, int a3);          // 0x468750, 226 B
BOOL __stdcall maphdr_UPGR_Bwar(void *pFile, int nSize, int a3);     // 0x468660, 226 B

// sub_469650 is a callback the dispatcher sub_4C1350 calls for each entry of
// the map-version table at 0x4F1034..0x4F110C; a zero return stops the scan.
// The call sets neither ECX nor EDX, so it takes exactly the three stack
// arguments.
int __stdcall sub_469650(const char *pszName, int nVersion,
                         mapver_ctx_t *pCtx);                        // 0x469650, 172 B

BOOL close_map(void);                                                // 0x469440, 32 B
void init_cycles(colorcycle_t *pCycles);                             // 0x46A3B0, 104 B
void init_color_cycles(void);                                        // 0x46A390, 22 B
maplink_t *connect_nydus(maplink_t *pHead, CUnit *pUnit,
                         int nId, int nLink);                        // 0x4693D0, 99 B
maplink_t *sub_46A320(maplink_t *pHead, maplink_t *pNode);           // 0x46A320, 69 B
int setup_startloc(chk_unit_t *pUnit, point16_t *pStarts);           // 0x46A280, 158 B
maplink_t *connect_addon(maplink_t *pHead, CUnit *pUnit,
                         int nId, int nLink);                        // 0x469360, 108 B
void sub_46A370(void);                                               // 0x46A370, 32 B
BOOL sub_469DB0(intptr_t a1, int a2);                                     // 0x469DB0, 38 B
void PlaceGameStart(void);                                               // 0x469E90, 39 B
// unit_create is not CreateUnitAt (TrigAct.h): it is the CHK placement entry
// point and calls CreateUnitAt itself, which SetHangarContents calls directly.
CUnit *unit_create(WORD wType, int x, int y, int nPlayer);           // 0x469460, 253 B
maplink_t *sub_4698E0(maplink_t *pNode);                             // 0x4698E0, 139 B
void apply_liftoff(CUnit *pUnit, DWORD dwProps, int nArg2);            // 0x46A210, 106 B
void SetHangarContents(CUnit *pUnit, DWORD dwProps, BYTE bCount);    // 0x46A180, 135 B
BOOL __stdcall maphdr_THG2(void *pFile, int nSize, int a3);          // 0x468BA0, 308 B
BOOL __fastcall map_load_1(void *pBuffer, DWORD dwSize, int nPass,
                           mapheader_t *pHdr);                       // 0x469970, 153 B
BOOL sub_46A640(void);                                               // 0x46A640, 285 B
// Called by map_load_1 and sub_46A640.
BOOL __fastcall GetMapVersionIndex(void *pBuffer, DWORD dwSize, int *pnIndex,
                                   mapheader_t *pHdr);                       // 0x469700, 186 B
// The two map-load passes, which map_load.cpp calls. LoadMapPass1 takes ECX and
// EDX, LoadMapPass2 ECX.
BOOL LoadMapPass1(const char *pszName, mapheader_t *pHdr);            // 0x469A10, 363 B
BOOL LoadMapPass2(const char *pszName);                              // 0x469B80, 323 B
// maphdr_data_init (0x469ED0) is declared in game.h.

// The unit in ECX, its CHK entry's properties in EDX.
struct chk_unit_props_t;
void apply_unit_properties(CUnit *pUnit, chk_unit_props_t *pProps);  // 0x46A420, 481 B

// sub_469810 -- the addon half of the map-load fixup pass.
maplink_t *sub_469810(maplink_t *pNode);                             // 0x469810, 200 B

// The map file's lifecycle. authenticate_map writes the archive handle through
// phArchive (its one caller, sub_477E90, passes NULL). It is `void **`, not
// `HANDLE *`: <windows.h> spells HANDLE two ways, and this header does not
// include it.
BOOL __fastcall authenticate_map(const char *pszName, int *pnMapId,
                                 BOOL bKeepOpen, void **phArchive);   // 0x469560, 156 B
BOOL __fastcall OpenScenarioArchive(const char *pszName, char *pszDest,
                                    int nDestMax, int *pnMapId);      // 0x469DE0, 169 B
void *__fastcall get_map_chk(const char *pszMapName, DWORD *pdwSize); // 0x469CD0, 215 B
