// gluModem.cpp -- the modem and direct-dial connection screens: device enumeration, the
// phone book and the dial-status dialog. Bodies are in oracle VA order.
//
// TU membership cuts both ways and needs re-checking after a change here: a callee
// defined in this file can be spliced into a caller in this file by /Ob2, which a
// cross-TU call never is, and a body whose size is exactly caller plus callee needs its
// callee here. __declspec(noinline) on the declaration is the lever.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>
#include "../h/gluModem.h"

#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"
#include "../h/dlg_lbox.h"
#include "../h/glues.h"
#include "../h/gluPopup.h"
#include "../h/gluRdy.h"
#include "../h/net_cmds.h"
#include "../h/menu_dlg.h"
#include "../h/net_glue.h"
#include "../h/minimap.h"
#include "../h/gametype.h"
#include "../h/gds_local.h"
#include "../h/netdesc.h"
#include "../h/save_dlg.h"
#include "../h/snd.h"
#include "../h/Storm.h"
#include "../h/rtti.h"
#include "../h/TSList.h"
#include "../gen/globals117.h"

// The one Win32 import this TU reaches: `push 190h / call dword ptr [0x4EB2F8]`
// in modem_status_dlg_fcn. Declared here exactly as event_Win.cpp:73 and
// net_player.cpp:152 do -- an IAT indirect, not one of ours.
extern "C" __declspec(dllimport) void __stdcall Sleep(DWORD dwMilliseconds);

// The CRT's _beginthreadex (0x4D360F), spelled exactly as snd.cpp spells it.
extern "C" ULONG_PTR __cdecl _beginthreadex(void *pSecurity, unsigned nStack,
                                                ULONG(__stdcall *pfnStart)(void *),
                                                void *pvArg, unsigned nFlags,
                                                unsigned *pnThreadId);

// The TU's types, derived from the oracle's allocations and call sites.

#pragma pack(push, 1)

// The modem device record, 0x10C bytes: glumodem_create_device allocates that much and
// fills +0x08 from its first argument and two 0x80-byte buffers at +0x0C and +0x8C, which
// close the object exactly. The link is at +0, the body zeroing [esi] and [esi+4] right
// after the allocation in TSList.h's new-node shape.
struct _DEVICE {
  /*0x000*/ TSLink<_DEVICE> link;
  // +0x08 the device id handed to SNetInitializeDevice
  // (glumodem_init_device: `push dword ptr [eax+8h]` as its first argument).
  /*0x008*/ DWORD dwId;
  // +0x0C the display name. modemlist_dlg_activate feeds `lea edx, [esi+0Ch]`
  // straight to lbox_add_item as the item text.
  /*0x00C*/ char szName[0x80];
  /*0x08C*/ char szDesc[0x80];
};
OC_X86_LAYOUT(sizeof(_DEVICE) == 0x10C,
              "_DEVICE: glumodem_create_device allocates exactly 0x10C");
static_assert(offsetof(_DEVICE, link) == 0x000, "_DEVICE::link  TSList node at +0");
OC_X86_LAYOUT(offsetof(_DEVICE, dwId) == 0x008, "_DEVICE::dwId  glumodem_init_device: push [eax+8h]");
OC_X86_LAYOUT(offsetof(_DEVICE, szName) == 0x00C, "_DEVICE::szName modemlist_dlg_activate: lea edx, [esi+0Ch]");
OC_X86_LAYOUT(offsetof(_DEVICE, szDesc) == 0x08C, "_DEVICE::szDesc glumodem_create_device: lea eax, [esi+8Ch]");

// The phone-book record, 0x54 bytes: glumodem_dlg_add_phonebook_entry allocates that
// much, and glumodem_phone_entry_deactivate gives both string extents as the SStrCopy
// buffer sizes it passes, 0x0E at +0x2C and 0x15 at +0x3A. +0x50 is a DWORD, which puts
// the object's last byte at 0x53.
struct _PHONENUMBER {
  /*0x00*/ TSLink<_PHONENUMBER> link;
  // +0x08 the list caption, "name<0x12>number", written with a 0x24 limit.
  /*0x08*/ char szLabel[0x24];
  /*0x2C*/ char szName[0x0E];
  /*0x3A*/ char szNumber[0x15];
  // +0x4F is not derived: one byte between szNumber's 0x15-byte extent and the DWORD
  // at +0x50, either padding or one more byte of the string, which nothing here tells
  // apart.
  /*0x4F*/ BYTE _unmapped4F[1];
  /*0x50*/ DWORD dwDeleted;
};
OC_X86_LAYOUT(sizeof(_PHONENUMBER) == 0x54,
              "_PHONENUMBER: glumodem_dlg_add_phonebook_entry allocates 0x54");
static_assert(offsetof(_PHONENUMBER, link) == 0x00, "_PHONENUMBER::link  TSList node at +0");
OC_X86_LAYOUT(offsetof(_PHONENUMBER, szLabel) == 0x08, "_PHONENUMBER::szLabel lea edx, [esi+8h]");
OC_X86_LAYOUT(offsetof(_PHONENUMBER, szName) == 0x2C, "_PHONENUMBER::szName SStrCopy(esi+2Ch, .., 0Eh)");
OC_X86_LAYOUT(offsetof(_PHONENUMBER, szNumber) == 0x3A, "_PHONENUMBER::szNumber SStrCopy(esi+3Ah, .., 15h)");
OC_X86_LAYOUT(offsetof(_PHONENUMBER, dwDeleted) == 0x50, "_PHONENUMBER::dwDeleted mov [eax+50h], 1h");

#pragma pack(pop)

// Storm's per-game record, as the SNetEnumGamesEx callback reads it. Only the
// three fields below are read; the size is not derived.
struct snetgame_t {
  /*0x00*/ DWORD _unmapped00;
  /*0x04*/ DWORD dwGameId;
  /*0x08*/ const char *pszGameName;
  /*0x0C*/ const char *pszGameStat;
};
OC_X86_LAYOUT(sizeof(snetgame_t) == 0x10, "snetgame_t: extent not derived past +0x0C");
static_assert(offsetof(snetgame_t, dwGameId) == 0x04, "snetgame_t::dwGameId");
static_assert(offsetof(snetgame_t, pszGameName) == 0x08, "snetgame_t::pszGameName");
OC_X86_LAYOUT(offsetof(snetgame_t, pszGameStat) == 0x0C, "snetgame_t::pszGameStat");

// Storm's dial-progress callback. `ret 14h` with no register input is
// __stdcall over five stack words; modem_status_fn IS one and
// glumodem_init_device plants its address in the program block below.
// The completion callback modem_status_fn is handed in its last stack word and
// modem_status_dlg_fcn calls back through with `call dword ptr [0x66B0CC]` --
// no register and no stack set up at the site, so it takes nothing.
typedef void(__cdecl *PFN_MODEM_DONE)(void);

typedef BOOL(__stdcall *PFN_MODEM_STATUS)(const char *pszStatus, LONG nA,
                                          LONG nB, int nUnused,
                                          PFN_MODEM_DONE pfnDone);

// The dialog-level tick installed at ctrl_t +0x4A. Only needed so the
// `void *` store in modem_status_dlg_activate can name one of the two
// sub_45F240 overloads the placeholder header leaves in scope.
typedef void (*PFN_MODEM_TICK)(ctrl_t *pDlg);

#pragma pack(push, 1)

