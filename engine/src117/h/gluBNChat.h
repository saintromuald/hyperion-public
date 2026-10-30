// gluBNChat.h -- declarations for src117/lang/gluBNChat.cpp: the Battle.net
// chat channel list, its number icons and the owner-draw callback Storm calls
// for each row. Free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_GLUBNCHAT_H
#define OC117_GLUBNCHAT_H

#include "platform.h"

struct tagDRAWITEMSTRUCT;

// 0x44A780 (346 B). Storm's SNet auth callback: __stdcall, seven stack words;
// it returns 0 or 1.
BOOL __stdcall UiAuthCallback(int nType, int nArg1, const char *pszInfo,
                              int nArg3, const char *pszGameData,
                              char *pszError, DWORD cchError);

// 0x44A8E0 (153 B). Storm's SNet owner-draw description callback: __stdcall,
// ten stack words; the last is the DRAWITEMSTRUCT (itemState at +0x10,
// hDC at +0x18). The third and fourth words are the row's name and record
// text, both painters read them as strings. Always returns 1.
BOOL __stdcall UiDrawDescCallback(int nArg0, int nType, const char *pszName,
                                  const char *pszRecord, int nFlags, int nStyle,
                                  int nArg6, BYTE *pBits, int nNumber,
                                  tagDRAWITEMSTRUCT *pDraw);

// 0x44A980 (1310 B) / 0x44AEC0 (433 B). UiDrawDescCallback's two painters:
// ECX and EDX are the name and the record text, the stack the rest. The one
// caller of each discards EAX.
void sub_44A980(const char *pszName, const char *pszRecord, int nFlags,
                int nStyle, int nArg6, tagDRAWITEMSTRUCT *pDraw);
void sub_44AEC0(const char *pszName, const char *pszRecord, int nFlags,
                int nStyle, int nArg6, BYTE *pBits, int nNumber,
                tagDRAWITEMSTRUCT *pDraw);

// 0x44B080 (386 B). ECX the DRAWITEMSTRUCT, EDX the row's source bits, then
// the number and the row height on the stack. EAX is icon 0's width, computed
// on purpose at the tail; the one caller discards it.
int sub_44B080(tagDRAWITEMSTRUCT *pDraw, BYTE *pBits, int nNumber, int nHeight);

// 0x44B210 (466 B). ECX the DRAWITEMSTRUCT, EDX the row's record text, then
// the row height on the stack. The one caller reads EAX, the icon's width.
int sub_44B210(tagDRAWITEMSTRUCT *pDraw, const char *pszRecord, int nHeight);

// 0x44B3F0 (84 B) / 0x44B450 (260 B). Nullary; both call sites of each discard
// EAX.
void bn_chat_free();
void bn_chat_init();

#endif  // OC117_GLUBNCHAT_H
