// signature.h -- the certificate-signature helpers in src117/lang/signature.cpp
// (oracle 0x49D370-0x49D520).

#ifndef OC117_SIGNATURE_H
#define OC117_SIGNATURE_H

#include "platform.h"
#include "sha1.h"

// Storm's big-number and case entry points, ordinals 510, 606, 609, 624, 628
// and 638, which storm.dll exports without names. 1.17 discards every result.
// The port defines them in port_stormbase.cpp and port_stormbig.cpp.
extern "C" {
void __stdcall SStrUpper(char *pszStr);
void __stdcall SBigDel(void *pBig);
void __stdcall SBigFromBinary(void *pBig, const void *pvData, DWORD cbData);
void __stdcall SBigNew(void **ppBig);
void __stdcall SBigPowMod(void *pResult, void *pBase, void *pExp, void *pMod);
void __stdcall SBigToBinaryBuffer(void *pBig, void *pvBuffer, DWORD cbBuffer,
                                  DWORD *pcbWritten);
}  // extern "C"

// ECX, EDX and two stack words.
void sig_sha(const char *pszName, const void *pvData, DWORD cbData,
             void *pvHash);

void sig_powmod(void *pvBuf, DWORD cbBuf);

// 0x49D460. __stdcall, seven stack words; map_validate_signature reads EAX.
// gluBN.cpp also installs it in SNETUIDATA's callback field.
int __stdcall sig_validate_certificate(const char *pszName, void *pvData,
                                       DWORD dwSize, const BYTE *pbKey,
                                       int nKeyLen, const BYTE *pbExp,
                                       int nExpLen);

#endif  // OC117_SIGNATURE_H