// The program block glumodem_init_device copies, patches and hands to
// SNetInitializeDevice. Its size and both fields come off the frame arithmetic: the local
// is at [ebp-5Ch] and a `rep movsd` of 23 dwords moves 0x5C bytes, the store of 0x5C into
// [ebp-5Ch] is field +0x00 and states the size again, and [ebp-34h] is field +0x28.
struct modemprog_t {
  /*0x00*/ DWORD dwSize;
  /*0x04*/ BYTE _unmapped04[0x24];
  /*0x28*/ PFN_MODEM_STATUS pfnStatus;
  /*0x2C*/ BYTE _unmapped2C[0x30];
};
OC_X86_LAYOUT(sizeof(modemprog_t) == 0x5C, "modemprog_t: rep movsd of 17h dwords");
static_assert(offsetof(modemprog_t, dwSize) == 0x00, "modemprog_t::dwSize mov [ebp-5Ch], 5Ch");
static_assert(offsetof(modemprog_t, pfnStatus) == 0x28, "modemprog_t::pfnStatus mov [ebp-34h], 45F7E0h");

#pragma pack(pop)

// The globals this TU reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. sgGamePalette_g (0x666568)
// and glGluesMode (0x66A274) come in from ../gen/globals117.h instead.

// 0x50820C gModemDeviceList -- a Storm TSList of _DEVICE. The base comes from the static
// constructor, which writes ~(&link.prev) into link.next: TSList.h puts link at +4 of the
// head, so link.prev is 0x508210 and the head is 0x50820C. The walks agree --
// modemlist_dlg_activate reloads the head as the literal 0x508210, which is &link.
#ifdef OC_DLL_MODE
#define gModemDeviceList (*reinterpret_cast<DEVICELIST *>(OC_VA(0x50820Cu)))
#else
extern DEVICELIST gModemDeviceList;
#endif

// 0x508234 sModemEvtFcnTbl_g -- the screen's control handler table, 0x34 bytes
// by glue_init_screen_dlg's size argument.
#ifdef OC_DLL_MODE
#define sModemEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x508234u)))
#else
extern PFN_CTRL_EVT sModemEvtFcnTbl_g[13];
#endif

// 0x508268 sModemAnims_g -- glues_transition_in's record table; four records.
#ifdef OC_DLL_MODE
#define sModemAnims_g (reinterpret_cast<char *>(OC_VA(0x508268u)))
#else
extern char sModemAnims_g[16] OC_UNALIGNED;
#endif

// 0x51090C -- the RTTI type descriptor for `struct _DEVICE`, SMemAlloc's owner
// tag in glumodem_create_device (`mov ecx, 51090Ch / call type_info::raw_name`).
#ifdef OC_DLL_MODE
#define gTypeInfo__DEVICE (*reinterpret_cast<type_info *>(OC_VA(0x51090Cu)))
#else
extern type_info gTypeInfo__DEVICE;
#endif

// 0x510924 -- the RTTI type descriptor for `struct _PHONENUMBER`, SMemAlloc's
// owner tag in sub_45EFF0 and glumodem_dlg_add_phonebook_entry.
#ifdef OC_DLL_MODE
#define gTypeInfo__PHONENUMBER (*reinterpret_cast<type_info *>(OC_VA(0x510924u)))
#else
extern type_info gTypeInfo__PHONENUMBER;
#endif

// 0x508218 gPhonebook -- the same, for _PHONENUMBER. static_ctor_gPhonebook is
// `mov eax, 50821Ch / not eax / mov dword ptr [0x508220], eax`. Declared for
// the record; no row in this slice reaches it.
#ifdef OC_DLL_MODE
#define gPhonebook (*reinterpret_cast<PHONEBOOK *>(OC_VA(0x508218u)))
#else
extern PHONEBOOK gPhonebook;
#endif

// 0x508224 szIllegalChars_g -- the characters a phone-book field refuses; the
// .data image holds "\\/|&+^?".
#ifdef OC_DLL_MODE
#define szIllegalChars_g (reinterpret_cast<const char *>(OC_VA(0x508224u)))
#else
extern const char szIllegalChars_g[8];
#endif

// 0x50822C -- the two control indices sub_45ECB0 greys together. A DWORD array read a
// word at a time: the walk steps by 4 and closes on 8, while the load is a word because
// ctrl_find_ndx's index parameter is a short. The .data image holds 5 and 8. The
// pointer-to-array cast below is byte-identical to a plain `int *` and is kept only as
// the more faithful type.
#ifdef OC_DLL_MODE
#define sgrgModemGrayIds (*reinterpret_cast<int (*)[2]>(OC_VA(0x50822Cu)))
#else
extern int sgrgModemGrayIds[2];
#endif

// 0x6CE6DC gnDlgReturnVal, a LONG: modem_status_dlg_fcn compares it as a whole dword.
// The inner guard keeps this spelling compatible with the generated one, which five TUs
// also declare locally.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x66B000 -- nonzero while the dial-status dialog is live. modem_status_fn
// refuses to record anything when it is clear (`cmp dword ptr [0x66B000], 0h`)
// and modem_status_dlg_fcn clears it on teardown
// (`mov dword ptr [0x66B000], 0h`). A whole dword at both sites.
#ifdef OC_DLL_MODE
#define sgbModemStatusLive (*reinterpret_cast<LONG_PTR *>(OC_VA(0x66B000u)))
#else
extern LONG_PTR sgbModemStatusLive;
#endif

// 0x66B004 -- the status line. modem_status_fn copies into it with
// `SStrCopy(66B004h, <arg0>, 1Eh)`, which states the buffer size, and
// sub_45F240 hands the same address to ctrl_set_text.
#ifdef OC_DLL_MODE
#define sgszModemStatus (reinterpret_cast<char *>(OC_VA(0x66B004u)))
#else
extern char sgszModemStatus[0x1E] OC_UNALIGNED;
#endif

// 0x66B024 -- sub_45F240's "the dialog is finished" flag: a whole-dword compare
// against 0 that decides between tearing the dialog down and refreshing it.
#ifdef OC_DLL_MODE
#define sgbModemDone (*reinterpret_cast<LONG *>(OC_VA(0x66B024u)))
#else
extern LONG sgbModemDone;
#endif

// 0x66B028 sgJoinedModemGame_g -- SNetJoinGame's result, stored whole.
#ifdef OC_DLL_MODE
#define sgJoinedModemGame_g (*reinterpret_cast<BOOL *>(OC_VA(0x66B028u)))
#else
extern BOOL sgJoinedModemGame_g;
#endif

// 0x66B02C sgbEnumGamesActive_g.
#ifdef OC_DLL_MODE
#define sgbEnumGamesActive_g (*reinterpret_cast<LONG *>(OC_VA(0x66B02Cu)))
#else
extern LONG sgbEnumGamesActive_g;
#endif

// 0x66B030 sgdwJoinError_g -- SErrGetLastError after a failed join.
#ifdef OC_DLL_MODE
#define sgdwJoinError_g (*reinterpret_cast<DWORD *>(OC_VA(0x66B030u)))
#else
extern DWORD sgdwJoinError_g;
#endif

// 0x66B038 gModemGame -- a GameHeader: a 0x8D memset clears it and sub_475770
// copies it whole into gGameHeader. +0x04 holds the game name (0x18) and +0x1C
// the gamedesc_t Txt2GameData fills.
#ifdef OC_DLL_MODE
#define gModemGame (*reinterpret_cast<GameHeader *>(OC_VA(0x66B038u)))
#else
extern GameHeader gModemGame;
#endif

