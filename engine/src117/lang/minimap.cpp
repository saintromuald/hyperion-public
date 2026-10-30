// minimap.cpp -- the minimap dialog and its image control: the terrain-colour
// pass, the ping pool, the blink and refresh timers, the drag-scroll and
// targeting click handlers, and the border draw.
//
// The file is two halves written apart, and the lower one's order matters:
// 1.17 inlines sub_46E840's shade loop into sub_46EA90 and
// sub_46EBA0 but calls it from DrawMinimap and sub_46E140, so those two timer
// callbacks stay below its definition. draw_minimap_body is the 83-byte redraw
// 1.17 repeats in five bodies, a shared static of Blizzard's. The halves each
// declare the globals they use, so some appear twice, spelled alike.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <stddef.h>

#include "../h/bmp.h"
#include "../h/dialog.h"
#include "../h/rect16.h"
#include "../h/rect32.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/filters.h"
#include "../h/menu_dlg.h"
#include "../h/statdata.h"    // ctxhelp_show, grhsize_t
#include "../h/grp.h"         // FRAME / GROUP
#include "../h/group.h"       // grh_get_header
#include "../h/point16.h"     // gUnitPlaceboxSize
#include "../h/CSprite.h"
#include "../h/CThingy.h"
#include "../h/statbtn.h"     // target_is_on / placebox_is_on
#include "../h/net_cmds.h"    // IsGamePaused
#include "../h/minimap.h"
#include "../h/gds_bmpcop.h"
#include "../h/glues.h"     // mem_free, alloc_load_bmp
#include "../h/snd.h"       // load_game_data
#include "../h/gamedata.h"  // ReadGroupFile
#include "../h/game.h"      // CPalette::find_closest_palette_entry
#include "../h/maphdr.h"    // is_color_cycled
#include "../h/rclick.h"    // rclick_action
#include "../port/render/port_console.h"  // Hyperion: PortConsoleShift
#include "../port/render/port_teamcolor.h"  // Hyperion: PortTeamMinimapShown
#include "../port/render/port_minimap.h"  // Hyperion: PortMinimapDotColour
#include "../port/render/port_looks.h"  // Hyperion: a looked unit's pen
#include "../port/tools/port_trace.h"  // Hyperion: PortMinimapPress
#include "../port/render/port_view.h"  // Hyperion
#include "../h/playervision.h"  // Hyperion: the replay viewer past slot 7 (stage D)
#include "../h/selectlimits.h"  // Hyperion: SelectionWide

// Hyperion: the view in megatiles, the viewport box's size: 0x14 across
// and 0x0D down in 1.17, the view's width and its rows on screen rounded up.
// Hyperion: what shows beside a HUD's side columns (PortViewShownWidth)
static inline int MiniViewTilesX() { return PortViewShownWidth() / 32; }
static inline int MiniViewTilesY() { return (g_portView.nViewRows + 31) / 32; }

typedef unsigned char  BYTE;
typedef unsigned short WORD;
#include "platform.h"  // DWORD

// USER32!SetRect through the IAT slot 0x4EB630, spelled as
// src117/lang/dlg_dlgdraw.cpp spells it.
extern "C" __declspec(dllimport)
int __stdcall SetRect(rect32_t *prc, int xLeft, int yTop, int xRight,
                      int yBottom);

// 0x405E1E, declared in src117/h/CImage.h, which this TU does not include.
void __fastcall rle_draw_normal(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE *pRemap);


// Callees declared in headers this TU does not include, spelled as they are
// there.

// status_get_str -- oracle 0x4A7D70, as src117/h/order.h declares it.
char *__fastcall status_get_str(WORD wStringId);
// send_net_cmd_MinimapPing -- oracle 0x473F70. Its one caller zero-extends each
// coordinate, so the parameters are int; net_cmds.h's WORD declaration is wrong.
void __fastcall send_net_cmd_MinimapPing(int nX, int nY);
// bmp_create -- oracle 0x4CB200, as src117/h/gluRdy.h declares it.
void bmp_create(bmp_t *pBmp, const char *pszFile, int nLine);
// sub_46CA20 / sub_46CEA0 / sub_46CD10 / sub_46CDC0 -- oracle 0x46CA20 /
// 0x46CEA0 / 0x46CD10 / 0x46CDC0, defined below; sub_46D850 installs them.
void __fastcall sub_46CA20(void);
void __fastcall sub_46CD10(void);
void __fastcall sub_46CDC0(void);
void __fastcall sub_46CEA0(void);
// vid_paint_invalid -- oracle 0x4CB6C0. Nullary; it sets one dword flag.
void __fastcall vid_paint_invalid(void);
// minimap_ctxhelp_terrain -- oracle 0x46E880, defined below.
void __fastcall minimap_ctxhelp_terrain(ctrl_t *pCtrl);
// minimap_image_ctrl_mousedown -- oracle 0x46E420, defined below its caller.
void __fastcall minimap_image_ctrl_mousedown(ctrl_t *pCtrl, dlgevt_t *pEvent);
// mou_cager's argument is really a rect pointer (the body copies 16 bytes from
// ECX), though minimap.h declares an int; the call sites cast, as dlgFatal.cpp
// does.

// The globals this TU reaches, dual-spelled: one #ifdef per name.

// cellscrl.cpp's three cell and matrix pointers: gfpMtxSet 32-byte-strided with
// a WORD first member, gfpCellMap a WORD *, gfpCellBuf 32-byte-strided with a
// byte at +0x37.
#ifdef OC_DLL_MODE
#define gfpMtxSet (*reinterpret_cast<BYTE **>(OC_VA(0x543238u)))
#else
extern BYTE *gfpMtxSet;
#endif
#ifdef OC_DLL_MODE
#define gfpCellMap (*reinterpret_cast<WORD **>(OC_VA(0x54323Cu)))
#else
extern WORD *gfpCellMap;
#endif
#ifdef OC_DLL_MODE
#define gfpCellBuf (*reinterpret_cast<BYTE **>(OC_VA(0x543244u)))
#else
extern BYTE *gfpCellBuf;
#endif

// gwMapWidth @0x5264B4 (cellscrl.cpp) -- WORD, the map width in cells.
#ifdef OC_DLL_MODE
#define gwMapWidth (*reinterpret_cast<WORD *>(OC_VA(0x5264B4u)))
#else
extern WORD gwMapWidth;
#endif

// The minimap's size in pixels: WORDs only this TU reads, so file-static
// outside the DLL build.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgwMiniWdt_g (*reinterpret_cast<WORD *>(OC_VA(0x671FB8u)))
#else
static WORD sgwMiniWdt_g;
#endif
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgwMiniHgt_g (*reinterpret_cast<WORD *>(OC_VA(0x671FBCu)))
#else
static WORD sgwMiniHgt_g;
#endif

// 0x671FC8 / 0x671FD0 -- the terrain cache bitmap and the composited one.
#ifdef OC_DLL_MODE
#define sgMiniCacheBMP_g (*reinterpret_cast<bmp_t *>(OC_VA(0x671FC8u)))
#else
extern bmp_t sgMiniCacheBMP_g;
#endif
#ifdef OC_DLL_MODE
#define sgMiniBMP (*reinterpret_cast<bmp_t *>(OC_VA(0x671FD0u)))
#else
extern bmp_t sgMiniBMP;
#endif

// 0x672078 -- 256-entry palette-index map, filled by minimap_init.
#ifdef OC_DLL_MODE
#define sgCycleRemaps (reinterpret_cast<BYTE *>(OC_VA(0x672078u)))
#else
extern BYTE sgCycleRemaps[256] OC_UNALIGNED;
#endif

// gwMapHeight @0x5264B6 (cellscrl.cpp) -- WORD, the map height in cells.
#ifdef OC_DLL_MODE
#define gwMapHeight (*reinterpret_cast<WORD *>(OC_VA(0x5264B6u)))
#else
extern WORD gwMapHeight;
#endif

// sgpMinimapCtrl @0x671F98 (minimap.cpp) -- the minimap image control.
#ifdef OC_DLL_MODE
#define sgpMinimapCtrl (*reinterpret_cast<ctrl_t **>(OC_VA(0x671F98u)))
#else
extern ctrl_t *sgpMinimapCtrl;
#endif

// The map-to-minimap scaling quartet, all WORD and all written together by
// sub_46D850: sgwMiniNum @0x671FA8 the divisor, sgwMiniDivAdj @0x671FAC,
// sgwMiniDivShift @0x671FB0 the shift, sgwMiniScale_g @0x671FB4 the cells per
// minimap pixel.
#ifdef OC_DLL_MODE
#define sgwMiniNum (*reinterpret_cast<WORD *>(OC_VA(0x671FA8u)))
#else
extern WORD sgwMiniNum;
#endif
#ifdef OC_DLL_MODE
#define sgwMiniDivAdj (*reinterpret_cast<WORD *>(OC_VA(0x671FACu)))
#else
extern WORD sgwMiniDivAdj;
#endif
#ifdef OC_DLL_MODE
#define sgwMiniDivShift (*reinterpret_cast<WORD *>(OC_VA(0x671FB0u)))
#else
extern WORD sgwMiniDivShift;
#endif
#ifdef OC_DLL_MODE
#define sgwMiniScale_g (*reinterpret_cast<WORD *>(OC_VA(0x671FB4u)))
#else
extern WORD sgwMiniScale_g;
#endif

// 0x671FA0 / 0x671FA4 -- WORD mirrors of the screen origin in cells.
#ifdef OC_DLL_MODE
#define sgwLastScrollX (*reinterpret_cast<WORD *>(OC_VA(0x671FA0u)))
#else
extern WORD sgwLastScrollX;
#endif
#ifdef OC_DLL_MODE
#define sgwLastScrollY (*reinterpret_cast<WORD *>(OC_VA(0x671FA4u)))
#else
extern WORD sgwLastScrollY;
#endif

// _bmp_pen @0x6D0C50 (gds/vidblit.cpp) -- BYTE, the drawing colour.
// sgbPens_g @0x6D2368 (gds/palette.cpp) -- BYTE[32], the pens.
#ifdef OC_DLL_MODE
#define _bmp_pen (*reinterpret_cast<BYTE *>(OC_VA(0x6D0C50u)))
#else
extern BYTE _bmp_pen;
#endif
#ifdef OC_DLL_MODE
#define sgbPens_g (reinterpret_cast<BYTE *>(OC_VA(0x6D2368u)))
#else
extern BYTE sgbPens_g[32] OC_UNALIGNED;
#endif

// sgPings_g @0x671E98 -- sixteen slots of 0x10 bytes, ending at 0x671F98 where
// sgpMinimapCtrl begins.
#ifdef OC_DLL_MODE
#define sgPings_g (reinterpret_cast<minimap_ping_t *>(OC_VA(0x671E98u)))
#else
extern minimap_ping_t sgPings_g[16] OC_UNALIGNED;
#endif

// dword_671FC0 / dword_671FC4 -- the two render-stage hooks sub_46D850 installs.
#ifdef OC_DLL_MODE
#define dword_671FC0 (*reinterpret_cast<PFN_VOID *>(OC_VA(0x671FC0u)))
#else
extern PFN_VOID dword_671FC0;
#endif
#ifdef OC_DLL_MODE
#define dword_671FC4 (*reinterpret_cast<PFN_VOID *>(OC_VA(0x671FC4u)))
#else
extern PFN_VOID dword_671FC4;
#endif

// gpSquareMap @0x666454 (gamemap.cpp) -- one DWORD per map tile, the low byte
// of which is the visibility mask.
#ifdef OC_DLL_MODE
#define gpSquareMap (*reinterpret_cast<DWORD **>(OC_VA(0x666454u)))
#else
extern DWORD *gpSquareMap;
#endif

// gdwLocalMask @0x672668 / gdwLocalBlackMask @0x67266C (net_data.cpp) -- the
// local player's currently-visible and never-explored slot bitmasks.
#ifdef OC_DLL_MODE
#define gdwLocalMask (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
#else
extern DWORD gdwLocalMask;
#endif
#ifdef OC_DLL_MODE
#define gdwLocalBlackMask (*reinterpret_cast<DWORD *>(OC_VA(0x67266Cu)))
#else
extern DWORD gdwLocalBlackMask;
#endif

