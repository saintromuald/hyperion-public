// statwire.cpp -- the status console unit wireframe and its damage overlay.
// Bodies are in oracle VA order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/statwire.h"

#include "../h/CUnit.h"
#include "../h/dlg_ctrl.h"
#include "../h/gamedata.h"
#include "../h/glues.h"
#include "../h/grp.h"       // GROUP, FRAME -- statwire_draw_dlg's frame header
#include "../h/rect32.h"    // rect32_t, the SetRect out-parameter
#include "../h/statdata.h"
#include "../gen/globals117.h"
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../port/data/port_moddata.h"  // Hyperion: ModUnitArtFrame, ModConsoleGroup
#include "../port/render/port_console.h"  // Hyperion: PortConsoleModUi
#include "../h/handicap.h"  // Hyperion: a player's handicap

// Callees declared here rather than by pulling their headers in, each spelled
// as the TU that has it spells it: statdata.cpp for SetRect and grh_get_header,
// opt_dlgs.cpp for rle_draw_normal.
extern "C" __declspec(dllimport)
int __stdcall SetRect(rect32_t *prc, int xLeft, int yTop, int xRight,
                      int yBottom);

// grh_get_header's frame is an unsigned 16-bit WORD: the callee compares it in
// 16 bits, and this call site needs that width. statdata.cpp and opt_dlgs.cpp
// spell it the same way, and the three change together.
void __fastcall grh_get_header(GROUP *pGroup, WORD wFrame, FRAME *pFrame,
                               grhsize_t *pSize);

void __fastcall rle_draw_normal(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE *pRemap);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it. The
// names are the naming sheet's for this TU.

// The tint palette loaded out of game\twire.pcx. Its extent is exact, not
// bounded: statwire_init passes 0x18 as the load size, and the next named
// global sits at 0x6AB524, which is 0x6AB50C + 0x18.
#ifdef OC_DLL_MODE
#define sgbTWire_g (reinterpret_cast<BYTE *>(OC_VA(0x6AB50Cu)))
#else
extern BYTE sgbTWire_g[0x18] OC_UNALIGNED;
#endif

// The wireframe GRP, one frame per unit type.
#ifdef OC_DLL_MODE
#define sgpWireFrames (*reinterpret_cast<GROUP **>(OC_VA(0x6AB524u)))
#else
extern GROUP *sgpWireFrames;
#endif

// The control the dialog loader handed us, cached at claim time.
#ifdef OC_DLL_MODE
#define sgpStatWireCtrl (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AB528u)))
#else
extern ctrl_t *sgpStatWireCtrl;
#endif

// status.cpp owns the current selection; this TU only reads it.
#ifdef OC_DLL_MODE
#define gpStatUnit (*reinterpret_cast<CUnit **>(OC_VA(0x6AB4C4u)))
#else
extern CUnit *gpStatUnit;
#endif

// The two hit-point ramps, one per wireframe style. Four sgbTWire_g indices per
// damage level and ten levels, because sub_4A7F50 scales the level with *9 and
// addresses the row as [n*4]; 10 * 4 is 0x28, which is the gap to the next name.
#ifdef OC_DLL_MODE
#define g_wire_colors_1_g (reinterpret_cast<BYTE *>(OC_VA(0x50D7DCu)))
#else
extern BYTE g_wire_colors_1_g[40] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define g_wire_colors_2_g (reinterpret_cast<BYTE *>(OC_VA(0x50D804u)))
#else
extern BYTE g_wire_colors_2_g[40] OC_UNALIGNED;
#endif

// The shield ramp: two indices per level, eight levels, because sub_4A80C0
// scales with *7 and addresses [n*2] / [n*2+1]. 8 * 2 is the gap to the next
// name exactly.
#ifdef OC_DLL_MODE
#define g_statwire_shields_remap_g (reinterpret_cast<BYTE *>(OC_VA(0x50D82Cu)))
#else
extern BYTE g_statwire_shields_remap_g[16] OC_UNALIGNED;
#endif

// The second hit-point ramp: four indices per level, addressed [n*4] by
// sub_4A8030, whose *5 scale reaches level 5. That is 24 bytes of rows; the
// extent below is the 28 the next name bounds, so the last row is unreachable.
#ifdef OC_DLL_MODE
#define g_statwire_hp_remap_g (reinterpret_cast<BYTE *>(OC_VA(0x50D83Cu)))
#else
extern BYTE g_statwire_hp_remap_g[28] OC_UNALIGNED;
#endif

// The control's +0x24 user slot holds the unit being drawn. dialog.h leaves
// 0x24..0x29 unmapped because no two subsystems agree on it, so each TU that
// uses it names its own view the way gluBrfPort.cpp does.
#define ctrl_wire_unit(p) (*reinterpret_cast<CUnit **>(&(p)->_unmapped24[2]))

