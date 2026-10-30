// gluConn.h -- declarations for src117/lang/gluConn.cpp, the connection
// screen: the provider and gateway list boxes over Storm's network-provider
// enumeration, and the two records the screen keeps
//
// Member functions live in their class header; this file owns the free
// functions, spelled without an explicit __fastcall, which /Gr already makes
// the default.

#ifndef OC117_GLUCONN_H
#define OC117_GLUCONN_H

#include "platform.h"
#include "dialog.h"
#include "dlg_flc.h"
#include "TSList.h"

// SNetEnumProviders is declared in src117/h/Storm.h, not here.

// 0x454250 do_conn_dlg (131 B). Nullary; it puts the screen's dialog up and
// tail-jumps glues_clear_all, which is what makes it void.
void do_conn_dlg();

// 0x4542E0 gluConn_gateway_list_item_draw (331 B). A list box's per-item draw
// callback: the box in ECX and the item index in EDX, read as 8 bits, plus
// three stack dwords, and `ret 0Ch`.
void gluConn_gateway_list_item_draw(void *pArg0, BYTE bArg1, void *pArg2, int nArg3, int nArg4);

// 0x454430 gluConn_provider_list_select (394 B). One register argument, and
// no caller reads the EAX it inherits from sub_4583C0.
void gluConn_provider_list_select(intptr_t nArg0);

// 0x4545C0 gluConn_dlg_activate (222 B). One register argument. It has no
// caller, and returns the BOOL of its tail jumps to AllocGametypes and
// CreateGametypes.
BOOL gluConn_dlg_activate(intptr_t nArg0);

// 0x4546A0 gluConn_dlg_deactivate (155 B). One register argument, and no
// caller reads the EAX it inherits from the provider list's Clear.
void gluConn_dlg_deactivate(intptr_t nArg0);

// 0x454740 gluConn_interact_fcn (234 B). The modal dialog's own event
// procedure: do_conn_dlg hands its address to dlg_run_modal, so it is a
// PFN_CTRL_EVT, and do_conn_dlg compares the int it returns against 9. Four
// paths tail-jump ctrl_dlg, which is a PFN_CTRL_EVT itself.
int gluConn_interact_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x454830 gluConn_dlg_init (142 B). One register argument, and no caller
// reads the EAX it inherits from glue_init_screen_dlg.
void gluConn_dlg_init(intptr_t nArg0);

// 0x454A00 gluConn_provider_list_ctrl_fcn (115 B). The provider list's event
// handler: two register arguments, and every path to the ret sets EAX.
BOOL gluConn_provider_list_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x454A80 gluConn_gateway_list_ctrl_fcn (122 B). The gateway list's handler,
// the same shape.
BOOL gluConn_gateway_list_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x454B20 gluConn_init_provider (56 B). Nullary; its one caller reads the
// full EAX.
int gluConn_init_provider();

// 0x454B60 gluConn_provider_list_find (115 B). One register argument; its one
// caller reads the full EAX.
intptr_t gluConn_provider_list_find(intptr_t nArg0);

// 0x454BE0 gluConn_provider_list_init (85 B). One register argument, and it
// tail-jumps gluConn_provider_list_select.
void gluConn_provider_list_init(intptr_t nArg0);

// 0x454C40 gluConn_gateway_list_init (91 B). One register argument, and it
// tail-jumps lbox_set_selection.
void gluConn_gateway_list_init(intptr_t nArg0);

// TPROVIDER -- the network-provider record, 0x118 bytes, read out of
// TPROVIDER::Create (0x4548C0). The derivation is complete rather than
// partial: the TSLink pair, three dwords, two 0x80-byte buffers at the two
// SStrCopy caps and the display-name pointer tile the record with nothing
// left over, and Create's `ret 10h` is four stack arguments on top of the
// ECX `this`.
struct TPROVIDER {
  /*0x000*/ TSLink<TPROVIDER> link;
  /*0x008*/ DWORD dwFlags;
  /*0x00C*/ DWORD dwId;
  /*0x010*/ DWORD dwMaxPlayers;
  /*0x014*/ char szName[0x80];
  /*0x094*/ char szDescription[0x80];
  /*0x114*/ const char *pszDisplayName;

  // Explicitly __stdcall. /Gr makes __fastcall the default, which would put
  // the first two arguments in ECX and EDX, and this is a callback Storm
  // calls: gluConn_dlg_activate hands its address to SNetEnumProviders as the
  // per-provider callback, so Storm's convention is the one that applies.
  static BOOL __stdcall Create(DWORD dwId, const char *pszName,
                               const char *pszDesc, void *pCaps);
};