// gdwReplayVisionMask @0x67CC84 / sgbShowAll_g @0x67CC88 (replay.cpp) -- both
// DWORD despite the second one's `sgb` prefix.
#ifdef OC_DLL_MODE
#define gdwReplayVisionMask (*reinterpret_cast<DWORD *>(OC_VA(0x67CC84u)))
#else
extern DWORD gdwReplayVisionMask;
#endif
#ifdef OC_DLL_MODE
#define sgbShowAll_g (*reinterpret_cast<DWORD *>(OC_VA(0x67CC88u)))
#else
extern DWORD sgbShowAll_g;
#endif

// byte_672178 -- four 256-byte shade tables as one object, which the fog
// stages index from one base.
#ifdef OC_DLL_MODE
#define byte_672178 (reinterpret_cast<BYTE(*)[256]>(OC_VA(0x672178u)))
#else
extern BYTE byte_672178[4][256] OC_UNALIGNED;
#endif

// 0x665E6A -- gKeyTbl[VK_MENU], the ALT key state byte, spelled as
// src117/lang/event_Win.cpp spells it.
#ifdef OC_DLL_MODE
#define byte_665E6A (*reinterpret_cast<BYTE *>(OC_VA(0x665E6Au)))
#else
extern BYTE byte_665E6A;
#endif

// sgMinimapArea_g @0x508BB0 -- the cursor cage rectangle handed to mou_cager,
// which copies its four dwords to ClipCursor.
#ifdef OC_DLL_MODE
#define sgMinimapArea_g (*reinterpret_cast<rect32_t *>(OC_VA(0x508BB0u)))
#else
extern rect32_t sgMinimapArea_g;
#endif

// 0x665E68 -- gKeyTbl[VK_SHIFT] (event_Win.cpp spells this entry byte_665E68).
#ifdef OC_DLL_MODE
#define byte_665E68 (*reinterpret_cast<BYTE *>(OC_VA(0x665E68u)))
#else
extern BYTE byte_665E68;
#endif

// gbMinimapUnitMode_g @0x671A88 -- BYTE, the three-state unit colouring mode;
// gbMinimapMtxMode_g sits one byte above it.
#ifdef OC_DLL_MODE
#define gbMinimapUnitMode_g (*reinterpret_cast<BYTE *>(OC_VA(0x671A88u)))
#else
extern BYTE gbMinimapUnitMode_g;
#endif

// 0x671A89 -- the minimap terrain/mtx display mode byte.
#ifdef OC_DLL_MODE
#define gbMinimapMtxMode_g (*reinterpret_cast<BYTE *>(OC_VA(0x671A89u)))
#else
extern BYTE gbMinimapMtxMode_g;
#endif

// dword_671F9C -- the control the cursor is currently over.
#ifdef OC_DLL_MODE
#define dword_671F9C (*reinterpret_cast<ctrl_t **>(OC_VA(0x671F9Cu)))
#else
extern ctrl_t *dword_671F9C;
#endif

// gbMultiPlayerMode @0x672664 (net_data.cpp) -- BYTE.
#ifdef OC_DLL_MODE
#define gbMultiPlayerMode (*reinterpret_cast<BYTE *>(OC_VA(0x672664u)))
#else
extern BYTE gbMultiPlayerMode;
#endif

// sgbIsMinimapPreview_g @0x666560 (gamemap.cpp) -- four bytes, not one: every
// access is a dword compare.
#ifdef OC_DLL_MODE
#define sgbIsMinimapPreview_g (*reinterpret_cast<DWORD *>(OC_VA(0x666560u)))
#else
extern DWORD sgbIsMinimapPreview_g;
#endif

// gCtrlEvtFcnTbl @0x4F10F8 (.rdata, dlg/ctrl.cpp) -- twenty entries, bounded by
// gCtrlDrawFcnTbl @0x4F1148. Indexed by ctrl_t::wType.
#ifdef OC_DLL_MODE
#define gCtrlEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x4F10F8u)))
#else
extern PFN_CTRL_EVT gCtrlEvtFcnTbl[20];
#endif

// _bmp_current @0x6D0C40 (gds/image.cpp) -- the current render target. The
// minimap passes retarget it at sgMiniBMP and put it back.
#ifdef OC_DLL_MODE
#define _bmp_current (*reinterpret_cast<bmp_t **>(OC_VA(0x6D0C40u)))
#else
extern bmp_t *_bmp_current;
#endif

// gMinimapBlink_g @0x671FD8 -- the ping GRP, passed to grh_get_header in ECX.
#ifdef OC_DLL_MODE
#define gMinimapBlink_g (*reinterpret_cast<GROUP **>(OC_VA(0x671FD8u)))
#else
extern GROUP *gMinimapBlink_g;
#endif

// sgBlinkBits_g @0x671FE0 -- eight bytes per player, eight players, written
// over the first eight remap entries as two dwords.
#ifdef OC_DLL_MODE
#define sgBlinkBits_g (reinterpret_cast<DWORD (*)[2]>(OC_VA(0x671FE0u)))
#else
extern DWORD sgBlinkBits_g[8][2];
#endif

// sgGamePalette_g @0x666568 (gamemap.cpp) and sgMinimapPalette_g @0x671A90 --
// 256 palentry_t each, spelled as src117/lang/game.cpp spells them.
#ifdef OC_DLL_MODE
#define sgGamePalette_g (reinterpret_cast<BYTE *>(OC_VA(0x666568u)))
#define sgMinimapPalette_g (reinterpret_cast<BYTE *>(OC_VA(0x671A90u)))
#else
extern BYTE sgGamePalette_g[1024] OC_UNALIGNED;
extern BYTE sgMinimapPalette_g[1024];
#endif

// _gbRemapColorTbl @0x501021 (rle.asm) -- the BYTE[256] the RLE blitters
// recolour through.
#ifdef OC_DLL_MODE
#define _gbRemapColorTbl (reinterpret_cast<BYTE *>(OC_VA(0x501021u)))
#else
extern BYTE _gbRemapColorTbl[256] OC_UNALIGNED;
#endif

// g_LocalNationID @0x508D50 (net_glue.cpp) -- the local player slot.
#ifdef OC_DLL_MODE
#define g_LocalNationID (*reinterpret_cast<int *>(OC_VA(0x508D50u)))
#else
extern int g_LocalNationID;
#endif

// sgnMiniRectSeq_g and sgnMiniVisHash_g are declared further down.

// gbMinimapColor_g @0x5290B6 -- one minimap palette index per player.
#ifdef OC_DLL_MODE
#define gbMinimapColor_g (reinterpret_cast<BYTE *>(OC_VA(0x5290B6u)))
#else
extern PlayerMinimapColorTable gbMinimapColor_g;
#endif

// sgbAlliances_g @0x534914 -- twelve by twelve, indexed [viewer][owner].
#ifdef OC_DLL_MODE
#define sgbAlliances_g (reinterpret_cast<BYTE (*)[12]>(OC_VA(0x534914u)))
#else
extern PlayerAlliancesMatrix sgbAlliances_g;
#endif

// gUnitPlaceboxSize @0x522728 (arr/units.cpp) -- each unit type's placement box.
#ifdef OC_DLL_MODE
#define gUnitPlaceboxSize (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
#else
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;
#endif

// gPlayback @0x67CC90 (replay.cpp) -- DWORD.
#ifdef OC_DLL_MODE
#define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
#else
extern DWORD gPlayback;
#endif

// The declarations the lower half's bodies were written against.

// A local's type, not a game object: the pair minimap_image_ctrl_target_mousedown
// hands to sub_46CFD0 by address, a struct for its stack layout (see that
// function). It has gCursorPos_g's shape.
struct point32_t {
  /*0x00*/ LONG x;
  /*0x04*/ LONG y;
};
static_assert(sizeof(point32_t) == 0x08, "point32_t size   two dword stack slots ([ebp-8], [ebp-4])");
static_assert(offsetof(point32_t, x) == 0x00, "point32_t::x   minimap_image_ctrl_target_mousedown: mov dword ptr [ebp-8h], eax first");
static_assert(offsetof(point32_t, y) == 0x04, "point32_t::y   minimap_image_ctrl_target_mousedown: the second of the pair");

// An unsigned 16-bit point, a local's type only: as a struct, MSVC keeps the
// pair in 16-bit registers, as 1.17 does. Unsigned because the shift is logical.
struct wpoint16_t {
  WORD x;
  WORD y;
};

// 0x5264B0 / 0x5264B2 -- the map view's origin in 32-pixel cells, a WORD pair;
// the naming sheet does not cover this range.
#ifdef OC_DLL_MODE
#define word_5264B0 (*reinterpret_cast<WORD*>(OC_VA(0x5264B0u)))
#else
extern WORD word_5264B0;
#endif
#ifdef OC_DLL_MODE
#define word_5264B2 (*reinterpret_cast<WORD*>(OC_VA(0x5264B2u)))
#else
extern WORD word_5264B2;
#endif

// cellscrl.cpp's three cell and matrix pointers, which sub_46C920 walks:
// gfpMtxSet 32-byte-strided with a WORD first member, gfpCellMap a WORD *,
// gfpCellBuf 32-byte-strided with a byte read at +0x37.
#ifdef OC_DLL_MODE
#define gfpMtxSet (*reinterpret_cast<BYTE**>(OC_VA(0x543238u)))
#else
extern BYTE *gfpMtxSet;
#endif
#ifdef OC_DLL_MODE
#define gfpCellMap (*reinterpret_cast<WORD**>(OC_VA(0x54323Cu)))
#else
extern WORD *gfpCellMap;
#endif
#ifdef OC_DLL_MODE
#define gfpCellBuf (*reinterpret_cast<BYTE**>(OC_VA(0x543244u)))
#else
extern BYTE *gfpCellBuf;
#endif

// sgbIsMinimapPreview_g @0x666560 (gamemap.cpp) -- four bytes: its `sgb` prefix
// stands for a BOOL, an int.
#ifdef OC_DLL_MODE
#define sgbIsMinimapPreview_g (*reinterpret_cast<DWORD*>(OC_VA(0x666560u)))
#else
extern DWORD sgbIsMinimapPreview_g;
#endif

// shiftDark_g @0x66DD20 (light.cpp) -- BYTE[32][256]; the 0x66EC20 the shade
// loop reads is its row 15, not a global of its own.
#ifdef OC_DLL_MODE
#define shiftDark_g (reinterpret_cast<BYTE(*)[256]>(OC_VA(0x66DD20u)))
#else
extern BYTE shiftDark_g[32][256];
#endif

// 0x671A89 -- byte flag gating the "clear the minimap bitmap" path.
#ifdef OC_DLL_MODE
#define gbMinimapMtxMode_g (*reinterpret_cast<BYTE*>(OC_VA(0x671A89u)))
#else
extern BYTE gbMinimapMtxMode_g;
#endif

// byte_671A8A / byte_671A8B -- a byte each: 0x671A8A is the blink phase
// sub_46EBA0 toggles, and 0x671A8B, set while a unit blinks, gates the rest of
// sub_46EBA0.
#ifdef OC_DLL_MODE
#define byte_671A8A (*reinterpret_cast<BYTE*>(OC_VA(0x671A8Au)))
#else
extern BYTE byte_671A8A;
#endif
#ifdef OC_DLL_MODE
#define byte_671A8B (*reinterpret_cast<BYTE*>(OC_VA(0x671A8Bu)))
#else
extern BYTE byte_671A8B;
#endif

// sgpMinimapDlg @0x671A8C -- the dialog root.
#ifdef OC_DLL_MODE
#define sgpMinimapDlg (*reinterpret_cast<ctrl_t**>(OC_VA(0x671A8Cu)))
#else
extern ctrl_t *sgpMinimapDlg;
#endif