// 0x66B034 -- written by modem_status_fn from its second stack word and zeroed
// by glumodem_init_device. A whole dword at both. This and the next two are
// reached only from this TU, so their proof arms are static.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgnModemStatusA (*reinterpret_cast<LONG *>(OC_VA(0x66B034u)))
#else
static LONG sgnModemStatusA;
#endif

// 0x66B0C8 -- the same, from the third stack word.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgnModemStatusB (*reinterpret_cast<LONG *>(OC_VA(0x66B0C8u)))
#else
static LONG sgnModemStatusB;
#endif

// 0x66B0CC -- the completion callback. modem_status_fn stores its last stack word here
// and modem_status_dlg_fcn both tests it and calls through it, which makes it a function
// pointer rather than a datum. It is typed as that pointer rather than `void *` so
// neither the store nor the call needs a cast.
#ifdef OC_DLL_MODE
#define sgpfnModemDone (*reinterpret_cast<PFN_MODEM_DONE *>(OC_VA(0x66B0CCu)))
#else
extern PFN_MODEM_DONE sgpfnModemDone;
#endif

// 0x66B0D0 -- the device the modem list closed on, as an index into the device list.
// modemlist_dlg_fcn stores lbox_get_selection's byte widened to a dword, and
// do_atzone_dlg and modemlist_get_device count that many links down the list. Signed:
// both of those store the loop counter back after it has run to -1. Static, since only
// those three reach it and the internal linkage is what lets the walk decrement it in
// EDX.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgnModemSel (*reinterpret_cast<LONG *>(OC_VA(0x66B0D0u)))
#else
static LONG sgnModemSel;
#endif

// 0x66CCA8 -- the Storm net-provider block glumodem_init_device hands to
// SNetInitializeDevice as its last argument, where NetInit pushes the same address in the
// same position. Address-taken only at both sites, so nothing here says what is inside
// it. It is gVersion (src117/h/init.h), whose layout changes at 64 bits.
#include "../h/init.h"
#define sgModemNetBlock (&gVersion)

// TSList's unlink in gluJoin.cpp's UnlinkGameNode shape (the link offset must be
// a phi). Forced inline so no oracle-less COMDAT is emitted for it.
static __forceinline void UnlinkPhoneNumber(TSLink<_PHONENUMBER> *pl) {
  _PHONENUMBER *pprev = pl->prev;
  if (pprev != NULL) {
    _PHONENUMBER *pnext = pl->next;
    TSLink<_PHONENUMBER> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<_PHONENUMBER> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<_PHONENUMBER> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// The same unlink for a device node.
static __forceinline void UnlinkDevice(TSLink<_DEVICE> *pl) {
  _DEVICE *pprev = pl->prev;
  if (pprev != NULL) {
    _DEVICE *pnext = pl->next;
    TSLink<_DEVICE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<_DEVICE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<_DEVICE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// Unlinks and frees every modem device. Normalising the head to null before
// the test keeps MSVC from rotating the loop.
void DEVICELIST::Clear(void) {
  for (;;) {
    _DEVICE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkDevice(&p->link);
    SMemFree(p, gTypeInfo__DEVICE.raw_name(), -2, 0);
  }
}

// Unlinks and frees every phone-book entry.
void PHONEBOOK::Clear(void) {
  for (;;) {
    _PHONENUMBER *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkPhoneNumber(&p->link);
    SMemFree(p, gTypeInfo__PHONENUMBER.raw_name(), -2, 0);
  }
}

// Some rows this TU reaches are still declared in src117/h/gluModem.h with placeholder
// parameter spellings (`int nArgN`), so a reach into one needs a width-identical cast.
// Each such site is tagged HEADER-DEFECT with the derived signature.

// The Storm list walk, which is not a null test. A TSList's last node holds
// ~(&list.link.prev) in its `next`, a large negative number rather than NULL
// (src117/h/TSList.h), and two walk shapes appear here that are not interchangeable:
// modemlist_dlg_activate steps with a null-guarded next and never clamps, which is what
// folds its first read to a bare absolute load, while do_atzone_dlg and
// modemlist_get_device clamp each step to NULL with a `cmovle` and load unguarded.
//
// The ternary's arms have to stay in that order. `(long)p <= 0 ? NULL : p` keeps the
// value in the destination and moves the zero in, which is the oracle's
// `xor ecx, ecx / cmovle eax, ecx`; the other way round MSVC zeroes the destination and
// moves the value in, and the head load loses the accumulator short form. It is written
// out at both sites rather than factored into a helper, which would be a COMDAT of its
// own.

// The bodies, in oracle VA order.

// 0x45E280, 120 B. Run the AT-zone list and return the device the user picked.
// The walk counts the global itself down; with a local counter stored back
// MSVC emits `lea ecx, [edx-1h]` where the oracle has `dec edx`.
intptr_t do_atzone_dlg() {
  ctrl_t *pDlg = dlg_load("rez\\gluATZoneList.bin",
                          reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                          reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                          "gluModem.cpp", 0x32C);
  set_popup_dlg(pDlg, 3);
  if (dlg_run_modal(get_popup_dlg(), 0, modemlist_dlg_fcn) == 3)
    return 0;

  _DEVICE *p = gModemDeviceList.link.next;
  if (reinterpret_cast<LONG_PTR>(p) <= 0)
    p = NULL;
  while (sgnModemSel--) {
    _DEVICE *q = p->link.next;
    p = reinterpret_cast<LONG_PTR>(q) <= 0 ? NULL : q;
  }
  return reinterpret_cast<intptr_t>(p);
}

// 0x45E300, 117 B. The modem screen's modal runner.
//
// The dispatch is `sub eax, 5h / jz` then `dec eax / jz` then `sub eax, 3h /
// jz` -- chained subtracts over 5, 6 and 9, which is a switch's lowering. Every
// arm tail-jumps into glues_clear_all, so the call is after the switch.
void do_modem_dlg() {
  ctrl_t *pDlg = dlg_load("rez\\gluModem.bin",
                          reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                          reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                          "gluModem.cpp", 0x401);
  // HEADER-DEFECT gluModem.h: glumodem_dlg_fcn is `void glumodem_dlg_fcn(int,
  // int)`; derived PFN_CTRL_EVT, i.e. `int glumodem_dlg_fcn(ctrl_t *pCtrl,
  // dlgevt_t *pEvent)` (`ret`, ECX + EDX, tail jump into ctrl_dlg).
  switch (dlg_run_modal(pDlg, 0,
                        reinterpret_cast<PFN_CTRL_EVT>(glumodem_dlg_fcn))) {
    case 5:
      glGluesMode = 3;
      break;
    case 6:
      glGluesMode = 2;
      break;
    case 9:
      glGluesMode = 0x0B;
      break;
    default:
      glGluesMode = 0;
      break;
  }
  glues_clear_all();
}

// 0x45E380, 182 B. Enumerate the modem devices, let the user pick one when
// there are several, and hand it to Storm. The list is cleared on every exit
// but the no-device one. The arguments are NetInit's provider id and its
// program, player and UI blocks.
// The init block is written twice; the backend merges the copies, which puts
// the pick block after both returns.
BOOL enum_devices(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3) {
  SNetEnumDevices(reinterpret_cast<void *>(glumodem_create_device));
  _DEVICE *p = gModemDeviceList.link.next;
  if (reinterpret_cast<LONG_PTR>(p) > 0) {
    _DEVICE *pLast = gModemDeviceList.link.prev->link.prev->link.next;
    if (p == (reinterpret_cast<LONG_PTR>(pLast) <= 0 ? NULL : pLast)) {
      if (!glumodem_init_device(p, nArg1, nArg2,
                                reinterpret_cast<const modemprog_t *>(nArg3))) {
        do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4A)));
        gModemDeviceList.Clear();
        return FALSE;
      }
      gModemDeviceList.Clear();
      return TRUE;
    }
    p = reinterpret_cast<_DEVICE *>(nArg0 == 'ATLK' ? do_atzone_dlg()
                                                    : modemlist_get_device());
    if (!p) {
      gModemDeviceList.Clear();
      return FALSE;
    }
    if (!glumodem_init_device(p, nArg1, nArg2,
                              reinterpret_cast<const modemprog_t *>(nArg3))) {
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4A)));
      gModemDeviceList.Clear();
      return FALSE;
    }
    gModemDeviceList.Clear();
    return TRUE;
  }
  if (nArg0 != 'MODM')
    return TRUE;
  do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4B)));
  return FALSE;
}