// CUnit +0xE1 carries this subsystem's damage bits: sub_4A7F50 tests bit 0 to
// pick the ramp and then reads a 3-bit window per pass. CUnit.h leaves the byte
// unmapped, so this TU names its own view of it rather than claiming the field.
#define unit_wire_bits(p) ((p)->_unmapped_E1[0])

// The TU's own owner tag: the .rdata string at 0x4F01C0 that both allocation
// sites push.
#define OC_STATWIRE_FILE "statwire.cpp"

// 0x4A7F10 sub_4A7F10 (52 B). Put the six tinted palette slots back to the
// identity, so the next unit's wireframe starts from an untinted ramp.
//
// The value is spelled `i - 0x30` rather than `0xD0 + i` on purpose: the oracle
// forms it with `lea eax, [ecx-30h]`, an 8-bit displacement, and the positive
// spelling costs a 32-bit one.
void sub_4A7F10() {
  for (int i = 0; i < 4; i++)
    _gbRemapColorTbl[0xD0 + i] = static_cast<BYTE>(i - 0x30);
  for (int i = 0; i < 2; i++)
    _gbRemapColorTbl[0xC0 + i] = static_cast<BYTE>(i - 0x40);
  for (int i = 0; i < 4; i++)
    _gbRemapColorTbl[0xD8 + i] = static_cast<BYTE>(i - 0x28);
}

// 0x4A7F50 sub_4A7F50 (212 B). Tint the four hit-point slots. Each pass picks a
// slot from a 3-bit window of the damage bits, skips past any slot already
// taken, and writes the ramp's next entry. The hit-point scale is recomputed
// after the zero-max guard.
// The reset is a loop, which MSVC folds into one dword store through
// `imul edx, edx, 1010101h`; four assignments or a memset widen differently.
void sub_4A7F50(CUnit *pUnit) {
  BYTE bBase = sgbTWire_g[2];
  const BYTE *pRamp;
  int nMax;
  int nLevel;
  int i;

  for (i = 0; i < 4; i++)
    _gbRemapColorTbl[0xD0 + i] = bBase;

  nMax = HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) >> 8;  // Hyperion: at the owner's handicap
  if (nMax == 0) {
    int nHp = (pUnit->lHitPoints + 0xFF) >> 8;
    nMax = nHp != 0 ? nHp : 1;
  }
  nLevel = ((pUnit->lHitPoints + 0xFF) >> 8) * 9 / nMax;

  if (unit_wire_bits(pUnit) & 1)
    pRamp = &g_wire_colors_1_g[nLevel * 4];
  else
    pRamp = &g_wire_colors_2_g[nLevel * 4];

  for (i = 0; i < 4; i++) {
    int n = ((unit_wire_bits(pUnit) >> i) & 7) * 3 / 7;
    while (_gbRemapColorTbl[0xD0 + n] != bBase) {
      if (++n == 4)
        n = 0;
    }
    _gbRemapColorTbl[0xD0 + n] = sgbTWire_g[*pRamp++];
  }
}

// 0x4A8030 sub_4A8030 (143 B). The second hit-point run, four slots straight
// off one ramp row. Same zero-max guard as sub_4A7F50 and the same reload of
// +0x08 after it.
void sub_4A8030(CUnit *pUnit) {
  int nMax = HandicapLife(gxUnitLife[pUnit->wType], pUnit->bPlayer) >> 8;  // Hyperion: at the owner's handicap
  int nLevel;

  if (nMax == 0) {
    int nHp = (pUnit->lHitPoints + 0xFF) >> 8;
    nMax = nHp != 0 ? nHp : 1;
  }
  nLevel = ((pUnit->lHitPoints + 0xFF) >> 8) * 5 / nMax;

  _gbRemapColorTbl[0xD8] = sgbTWire_g[g_statwire_hp_remap_g[nLevel * 4]];
  _gbRemapColorTbl[0xD9] = sgbTWire_g[g_statwire_hp_remap_g[nLevel * 4 + 1]];
  _gbRemapColorTbl[0xDA] = sgbTWire_g[g_statwire_hp_remap_g[nLevel * 4 + 2]];
  _gbRemapColorTbl[0xDB] = sgbTWire_g[g_statwire_hp_remap_g[nLevel * 4 + 3]];
}