// 0x671E90 / 0x671E94 -- DWORDs, the count of dots drawn and the hash of the
// units' visibility; sub_46C8E0 reads only their low byte.
#ifdef OC_DLL_MODE
#define sgnMiniRectSeq_g (*reinterpret_cast<DWORD*>(OC_VA(0x671E90u)))
#else
extern DWORD sgnMiniRectSeq_g;
#endif
#ifdef OC_DLL_MODE
#define sgnMiniVisHash_g (*reinterpret_cast<DWORD*>(OC_VA(0x671E94u)))
#else
extern DWORD sgnMiniVisHash_g;
#endif

// sgPings_g @0x671E98 -- sixteen slots of 0x10 bytes, ending where
// sgpMinimapCtrl begins.
#ifdef OC_DLL_MODE
#define sgPings_g (reinterpret_cast<minimap_ping_t*>(OC_VA(0x671E98u)))
#else
extern minimap_ping_t sgPings_g[16] OC_UNALIGNED;
#endif

// sgpMinimapCtrl @0x671F98 -- the minimap image control, fed to ctrl_set_update
// by four functions here.
#ifdef OC_DLL_MODE
#define sgpMinimapCtrl (*reinterpret_cast<ctrl_t**>(OC_VA(0x671F98u)))
#else
extern ctrl_t *sgpMinimapCtrl;
#endif

// dword_671F9C -- the control the cursor is currently over, which
// minimap_mouse_moved and minimap_btn_mouse_moved set and clear.
#ifdef OC_DLL_MODE
#define dword_671F9C (*reinterpret_cast<ctrl_t**>(OC_VA(0x671F9Cu)))
#else
extern ctrl_t *dword_671F9C;
#endif

// 0x671FA0 / 0x671FA4 -- WORD mirrors of the screen origin (minimap_check_scroll compares
// then writes them; minimap_init seeds them from 0x5264B0/0x5264B2).
#ifdef OC_DLL_MODE
#define sgwLastScrollX (*reinterpret_cast<WORD*>(OC_VA(0x671FA0u)))
#else
extern WORD sgwLastScrollX;
#endif
#ifdef OC_DLL_MODE
#define sgwLastScrollY (*reinterpret_cast<WORD*>(OC_VA(0x671FA4u)))
#else
extern WORD sgwLastScrollY;
#endif

// 0x671FA2 -- byte "redraw pending" flag: set to 1 by minimap_force_redraw,
// consumed and cleared by sub_46E8D0, sub_46EA90 and sub_46EBA0.
#ifdef OC_DLL_MODE
#define sgbRedrawMinimap_g (*reinterpret_cast<BYTE*>(OC_VA(0x671FA2u)))
#else
extern BYTE sgbRedrawMinimap_g;
#endif

// 0x671FA8 -- WORD divisor, 0x671FB0 -- WORD shift; both drive the
// map->minimap coordinate scaling.
#ifdef OC_DLL_MODE
#define sgwMiniNum (*reinterpret_cast<WORD*>(OC_VA(0x671FA8u)))
#else
extern WORD sgwMiniNum;
#endif
#ifdef OC_DLL_MODE
#define sgwMiniDivShift (*reinterpret_cast<WORD*>(OC_VA(0x671FB0u)))
#else
extern WORD sgwMiniDivShift;
#endif

// sgwMiniWdt_g @0x671FB8 / sgwMiniHgt_g @0x671FBC are declared at the top of
// the file.

// dword_671FC0 / dword_671FC4 -- the terrain and fog render stages sub_46D850
// installs, called with no arguments.
#ifdef OC_DLL_MODE
#define dword_671FC0 (*reinterpret_cast<PFN_VOID*>(OC_VA(0x671FC0u)))
#else
extern PFN_VOID dword_671FC0;
#endif
#ifdef OC_DLL_MODE
#define dword_671FC4 (*reinterpret_cast<PFN_VOID*>(OC_VA(0x671FC4u)))
#else
extern PFN_VOID dword_671FC4;
#endif

// 0x671FC8 / 0x671FD0 -- two adjacent bitmaps, the terrain cache and the
// composited minimap. The cache's pPixels, at 0x671FCC, is what sub_46C920
// writes through.
#ifdef OC_DLL_MODE
#define sgMiniCacheBMP_g (*reinterpret_cast<bmp_t*>(OC_VA(0x671FC8u)))
#else
extern bmp_t sgMiniCacheBMP_g;
#endif
#ifdef OC_DLL_MODE
#define sgMiniBMP (*reinterpret_cast<bmp_t*>(OC_VA(0x671FD0u)))
#else
extern bmp_t sgMiniBMP;
#endif

// 0x672078 -- 256-entry palette-index map, filled by minimap_init.
#ifdef OC_DLL_MODE
#define sgCycleRemaps (reinterpret_cast<BYTE*>(OC_VA(0x672078u)))
#else
extern BYTE sgCycleRemaps[256] OC_UNALIGNED;
#endif

// byte_672178 -- the four 256-byte shade tables, one BYTE[4][256] object. As
// four arrays, MSVC splits sub_46E840's loop into a memset and a memcpy; as one
// it stays the single loop 1.17 has.
#ifdef OC_DLL_MODE
#define byte_672178 (reinterpret_cast<BYTE(*)[256]>(OC_VA(0x672178u)))
#else
extern BYTE byte_672178[4][256] OC_UNALIGNED;
#endif

// gbMultiPlayerMode @0x672664 (net_data.cpp) -- BYTE.
#ifdef OC_DLL_MODE
#define gbMultiPlayerMode (*reinterpret_cast<BYTE*>(OC_VA(0x672664u)))
#else
extern BYTE gbMultiPlayerMode;
#endif

// gPlayback @0x67CC90 (replay.cpp) -- DWORD.
#ifdef OC_DLL_MODE
#define gPlayback (*reinterpret_cast<DWORD*>(OC_VA(0x67CC90u)))
#else
extern DWORD gPlayback;
#endif

// 0x6D0C40 -- the current render target (gds/image.cpp).
#ifdef OC_DLL_MODE
#define _bmp_current (*reinterpret_cast<bmp_t**>(OC_VA(0x6D0C40u)))
#else
extern bmp_t *_bmp_current;
#endif

// _bmp_pen @0x6D0C50 (gds/vidblit.cpp) -- BYTE.
#ifdef OC_DLL_MODE
#define _bmp_pen (*reinterpret_cast<BYTE*>(OC_VA(0x6D0C50u)))
#else
extern BYTE _bmp_pen;
#endif

// sgbPens_g @0x6D2368 (gds/palette.cpp) -- BYTE[32]; 0x6D2377 is sgbPens_g[15],
// not a global of its own.
#ifdef OC_DLL_MODE
#define sgbPens_g (reinterpret_cast<BYTE*>(OC_VA(0x6D2368u)))
#else
extern BYTE sgbPens_g[32] OC_UNALIGNED;
#endif

// gCursorPos_g @0x6D3154 (gds/mou_win.cpp) -- one eight-byte object, x and y;
// 0x6D3158 is gCursorPos_g[1].
#ifdef OC_DLL_MODE
#define gCursorPos_g (reinterpret_cast<LONG*>(OC_VA(0x6D3154u)))
#else
extern LONG gCursorPos_g[2];
#endif

// sMinimapEvtFcnTbl_g @0x508C40 -- the four control handlers
// minimap_dlg_activate registers for a live game, a byte count of 0x10.
#ifdef OC_DLL_MODE
#define sMinimapEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT*>(OC_VA(0x508C40u)))
#else
extern PFN_CTRL_EVT sMinimapEvtFcnTbl_g[4] OC_UNALIGNED;
#endif

// sMinimapPreviewEvtFcnTbl_g @0x508C50 -- the map-preview variant, registered
// with one entry.
#ifdef OC_DLL_MODE
#define sMinimapPreviewEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT*>(OC_VA(0x508C50u)))
#else
extern PFN_CTRL_EVT sMinimapPreviewEvtFcnTbl_g[1];
#endif

// gCtrlDrawFcnTbl @0x4F1148 (dlg/draw.cpp, .rdata) -- the draw function per
// control type that dlg_load_ctrl installs and minimap_dlg_draw re-dispatches
// through, declared at the 15 entries indexed here.
#ifdef OC_DLL_MODE
#define gCtrlDrawFcnTbl (reinterpret_cast<PFN_CTRL_DRAW*>(OC_VA(0x4F1148u)))
#else
extern PFN_CTRL_DRAW gCtrlDrawFcnTbl[15];
#endif

// The tag below belongs to sub_46C8E0, whose body is further down, after
// minimap_ctxhelp_terrain.

// The context-help bubble for one minimap button, anchored at the button's right
// edge in the owning dialog's coordinates. 1.17 has it in minimap_ctxhelp_terrain
// (0x46E880), minimap_btn_command and sub_46E1E0, a shared helper.
static __forceinline void ctxhelp_at(ctrl_t *pCtrl, WORD wStrId)
{
  ctrl_t *pParent = pCtrl->pParent;
  char *pszText = status_get_str(wStrId);

  ctxhelp_show(pParent->rc.left + pCtrl->rc.right,
               pParent->rc.top + pCtrl->rc.top,
               reinterpret_cast<intptr_t>(pszText));
}

// oracle 0x46C990 (130 B). The terrain pass at 1:2, one output pixel per two
// map cells; sub_46C920 is the 1:1 sibling. The WORD width is copied into an int
// each row, as 1.17 reloads it.
void __fastcall sub_46C990(void)
{
  const WORD *src = gfpCellMap;
  int nRows = (int)(WORD)sgwMiniHgt_g;
  BYTE *dst = sgMiniCacheBMP_g.pPixels;

  if (nRows != 0) {
    WORD nWidth = (WORD)sgwMiniWdt_g;

    do {
      int nCols = nWidth;

      nRows--;
      while (nCols--) {
        unsigned int grp = (unsigned int)*src & 0x7FFF;
        unsigned int mt = *(const DWORD *)(gfpMtxSet + (grp << 6));  // Hyperion: 32-bit cell references (PortMegatilesLoad)
        unsigned int mi = gfpCellBuf[(static_cast<size_t>(mt & ~1u) << 5) + 0x37];

        src += 2;
        *dst++ = sgCycleRemaps[mi];
      }
      src += gwMapWidth;
    } while (nRows != 0);
  }
}

// One minimap pixel for the minitile at nOfs inside the cell's megatile: map
// tile -> CV5 group -> megatile -> minitile byte -> palette map. The cell is
// re-read at every use because the stores through the output pointer may alias
// it.
static __forceinline int mini_pixel(const WORD *pCell, int nOfs)
{
  unsigned int grp = (unsigned int)*pCell & 0x7FFF;
  unsigned int mt = *(const DWORD *)(gfpMtxSet + (grp << 6) + nOfs * 2);  // Hyperion: 32-bit cell references (PortMegatilesLoad)
  unsigned int mi = gfpCellBuf[(static_cast<size_t>(mt & ~1u) << 5) + 0x37];

  return sgCycleRemaps[mi];
}

// oracle 0x46CA20 (346 B). The terrain pass at 2:1, a 2x2 output block per map
// cell taken from the megatile's four corner minitiles.
void __fastcall sub_46CA20(void)
{
  int nPitch = sgMiniCacheBMP_g.nWidth;
  BYTE *dst = sgMiniCacheBMP_g.pPixels;
  unsigned nRows = (WORD)sgwMiniHgt_g >> 1;
  const WORD *src = gfpCellMap;

  if (nRows != 0) {
    do {
      WORD nW = sgwMiniWdt_g;
      const WORD *s = src;
      nRows--;
      unsigned nCols = nW >> 1;
      if (nCols != 0) {
        do {
          int nStride = (WORD)sgwMiniWdt_g;
          dst[0] = mini_pixel(s, 0);
          dst[nStride] = mini_pixel(s, 8);
          dst[1] = mini_pixel(s, 2);
          dst[nStride + 1] = mini_pixel(s, 0x0A);
          s++;
          // Two identical arms, for 1.17's register order; they compile to one add.
          if (nCols) { dst += 2; } else { dst += 2; }
        } while (--nCols);
      }
      src = s;
      dst += nPitch * 2 - nW;
    } while (nRows != 0);
  }
  // No effect; part of the only shape found that compiles as 1.17 does in both builds.
  __assume(1);
}