// The provider list, with the one member the game's own code reaches.
#pragma pack(push, 1)
template <> struct TSList<TPROVIDER,TSGetLink<TPROVIDER> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TPROVIDER> link;
  // 0x4541F0 (84 B). Unlinks and frees every provider.
  void Clear(void);
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(TPROVIDER)                    == 0x118, "push 118h / SMemAlloc in TPROVIDER::Create");
static_assert(offsetof(TPROVIDER, link)            == 0x000, "TSList intrusive link: Create zeroes [edi] and [edi+4h]");
OC_X86_LAYOUT(offsetof(TPROVIDER, dwFlags)         == 0x008, "[edi+8h] = [ecx+4h] & 2");
OC_X86_LAYOUT(offsetof(TPROVIDER, dwId)            == 0x00C, "[edi+0Ch] = [ebp+8h]");
OC_X86_LAYOUT(offsetof(TPROVIDER, dwMaxPlayers)    == 0x010, "[edi+10h], clamped to <= 8 by cmovnbe");
OC_X86_LAYOUT(offsetof(TPROVIDER, szName)          == 0x014, "lea eax,[edi+14h] / push 80h / SStrCopy");
OC_X86_LAYOUT(offsetof(TPROVIDER, szDescription)   == 0x094, "lea esi,[edi+94h] / push 80h / SStrCopy");
OC_X86_LAYOUT(offsetof(TPROVIDER, pszDisplayName)  == 0x114, "[edi+114h] = glues_get_str(...)");

// BNGatewayAccess -- BNetGW.cpp's record, 0x20 bytes, reached here through the
// `gateways` global @0x53DB60. The extent is proven twice over: Load() clears
// +0 as a byte and then writes dwords through +0x1C, and the next named global
// is 0x53DB80. Only the members this TU reaches are declared; the interior of
// the record is BNetGW.cpp's to name when it opens.
class BNGatewayAccess {
 public:
  void GetGatewayList(const char *pszValue);
  void Load(void);
  char *Name(int nIndex);
  char *Nth(int nIndex);
  void PickClosestZone(int nBias);
  void SetCurGateway(int nIndex);
  void Unload(void);
  void WriteDefaultGatewayList(void);

 public:
  // gluConn_gateway_list_init compares a 1-based index against nCount with two
  // signed branches, and reads the low byte of nCurGateway, where Load stores a
  // whole dword, so the narrowing is the source's and the field is an int.
  /*0x00*/ char szEmpty[4];
  /*0x04*/ int nDirty;
  /*0x08*/ int nCount;
  /*0x0C*/ int nCurGateway;
  /*0x10*/ char *pszText;
  /*0x14*/ int nTextSize;
  /*0x18*/ DWORD dwVersion;
  /*0x1C*/ int nOverride;
};
static_assert(offsetof(BNGatewayAccess, szEmpty) == 0x00,
              "Nth returns (char *)this and Load writes one byte there");
static_assert(offsetof(BNGatewayAccess, nDirty) == 0x04,
              "Unload: cmp dword ptr [esi+4h], 0h / jz over the whole body");
static_assert(offsetof(BNGatewayAccess, pszText) == 0x10,
              "GetGatewayList: mov dword ptr [esi+10h], eax from SMemAlloc");
OC_X86_LAYOUT(offsetof(BNGatewayAccess, nTextSize) == 0x14,
              "GetGatewayList: mov dword ptr [esi+14h], eax, SRegLoadData's out length");
OC_X86_LAYOUT(offsetof(BNGatewayAccess, dwVersion) == 0x18,
              "Load: cmp dword ptr [esi+18h], 3E8h, unsigned");
OC_X86_LAYOUT(offsetof(BNGatewayAccess, nOverride) == 0x1C,
              "Load: mov dword ptr [esi+1Ch], 1h on the override key");
static_assert(offsetof(BNGatewayAccess, nCount) == 0x08,
              "gluConn_gateway_list_init: cmp dword ptr [0x53DB68], esi");
static_assert(offsetof(BNGatewayAccess, nCurGateway) == 0x0C,
              "gluConn_gateway_list_init: mov dl, byte ptr [0x53DB6C]");
OC_X86_LAYOUT(sizeof(BNGatewayAccess) == 0x20, "Load() clears +0 BYTE then DWORDs to +0x1C; next sheet global 0x53DB80");

#endif  // OC117_GLUCONN_H