// 0x45E440, 188 B. Set up the modem screen: gray the join buttons, poll for a
// game once, and fall back to the phone book when no enumerator is running.
void glumodem_dlg_activate(ctrl_t *pDlg) {
  ctrl_t *pBox = ctrl_find_ndx(pDlg, 0x0B);
  pBox->dwFlags |= 0x20000;
  ctrl_t *pText = ctrl_find_ndx(pBox, 0x0D);
  ctrl_set_text(pText, glues_get_str(0x53));
  ctrl_gray(ctrl_find_ndx(pDlg, 5));
  ctrl_t *pBtn = ctrl_find_ndx(pDlg, 8);
  ctrl_gray(pBtn);
  sub_45F990(pDlg, 0xFFFF);
  if (sgbEnumGamesActive_g) {
    pBtn = ctrl_find_ndx(pBtn, 7);
    ctrl_gray(pBtn);
    ctrl_gray(ctrl_find_ndx(pBtn, 8));
  } else {
    sub_45EFF0(pDlg);
  }
  sgJoinedModemGame_g = 0;
}

// 0x45E500, 132 B. Load the phone-book record into the entry dialog's two edit
// controls.
//
// The record is the dialog's own +0x26 slot. The third ctrl_find_ndx searches from the
// second edit rather than from the dialog, where the second searches from the dialog.
// Each edit gets its flag bit, its event handler, a maximum length at +0x26 and a 1 at
// +0x24; MSVC materialises that shared 1 once and reaches the indices 2 and 3 off it.
void glumodem_phone_entry_activate(ctrl_t *pDlg) {
  _PHONENUMBER *pEntry = *reinterpret_cast<_PHONENUMBER **>(pDlg->_unmapped24 + 2);

  ctrl_t *pName = ctrl_find_ndx(pDlg, 1);
  edit_set_text(pName, pEntry->szName);
  pName->dwFlags |= 0x20000;
  pName->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_45E770);
  *reinterpret_cast<LONG *>(pName->_unmapped24 + 2) = 0x0E;
  *reinterpret_cast<short *>(pName->_unmapped24) = 1;

  ctrl_t *pNumber = ctrl_find_ndx(pDlg, 2);
  edit_set_text(pNumber, pEntry->szNumber);
  pNumber->dwFlags |= 0x20000;
  pNumber->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_45E770);
  *reinterpret_cast<LONG *>(pNumber->_unmapped24 + 2) = 0x15;
  *reinterpret_cast<short *>(pNumber->_unmapped24) = 1;

  ctrl_gray(ctrl_find_ndx(pNumber, 3));
}

// 0x45E590, 207 B. A phone-book edit field's key filter: drop an illegal
// character, clear a pending placeholder, enforce the length limit, then
// refresh the OK button (sub_45EC60 is inlined here).
void sub_45E590(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  for (const char *p = szIllegalChars_g; *p; p++) {
    if (*reinterpret_cast<WORD *>(pEvent->_unmapped08) == static_cast<BYTE>(*p))
      return;
  }
  if (*reinterpret_cast<short *>(pCtrl->_unmapped24) != 0) {
    edit_set_text(pCtrl, "");
    *reinterpret_cast<short *>(pCtrl->_unmapped24) = 0;
  }
  const char *pszText = static_cast<const char *>(pCtrl->pData);
  if (strlen(pszText) < static_cast<unsigned>(
                            *reinterpret_cast<LONG *>(pCtrl->_unmapped24 + 2) - 1) ||
      *reinterpret_cast<WORD *>(pEvent->_unmapped08) == 8) {
    if (*reinterpret_cast<WORD *>(pEvent->_unmapped08) != ' ' || *pszText != 0)
      edit_ctrl_edit(pCtrl, pEvent);
  }
  sub_45EC60(pCtrl);
}

// 0x45E660, 66 B. Write the two edit fields back into the phone-book record and
// close the popup.
//
// The record is read once up front and used twice, so it is a local here rather than read
// again. Both SStrCopy sizes are the destination buffers' extents, which is where
// _PHONENUMBER's two string fields come from.
void glumodem_phone_entry_deactivate(ctrl_t *pDlg) {
  _PHONENUMBER *pEntry = *reinterpret_cast<_PHONENUMBER **>(pDlg->_unmapped24 + 2);
  ctrl_t *pName = ctrl_find_ndx(pDlg, 1);
  SStrCopy(pEntry->szName, static_cast<const char *>(pName->pData), 0x0E);
  ctrl_t *pNumber = ctrl_find_ndx(pDlg, 2);
  SStrCopy(pEntry->szNumber, static_cast<const char *>(pNumber->pData), 0x15);
  deactivate_popup_dlg(pDlg);
}

// 0x45E6B0, 184 B. SNetEnumDevices callback: append one device record to
// gModemDeviceList. A failed allocation still stores through the null node.
BOOL __stdcall glumodem_create_device(DWORD dwId, const char *pszName,
                                      const char *pszDesc) {
  _DEVICE *node;
  TSLink<_DEVICE> *nl;
  void *pv = SMemAlloc(sizeof(_DEVICE), gTypeInfo__DEVICE.raw_name(), -2, 8);
  if (pv != NULL) {
    node = static_cast<_DEVICE *>(pv);
    node->link.prev = NULL;
    node->link.next = NULL;
    nl = &node->link;
  } else {
    node = NULL;
    nl = &gModemDeviceList.link;
  }
  // TSList's unlink, in gluJoin.cpp's UnlinkGameNode shape: the link offset must
  // be a phi or MSVC folds the three-term sum.
  _DEVICE *pprev = nl->prev;
  if (pprev != NULL) {
    _DEVICE *pnext = nl->next;
    TSLink<_DEVICE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<_DEVICE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0)
        nOff = (intptr_t)nl - (intptr_t)pprev->link.next;
      pnl = reinterpret_cast<TSLink<_DEVICE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    nl->prev->link.next = nl->next;
    nl->prev = NULL;
    nl->next = NULL;
  }
  _DEVICE *ptail = gModemDeviceList.link.prev;
  nl->prev = ptail;
  nl->next = ptail->link.next;
  ptail->link.next = node;
  gModemDeviceList.link.prev = reinterpret_cast<_DEVICE *>(nl);
  node->dwId = dwId;
  SStrCopy(node->szName, pszName, 0x80);
  SStrCopy(node->szDesc, pszDesc, 0x80);
  return TRUE;
}

