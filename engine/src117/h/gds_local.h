// gds_local.h -- declarations for src117/lang/gds_local.cpp, the localisation
// resource module: the satellite DLL the game loads its strings, dialogs,
// accelerators and binary resources out of.
//
// Every entry point here falls back to the executable's own instance when the
// satellite DLL is absent or does not carry the resource.

#ifndef OC117_GDS_LOCAL_H
#define OC117_GDS_LOCAL_H

#include "platform.h"

// 0x4CB6F0 FreeLocaleModule (27 B). Nullary; FreeLibrary's the satellite and
// nulls the handle. No direct caller, so nothing pins a return.
void FreeLocaleModule();

// 0x4CB710 GdsDialogBoxParam (62 B). ECX is the template id, EDX the dialog
// procedure, the one stack word the init parameter. The id narrows to 16 bits
// only through MAKEINTRESOURCE; the failure path's appcritf pushes the whole
// register.
int GdsDialogBoxParam(int nTemplate, void *pfnDlgProc, void *pInitParam);
#if !defined(_MSC_VER)
// clang and gcc: a string literal or a function as the void * (h/compiler.h, OcVoidPtr)
template <typename F> inline int GdsDialogBoxParam(int nTemplate, F *pfnDlgProc, void *pInitParam)
{
  return GdsDialogBoxParam(nTemplate, OcVoidPtr(pfnDlgProc), pInitParam);
}
#endif

// 0x4CB750 GetLocale (89 B). Nullary. Reads string resource 3 as a hex number,
// from the satellite first and the executable second, 0x409 when neither has
// it. `int`, as netdesc.h spells it: a return type is part of the mangled name,
// so `DWORD` here would be a second symbol.
int GetLocale();

// 0x4CB7B0 GetResource (87 B). ECX is the resource name, EDX the type, the one
// stack word an optional out-parameter for the size. Returns the locked bytes,
// or 0 if any of the three Win32 steps fails. The tail is a jmp into
// LockResource over the incoming parameter slot, so `ret 4h` is its own.
void *GetResource(const char *pszName, const char *pszType, DWORD *pcbSize);

// 0x4CB810 GetAcceleratorTable (39 B). ECX is the table id, passed to
// LoadAcceleratorsA whole. Spelled exactly as event_Win.h spells it: the
// integer is the caller's type and the LPCSTR the Win32 boundary.
void *GetAcceleratorTable(int nResId);

// 0x4CB840 InitLocaleModule (22 B). ECX is the DLL path; the result is a BOOL,
// not the handle. Spelled exactly as init.cpp spells it.
BOOL InitLocaleModule(const char *pszPath);

// 0x4CB860 psnprintf (37 B). __cdecl varargs. Forces a terminator at cchBuf-1,
// the reason it exists over _vsnprintf. victory.h includes this header, so its
// many callers all see this one signature.
int __cdecl psnprintf(char *pszBuf, size_t cchBuf, const char *pszFormat, ...);

#endif  // OC117_GDS_LOCAL_H