// The fog shade row for one map tile: 0..3, built from whether the tile is
// currently visible and whether it has never been explored. In a replay the
// two flags come from the replay vision mask instead.
static __forceinline int fog_shade_row(const DWORD *pSquare)
{
  int nVis = (*pSquare & gdwLocalMask) == 0;
  int nBlack = (*pSquare & gdwLocalBlackMask) == 0;

  if (gPlayback) {
    if (sgbShowAll_g == 0) {
      DWORD dwMask = gdwReplayVisionMask;
      unsigned int nNot = ~*pSquare;
      const int nTile = static_cast<int>(pSquare - gpSquareMap);  // Hyperion: the viewer's slots past 7 (stage D)

      if ((nNot & ((BYTE)dwMask << 8)) == 0 && !ViewHxTileExplored(nTile)) {
        nVis = 0;
        nBlack = 0;
      } else if (((BYTE)dwMask & (BYTE)nNot) == 0 && !ViewHxTileSeen(nTile)) {
        nVis = 0;
        nBlack = 1;
      } else {
        nVis = 1;
        nBlack = 0;
      }
    } else {
      nVis = 1;
      nBlack = 0;
    }
  }
  return nVis * 2 | nBlack;
}

// The fog pass at 1:1: shade each cached terrain pixel
// by its tile's visibility and write it to the composited minimap.
void __fastcall sub_46CD10(void)
{
  BYTE *pOut = sgMiniBMP.pPixels;
  BYTE *pCache = sgMiniCacheBMP_g.pPixels;
  int n = (int)((unsigned int)(WORD)sgwMiniWdt_g * (unsigned int)(WORD)sgwMiniHgt_g);
  const DWORD *pSquare = gpSquareMap;

  while (n--) {
    int nRow = fog_shade_row(pSquare);

    pSquare++;
    *pOut++ = byte_672178[nRow][*pCache++];
  }
}

// oracle 0x46CDC0 (221 B). The fog pass at 2:1: one output pixel per two map
// tiles across, one row of tiles per output row. The WORD width and the advance
// written in both arms give 1.17's register order; MSVC merges the arms.
void __fastcall sub_46CDC0(void)
{
  BYTE *pOut = sgMiniBMP.pPixels;
  BYTE *pCache = sgMiniCacheBMP_g.pPixels;
  int nRows = (WORD)sgwMiniHgt_g;
  const DWORD *pSquare = gpSquareMap;

  if (nRows) {
    WORD nWidth = (WORD)sgwMiniWdt_g;

    do {
      nRows--;
      int nCols = nWidth;

      while (nCols--) {
        int nRow = fog_shade_row(pSquare);

        *pOut++ = byte_672178[nRow][*pCache++];
        if (nCols)
          pSquare += 2;
        else
          pSquare += 2;
      }
      pSquare += gwMapWidth;
    } while (nRows);
  }
}

// Hyperion: the terrain and fog passes at N:1, for a map past 256 tiles a side
//: one output pixel per N map tiles across, one row per N tile rows,
// as sub_46C990 and sub_46CDC0 take one in two. N is sgwMiniNum, 4 to 512 tiles, 8 to 1,024.
static void __fastcall MinimapTerrainN(void)
{
  const int n = sgwMiniNum;
  const WORD *src = gfpCellMap;
  BYTE *dst = sgMiniCacheBMP_g.pPixels;

  for (int nRows = (WORD)sgwMiniHgt_g; nRows != 0; nRows--) {
    for (int nCols = (WORD)sgwMiniWdt_g; nCols != 0; nCols--) {
      unsigned int grp = (unsigned int)*src & 0x7FFF;
      unsigned int mt = *(const DWORD *)(gfpMtxSet + (grp << 6));  // Hyperion: 32-bit cell references (PortMegatilesLoad)
      unsigned int mi = gfpCellBuf[(static_cast<size_t>(mt & ~1u) << 5) + 0x37];

      src += n;
      *dst++ = sgCycleRemaps[mi];
    }
    src += (n - 1) * gwMapWidth;
  }
}

static void __fastcall MinimapFogN(void)
{
  const int n = sgwMiniNum;
  BYTE *pOut = sgMiniBMP.pPixels;
  BYTE *pCache = sgMiniCacheBMP_g.pPixels;
  const DWORD *pSquare = gpSquareMap;

  for (int nRows = (WORD)sgwMiniHgt_g; nRows != 0; nRows--) {
    for (int nCols = (WORD)sgwMiniWdt_g; nCols != 0; nCols--) {
      *pOut++ = byte_672178[fog_shade_row(pSquare)][*pCache++];
      pSquare += n;
    }
    pSquare += (n - 1) * gwMapWidth;
  }
}

// oracle 0x46CEA0 (294 B). The fog pass at 1:2: one map tile fills a 2x2 block
// of output pixels, so the pass walks half the rows and half the columns.
void __fastcall sub_46CEA0(void)
{
  unsigned int nWidth = (WORD)sgwMiniWdt_g;
  int nStride = sgMiniBMP.nWidth * 2 - (int)nWidth;
  unsigned int nRows = (unsigned int)(WORD)sgwMiniHgt_g >> 1;
  const DWORD *pSquare = gpSquareMap;
  BYTE *pOut = sgMiniBMP.pPixels;
  BYTE *pCache = sgMiniCacheBMP_g.pPixels;

  if (nRows) {
    // The counter is reset from its saved copy at the latch.
    unsigned int nCols = nWidth >> 1;
    unsigned int nCols0 = nCols;

    do {
      nRows--;
      while (nCols--) {
        int nRow = fog_shade_row(pSquare);
        int nPitch = (WORD)sgwMiniWdt_g;

        pOut[0] = byte_672178[nRow][pCache[0]];
        pOut[nPitch] = byte_672178[nRow][pCache[nPitch]];
        pOut[1] = byte_672178[nRow][pCache[1]];
        pOut[nPitch + 1] = byte_672178[nRow][pCache[nPitch + 1]];
        pCache += 2;
        pOut += 2;
        pSquare++;
      }
      pCache += nStride;
      pOut += nStride;
      nCols = nCols0;
    } while (nRows);
  }
}

// Clamps a raw cursor position into the minimap control's bitmap and scales it
// to map pixels. Only the low WORD of each out-parameter is touched. The scale
// is read once into a WORD local, which keeps it in DI for both multiplies.
void __fastcall sub_46CFD0(LONG *px, LONG *py)
{
  ctrl_t *pCtrl = sgpMinimapCtrl;
  WORD *pX = reinterpret_cast<WORD *>(px);
  WORD *pY = reinterpret_cast<WORD *>(py);

  // Hyperion: the dialog's left is the console's, 0 in 1.17 (port_view.cpp).
  if ((short)(*pX -= pCtrl->rc.left + PortConsoleShift(sgpMinimapDlg)) < 0)
    *pX = 0;
  else if ((WORD)*pX >= sgMiniCacheBMP_g.nWidth)
    *pX = sgMiniCacheBMP_g.nWidth - 1;
  WORD wScale = sgwMiniScale_g;
  *pX *= wScale;

  if ((short)(*pY += -315 - pCtrl->rc.top) < 0)
    *pY = 0;
  else if ((WORD)*pY >= sgMiniCacheBMP_g.nHeight)
    *pY = sgMiniCacheBMP_g.nHeight - 1;
  *pY *= wScale;
}

// The minimap button bar's command handler. Case
// bodies are laid out 4, 3, 2, which under /Ox is their source order; the
// compare chain stays ascending regardless.
void __fastcall minimap_btn_command(ctrl_t *pCtrl)
{
  switch (pCtrl->nIndex) {
  case 4:
    ctxhelp_hide();
    do_ally_filter_dlg();
    break;
  case 3:
    ctxhelp_hide();
    do_msg_filter_dlg();
    break;
  case 2:
    gbMinimapMtxMode_g ^= 1;
    dword_671F9C = 0;
    DrawMinimap();
    if (gbMinimapMtxMode_g)
      ctxhelp_at(pCtrl, 0x32A);
    else
      ctxhelp_at(pCtrl, 0x32B);
    break;
  }
}

// The activate handler for one minimap button: set
// its frame and then show or hide it. Case bodies are laid out 4, 3, 2, which
// is their source order; the compare chain stays ascending.
void __fastcall minimap_btn_activate(ctrl_t *pCtrl)
{
  BYTE bShow = 0;

  switch (pCtrl->nIndex) {
  case 4:
    set_btn_size(pCtrl, gPlayback ? 7 : 1, 0);
    if (gbMultiPlayerMode && get_num_active_players() > 1)
      bShow = 1;
    else
      bShow = 0;
    if (gbMultiPlayerMode && IsTVBGame())
      bShow = 1;
    if (gPlayback)
      bShow = 1;
    break;
  case 3:
    set_btn_size(pCtrl, 6, 0);
    if (gbMultiPlayerMode && get_num_active_players() > 1)
      bShow = 1;
    else
      bShow = 0;
    break;
  case 2:
    set_btn_size(pCtrl, 0, 0);
    if (gbMultiPlayerMode && get_num_active_players() > 1)
      bShow = 0;
    else
      bShow = 1;
    if (gbMultiPlayerMode && IsTVBGame())
      bShow = 0;
    if (gPlayback)
      bShow = 0;
    break;
  }
  ctrl_showhide(pCtrl, bShow);
}

// The timer-5 callback minimap_dlg_activate registers:
// re-show the ally and message filter buttons when the game turns multiplayer,
// and retire the timer once it has. The timer dispatcher (timer_update) reaches
// every callback with the owning dialog in ECX and the timer id in EDX. The
// ally button keeps the first disjunct's value, so it is saved before the
// other two arms widen it.
void __fastcall sub_46D240(ctrl_t *pDlg, int nTimerId)
{
  BYTE bShow;
  BYTE bAlly;

  if (gbMultiPlayerMode && get_num_active_players() > 1)
    bShow = 1;
  else
    bShow = 0;
  bAlly = bShow;
  if (gbMultiPlayerMode && IsTVBGame())
    bShow = 1;
  if (gPlayback)
    bShow = 1;

  if (sgbIsMinimapPreview_g == 0) {
    // The temporary puts the lookup before the flag; nested, MSVC forms the
    // flag first and needs a callee-saved register to carry it across.
    ctrl_t *pBtn;

    pBtn = ctrl_find_ndx(pDlg, 2);
    ctrl_showhide(pBtn, bShow == 0);
    pBtn = ctrl_find_ndx(pDlg, 4);
    ctrl_showhide(pBtn, bShow);
    pBtn = ctrl_find_ndx(pDlg, 3);
    ctrl_showhide(pBtn, bAlly);
  }
  if (!bShow)
    timer_remove_node(pDlg, nTimerId);
}

// Draws every live minimap ping into the composited
// minimap, recolouring the blink GRP through the first eight remap entries and
// clipping each frame to the minimap extent.
void __fastcall sub_46D3F0(void)
{
  bmp_t *pSave = _bmp_current;
  int i;

  _bmp_current = &sgMiniBMP;
  for (i = 0; i < 16; i++) {
    if (sgPings_g[i].active) {
      FRAME frame;
      grhsize_t size;
      rect32_t rc;
      int nX;
      int nY;
      int nClip;
      int n;

      *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[0]) =
          sgBlinkBits_g[sgPings_g[i].player][0];
      *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[4]) =
          sgBlinkBits_g[sgPings_g[i].player][1];
      grh_get_header(gMinimapBlink_g, sgPings_g[i].timer, &frame, &size);
      SetRect(&rc, 0, 0, frame.bWidth, frame.bHeight);
      nX = frame.bOffsetX + sgPings_g[i].x;
      nY = frame._unmapped_01 + sgPings_g[i].y;
      if (nX < 0) {
        rc.left -= nX;
        rc.right += nX;
        nX = 0;
      }
      nClip = rc.right - (WORD)sgwMiniWdt_g + nX;
      if (nClip > 0)
        rc.right -= nClip;
      if (nY < 0) {
        rc.top -= nY;
        rc.bottom += nY;
        nY = 0;
      }
      nClip = rc.bottom - (WORD)sgwMiniHgt_g + nY;
      if (nClip > 0)
        rc.bottom -= nClip;
      rle_draw_normal(nX, nY, &frame, &rc, 0);
      for (n = 0; n < 8; n++)
        _gbRemapColorTbl[n] = (BYTE)n;
    }
  }
  _bmp_current = pSave;
}