// 0x45E770, 58 B. The phone-entry dialog's edit control handler, and the same
// three-arm shape as the spine's glupedit_edit_fcn: `test eax, eax / js` (the
// tail-jmp arm), `cmp eax, 1h / jle` (0 and 1 -> the middle arm), then
// `cmp eax, 0Fh / jnz` (anything but 15 -> the tail-jmp arm again). The `js` is
// dead -- the load is a `movzx` so the value cannot be negative -- and it is
// reproduced rather than repaired.
BOOL sub_45E770(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  int nTag = pEvent->wTag;
  if (nTag >= 0 && (nTag <= 1 || nTag == 0x0F)) {
    if (nTag > 1) {
      // sub_45E590 is declared `void sub_45E590(ctrl_t *, dlgevt_t *)` in gluModem.h,
      // so this call needs no cast.
      sub_45E590(pCtrl, pEvent);
      return TRUE;
    }
    edit_ctrl_edit(pCtrl, pEvent);
    sub_45EC60(pCtrl);
    return TRUE;
  }
  return edit_ctrl_edit(pCtrl, pEvent);
}

// 0x45E7B0, 919 B. Run the phone-entry dialog for a new record and add it at
// the head of gPhonebook; a record whose name already exists replaces the old
// one after confirmation. Then rebuild the list and select the new entry.
void glumodem_dlg_add_phonebook_entry(ctrl_t *pDlg) {
  char szPrompt[0x800];
  lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 0x0B));
  if (lbox_get_count(pBox) >= 10) {
    do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x49)));
    return;
  }
  _PHONENUMBER *pNew;
  void *pv = SMemAlloc(sizeof(_PHONENUMBER), gTypeInfo__PHONENUMBER.raw_name(), -2, 8);
  if (pv != NULL) {
    pNew = static_cast<_PHONENUMBER *>(pv);
    pNew->link.prev = NULL;
    pNew->link.next = NULL;
  } else {
    pNew = NULL;
  }
  ctrl_t *pEntryDlg = dlg_load("rez\\gluModemEntry.bin",
                               reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                               reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                               "gluModem.cpp", 0xD3);
  *reinterpret_cast<_PHONENUMBER **>(pEntryDlg->_unmapped24 + 2) = pNew;
  set_popup_dlg(pEntryDlg, 4);
  if (dlg_run_modal(pEntryDlg, 0, glumodem_phone_entry_dlg_fcn) != 3) {
    if (pNew) {
      UnlinkPhoneNumber(&pNew->link);
      SMemFree(pNew, "delete", -1, 0);
    }
    return;
  }
  {
    _PHONENUMBER *p;
    for (p = gPhonebook.link.next; reinterpret_cast<LONG_PTR>(p) > 0;
         p = (p ? reinterpret_cast<TSLink<_PHONENUMBER> *>(p) : &gPhonebook.link)->next) {
      if (_stricmp(pNew->szName, p->szName) == 0)
        goto found;
    }
    goto add;
  found:
    psnprintf(szPrompt, 0x800, glues_get_str(0x48), p->szName);
    if (!p->dwDeleted && !do_pok_cancel_dlg(reinterpret_cast<intptr_t>(szPrompt))) {
      if (pNew) {
        UnlinkPhoneNumber(&pNew->link);
        SMemFree(pNew, "delete", -1, 0);
      }
      return;
    }
    UnlinkPhoneNumber(&p->link);
    p->dwDeleted = 0;
    SStrCopy(p->szName, pNew->szName, 0x0E);
    SStrCopy(p->szNumber, pNew->szNumber, 0x15);
    if (pNew) {
      UnlinkPhoneNumber(&pNew->link);
      SMemFree(pNew, "delete", -1, 0);
    }
    pNew = p;
  }
add:
  psnprintf(pNew->szLabel, 0x24, "%s%c%s", pNew->szName, 0x12, pNew->szNumber);
  {
    TSLink<_PHONENUMBER> *nl = pNew ? &pNew->link : &gPhonebook.link;
    UnlinkPhoneNumber(nl);
    nl->prev = reinterpret_cast<_PHONENUMBER *>(&gPhonebook.link);
    nl->next = gPhonebook.link.next;
    _PHONENUMBER *pnext = gPhonebook.link.next;
    intptr_t nOff = static_cast<int>(gPhonebook._unmapped_00);
    TSLink<_PHONENUMBER> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<_PHONENUMBER> *>(~(intptr_t)pnext);
    } else {
      if (nOff < 0)
        nOff = (intptr_t)&gPhonebook.link - (intptr_t)gPhonebook.link.prev->link.next;
      pnl = reinterpret_cast<TSLink<_PHONENUMBER> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = reinterpret_cast<_PHONENUMBER *>(nl);
    gPhonebook.link.next = pNew;
  }
  lbox_set_recalc(pBox, 0);
  lbox_remove_all(pBox);
  {
    int nSelect = -1;
    for (_PHONENUMBER *p = gPhonebook.link.next; reinterpret_cast<LONG_PTR>(p) > 0;
         p = p->link.next) {
      if (p->dwDeleted)
        continue;
      int nItem = static_cast<BYTE>(
          lbox_add_item(reinterpret_cast<ctrl_t *>(pBox), p->szLabel, 0));
      if (nItem == 0xFF) {
        UnlinkPhoneNumber(&p->link);
        SMemFree(p, gTypeInfo__PHONENUMBER.raw_name(), -2, 0);
      } else {
        lbox_set_data(pBox, static_cast<unsigned char>(nItem), p);
        if (p == pNew)
          nSelect = nItem;
      }
    }
    lbox_set_recalc(pBox, 1);
    if (nSelect >= 0)
      lbox_set_selection(pBox, static_cast<unsigned char>(nSelect));
  }
  ctrl_set_update(reinterpret_cast<ctrl_t *>(pBox));
  sub_45ECB0(pDlg, pBox);
}

