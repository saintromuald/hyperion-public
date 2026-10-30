// charactr.h -- declarations for src117/lang/charactr.cpp: single-player
// character file I/O. ReadCharacterFile, the TU's fifth function, is declared
// in gluCmpgn.h.

#ifndef OC117_CHARACTR_H
#define OC117_CHARACTR_H

#include "platform.h"

// The per-file callback EnumCharacterFiles drives; __stdcall, so the callback
// pops the record.
typedef void(__stdcall *PFN_ENUMCHARACTER)(void *pRec);

// 0x40D4F0 (127 B). Deletes the named character's file in the characters
// folder, .mpc in multiplayer and .spc otherwise, and returns DeleteFileA's
// result; remove_character tail-jumps here.
int __fastcall DeleteCharacterFile(const char *pszName);

// 0x40D570 (264 B). ECX is the per-file callback; gluLogin_dlg_activate passes
// gluLogin_retrieve_character. void, though the exits leave FindFirstFileA's or
// FindClose's result in EAX: no path sets a return value, and an `int` return
// adds a `mov eax` 1.17 does not have.
void __fastcall EnumCharacterFiles(PFN_ENUMCHARACTER pfn);

// 0x40D7C0 (110 B). ECX and EDX; it tail-jumps to WriteCharacterFile, and all
// three callers read the full EAX.
int __fastcall CreateNewCharacterFile(const char *pszName, void *pRecOut);

// 0x40D830 (239 B). ECX only; both call sites discard EAX. The record is
// ReadCharacterFile's 0x70-byte one, spelled void * to match gluCmpgn.h.
int __fastcall WriteCharacterFile(void *pRec);

#endif  // OC117_CHARACTR_H