// True outside the 0xCB..0xD5 type run. As a helper, MSVC folds its two
// compares into one, as 1.17 has it.
static __forceinline bool mm_not_marker(WORD w) { return w < 0xCB || w > 0xD5; }

// The unit-dot pass: run the per-player
// fog passes, then walk the fogged thingy list and blit one coloured dot per
// thingy.
void __fastcall sub_46D510(void)
{
  bmp_t *pSave = _bmp_current;
  int nPlayer = PlayerSlots();  // Hyperion: 12 in 1.17, every slot
  CThingy *pThingy;

  _bmp_current = &sgMiniBMP;
  sgnMiniRectSeq_g = 0;
  sgnMiniVisHash_g = 0;
  do {
    nPlayer--;
    if (nPlayer >= 8)
      sub_46E5B0(nPlayer);
    else if (g_LocalNationID != nPlayer || gPlayback)
      sub_46E700(nPlayer);
  } while (nPlayer);

  if (!gPlayback)
    sub_46E490(g_LocalNationID);

  for (pThingy = CThingy::FirstFoggedThingy(); pThingy; pThingy = pThingy->pNext) {
    if (mm_not_marker(pThingy->wUnitType)) {
      if (!gPlayback || !pThingy->sub_418520(1)) {
        CSprite *pSprite = pThingy->pSprite;
        BYTE bColor;

        if (pSprite->wSpriteID == 0x113 || pSprite->wSpriteID == 0x117 ||
            pSprite->wSpriteID == 0x118 || pSprite->wSpriteID == 0x119)
          // Hyperion: a looked one in its look's
          bColor = PortLookPlacePen(pSprite->wPosX, pSprite->wPosY, pThingy->wUnitType, sgbPens_g[0x19]);
        else if (gbMinimapUnitMode_g)
          bColor = sgbAlliances_g[g_LocalNationID][pSprite->bCreator] == 0
                       ? sgbPens_g[0x14] : sgbPens_g[0x11];
        else
          bColor = PortTeamMinimapShown(pSprite->bCreator);  // Hyperion: as Settings show it
        // Hyperion: a building under fog as the minimap's looks give it (port_minimap.h)
        bColor = PortMinimapDotColour(pSprite->bCreator, pThingy->wUnitType, bColor);
        sub_46CB80(static_cast<short>(pSprite->wPosX), pSprite->wPosY,
                   gUnitPlaceboxSize[pThingy->wUnitType].x,
                   gUnitPlaceboxSize[pThingy->wUnitType].y, bColor, 1);
        sgnMiniRectSeq_g--;
      }
    }
  }
  _bmp_current = pSave;
}

// The minimap image control's event handler. Two
// switches; case 14 is written first, as 1.17 lays it out, so its break falls into
// the shared tail dispatch.
int __fastcall minimap_image_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 14:
    switch (pEvent->dwId) {
    case 0:
      minimap_image_ctrl_activate(pCtrl);
      break;
    case 1:
      dlg_unhook_all(pCtrl);
      mou_cager(0);
      break;
    case 6:
      dlg_unhook_all(pCtrl);
      mou_cager(0);
      break;
    case 4:
      return 1;
    }
    break;

  case 4:
    if (!IsGamePaused()) {
      if (target_is_on())
        minimap_image_ctrl_target_mousedown(pEvent);
      else
        minimap_image_ctrl_mousedown(pCtrl, pEvent);
    }
    return 1;

  case 6:
    if (!IsGamePaused()) {
      if (target_is_on())
        minimap_image_ctrl_target_mousedown(pEvent);
      else
        minimap_image_ctrl_mousedown(pCtrl, pEvent);
    }
    return 1;

  case 7:
    if (!IsGamePaused()) {
      if (!placebox_is_on() && !target_is_on()) {
        rclick_action(pEvent);
      } else {
        mou_cager(reinterpret_cast<intptr_t>(&sgMinimapArea_g));
        dlg_hook(pCtrl, 8);
        dlg_hook(pCtrl, 3);
        minimap_image_ctrl_mousemoved(pCtrl);
      }
    }
    return 1;

  case 9:
    if (!IsGamePaused()) {
      if (!placebox_is_on() && !target_is_on()) {
        rclick_action(pEvent);
      } else {
        mou_cager(reinterpret_cast<intptr_t>(&sgMinimapArea_g));
        dlg_hook(pCtrl, 8);
        dlg_hook(pCtrl, 3);
        minimap_image_ctrl_mousemoved(pCtrl);
      }
    }
    return 1;

  case 3:
    minimap_image_ctrl_mousemoved(pCtrl);
    return 1;

  case 5:
    dlg_unhook_all(pCtrl);
    mou_cager(0);
    return 1;

  case 8:
    dlg_unhook_all(pCtrl);
    mou_cager(0);
    return 1;
  }

  return reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl,
                                                                      pEvent);
}

// A 16-bit dimension pair, and not a claim about memory: it exists only as a
// register-resident local, spelled as a struct so MSVC models the pair as two
// 16-bit register variables.
struct wdim16_t {
  WORD w;
  WORD h;
};

// The virtual key of a key event, the WORD at dlgevt_t +0x08. dialog.h leaves
// the slot unnamed on purpose: 0x08 is a per-event-id union and naming it after
// the keyboard arm would assert one arm as the field.
#define dlgevt_vkey(p) (*reinterpret_cast<dlg_word_t *>(&(p)->_unmapped08[0]))

// The scale ladder: picks the minimap scale from the
// map extent, installs the matching terrain and fog stages, sizes the two bitmaps and
// centres the control in its 0x80-pixel frame. ECX is never read: the control comes from
// sgpMinimapCtrl. The width is read four bytes wide, the height as a WORD field of a local.
void __fastcall sub_46D850(ctrl_t *pDlg)
{
  DWORD dwWidth = *reinterpret_cast<const DWORD *>(&gwMapWidth);
  struct { WORD w, h; } sz;
  sz.h = gwMapHeight;

  switch ((WORD)dwWidth > sz.h ? (WORD)dwWidth : sz.h) {
  case 64:
    sz.h <<= 1;
    sz.w = (WORD)(dwWidth * 2);
    sgwMiniHgt_g = sz.h;
    sgwMiniWdt_g = sz.w;
    sgwMiniNum = 1;
    sgwMiniDivAdj = 0;
    sgwMiniDivShift = 1;
    sgwMiniScale_g = 0x10;
    dword_671FC0 = sub_46CA20;
    dword_671FC4 = sub_46CEA0;
    break;
  case 96:
  case 128:
    sz.w = gwMapWidth;
    sgwMiniHgt_g = sz.h;
    sgwMiniWdt_g = sz.w;
    sgwMiniNum = 1;
    sgwMiniDivAdj = 0;
    sgwMiniDivShift = 0;
    sgwMiniScale_g = 0x20;
    dword_671FC0 = sub_46C920;
    dword_671FC4 = sub_46CD10;
    break;
  case 192:
  case 256:
    sz.w = (WORD)dwWidth >> 1;
    sz.h >>= 1;
    sgwMiniWdt_g = sz.w;
    sgwMiniHgt_g = sz.h;
    sgwMiniNum = 2;
    sgwMiniDivAdj = 0;
    sgwMiniDivShift = 0;
    sgwMiniScale_g = 0x40;
    dword_671FC0 = sub_46C990;
    dword_671FC4 = sub_46CDC0;
    break;
  default:
    // Hyperion: N:1 for a map past 256 tiles a side, N the power of two from 4 that
    // brings the longer side within 128 pixels
    if ((WORD)dwWidth > 256 || sz.h > 256) {
      const int nSide = (WORD)dwWidth > sz.h ? (WORD)dwWidth : sz.h;
      int n = 4;
      while (nSide > 128 * n)
        n *= 2;
      sz.w = (WORD)((WORD)dwWidth / n);
      sz.h = (WORD)(sz.h / n);
      sgwMiniWdt_g = sz.w;
      sgwMiniHgt_g = sz.h;
      sgwMiniNum = (WORD)n;
      sgwMiniDivAdj = 0;
      sgwMiniDivShift = 0;
      sgwMiniScale_g = (WORD)(0x20 * n);
      dword_671FC0 = MinimapTerrainN;
      dword_671FC4 = MinimapFogN;
      break;
    }
    sz.w = (WORD)sgwMiniWdt_g;
    sz.h = (WORD)sgwMiniHgt_g;
    break;
  }

  {
    WORD wMiniW = sz.w;
    WORD wMiniH = sz.h;
    int nOfs = (0x80 - wMiniW) / 2;
    ctrl_t *pCtrl = sgpMinimapCtrl;

    pCtrl->rc.left = (short)(pCtrl->rc.left + nOfs);
    pCtrl->rc.right = (short)(pCtrl->rc.right - nOfs);
    nOfs = (0x80 - wMiniH) / 2;
    pCtrl->rc.top = (short)(pCtrl->rc.top + nOfs);
    pCtrl->rc.bottom = (short)(pCtrl->rc.bottom - nOfs);

    sgMiniBMP.nWidth = (short)wMiniW;
    sgMiniCacheBMP_g.nWidth = (short)wMiniW;
    sgMiniBMP.nHeight = (short)wMiniH;
    sgMiniCacheBMP_g.nHeight = (short)wMiniH;
    bmp_create(&sgMiniCacheBMP_g, "minimap.cpp", 0x664);
    bmp_create(&sgMiniBMP, "minimap.cpp", 0x665);
  }

  {
    ctrl_t *pCtrl = sgpMinimapCtrl;

    // Hyperion: the dialog's left is the console's, 0 in 1.17.
    sgMinimapArea_g.left = pCtrl->rc.left + PortConsoleShift(sgpMinimapDlg);
    sgMinimapArea_g.top = pCtrl->rc.top + 0x13B;
    sgMinimapArea_g.right = pCtrl->rc.left + PortConsoleShift(sgpMinimapDlg) + (WORD)sgwMiniWdt_g - 1;
    sgMinimapArea_g.bottom = (WORD)sgwMiniHgt_g + 0x13A + pCtrl->rc.top;
  }
}

// The minimap image control's draw: blit the composed
// minimap and then outline the current viewport on it. The divisions are signed
// and each quotient is narrowed to sixteen bits before it is used.
void __fastcall minimap_image_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                   rect16_t *prcClip)
{
  int nLeft = pCtrl->rc.left;
  int nTop = pCtrl->rc.top;
  int nBottom;
  unsigned int nShift;
  int nAdj;
  int nNum;
  int nBoxW;
  int nBoxH;
  int nX;
  int nY;
  int nH;

  bmp_copy((unsigned short)nLeft, (unsigned short)nTop, &sgMiniBMP);
  if (sgbIsMinimapPreview_g != 0)
    return;

  _bmp_pen = sgbPens_g[0];
  nBottom = nTop + 1 + (pCtrl->rc.bottom - pCtrl->rc.top);
  nAdj = sgwMiniDivAdj;
  nShift = sgwMiniDivShift;
  nNum = sgwMiniNum;
  nBoxH = (WORD)(((MiniViewTilesY() << nShift) + nAdj) / nNum);
  nBoxW = (WORD)(((MiniViewTilesX() << nShift) + nAdj) / nNum);
  nY = (short)((sgwLastScrollY << nShift) / nNum);
  nX = (short)((sgwLastScrollX << nShift) / nNum);
  nY += nTop;
  nX += nLeft;

  // Hyperion: zoomed out, the box is what the camera shows, kept inside
  // the minimap, which a map smaller than the camera's sight would leave
  int nSeenL, nSeenT, nSeenR, nSeenB;
  PortViewSeenRect(&nSeenL, &nSeenT, &nSeenR, &nSeenB);
  if (nSeenR - nSeenL != g_portView.nWidth || nSeenB - nSeenT != g_portView.nViewRows) {
    const int nScale = 32 * nNum;
    int nX0 = (nSeenL > 0 ? nSeenL : 0) * (1 << nShift) / nScale;
    int nY0 = (nSeenT > 0 ? nSeenT : 0) * (1 << nShift) / nScale;
    int nX1 = (nSeenR < gwMapWidth * 32 ? nSeenR : gwMapWidth * 32) * (1 << nShift) / nScale;
    int nY1 = (nSeenB > 0 ? nSeenB : 0) * (1 << nShift) / nScale;
    const int nRight = pCtrl->rc.right - pCtrl->rc.left + 1;
    if (nX1 > nRight)
      nX1 = nRight;
    nX = nLeft + nX0;
    nY = nTop + nY0;
    nBoxW = nX1 - nX0 > 2 ? nX1 - nX0 : 2;
    nBoxH = nY1 - nY0 > 2 ? nY1 - nY0 : 2;
  }

  if (nY + nBoxH > nBottom)
    nH = nBottom - nY;
  else
    nH = nBoxH;

  bmp_hline((unsigned short)nX, (unsigned short)nY, (unsigned short)nBoxW);
  bmp_vline((unsigned short)nX, (unsigned short)nY, (unsigned short)nH);
  bmp_vline((unsigned short)(nX + nBoxW - 1), (unsigned short)nY,
            (unsigned short)nH);
  bmp_hline((unsigned short)nX, (unsigned short)(nY + nH - 1),
            (unsigned short)nBoxW);
}