// 0x45EB50, 98 B. The phone-entry dialog's event handler.
//
// A two-level switch: an 11-byte case map at 0x45EBC8 feeding a five-entry address table
// at 0x45EBB4, both in the COMDAT tail past the body. The map holds cases 0, 1, 7 and 10
// with everything else defaulting, and carries no relocations, so a wrong case order
// shows up there and nowhere else.
int glumodem_phone_entry_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        glumodem_phone_entry_init(pCtrl);
        break;
      case 0:
        glumodem_phone_entry_activate(pCtrl);
        break;
      case 7:
        title_position(pCtrl);
        break;
      case 1:
        glumodem_phone_entry_deactivate(pCtrl);
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x45EBE0, 118 B. Build the phone-entry screen dialog.
//
// `and eax, -20000001h` then `or eax, 40000h` is one read-modify-write of
// dwFlags: clear 0x20000000, set 0x40000. glue_init_screen_dlg is called with
// a NULL table and a zero size (`xor edx, edx` / `push 0h` / `push 0h`).
void glumodem_phone_entry_init(ctrl_t *pDlg) {
  char szPath[0x104];
  glue_init_screen_dlg(pDlg, NULL, 0, 0);
  pDlg->dwFlags = (pDlg->dwFlags & ~0x20000000) | 0x40000;
  glues_get_path(szPath, 0x104);
  SStrPack(szPath, "pEntry.pcx", 0x104);
  alloc_load_bmp(szPath, pDlg->_unmapped0C,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluModem.cpp", 0x8C);
  ctrl_set_update(pDlg);
}

// 0x45EC60, 72 B. Gray the phone-entry dialog's OK button by how many of its
// two text fields are still empty.
//
// The three ctrl_find_ndx calls are chained: each searches from the previous result
// rather than from the dialog.
//
// The counter is a byte, worth two bytes of code -- an `int` lands at 70 against the
// oracle's 72 -- and the last find has to be its own statement: nested as ctrl_set_gray's
// argument it makes MSVC widen the counter before the call.
void sub_45EC60(ctrl_t *pDlg) {
  BYTE nEmpty = 2;
  ctrl_t *pName = ctrl_find_ndx(pDlg, 1);
  if (*static_cast<const char *>(pName->pData) != 0)
    nEmpty = 1;
  ctrl_t *pNumber = ctrl_find_ndx(pName, 2);
  if (*static_cast<const char *>(pNumber->pData) != 0)
    nEmpty--;
  ctrl_t *pBtn = ctrl_find_ndx(pNumber, 3);
  ctrl_set_gray(pBtn, nEmpty);
}

// 0x45ECB0, 67 B. Gray or ungray the phone-book buttons by whether the list is
// empty.
//
// The flag comes off lbox_get_count's byte result. The index has to be unsigned: with
// `int i` MSVC rewrites the walk as a pointer induction variable and the body lands at 66
// against 67, where `unsigned i` keeps the scaled index the oracle's `jb` shows.
void sub_45ECB0(ctrl_t *pDlg, lbox_t *pBox) {
  int bGray = (lbox_get_count(pBox) == 0);
  for (unsigned i = 0; i < 2; i++)
    ctrl_set_gray(ctrl_find_ndx(pDlg, static_cast<short>(sgrgModemGrayIds[i])), bGray);
}

// 0x45ED00, 113 B. Confirm, then delete the selected phone-book entry.
//
// The selection is a byte with 0xFF for nothing selected, tested twice off the same load,
// once for the sentinel and once for "not the first row", so the re-selection only
// happens above zero. The record is not freed here: it is flagged at +0x50 and removed
// from the listbox, and sub_45ECB0 refreshes the buttons.
void glumodem_dlg_delete_phonebook_entry(ctrl_t *pDlg) {
  // HEADER-DEFECT gluPopup.h: do_pok_cancel_dlg is declared `BYTE(int)`; it takes a
  // `const char *`, the oracle feeding it glues_get_str's result directly.
  if (!do_pok_cancel_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x47))))
    return;
  lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 0x0B));
  int nSel = lbox_get_selection(pBox);
  if (nSel == 0xFF)
    return;
  if (nSel != 0)
    lbox_set_selection(pBox, static_cast<unsigned char>(nSel - 1));
  _PHONENUMBER *pEntry = static_cast<_PHONENUMBER *>(
      lbox_get_data(pBox, static_cast<unsigned char>(nSel)));
  pEntry->dwDeleted = 1;
  lbox_remove_item(pBox, static_cast<unsigned char>(nSel));
  sub_45ECB0(pDlg, pBox);
}

// 0x45ED80, 318 B. The modem screen's event handler. Case order sets the
// block order: 10, 0, 2, 1 outside and 7, 8, 5, 9, 6 inside.
int glumodem_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        glue_init_screen_dlg(pCtrl, sModemEvtFcnTbl_g, 0x34, 0);
        glues_transition_in(reinterpret_cast<intptr_t>(pCtrl), sModemAnims_g,
                            4, 0, 0);
        break;
      case 0:
        glumodem_dlg_activate(pCtrl);
        glue_screen_activate(pCtrl);
        break;
      case 2:
        switch (gnDlgReturnVal) {
          case 7:
            glumodem_dlg_add_phonebook_entry(pCtrl);
            return 1;
          case 8:
            glumodem_dlg_delete_phonebook_entry(pCtrl);
            return 1;
          case 5:
            if (!modemlist_dlg_answer(pCtrl))
              return 1;
            gnDlgReturnVal = 5;
            return glues_transition_out(pCtrl);
          case 9:
            if (IsSpawnedCopy()) {
              do_pok_dlg(reinterpret_cast<intptr_t>(error_get_str(0x68)));
              return 1;
            }
            break;
          case 6:
            if (sgbEnumGamesActive_g &&
                !do_pok_cancel_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x52))))
              return 1;
            gnDlgReturnVal = 6;
            break;
        }
        return glues_transition_out(pCtrl);
      case 1:
        Sleep(400);
        timer_remove_node(pCtrl, 0x38);
        modemlist_deactivate(pCtrl);
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x45EF00, 57 B. SNetEnumGamesEx callback: keep the last game whose stat text
// parses.
BOOL __stdcall glumodem_enum_games_fn(snetgame_t *pGame) {
  if (Txt2GameData(pGame->pszGameStat,
                   reinterpret_cast<gamedesc_t *>(&gModemGame.dwSaveId))) {
    SStrCopy(reinterpret_cast<char *>(gModemGame._unmapped_04),
             pGame->pszGameName, 0x18);
    gModemGame.dwSaveReserved = pGame->dwGameId;
  }
  return TRUE;
}

// 0x45EF40, 96 B. Hand one device to Storm: copy the program block to the
// stack, patch its callback and size, and clear the three status globals.
// Those globals are static in the proof arm; extern, their stores may alias
// the block read through pProgram and pin the rep-movsd count below them.
// The copy between the second and third store keeps the shipped form closest.
int glumodem_init_device(_DEVICE *pDevice, DWORD_PTR dwCaps, DWORD_PTR dwArg2,
                         const modemprog_t *pProgram) {
  sgnModemStatusA = 0;
  sgnModemStatusB = 0;
  modemprog_t data = *pProgram;
  sgpfnModemDone = NULL;
  data.pfnStatus = modem_status_fn;
  data.dwSize = sizeof(data);
  return SNetInitializeDevice(pDevice->dwId, (DWORD)dwCaps, dwArg2, &data,
                              sgModemNetBlock);
}

// 0x45EFA0, 74 B. Join-thread body: join the named game, record the result and
// the error, then flag the join finished.
ULONG __stdcall glumodem_join_fn(void *pszTarget) {
  SNETPLAYERDATA player;
  InitSNetPlayerData(&player);
  sgJoinedModemGame_g = SNetJoinGame(0, static_cast<const char *>(pszTarget), NULL,
                                     player.pszName, player.pszDescription,
                                     &g_LocalStormID);
  if (!sgJoinedModemGame_g)
    sgdwJoinError_g = SErrGetLastError();
  sgbModemDone = 1;
  return 0;
}