// 0x4A80C0 sub_4A80C0 (139 B). The two shield slots. A unit without shields, or
// whose type has no entry in the max-shields table, gets the untinted colour in
// both.
// The shield read is cast to long: the oracle shifts it `sar` and CUnit.h
// declares +0x60 a DWORD. The untinted arm is a loop over a hoisted local, the
// only spelling that folds to the oracle's `shl cx, 8h` / `or cx, ax` store.
void sub_4A80C0(CUnit *pUnit) {
  if (gbUnitHasShields[pUnit->wType] &&
      guwUnitEnergy[pUnit->GetTypeEx()] != 0) {
    int nShields = static_cast<LONG>(pUnit->lShields) >> 8;
    int nLevel = nShields * 7 / HandicapShields(guwUnitEnergy[pUnit->GetTypeEx()], pUnit->bPlayer);  // Hyperion: at the owner's handicap

    _gbRemapColorTbl[0xC0] = sgbTWire_g[g_statwire_shields_remap_g[nLevel * 2]];
    _gbRemapColorTbl[0xC1] =
        sgbTWire_g[g_statwire_shields_remap_g[nLevel * 2 + 1]];
    return;
  }
  BYTE bBase = sgbTWire_g[2];
  for (int i = 0; i < 2; i++)
    _gbRemapColorTbl[0xC0 + i] = bBase;
}

// 0x4A8150 statwire_draw_dlg (231 B). The control's draw handler: tint the
// palette for the unit at +0x26, blit its wireframe frame centred in the
// control, then put the palette back.
// The reset at the tail is sub_4A7F10 spliced in by /Ob2, so that body has to
// stay in this file. The oracle pushes grh_get_header's two stack arguments
// before the GetTypeEx call that produces its EDX: scheduling, not a new order.
void statwire_draw_dlg(intptr_t nArg0, int nArg1, int nArg2, int nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  CUnit *pUnit = ctrl_wire_unit(pCtrl);
  rect32_t rc;
  FRAME frame;
  grhsize_t size;
  int x;
  int y;

  sub_4A7F50(pUnit);
  sub_4A80C0(pUnit);
  sub_4A8030(pUnit);

  const unsigned nWire = ModUnitArtFrame(MOD_ART_WIRE, pUnit->GetTypeEx());  // Hyperion: a mod's own
  grh_get_header(sgpWireFrames, nWire, &frame, &size);
  SetRect(&rc, 0, 0, frame.bWidth, frame.bHeight);

  y = (pCtrl->rc.bottom - frame.bHeight - pCtrl->rc.top + 1) / 2 +
      pCtrl->rc.top;
  x = (pCtrl->rc.right - frame.bWidth - pCtrl->rc.left + 1) / 2 +
      pCtrl->rc.left;
  // Hyperion: a mod's own wireframe the GPU draws at the HD scale is left out
  if (!PortConsoleModUi(MOD_ART_WIRE, nWire))
    rle_draw_normal(x, y, &frame, &rc, 0);

  sub_4A7F10();
}

// 0x4A8240 statwire_deactivate (32 B). Drop the wireframe GRP. Line 367 is the
// literal the oracle pushes.
void statwire_deactivate() {
  mem_free(sgpWireFrames, OC_STATWIRE_FILE, 0x16F);
  sgpWireFrames = NULL;
}

// 0x4A8260 statwire_init (53 B). Load the per-type wireframe GRP and the
// 24-entry tint palette. Line 355 is the literal the oracle pushes; the 0x18 is
// what fixes sgbTWire_g's extent.
void statwire_init() {
  sgpWireFrames = ReadGroupFile("unit\\wirefram\\wirefram.grp", 0,
                                OC_STATWIRE_FILE, 0x163);
  sgpWireFrames = ModConsoleGroup(MOD_ART_WIRE, sgpWireFrames);  // Hyperion: a mod's wireframes
  GameLoadImage(reinterpret_cast<intptr_t>("game\\twire.pcx"), 0,
                reinterpret_cast<intptr_t>(sgbTWire_g), 0x18, 0, 0, 0);
}

// 0x4A82A0 statwire_update (55 B). Pushes the current selection at the
// control, shows or hides it, and marks it dirty. The control is read once into
// a local for the store base and the show or hide argument (a second read is
// hoisted above the branch, six bytes 1.17 lacks), each arm keeps its own tail
// call, and the test re-reads the unit global, which the store may alias.
void statwire_update() {
  ctrl_t *pCtrl = sgpStatWireCtrl;

  ctrl_wire_unit(pCtrl) = gpStatUnit;
  if (gpStatUnit != NULL) {
    ctrl_show(pCtrl);
    ctrl_set_update(sgpStatWireCtrl);
    return;
  }
  ctrl_hide(pCtrl);
  ctrl_set_update(sgpStatWireCtrl);
}

// 0x4A82E0 statwire_evn_ctrl (51 B). Claim the control on the id-0 initialise
// event: clear its unit, install the draw handler, and cache it. Every other
// event tail jumps into the per-type handler the loader would otherwise have
// installed.
BOOL statwire_evn_ctrl(void *pArg0, void *pArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(pArg1);

  if (pEvent->wTag == 0x0E && pEvent->dwId == 0) {
    ctrl_wire_unit(pCtrl) = NULL;
    pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(statwire_draw_dlg);
    sgpStatWireCtrl = pCtrl;
    return TRUE;
  }
  return reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl,
                                                                      pEvent);
}