// Claim the first free ping slot, or drop the ping
// when all sixteen are taken. `ret 4h` places the third parameter on the stack
// under /Gr.
void __fastcall do_minimap_ping(DWORD x, DWORD y, BYTE player)
{
  int i;

  for (i = 0; i < 16; i++)
    if (!sgPings_g[i].active)
      break;
  if (i == 16)
    return;

  unsigned int sh = sgwMiniDivShift;
  unsigned int dv = sgwMiniNum;

  sgPings_g[i].active = 1;
  sgPings_g[i].x = (((x >> 5) << sh) / dv) - 0x40;
  sgPings_g[i].y = (((y >> 5) << sh) / dv) - 0x40;
  sgPings_g[i].player = player;
  sgPings_g[i].timer = 0;
}

// Hyperion: the body of the minimap's Tab (minimap_dlg_fcn's case 15), for Hyperion's
// terrain key as well: with Shift the units' colouring mode, else the terrain.
static void MinimapTab(ctrl_t *pDlg)
{
  ctrl_t *pBtn;

  if (byte_665E68) {
    BYTE bMode = (BYTE)(gbMinimapUnitMode_g + 1);

    gbMinimapUnitMode_g = bMode;
    if (bMode > 2) {
      gbMinimapUnitMode_g = 0;
      vid_paint_invalid();
    } else if (bMode != 1) {
      vid_paint_invalid();
    }
  } else {
    gbMinimapMtxMode_g = (gbMinimapMtxMode_g == 0);
  }

  DrawMinimap();
  pBtn = ctrl_find_ndx(pDlg, 2);
  if (pBtn->dwFlags & 0x80)
    minimap_ctxhelp_terrain(pBtn);
}

void PortMinimapTerrainKey(void)
{
  if (sgpMinimapDlg != NULL && !IsPausedSingleplayer() && sgbIsMinimapPreview_g == 0)
    MinimapTab(sgpMinimapDlg);
}

// The minimap dialog's event handler, installed by
// dlg_activate. TAB with shift cycles the unit-colouring mode and without it
// toggles the terrain mode; every other event either falls through to ctrl_dlg
// or is refused.
int __fastcall minimap_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
  case 0:
  case 1:
    return 0;

  case 15: {
    if (IsPausedSingleplayer())
      return 0;
    if (sgbIsMinimapPreview_g != 0)
      return 0;
    if (dlgevt_vkey(pEvent) != 9)
      return 0;
    // Hyperion: in a game of Hyperion's selection Tab is not the terrain's: Hyperion's
    // keys take it (port_keys.cpp; event_Win.cpp)
    if (SelectionWide())
      return 0;

    MinimapTab(pDlg);
    return 1;
  }

  case 14:
    switch (pEvent->dwId) {
    case 0:
      minimap_dlg_activate(pDlg);
      break;
    case 1:
      minimap_dlg_deactivate(pDlg);
      break;
    case 4:
      return 1;
    }
    break;

  case 3:
    if (IsPausedSingleplayer())
      return 0;
    minimap_mouse_moved(pDlg, pEvent);
    break;

  case 4:
  case 6:
  case 7:
  case 9: {
    ctrl_t *pHit;

    if (IsPausedSingleplayer())
      return 0;
    pHit = ctrl_find(pDlg, pEvent);
    if (pHit == 0)
      return 0;
    if (pHit == pDlg)
      return 1;
    pHit->pfnEvt(pHit, pEvent);
    return 1;
  }
  }

  return ctrl_dlg(pDlg, pEvent);
}

// Raise the context help for whichever minimap button
// the cursor is over. Cases 2 and 3 share a tail in the oracle; that is MSVC
// cross-jumping two expansions of ctxhelp_at, not a third source path.
void __fastcall sub_46E1E0(ctrl_t *pCtrl)
{
  switch (pCtrl->nIndex) {
  case 4: {
    // This arm reads the parent after the call, the other two before it, and
    // MSVC cannot sink a memory load past a call.
    char *pszText;

    if (gPlayback)
      pszText = status_get_str(0x604);
    else
      pszText = status_get_str(0x32C);

    ctrl_t *pParent = pCtrl->pParent;
    ctxhelp_show(pParent->rc.left + pCtrl->rc.right,
                 pParent->rc.top + pCtrl->rc.top,
                 reinterpret_cast<intptr_t>(pszText));
    break;
  }
  case 3:
    ctxhelp_at(pCtrl, 0x32D);
    break;
  case 2:
    if (gbMinimapMtxMode_g)
      ctxhelp_at(pCtrl, 0x32A);
    else
      ctxhelp_at(pCtrl, 0x32B);
    break;
  default:
    ctxhelp_hide();
    break;
  }
}

// A click on the minimap image: ALT pings, anything
// else starts the drag-scroll. The two coordinate locals are four bytes each
// while sub_46CFD0 writes them as WORDs -- see that function's note in
// src117/h/minimap.h.
void __fastcall minimap_image_ctrl_mousedown(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (byte_665E6A) {
    LONG nX = static_cast<dlg_word_t>(pEvent->nX);
    LONG nY = static_cast<dlg_word_t>(pEvent->nY);

    sub_46CFD0(&nX, &nY);
    // The outer cast picks the int overload; net_cmds.h declares the callee with
    // WORD parameters, wrongly.
    send_net_cmd_MinimapPing(static_cast<int>(static_cast<WORD>(nX)),
                             static_cast<int>(static_cast<WORD>(nY)));
  } else {
    mou_cager(reinterpret_cast<intptr_t>(&sgMinimapArea_g));
    dlg_hook(pCtrl, 5);
    dlg_hook(pCtrl, 3);
    PortViewMinimapPress();  // Hyperion: the camera goes there at once (port_view.cpp)
    minimap_image_ctrl_mousemoved(pCtrl);
  }
}

void __fastcall minimap_ctxhelp_terrain(ctrl_t *pCtrl)
{
  if (gbMinimapMtxMode_g)
    ctxhelp_at(pCtrl, 0x32A);
  else
    ctxhelp_at(pCtrl, 0x32B);
}

// sub_46C8E0 -- oracle 0x46C8E0 (19 B). Its tag is further up, above ctxhelp_at.
// Stores the low byte of each counter; SaveMinimapCounts passes two adjacent bytes.
void __fastcall sub_46C8E0(BYTE *pa, BYTE *pb) {
  *pa = (BYTE)sgnMiniVisHash_g;
  *pb = (BYTE)sgnMiniRectSeq_g;
}

// Clears the two counters, in the reverse of minimap_init's order, as 1.17 has
// each.
void __fastcall sub_46C900(void) {
  sgnMiniVisHash_g = 0;
  sgnMiniRectSeq_g = 0;
}

// The terrain-colour pass at 1:1: one output byte per minimap pixel, resolved
// map tile -> CV5 group -> megatile -> minitile byte -> palette map.
void __fastcall sub_46C920(void) {
  // Height times width: MSVC loads the right operand first and it becomes the
  // loop counter, as in 1.17.
  int n = (int)((unsigned int)(WORD)sgwMiniHgt_g * (unsigned int)(WORD)sgwMiniWdt_g);
  const WORD *src = gfpCellMap;
  BYTE *dst = sgMiniCacheBMP_g.pPixels;

  // `while (n--)` is the counter form that gives 1.17's register assignment.
  while (n--) {
    unsigned int grp = (unsigned int)*src++ & 0x7FFF;
    unsigned int mt = *(const DWORD *)(gfpMtxSet + (grp << 6));  // Hyperion: 32-bit cell references (PortMegatilesLoad)
    unsigned int mi = gfpCellBuf[(static_cast<size_t>(mt & ~1u) << 5) + 0x37];
    *dst++ = sgCycleRemaps[mi];
  }
}

// Mirrors the screen origin into the minimap's copy and invalidates the control
// when it moved.
void __fastcall minimap_check_scroll(void) {
  WORD x = word_5264B0;
  WORD y = word_5264B2;
  if (sgwLastScrollX != x || sgwLastScrollY != y) {
    sgwLastScrollX = x;
    sgwLastScrollY = y;
    ctrl_set_update(sgpMinimapCtrl);
  }
  // Hyperion: and when what the camera shows changes with the zoom
  static int s_anSeen[4];
  int anSeen[4];
  PortViewSeenRect(&anSeen[0], &anSeen[1], &anSeen[2], &anSeen[3]);
  if (memcmp(anSeen, s_anSeen, sizeof(anSeen)) != 0) {
    memcpy(s_anSeen, anSeen, sizeof(anSeen));
    ctrl_set_update(sgpMinimapCtrl);
  }
}

// draw_minimap_body -- the 83-byte redraw 1.17 repeats in DrawMinimap, the tail
// of sub_46E140, sub_46EA90, sub_46EBA0 and minimap_init: Blizzard's shared static.
// no address of its own; the callers expand it
static __forceinline void draw_minimap_body(void) {
  if (sgbIsMinimapPreview_g != 1 && gbMinimapMtxMode_g) {
    bmp_t *pSave = _bmp_current;
    _bmp_current = &sgMiniBMP;
    bmp_clear(0);
    _bmp_current = pSave;
  } else {
    sub_46E840();
    dword_671FC4();
  }
  sub_46D510();
  sub_46D3F0();
  ctrl_set_update(sgpMinimapCtrl);
}

void __fastcall DrawMinimap(void) { draw_minimap_body(); }

// The same redraw, after the terrain stage when bFull is set.
void __fastcall sub_46E140(BYTE bFull) {
  if (bFull)
    dword_671FC0();
  draw_minimap_body();
}

// Rebuilds the four 256-byte shade tables. minimap_init, sub_46EA90 and
// sub_46EBA0 inline the loop, so they stay below this definition.
void __fastcall sub_46E840(void) {
  for (int i = 0; i < 256; i++) {
    byte_672178[0][i] = sgbPens_g[15];
    byte_672178[1][i] = shiftDark_g[15][i];
    byte_672178[2][i] = (BYTE)i;
    byte_672178[3][i] = (BYTE)i;
  }
}