// 0x45EFF0, 582 B. Load the phone book from the registry into gPhonebook and
// the list, then refresh the buttons (sub_45ECB0 is inlined here).
void sub_45EFF0(ctrl_t *pDlg) {
  char szKey[0x104];
  char szValue[0x40];
  char szEntry[0x24];
  char szIndex[0x10];
  lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 0x0B));
  lbox_remove_all(pBox);
  SStrCopy(szKey, "Starcraft", 0x104);
  SStrPack(szKey, "\\", 0x104);
  SStrPack(szKey, glues_get_str(0x44), 0x104);
  lbox_set_recalc(pBox, 0);
  for (int i = 0; i < 10; i++) {
    psnprintf(szIndex, 0x10, "%u", i);
    SStrCopy(szValue, glues_get_str(0x45), 0x40);
    SStrPack(szValue, szIndex, 0x40);
    if (!SRegLoadString(szKey, szValue, 0, szEntry, 0x24))
      continue;
    _PHONENUMBER *node;
    TSLink<_PHONENUMBER> *nl;
    void *pv = SMemAlloc(sizeof(_PHONENUMBER), gTypeInfo__PHONENUMBER.raw_name(), -2, 8);
    if (pv != NULL) {
      node = static_cast<_PHONENUMBER *>(pv);
      node->link.prev = NULL;
      node->link.next = NULL;
      nl = &node->link;
    } else {
      node = NULL;
      nl = &gPhonebook.link;
    }
    _PHONENUMBER *pprev = nl->prev;
    if (pprev != NULL) {
      _PHONENUMBER *pnext = nl->next;
      TSLink<_PHONENUMBER> *pnl;
      if ((intptr_t)pnext <= 0) {
        pnl = reinterpret_cast<TSLink<_PHONENUMBER> *>(~(intptr_t)pnext);
      } else {
        intptr_t nOff = 0;
        if ((intptr_t)pnext > 0)
          nOff = (intptr_t)nl - (intptr_t)pprev->link.next;
        pnl = reinterpret_cast<TSLink<_PHONENUMBER> *>((intptr_t)pnext + nOff);
      }
      pnl->prev = pprev;
      nl->prev->link.next = nl->next;
      nl->prev = NULL;
      nl->next = NULL;
    }
    _PHONENUMBER *ptail = gPhonebook.link.prev;
    nl->prev = ptail;
    nl->next = ptail->link.next;
    ptail->link.next = node;
    gPhonebook.link.prev = reinterpret_cast<_PHONENUMBER *>(nl);
    // The C string.h entry point; the C++ char * overload is an extra COMDAT.
    char *pszSep = const_cast<char *>(strrchr(static_cast<const char *>(szEntry), '/'));
    if (pszSep)
      *pszSep = 0;
    SStrCopy(node->szName, szEntry, 0x0E);
    SStrCopy(node->szNumber, pszSep + 1, 0x15);
    psnprintf(node->szLabel, 0x24, "%s%c%s", node->szName, 0x12, node->szNumber);
    int nItem = static_cast<BYTE>(
        lbox_add_item(reinterpret_cast<ctrl_t *>(pBox), node->szLabel, 0));
    if (nItem == 0xFF)
      break;
    lbox_set_data(pBox, static_cast<unsigned char>(nItem), node);
  }
  lbox_set_recalc(pBox, 1);
  if (lbox_get_count(pBox))
    lbox_set_selection(pBox, 0);
  sub_45ECB0(pDlg, pBox);
}

// 0x45F240, 60 B. The dial-status dialog's timer tick: either tear the dialog
// down or refresh the status line from the buffer modem_status_fn fills.
//
// The two arms cannot be merged: the teardown arm tail-jumps into
// dlg_deactivate while the refresh arm tail-jumps into ctrl_set_update.
void sub_45F240(ctrl_t *pDlg) {
  if (sgbModemDone != 0) {
    dlg_deactivate(pDlg);
    return;
  }
  ctrl_set_text(ctrl_find_ndx(pDlg, 1), sgszModemStatus);
  ctrl_set_update(ctrl_find_ndx(pDlg, 2));
}

// 0x45F280, 394 B. Write the phone book back to the registry and free it:
// deleted entries are removed, the rest saved as "name/number" by index. The
// key opened for the delete is read-only and never closed; that is the oracle's.
void modemlist_deactivate(ctrl_t *pDlg) {
  char szRegPath[0x104];
  char szKey[0x104];
  char szValue[0x40];
  char szEntry[0x24];
  char szIndex[0x10];
  HKEY hKey;
  lbox_remove_all(reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 0x0B)));
  _PHONENUMBER *pHead = gPhonebook.link.next;
  if (reinterpret_cast<LONG_PTR>(pHead) <= 0 || !pHead)
    return;
  SStrCopy(szKey, "Starcraft", 0x104);
  SStrPack(szKey, "\\", 0x104);
  SStrPack(szKey, glues_get_str(0x44), 0x104);
  unsigned i = 0;
  for (_PHONENUMBER *p = gPhonebook.link.next; reinterpret_cast<LONG_PTR>(p) > 0;
       p = p->link.next) {
    psnprintf(szIndex, 0x10, "%u", i++);
    SStrCopy(szValue, glues_get_str(0x45), 0x40);
    SStrPack(szValue, szIndex, 0x40);
    if (p->dwDeleted) {
      SStrCopy(szRegPath, "Software\\Blizzard Entertainment\\", 0x104);
      SStrPack(szRegPath, szKey, 0x104);
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, szRegPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        RegDeleteValueA(hKey, szValue);
    } else {
      SStrCopy(szEntry, p->szName, 0x24);
      SStrPack(szEntry, "/", 0x24);
      SStrPack(szEntry, p->szNumber, 0x24);
      SRegSaveString(szKey, szValue, 0, szEntry);
    }
  }
  gPhonebook.Clear();
}

// 0x45F410, 64 B. Fill the modem-device listbox from the device list.
//
// The null-guarded step shape -- see the note above clamp_device.
void modemlist_dlg_activate(ctrl_t *pDlg) {
  ctrl_t *pBox = ctrl_find_ndx(pDlg, 1);
  pBox->dwFlags |= 0x20000;
  for (_DEVICE *p = gModemDeviceList.link.next; reinterpret_cast<LONG_PTR>(p) > 0;
       p = (p ? reinterpret_cast<TSLink<_DEVICE> *>(p) : &gModemDeviceList.link)->next)
    lbox_add_item(pBox, p->szName, 0);
}

// 0x45F450, 450 B. Dial or answer the selected entry on a join thread under
// the status dialog, then adopt the joined game or report why it failed.
int modemlist_dlg_answer(ctrl_t *pDlg) {
  lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 0x0B));
  unsigned nThreadId;
  int nSel = lbox_get_selection(pBox);
  if (nSel == 0xFF)
    return 0;
  const char *pszTarget;
  if (sgbEnumGamesActive_g)
    pszTarget = reinterpret_cast<char *>(gModemGame._unmapped_04);
  else
    pszTarget = static_cast<_PHONENUMBER *>(
        lbox_get_data(pBox, static_cast<unsigned char>(nSel)))->szNumber;
  HANDLE hThread = reinterpret_cast<HANDLE>(_beginthreadex(
      NULL, 0, glumodem_join_fn, const_cast<char *>(pszTarget), 0, &nThreadId));
  if (!hThread)
    return 0;
  sgbModemDone = 0;
  sgbModemStatusLive = reinterpret_cast<LONG_PTR>(
      dlg_load("rez\\gluModemStatus.bin",
               reinterpret_cast<PFN_DLGLOADER>(load_game_data),
               reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
               "gluModem.cpp", 0x241));
  set_popup_dlg(reinterpret_cast<ctrl_t *>(sgbModemStatusLive), 3);
  dlg_run_modal(reinterpret_cast<ctrl_t *>(sgbModemStatusLive), 0,
                modem_status_dlg_fcn);
  WaitForSingleObject(hThread, INFINITE);
  CloseHandle(hThread);
  if (sgJoinedModemGame_g) {
    if (!sgbEnumGamesActive_g)
      sub_45F990(pDlg, 0xFFFF);
    return sub_475770(reinterpret_cast<intptr_t>(&gModemGame));
  }
  if (glGluesMode != 0x14)
    return 0;
  switch (sgdwJoinError_g) {
    case 0x4C6:
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4E)));
      return 0;
    case 0x4B4:
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x50)));
      return 0;
    case 0x36:
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4F)));
      return 0;
    default:
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x51)));
      return 0;
    case 0x85100079:
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4C)));
      return 0;
    case 0x8CA:
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x4D)));
      return 0;
    case 0x4C7:
      return 0;
  }
}

