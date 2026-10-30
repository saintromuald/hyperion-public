// grid.h -- declarations for src117/lang/grid.cpp: CD-key storage, validation
// and the Brood War install check. Free functions are __fastcall through /Gr
// and are declared without the keyword.
#pragma once

#include "platform.h"   // BOOL
#include "codec.h"      // DecodeFile

// 0x4635B0 (6 B). Nullary; returns the dword at 0x66CAC0, the flag
// CheckIsBroodwar fills from FileExists.
BOOL IsBroodwar();

// 0x4634E0 (194 B). Nullary; four callers read the full EAX.
int sub_4634E0();

// IsSpawnedCopy (0x4635C0), RetrieveKey (0x4635D0) and cleanup_key (0x463820)
// are grid.cpp's too, declared in glues.h and netdesc.h.

// 0x463710 (177 B). ECX is the file name, passed on to MemFileLoad, and EDX the
// plaintext the decrypted file must equal; every path returns 0 or 1.
int check_genuine(const char *pszFile, const char *pszExpect);

// 0x4637D0 (71 B). Nullary. Its address is taken once and it has no direct call
// site.
void free_cdkey();

// 0x463880 (18 B). Nullary; fills the Brood War cell IsBroodwar returns.
void CheckIsBroodwar();

// 0x4638A0 (262 B). Nullary; loads both key files and sets the spawned flag.
void init_cdkey();