// 0x46E260 minimap_init (436 B).
// Loads the ping GRP and blink colours, maps the palette onto the minimap's
// own, clears the pings, then either loads the in-game minimap dialog or, for
// the lobby preview, draws straight away.
void __fastcall minimap_init(void)
{
  int i;

  sgwLastScrollX = word_5264B0;
  sgnMiniRectSeq_g = 0;
  sgnMiniVisHash_g = 0;
  sgwLastScrollY = word_5264B2;
  gMinimapBlink_g = ReadGroupFile("game\\blink.grp", 0, "minimap.cpp", 0x7DB);
  GameLoadImage(reinterpret_cast<intptr_t>("game\\tblink.pcx"), 0,
                reinterpret_cast<intptr_t>(sgBlinkBits_g), 0x98, 0, 0, 0);

  if (sgbIsMinimapPreview_g == 0) {
    for (i = 0; i < 256; i++)
      sgCycleRemaps[i] = is_color_cycled(i)
          ? CPalette::find_closest_palette_entry(
                sgGamePalette_g,
                reinterpret_cast<palentry_t *>(sgGamePalette_g)[i])
          : i;
  } else {
    for (i = 0; i < 256; i++)
      sgCycleRemaps[i] = CPalette::find_closest_palette_entry(
          sgMinimapPalette_g,
          reinterpret_cast<palentry_t *>(sgGamePalette_g)[i]);
  }

  for (i = 0; i < 16; i++)
    sgPings_g[i].active = 0;

  if (sgbIsMinimapPreview_g == 0) {
    sgpMinimapDlg = dlg_load("rez\\minimap.bin",
                             reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                             reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                             "minimap.cpp", 0x7F0);
    dlg_activate(sgpMinimapDlg, 0, minimap_dlg_fcn);
    return;
  }

  sub_46D850(sgpMinimapDlg);
  dword_671FC0();
  draw_minimap_body();
}
void __fastcall minimap_force_redraw(void) { sgbRedrawMinimap_g = 1; }

// Timer 1's callback: the redraw, after the terrain stage when one was asked for.
void __fastcall sub_46E8D0(void) {
  sub_46E140(sgbRedrawMinimap_g);
  sgbRedrawMinimap_g = 0;
}


void __fastcall minimap_btn_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (IsPausedSingleplayer()) {
    dword_671F9C = 0;
    ctxhelp_hide();
    return;
  }
  // pCtrl is on the left of both compares, 1.17's operand order.
  if (pCtrl != ctrl_find(pCtrl->pParent, pEvent)) {
    dword_671F9C = 0;
    ctxhelp_hide();
    return;
  }
  if (pCtrl != dword_671F9C) {
    dword_671F9C = pCtrl;
    sub_46E1E0(pCtrl);
  }
}

// 0x46D0E0 minimap_btn_ctrl_fcn (80 B). The button's event demultiplexer; every
// branch ends in the one tail call to btn_ctrl_pushbutton.
int __fastcall minimap_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  // Case bodies are laid out in source order, so 0x0E comes first and case 2
  // precedes case 0; the compare chain stays ascending.
  switch (pEvent->wTag) {
  case 0x0E:
    switch (pEvent->dwId) {
    case 2:
      minimap_btn_command(pCtrl);
      break;
    case 0:
      minimap_btn_activate(pCtrl);
      break;
    }
    break;
  case 0x03:
    minimap_btn_mouse_moved(pCtrl, pEvent);
    break;
  }
  return btn_ctrl_pushbutton(pCtrl, pEvent);
}

void __fastcall minimap_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  ctrl_t *pHit;
  if (IsPausedSingleplayer())
    pHit = 0;
  else
    pHit = ctrl_find(pCtrl, pEvent);
  if (pHit != dword_671F9C) {
    dword_671F9C = pHit;
    if (pHit && pCtrl != pHit)
      sub_46E1E0(pHit);
    else
      ctxhelp_hide();
  }
}

// Resets the minimap's size to 0x80 square, retires the timers and frees the
// two bitmaps.
void __fastcall minimap_dlg_deactivate(ctrl_t *pDlg) {
  sgwMiniWdt_g = 0x80;
  sgwMiniHgt_g = 0x80;
  if (sgbIsMinimapPreview_g == 0) {
    timer_remove_node(pDlg, 3);
    timer_remove_node(pDlg, 2);
    timer_remove_node(pDlg, 1);
    timer_remove_node(pDlg, 5);
    timer_remove_node(pDlg, 6);
  }
  bmp_destroy(&sgMiniCacheBMP_g);
  bmp_destroy(&sgMiniBMP);
}

// Installs the image control's draw handler and does one full rebuild. The
// if/else is draw_minimap_body's, written out: 1.17 has it here without the
// ping pass and the update.
void __fastcall minimap_image_ctrl_activate(ctrl_t *pCtrl) {
  pCtrl->pfnDraw = minimap_image_draw;
  if (sgbIsMinimapPreview_g == 0) {
    dword_671FC0();
    if (sgbIsMinimapPreview_g != 1 && gbMinimapMtxMode_g) {
      bmp_t *pSave = _bmp_current;
      _bmp_current = &sgMiniBMP;
      bmp_clear(0);
      _bmp_current = pSave;
    } else {
      sub_46E840();
      dword_671FC4();
    }
    sub_46D510();
  }
  ctrl_set_update(pCtrl);
}

// Brings the minimap dialog up: claims the dialog-level flag, installs the draw
// handler and the event table, caches the image control, then starts the
// refresh timers. Everything after the control lookup is skipped in map-preview
// mode.
void __fastcall minimap_dlg_activate(ctrl_t *pDlg) {
  pDlg->dwFlags |= 0x20000000;
  pDlg->pfnDraw = minimap_dlg_draw;
  if (sgbIsMinimapPreview_g == 1)
    pDlg->dwFlags |= 0x2000;
  // The flag is read again, as 1.17 does: the store above may alias it.
  if (sgbIsMinimapPreview_g == 0) {
    ctrl_set_evt_fcns(pDlg, sMinimapEvtFcnTbl_g, 0x10);
    status_get_console_art(pDlg);
  } else {
    ctrl_set_evt_fcns(pDlg, sMinimapPreviewEvtFcnTbl_g, 4);
  }
  sgpMinimapCtrl = ctrl_find_ndx(sgpMinimapDlg, 1);

  // Three separate tests, not one block, as 1.17 has them: the flag is read
  // again after each call.
  if (sgbIsMinimapPreview_g == 0)
    sub_46D850(pDlg);
  if (sgbIsMinimapPreview_g == 0) {
    timer_create(pDlg, 1, gbMultiPlayerMode ? 2000 : 1000, sub_46E8D0);
    timer_create(pDlg, 2, 200, minimap_check_scroll);
    timer_create(pDlg, 3, 200, sub_46EBA0);
    timer_create(pDlg, 6, 100, sub_46EA90);
    // The multiplayer flag is tested once per disjunct, as 1.17 branches.
    if ((gbMultiPlayerMode && get_num_active_players() > 1) ||
        (gbMultiPlayerMode && IsTVBGame()) ||
        gPlayback)
      timer_create(pDlg, 5, 500, reinterpret_cast<PFN_TIMER_FN *>(sub_46D240));
  }
  if (sgbIsMinimapPreview_g == 0)
    ctrl_show(sgpMinimapCtrl);
  ctrl_set_update(sgpMinimapCtrl);
}

// Timer 6, every 100 ms: ages every live minimap ping by one tick, retiring it
// at 0x13, and repaints the minimap if at least one ping was live.
void __fastcall sub_46EA90(void) {
  int bAnyLive = 0;
  for (int i = 0; i < 16; i++) {
    if (sgPings_g[i].active) {
      if (sgPings_g[i].timer == 0x13)
        sgPings_g[i].active = 0;
      else
        sgPings_g[i].timer++;
      bAnyLive = 1;
    }
  }
  if (!bAnyLive)
    return;
  if (sgbRedrawMinimap_g)
    dword_671FC0();
  draw_minimap_body();
  sgbRedrawMinimap_g = 0;
}

// The dialog's draw, installed by minimap_dlg_activate: the stock dialog draw
// through gCtrlDrawFcnTbl, then a border round the minimap image on the sides
// where the image is smaller than its 0x80-pixel frame. The three blocks are in
// 1.17's order: the vlines alone, the hlines alone, then all four inset by one.
void __fastcall minimap_dlg_draw(ctrl_t *pDlg, int nXOfs, int nYOfs, rect16_t *prcClip) {
  gCtrlDrawFcnTbl[pDlg->wType](pDlg, nXOfs, nYOfs, prcClip);
  ctrl_t *c = ctrl_find_ndx(pDlg, 1);
  if (!(c->dwFlags & 8))
    return;
  // No local caches the height: 1.17 reads it from memory at each test.
  if (sgbIsMinimapPreview_g != 1) {
    if ((WORD)sgwMiniWdt_g < 0x80) {
      if ((WORD)sgwMiniHgt_g >= 0x80) {
        _bmp_pen = sgbPens_g[2];
        bmp_vline((WORD)(c->rc.right + 1), (WORD)c->rc.top, (WORD)sgwMiniHgt_g);
        _bmp_pen = sgbPens_g[5];
        bmp_vline((WORD)(c->rc.left - 1), (WORD)c->rc.top, (WORD)sgwMiniHgt_g);
        return;
      }
    } else {
      if ((WORD)sgwMiniHgt_g >= 0x80)
        return;
      _bmp_pen = sgbPens_g[2];
      bmp_hline((WORD)c->rc.left, (WORD)(c->rc.top - 1), (WORD)sgwMiniWdt_g);
      _bmp_pen = sgbPens_g[5];
      bmp_hline((WORD)c->rc.left, (WORD)(c->rc.bottom + 1), (WORD)sgwMiniWdt_g);
      return;
    }
  }
  // All four sides. These lengths are not narrowed to 16 bits, unlike the two
  // blocks above, as in 1.17.
  _bmp_pen = sgbPens_g[2];
  bmp_vline((WORD)(c->rc.right + 1), (WORD)(c->rc.top - 1), sgwMiniHgt_g + 2);
  bmp_hline((WORD)(c->rc.left - 1), (WORD)(c->rc.top - 1), sgwMiniWdt_g + 2);
  _bmp_pen = sgbPens_g[5];
  bmp_vline((WORD)(c->rc.left - 1), (WORD)(c->rc.top - 1), sgwMiniHgt_g + 2);
  bmp_hline((WORD)(c->rc.left - 1), (WORD)(c->rc.bottom + 1), sgwMiniWdt_g + 2);
}

// Timer 3, every 200 ms: the blink phase. Flips 0x671A8A, then repaints the
// minimap only if no ping is live, the complement of sub_46EA90, so one of the
// two timers drives the repaint at a time.
void __fastcall sub_46EBA0(void) {
  byte_671A8A ^= 1;
  if (byte_671A8B == 0)
    return;
  for (int i = 0; i < 16; i++)
    if (sgPings_g[i].active)
      return;
  if (sgbRedrawMinimap_g)
    dword_671FC0();
  draw_minimap_body();
  sgbRedrawMinimap_g = 0;
}

// A targeting click on the minimap: converts the event point into map
// coordinates, drops the cursor hold, issues the click order and drops a black
// selection X at the target. Its one parameter is the event, not the control.
// The two locals are four bytes each; sub_46CFD0 writes them as WORDs.
void __fastcall minimap_image_ctrl_target_mousedown(dlgevt_t *pEvent) {
  // One struct, not two locals: 1.17 has x at the lower address and stores it
  // first, which two locals do not give.
  point32_t pt;
  pt.x = (dlg_word_t)pEvent->nX;
  pt.y = (dlg_word_t)pEvent->nY;
  sub_46CFD0(&pt.x, &pt.y);
  game_hold_cursor(0);
  // Read back once into temporaries, which stay in registers across the call.
  LONG nPtY = pt.y;
  LONG nPtX = pt.x;
  usel_click_target((WORD)nPtX, (WORD)nPtY, 0, 0xE4);
  AllocBlackX(nPtX, nPtY);
}