// 0x45F620, 137 B. The modem-list dialog's event handler -- the same two-level
// switch as glumodem_phone_entry_dlg_fcn, over the same four ids, with the
// identical byte map 00 01 04 04 04 04 04 02 04 04 03.
//
// The `case 1` arm falls into the shared tail rather than carrying its own copy, so the
// oracle emits it last.
int modemlist_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        modemlist_dlg_init(pCtrl);
        break;
      case 0:
        modemlist_dlg_activate(pCtrl);
        break;
      case 7:
        title_position(pCtrl);
        break;
      case 1: {
        lbox_t *pBox = reinterpret_cast<lbox_t *>(ctrl_find_ndx(pCtrl, 1));
        sgnModemSel = lbox_get_selection(pBox);
        lbox_remove_all(pBox);
        deactivate_popup_dlg(pCtrl);
        break;
      }
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x45F6D0, 133 B. Build the modem-list screen dialog.
//
// glues_get_path reads glGluesMode, so this body saves it, forces 20 for the
// duration of that one call and puts it back -- `mov esi, [0x66A274]` /
// `mov dword ptr [0x66A274], 14h` / ... / `mov dword ptr [0x66A274], esi`. The
// restore is scheduled between glues_get_path and SStrPack, which is where the
// source puts it.
void modemlist_dlg_init(ctrl_t *pDlg) {
  char szPath[0x104];
  glue_init_screen_dlg(pDlg, NULL, 0, 0);
  pDlg->dwFlags |= 0x40000;
  DWORD dwSavedMode = glGluesMode;
  glGluesMode = 20;
  glues_get_path(szPath, 0x104);
  glGluesMode = dwSavedMode;
  SStrPack(szPath, "pSelect.pcx", 0x104);
  alloc_load_bmp(szPath, pDlg->_unmapped0C,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluModem.cpp", 0x2EE);
  ctrl_set_update(pDlg);
}

// 0x45F760, 120 B. Run the modem-device list and return the device the user
// picked. The same body as do_atzone_dlg modulo the resource name and the
// owner line.
intptr_t modemlist_get_device() {
  ctrl_t *pDlg = dlg_load("rez\\gluModemList.bin",
                          reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                          reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                          "gluModem.cpp", 0x320);
  set_popup_dlg(pDlg, 3);
  if (dlg_run_modal(get_popup_dlg(), 0, modemlist_dlg_fcn) == 3)
    return 0;

  _DEVICE *p = gModemDeviceList.link.next;
  if (reinterpret_cast<LONG_PTR>(p) <= 0)
    p = NULL;
  while (sgnModemSel--) {
    _DEVICE *q = p->link.next;
    p = reinterpret_cast<LONG_PTR>(q) <= 0 ? NULL : q;
  }
  return reinterpret_cast<intptr_t>(p);
}

// 0x45F7E0, 57 B. Storm's dial-progress callback.
//
// `ret 14h` with no register input is __stdcall: five stack words, of which the
// fourth is never read. The whole body is gated on the status dialog still
// being live, and the return is a flat zero on both paths.
BOOL __stdcall modem_status_fn(const char *pszStatus, LONG nA, LONG nB,
                               int nUnused, PFN_MODEM_DONE pfnDone) {
  if (sgbModemStatusLive) {
    SStrCopy(sgszModemStatus, pszStatus, 0x1E);
    sgnModemStatusA = nA;
    sgnModemStatusB = nB;
    sgpfnModemDone = pfnDone;
  }
  return FALSE;
}

// 0x45F820, 135 B. Build the dial-status screen dialog.
//
// Two handlers are installed at the end: the message control's draw callback at
// +0x2E and the dialog's own +0x4A slot, which dlg_run_modal loads back and
// calls -- the timer tick sub_45F240.
void modem_status_dlg_activate(ctrl_t *pDlg) {
  char szPath[0x104];
  glue_init_screen_dlg(pDlg, NULL, 0, 0);
  pDlg->dwFlags |= 0x40000;
  glues_get_path(szPath, 0x104);
  SStrPack(szPath, "pStatus.pcx", 0x104);
  alloc_load_bmp(szPath, pDlg->_unmapped0C,
                 reinterpret_cast<DWORD_PTR>(sgGamePalette_g), "gluModem.cpp", 0x1F6);
  ctrl_t *pText = ctrl_find_ndx(pDlg, 2);
  // HEADER-DEFECT gluModem.h: sub_45F930 is `void sub_45F930(int, int, int,
  // int)`; derived PFN_CTRL_DRAW, i.e. `void sub_45F930(ctrl_t *pCtrl, int
  // nXOfs, int nYOfs, rect16_t *prcClip)` (`ret 8h`, ECX + two stack words,
  // tail call into draw_ctext).
  pText->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(sub_45F930);
  pDlg->pfnDlgProc = reinterpret_cast<void *>(
      static_cast<PFN_MODEM_TICK>(sub_45F240));
  ctrl_set_update(pDlg);
}

// 0x45F8B0, 123 B. The dial-status dialog's event handler.
//
// Three cases dispatched by chained subtracts rather than a jump table, the ids 0, 1 and
// 7 being too sparse to pay for one. The `case 1` arm is the teardown: it waits 400 ms
// and calls Storm's completion hook back, but only when the modal closed on 3 and a hook
// was ever registered.
int modem_status_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        modem_status_dlg_activate(pCtrl);
        break;
      case 1:
        if (gnDlgReturnVal == 3 && sgpfnModemDone != NULL) {
          Sleep(400);
          sgpfnModemDone();
        }
        deactivate_popup_dlg(pCtrl);
        sgbModemStatusLive = 0;
        break;
      case 7:
        title_position(pCtrl);
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x45F930, 86 B. The dial-status bar's draw callback: a progress bar two
// pixels below the control, then its centred text.
void sub_45F930(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip) {
  rect16_t rc = pCtrl->rc;
  rc.top += 2;
  rc.bottom += 2;
  draw_bar_graph(&rc, sgnModemStatusB + sgnModemStatusA, sgnModemStatusA);
  draw_ctext(pCtrl, 0, 0, prcClip);
}

// 0x45F990, 216 B. Poll for a modem game: re-enumerate into gModemGame, show it
// in the list and re-arm the 0x38 timer; a busy enumerator kills the timer.
void sub_45F990(ctrl_t *pDlg, int nTimerId) {
  memset(&gModemGame, 0, sizeof(gModemGame));
  ctrl_t *pBox = ctrl_find_ndx(pDlg, 0x0B);
  lbox_remove_all(reinterpret_cast<lbox_t *>(pBox));
  if (!SNetEnumGamesEx(0, 0, reinterpret_cast<void *>(glumodem_enum_games_fn), NULL)) {
    if (SErrGetLastError() == 0x4C6) {
      sgbEnumGamesActive_g = 0;
      if (static_cast<WORD>(nTimerId))
        timer_remove_node(pDlg, nTimerId);
    }
    return;
  }
  sgbEnumGamesActive_g = 1;
  if (gModemGame._unmapped_04[0]) {
    lbox_set_recalc(reinterpret_cast<lbox_t *>(pBox), 0);
    lbox_add_item(pBox, reinterpret_cast<char *>(gModemGame._unmapped_04), 0);
    lbox_set_recalc(reinterpret_cast<lbox_t *>(pBox), 1);
    lbox_set_selection(reinterpret_cast<lbox_t *>(pBox), 0);
    ctrl_ungray(ctrl_find_ndx(pDlg, 5));
  }
  timer_create(pDlg, 0x38, 200, reinterpret_cast<PFN_TIMER_FN *>(sub_45F990));
}
