// cheat.h -- declarations for src117/lang/cheat.cpp: cheat code parsing and the
// cheat bitfield. cheat_set_bits, also in that TU, is declared in net_cmds.h.

#ifndef OC117_CHEAT_H
#define OC117_CHEAT_H

#include "platform.h"

// 0x40D920 (171 B). ECX is the token and EDX the 8-byte cipher block it fills;
// parse_command, the only caller, discards EAX.
void __fastcall cheat_encrypt(const char *pszSrc, DWORD *pdwDst);

// 0x40DC80 (600 B). ECX is the encrypted token, read as two DWORDs, and EDX the
// cheat bitfield each arm xors a bit into. parse_command reads the full EAX.
int __fastcall parse_cheat_command(const DWORD *pdwEnc, DWORD *pdwCheatBits);

// 0x40DEE0 (171 B). init.cpp declares it too, with the same text.
BOOL __fastcall parse_command(const char *pszToken, DWORD *pdwCheatBits);

// 0x40DFA0 (137 B). Same two arguments as parse_cheat_command; parse_command
// consumes the full EAX.
int __fastcall parse_debug_command(const DWORD *pdwEnc, DWORD *pdwCheatBits);

// 0x40E030 (234 B). ECX is the raw token. The body never reads EDX, but the
// parameter is real: parse_command loads EDX before the call.
int __fastcall parse_map_command(const char *pszToken, DWORD *pdwCheatBits);

#endif  // OC117_CHEAT_H