// The drag-scroll: while the mouse is down over the minimap, recentres the map
// view on the cursor. Half the view in minimap pixels is subtracted, which turns
// the cursor position into the view's top-left corner.
void __fastcall minimap_image_ctrl_mousemoved(ctrl_t *pCtrl) {
  if (IsAutoScroll()) {
    dlg_unhook_all(pCtrl);
    mou_cager(0);
    return;
  }

  // CL is loaded once and reused for both shifts, and ESI once for both
  // divides, so both are locals rather than repeated global reads.
  int nShift = sgwMiniDivShift;
  int nDiv = sgwMiniNum * 2;
  // The (WORD) casts truncate the differences to 16 bits, as 1.17 does.
  LONG nX = (WORD)(gCursorPos_g[0] - ((MiniViewTilesX() << nShift) / nDiv));
  LONG nY = (WORD)(gCursorPos_g[1] - ((MiniViewTilesY() << nShift) / nDiv));
  sub_46CFD0(&nX, &nY);

  // Read back 16-bit and shift LOGICALLY (`shr`), i.e. as unsigned.
  wpoint16_t cell;
  cell.x = (WORD)nX >> 5;
  cell.y = (WORD)nY >> 5;
  // cell is on the left of both compares, 1.17's order, and ctrl_set_update
  // runs whether or not the view moved.
  if (cell.x != word_5264B0 || cell.y != word_5264B2) {
    PortViewMinimapMoved();  // Hyperion: the camera follows the minimap (port_view.cpp)
    cell_set_pos(cell.x << 5, cell.y << 5);
  }
  ctrl_set_update(pCtrl);
}

// Hyperion: Debug Tools' minimap press (port_trace.cpp): the cursor at a point
// of the minimap, u and v from 0 to 1 across and down, and the minimap's own drag handler
// run, as a press there or a drag across it moves the view.
void PortMinimapPress(double u, double v)
{
  ctrl_t *pCtrl = sgpMinimapCtrl;
  if (pCtrl == NULL)
    return;
  u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
  v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
  const LONG nW = sgMinimapArea_g.right - sgMinimapArea_g.left;
  const LONG nH = sgMinimapArea_g.bottom - sgMinimapArea_g.top;
  gCursorPos_g[0] = sgMinimapArea_g.left + static_cast<LONG>(u * nW + 0.5);
  gCursorPos_g[1] = sgMinimapArea_g.top + static_cast<LONG>(v * nH + 0.5);
  minimap_image_ctrl_mousemoved(pCtrl);
}

// Tear down the minimap dialog and free the
// ping GRP. The pointer is read into a temporary before the dialog store, 1.17's
// order of the load and the store.
void __fastcall minimap_deactivate()
{
  if (sgpMinimapDlg != NULL)
    dlg_deactivate(sgpMinimapDlg);

  GROUP *pBlink = gMinimapBlink_g;
  sgpMinimapDlg = NULL;
  mem_free(pBlink, "minimap.cpp", 0x811);
  gMinimapBlink_g = NULL;
}

// Load the lobby's minimap preview dialog
// and activate it with the minimap's own handler.
void do_minimappreview_dlg()
{
  sgpMinimapDlg = dlg_load("rez\\minimappreview.bin",
                           reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                           reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                           "minimap.cpp", 0x7C5);
  dlg_activate(sgpMinimapDlg, 0, minimap_dlg_fcn);
}

// 0x521660 gulUnitTypeFlags, spelled as src117/gen/globals117.h spells it.
#ifdef OC_DLL_MODE
#define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
#else
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;
#endif

// CUnit +0x8A / +0x8B: a blink countdown and the colour it blinks in. Both sit
// inside CUnit.h's unnamed _unmapped_86 run, so they are reached by index.
#define unit_blink_count(p) ((p)->_unmapped_86[4])
#define unit_blink_color(p) ((p)->_unmapped_86[5])

// The local player's own unit dots: skip
// hidden types and the resource and doodad ranges, fold each unit's visibility
// into the hash, and draw it, blinking while its countdown runs.
void __fastcall sub_46E490(int nPlayer)
{
  CUnit *pUnit;

  byte_671A8B = 0;
  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    WORD wType = pUnit->wType;
    CSprite *pSprite;
    DWORD dwVis;
    BYTE bColor;

    if (gulUnitTypeFlags[wType] & 0x10)
      continue;
    if ((wType >= 0xCB && wType <= 0xD5) || wType == 0xCA || wType == 0x69)
      continue;
    pSprite = pUnit->pSprite;
    dwVis = pSprite->bVisibilityFlags;
    if (pUnit->dwFlags & 0x300)
      dwVis &= pUnit->dwUnionE4;
    sgnMiniVisHash_g ^= dwVis;
    if ((gdwLocalMask & dwVis) == 0 || sgbIsMinimapPreview_g == 1)
      continue;
    // Hyperion: as the minimap's looks give it (port_minimap.h); a flash still shows
    bColor = PortMinimapDotColour(nPlayer, wType, sgbPens_g[0x12]);
    // The countdown is tested, then decremented and its OLD value tested again
    // through an int; the fused `b && b--` loses the second test.
    if (unit_blink_count(pUnit) != 0) {
      int nOld = unit_blink_count(pUnit)--;
      if (nOld != 0) {
        byte_671A8B = 1;
        if (byte_671A8A != 0)
          bColor = unit_blink_color(pUnit);
      }
    }
    sub_46CB80(static_cast<short>(pSprite->wPosX), pSprite->wPosY,
               static_cast<BYTE>(gUnitPlaceboxSize[wType].x),
               static_cast<BYTE>(gUnitPlaceboxSize[wType].y), bColor,
               static_cast<BYTE>(gulUnitTypeFlags[wType] & 1));
  }
}

// The dots of a player the local player
// cannot command: coloured by alliance in unit mode, else by the player's
// minimap colour, with mineral fields and type 0xBC in their own colour.
void __fastcall sub_46E5B0(int nPlayer)
{
  BYTE bBase;
  CUnit *pUnit;

  if (gbMinimapUnitMode_g != 0)
    bBase = sgbAlliances_g[g_LocalNationID][nPlayer] == 0 ? sgbPens_g[0x14]
                                                          : sgbPens_g[0x11];
  else
    bBase = PortTeamMinimapShown(nPlayer);  // Hyperion: as Settings show it

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    WORD wType = pUnit->wType;
    CSprite *pSprite;
    DWORD dwVis;
    BYTE bColor;

    if (gulUnitTypeFlags[wType] & 0x10)
      continue;
    if ((wType >= 0xCB && wType <= 0xD5) || wType == 0xCA || wType == 0x69)
      continue;
    dwVis = pUnit->pSprite->bVisibilityFlags;
    if (pUnit->dwFlags & 0x300)
      dwVis &= pUnit->dwUnionE4;
    sgnMiniVisHash_g ^= dwVis;
    if (sgbIsMinimapPreview_g == 0) {
      if (gPlayback) {
        if ((gdwReplayVisionMask & static_cast<BYTE>(dwVis)) == 0 &&
            sgbShowAll_g == 0 && !ViewHxUnitSeen(pUnit))  // Hyperion: the viewer's slots past 7 (stage D)
          continue;
      } else if ((gdwLocalMask & dwVis) == 0) {
        continue;
      }
    }
    if (pUnit->IsMineralSrc() || pUnit->wType == 0xBC)
      bColor = PortLookUnitPen(pUnit, sgbPens_g[0x19]);  // Hyperion: a looked one in its look's
    else
      bColor = bBase;
    bColor = PortMinimapDotColour(nPlayer, pUnit->wType, bColor);  // Hyperion: port_minimap.h
    pSprite = pUnit->pSprite;
    sub_46CB80(static_cast<short>(pSprite->wPosX), pSprite->wPosY,
               static_cast<BYTE>(gUnitPlaceboxSize[pUnit->wType].x),
               static_cast<BYTE>(gUnitPlaceboxSize[pUnit->wType].y), bColor,
               static_cast<BYTE>(gulUnitTypeFlags[pUnit->wType] & 1));
  }
}

// The dots of another commanding player,
// coloured by alliance in unit mode, else by the player's minimap colour.
void __fastcall sub_46E700(int nPlayer)
{
  BYTE bColor;
  CUnit *pUnit;

  if (gbMinimapUnitMode_g != 0)
    bColor = sgbAlliances_g[g_LocalNationID][nPlayer] == 0 ? sgbPens_g[0x14]
                                                           : sgbPens_g[0x11];
  else
    bColor = PortTeamMinimapShown(nPlayer);  // Hyperion: as Settings show it

  for (pUnit = CUnit::GetPlayerUnitList(nPlayer); pUnit != NULL;
       pUnit = pUnit->pPlayerUnitNext) {
    WORD wType = pUnit->wType;
    CSprite *pSprite;
    DWORD dwVis;

    if (gulUnitTypeFlags[wType] & 0x10)
      continue;
    if ((wType >= 0xCB && wType <= 0xD5) || wType == 0xCA || wType == 0x69)
      continue;
    pSprite = pUnit->pSprite;
    dwVis = pSprite->bVisibilityFlags;
    if (pUnit->dwFlags & 0x300)
      dwVis &= pUnit->dwUnionE4;
    sgnMiniVisHash_g ^= dwVis;
    if (sgbIsMinimapPreview_g == 1)
      continue;
    if (gPlayback) {
      if ((gdwReplayVisionMask & static_cast<BYTE>(dwVis)) == 0 &&
          sgbShowAll_g == 0 && !ViewHxUnitSeen(pUnit))  // Hyperion: the viewer's slots past 7 (stage D)
        continue;
    } else if ((gdwLocalMask & dwVis) == 0) {
      continue;
    }
    sub_46CB80(static_cast<short>(pSprite->wPosX), pSprite->wPosY,
               static_cast<BYTE>(gUnitPlaceboxSize[wType].x),
               static_cast<BYTE>(gUnitPlaceboxSize[wType].y),
               PortMinimapDotColour(nPlayer, wType, bColor),  // Hyperion: port_minimap.h
               static_cast<BYTE>(gulUnitTypeFlags[wType] & 1));
  }
}

// Fill one unit's dot: centre the unit's
// placement box on its map position, scale it into minimap pixels, clamp the
// dot to 2x2 (4x4 for flagged types) and to the minimap, and fill it. Every
// edge is read from the rect's fields, never from a local copy, which keeps
// the rect in the frame as the oracle has it.
void __fastcall sub_46CB80(unsigned nX, unsigned nY, BYTE bWidth, BYTE bHeight,
                           BYTE bColor, BYTE bFlag)
{
  bmpsrc_t r;
  unsigned nHalfW;
  unsigned nHalfH;

  _bmp_pen = bColor;
  nHalfW = bWidth >> 1;
  if (nX < nHalfW)
    nX = nHalfW;
  nHalfH = bHeight >> 1;
  if (nY < nHalfH)
    nY = nHalfH;
  r.rc.left = ((nX - nHalfW) << sgwMiniDivShift) / sgwMiniNum >> 5;
  r.rc.top = ((nY - nHalfH) << sgwMiniDivShift) / sgwMiniNum >> 5;
  r.nWidth = ((int)bWidth << sgwMiniDivShift) / (int)sgwMiniNum / 32;
  r.nHeight = ((int)bHeight << sgwMiniDivShift) / (int)sgwMiniNum / 32;

  if (r.nWidth < 2)
    r.nWidth = 2;
  else if (bFlag) {
    if (r.nWidth > 4)
      r.nWidth = 4;
  } else if (r.nWidth > 2)
    r.nWidth = 2;
  if (r.nHeight < 2)
    r.nHeight = 2;
  else if (bFlag) {
    if (r.nHeight > 4)
      r.nHeight = 4;
  } else if (r.nHeight > 2)
    r.nHeight = 2;

  r.rc.right = r.rc.left + r.nWidth - 1;
  r.rc.bottom = r.rc.top + r.nHeight - 1;

  // Only a right or bottom clamp recomputes the extent; a left or top clamp
  // leaves it as it was.
  if (r.rc.left < 0) {
    r.rc.left = 0;
  } else if (r.rc.right >= (int)(WORD)sgwMiniWdt_g) {
    r.rc.right = sgwMiniWdt_g - 1;
    r.nWidth = r.rc.right - r.rc.left + 1;
  }
  if (r.rc.top < 0) {
    r.rc.top = 0;
  } else if (r.rc.bottom >= (int)(WORD)sgwMiniHgt_g) {
    r.rc.bottom = sgwMiniHgt_g - 1;
    r.nHeight = r.rc.bottom - r.rc.top + 1;
  }
  bmp_rect_fill(r.rc.left, r.rc.top, r.nWidth, r.nHeight);
  sgnMiniRectSeq_g++;
}
