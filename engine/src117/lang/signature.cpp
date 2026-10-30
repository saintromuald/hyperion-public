// signature.cpp -- the certificate-signature helpers, oracle 0x49D370-0x49D460:
// the name-salted SHA-1 and the RSA-style modular exponentiation in place.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/signature.h"
#include "../h/Storm.h"
#include <malloc.h>       // _alloca -- __alloca_probe_16
#include "../gen/globals117.h"

// The key material sig_powmod exponentiates with. Both are pushed whole as
// SBigPowMod arguments, so each is one handle-wide slot.

// 0x6A61BC sig_mod_g -- SBigPowMod's fourth argument, the modulus.
#ifdef OC_DLL_MODE
  #ifndef sig_mod_g
  #define sig_mod_g (*reinterpret_cast<void **>(OC_VA(0x6A61BCu)))
  #endif
#else
extern void *sig_mod_g;
#endif

// 0x6A61C0 sig_pow_g -- SBigPowMod's third argument, the exponent.
#ifdef OC_DLL_MODE
  #ifndef sig_pow_g
  #define sig_pow_g (*reinterpret_cast<void **>(OC_VA(0x6A61C0u)))
  #endif
#else
extern void *sig_pow_g;
#endif

// Storm ordinals 492 and 495, imported through storm_imports.def.
extern "C" {
void __stdcall SMemFill(void *pvDest, DWORD cbSize, BYTE bValue);
int  __stdcall SMemCmp(const void *pv1, const void *pv2, DWORD cbSize);
}

// Hyperion: sig_mod_g and sig_pow_g hold numbers the original's start-up
// made with storm.dll's SBigNew (at 0x402460 and 0x402480, freed at exit), which
// the port's SBig functions (port_stormbig.cpp) cannot read. The key and the
// exponent go into numbers of the port's own instead, made on first use
//.
static void *s_pSigMod, *s_pSigPow;

static void SigKeyNumbers()
{
  if (!s_pSigMod) {
    SBigNew(&s_pSigMod);
    SBigNew(&s_pSigPow);
  }
}

// The bodies, in oracle VA order.

// Hash a blob, then the upper-cased account name after it.
void sig_sha(const char *pszName, const void *pvData, DWORD cbData,
             void *pvHash) {
  char szName[0x400];
  shactx_t ctx;

  SHA1_Init(&ctx);
  SHA1_Update(&ctx, pvData, cbData);
  SStrCopy(szName, pszName, sizeof(szName));
  SStrUpper(szName);
  SHA1_Update(&ctx, szName, SStrLen(szName));
  SHA1_Final(pvHash, &ctx);
}

// Raise the buffer to the stored exponent modulo the stored modulus, in place,
// zeroing whatever the shorter result leaves behind.
void sig_powmod(void *pvBuf, DWORD cbBuf) {
  void *pBase;
  void *pResult;
  DWORD cbWritten;

  SBigNew(&pBase);
  SBigFromBinary(pBase, pvBuf, cbBuf);
  SBigNew(&pResult);
  SBigPowMod(pResult, pBase, s_pSigPow, s_pSigMod);
  SBigToBinaryBuffer(pResult, pvBuf, cbBuf, &cbWritten);
  if (cbWritten < cbBuf)
    SMemZero(static_cast<BYTE *>(pvBuf) + cbWritten, cbBuf - cbWritten);
  SBigDel(pResult);
  SBigDel(pBase);
}

// Check the "NGIS" block at the end of the data: its signature, raised to the
// given exponent modulo the given key, must equal the padded name-salted hash
// of everything before it. nKeyLen is also the signature's length.
int __stdcall sig_validate_certificate(const char *pszName, void *pvData,
                                       DWORD dwSize, const BYTE *pbKey,
                                       int nKeyLen, const BYTE *pbExp,
                                       int nExpLen)
{
  DWORD cbData = dwSize - nKeyLen - 4;
  if (dwSize >= (DWORD)(nKeyLen + 4)) {
    BYTE *pSig = static_cast<BYTE *>(pvData) + cbData;
    if (*reinterpret_cast<DWORD *>(pSig) == 0x5349474E) {
      BYTE *pHash = static_cast<BYTE *>(_alloca(nKeyLen));
      SMemFill(pHash, nKeyLen, 0xBB);
      pHash[nKeyLen - 1] = 0x0B;
      sig_sha(pszName, pvData, cbData, pHash);

      BYTE *pDecoded = static_cast<BYTE *>(_alloca(nKeyLen));
      SMemCopy(pDecoded, pSig + 4, nKeyLen);
      SigKeyNumbers();
      SBigFromBinary(s_pSigMod, pbKey, nKeyLen);
      SBigFromBinary(s_pSigPow, pbExp, nExpLen);
      sig_powmod(pDecoded, nKeyLen);
      return SMemCmp(pDecoded, pHash, nKeyLen) == 0;
    }
  }
  return 0;
}
