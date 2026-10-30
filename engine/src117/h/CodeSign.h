// CodeSign.h -- declarations for src117/lang/CodeSign.cpp, the Authenticode
// check on Blizzard's signed files. The free functions are __fastcall through
// /Gr and are declared without the keyword.

#ifndef OC117_CODESIGN_H
#define OC117_CODESIGN_H

#include "platform.h"

// 0x411E40. ECX only; the callers read AL and nothing wider, so the return is a
// BYTE.
BYTE VerifyBlizzardSignature(const char *pszPath);

// 0x411EA0. ECX, EDX and one stack word; the one caller reads AL.
BYTE GetCertificateOrganization(const char *pszFile, char *pszOrg, int nSize);

// 0x412120. ECX only; the one caller reads all of EAX.
int VerifyTrust(const char *pszFile);

#endif  // OC117_CODESIGN_H
