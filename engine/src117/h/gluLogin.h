// gluLogin.h -- declarations for src117/lang/gluLogin.cpp: the login screen, its
// character list box and the create and delete commands behind it.
//
// Member functions live in their class header; this file owns the TU's free functions.
// They are spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLULOGIN_H
#define OC117_GLULOGIN_H

#include "platform.h"
#include "dialog.h"
#include "TSList.h"

// The list node and its list class live here, so the TSList<_ID,TSGetLink<_ID> > members
// this TU calls have a declaration.
//
// _ID is the character-id list node, in the ledger's own spelling: an 8-byte TSLink<_ID>
// followed by a 0x70-byte record, 0x78 in all. gluLogin_retrieve_character allocates 0x78,
// clears the two link words and copies 0x1C dwords to node+8; the record is assigned whole
// by a `rep movsd` with a compile-time count, which is a struct assignment rather than a
// blob copy.
#pragma pack(push, 1)

// The 0x70-byte character record. Three fields are named; the rest is copied wholesale
// and no instruction in this TU reads it.
struct ID_REC {
  /*0x00*/ BYTE _unmapped00[4];
  // +0x04 an expansion or multiplayer flag: gluLogin_retrieve_character compares it with
  // gbMultiPlayerMode's own truth value and rejects the record unless they agree.
  /*0x04*/ BYTE bMultiPlayer;
  /*0x05*/ BYTE _unmapped05[3];
  // +0x08 the list's sort key, unsigned: gluLogin_insert_chara compares node+0x10 with
  // `jb`.
  /*0x08*/ DWORD dwSortKey;
  // +0x0C the NUL-terminated character name, reached as node+0x14 by every consumer. Its
  // extent is the remainder of the record: no instruction reads past it, so the capacity
  // is derived rather than proven.
  /*0x0C*/ char szName[0x70 - 0x0C];
};

struct _ID {
  /*0x00*/ TSLink<_ID> link;
  /*0x08*/ ID_REC rec;
};

#pragma pack(pop)

static_assert(sizeof(ID_REC) == 0x70, "ID_REC size        gluLogin_new_id: [ebp-94h] .. [ebp-24h], and rep movsd of 0x1C dwords");
static_assert(offsetof(ID_REC, bMultiPlayer) == 0x04, "ID_REC::bMultiPlayer gluLogin_retrieve_character: cmp byte ptr [esi+4h], al");
static_assert(offsetof(ID_REC, dwSortKey) == 0x08, "ID_REC::dwSortKey  gluLogin_insert_chara: cmp edx, dword ptr [eax+10h] (node+0x10)");
static_assert(offsetof(ID_REC, szName) == 0x0C, "ID_REC::szName     gluLogin_OK: add ecx, 14h (node+0x14)");

OC_X86_LAYOUT(sizeof(_ID) == 0x78, "_ID size           gluLogin_retrieve_character: push 78h / call SMemAlloc");
static_assert(offsetof(_ID, link) == 0x00, "_ID::link          mov dword ptr [eax], 0h / mov dword ptr [eax+4h], 0h");
OC_X86_LAYOUT(offsetof(_ID, rec) == 0x08, "_ID::rec           lea edi, dword ptr [eax+8h] / rep movsd");


// An explicit specialization of the list class, because this TU calls three of its
// members -- Clear @0x459E40, DeleteNode @0x459EA0 and LinkNode @0x440C10 -- and
// src117/h/TSList.h's primary template declares only Clear. A class cannot be reopened,
// and that header is on this TU's include path, so a specialization is the one spelling
// that adds the two missing members without touching it and without changing any other
// instantiation. The `0` the ledger's names carry is an IDA rendering of TSGetLink<_ID>,
// not a second template parameter. The layout is the primary template's, re-asserted
// below on the concrete instantiation.
#pragma pack(push, 1)
template <> struct TSList<_ID,TSGetLink<_ID> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_ID> link;

  // 0x459E40 (84 B). `ret`, ECX only -> nullary thiscall, returns nothing.
  void Clear(void);

  // 0x459EA0 (103 B). `ret 4h`, ECX + one 4-byte stack word. It returns the successor,
  // normalised to NULL at the terminator: the body materialises EAX on every path
  // (`xor eax,eax` / `cmovle edi,eax` / `mov eax,edi` before the epilogue), which is
  // invisible from the one call site in this TU, gluLogin_delete_id, since that discards
  // the value. The body is defined below.
  _ID *DeleteNode(_ID *pNode);

  // 0x440C10 (160 B). `ret 0Ch`, ECX + three stack words. gluLogin_insert_chara's pushes
  // give the order: [ebp+08h] the node, [ebp+0Ch] the mode, [ebp+10h] the anchor. The
  // mode is `unsigned long`, and 2 is the only value this TU passes.
  void LinkNode(_ID *pNode, ULONG dwWhere, _ID *pBefore);
};
#pragma pack(pop)

// The instantiation is typedef'd before it is asserted on, because the comma inside a
// template argument list does not survive the preprocessor; src117/h/TSList.h does the
// same.
typedef TSList<_ID, TSGetLink<_ID> > IDLIST;

OC_X86_LAYOUT(sizeof(IDLIST) == 12, "IDLIST size       0x5081A0 .. the next named global 0x5081AC");
static_assert(offsetof(IDLIST, _unmapped_00) == 0x00, "IDLIST::_unmapped_00 no instruction in this TU reads it");
static_assert(offsetof(IDLIST, link) == 0x04, "IDLIST::link      this==0x5081A0 while every head read is [0x5081A8]");

// 0x459F10 (207 B)
void do_login_dlg();

// 0x45A000 (57 B)
void gluLogin_dlg_activate(intptr_t nArg0);

// 0x45A040 (282 B) -- reached only by address, so the argument list is a lower bound. A
// PFN_CTRL_EVT, `int(__fastcall *)(ctrl_t *, dlgevt_t *)`: do_login_dlg pushes 0x45A040
// into dlg_run_modal, whose third parameter has that type, which is what makes the return
// BOOL rather than void.
BOOL gluLogin_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x45A180 (71 B) -- ECX is a pointer whose class the bytes do not name.
int gluLogin_insert_chara(void *pArg0);

// 0x45A1D0 (107 B) -- Reached only by address, so the argument list is a lower bound.
// The bytes do not name what arg0 points at. __stdcall: the body reads no register
// input and pops its arguments, which under /Gr is byte-identical to a fastcall that
// ignores its two register arguments.
void __stdcall gluLogin_retrieve_character(void *pArg0);

// 0x45A240 (59 B) -- ECX is a pointer whose class the bytes do not name.
BOOL gluLogin_OK(void *pArg0);

// 0x45A280 (148 B) -- ECX is a pointer whose class the bytes do not name.
void gluLogin_listbox_ctrl_select(void *pArg0);

// 0x45A320 (391 B)
void gluLogin_new_id(intptr_t nArg0);

// 0x45A4B0 (224 B)
void gluLogin_delete_id(intptr_t nArg0);

// 0x45A590 (110 B)
BOOL gluLogin_listbox_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x45A620 (110 B)
void gluLogin_listbox_ctrl_activate(intptr_t nArg0);

#endif  // OC117_GLULOGIN_H
