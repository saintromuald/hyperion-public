// CodeSign.cpp -- the Authenticode check on Blizzard's signed files. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>
#include "../h/CodeSign.h"
#include "../h/Storm.h"
#if defined(_WIN32)
// WinVerifyTrust's 0x30-byte WINTRUST_DATA is the pre-Win8 layout.
#undef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN7
#include <wintrust.h>
#include <softpub.h>
#endif
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The organization name VerifyBlizzardSignature compares against.
#ifdef OC_DLL_MODE
#define off_5031A8 (*reinterpret_cast<const char **>(OC_VA(0x5031A8u)))
#else
extern const char *off_5031A8;
#endif

// The bodies, in oracle VA order.

// Accepts a file only when its Authenticode signature verifies and the
// signer's organization is Blizzard's.
BYTE VerifyBlizzardSignature(const char *pszPath) {
  char szOrg[0x200];
  if (VerifyTrust(pszPath) != 0)
    return FALSE;
  if (!GetCertificateOrganization(pszPath, szOrg, sizeof(szOrg)))
    return FALSE;
  return _strnicmp(szOrg, off_5031A8, sizeof(szOrg)) == 0;
}

#if defined(_WIN32)
// Reads the organization name of the certificate that signed pszFile. The
// first wide buffer is leaked when the second is allocated, as in the oracle.
// cbSigner must be declared before the __try for its stack slot.
BYTE GetCertificateOrganization(const char *pszFile, char *pszOrg, int nSize) {
  if (!pszFile || !*pszFile)
    return FALSE;
  int nLen = MultiByteToWideChar(CP_UTF8, 0, pszFile, -1, NULL, 0);
  if (nLen <= 0)
    return FALSE;
  WCHAR *pwszBuf = new WCHAR[nLen];
  MultiByteToWideChar(CP_UTF8, 0, pszFile, -1, pwszBuf, nLen);
  BYTE bResult = FALSE;
  HCERTSTORE hStore = NULL;
  HCRYPTMSG hMsg = NULL;
  PCCERT_CONTEXT pCert = NULL;
  PCMSG_SIGNER_INFO pSigner = NULL;
  DWORD cbSigner;
  __try {
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, pwszBuf,
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, NULL, NULL, NULL,
                          &hStore, &hMsg, NULL))
      __leave;
    if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &cbSigner))
      __leave;
    pSigner = static_cast<PCMSG_SIGNER_INFO>(LocalAlloc(LPTR, cbSigner));
    if (!pSigner)
      __leave;
    if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, pSigner, &cbSigner))
      __leave;
    CERT_INFO certInfo;
    certInfo.Issuer = pSigner->Issuer;
    certInfo.SerialNumber = pSigner->SerialNumber;
    pCert = CertFindCertificateInStore(hStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                       0, CERT_FIND_SUBJECT_CERT, &certInfo, NULL);
    if (!pCert)
      __leave;
    DWORD nName = CertGetNameStringW(pCert, CERT_NAME_ATTR_TYPE, 0,
                                     (void *)szOID_ORGANIZATION_NAME, NULL, 0);
    if (!nName)
      __leave;
    pwszBuf = new WCHAR[nName];
    if (CertGetNameStringW(pCert, CERT_NAME_ATTR_TYPE, 0,
                           (void *)szOID_ORGANIZATION_NAME, pwszBuf, nName) &&
        WideCharToMultiByte(CP_UTF8, 0, pwszBuf, -1, NULL, 0, NULL, NULL) > 0) {
      WideCharToMultiByte(CP_UTF8, 0, pwszBuf, -1, pszOrg, nSize, NULL, NULL);
      bResult = TRUE;
    }
    delete[] pwszBuf;
  } __finally {
    if (pSigner)
      LocalFree(pSigner);
    if (pCert)
      CertFreeCertificateContext(pCert);
    if (hStore)
      CertCloseStore(hStore, 0);
    if (hMsg)
      CryptMsgClose(hMsg);
  }
  return bResult;
}

// Runs WinVerifyTrust on pszFile: 0 trusted, 1 unsigned, 2 bad signature,
// 3 distrusted, 4 anything else.
int VerifyTrust(const char *pszFile) {
  if (!pszFile || !*pszFile)
    return 4;
  int nLen = MultiByteToWideChar(CP_ACP, 0, pszFile, -1, NULL, 0);
  if (nLen <= 0)
    return 4;
  WCHAR *pwszFile = new WCHAR[nLen];
  MultiByteToWideChar(CP_ACP, 0, pszFile, -1, pwszFile, nLen);
  WINTRUST_FILE_INFO fileInfo;
  memset(&fileInfo, 0, sizeof(fileInfo));
  fileInfo.cbStruct = sizeof(WINTRUST_FILE_INFO);
  fileInfo.pcwszFilePath = pwszFile;
  fileInfo.hFile = NULL;
  fileInfo.pgKnownSubject = NULL;
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  WINTRUST_DATA trustData;
  memset(&trustData, 0, sizeof(trustData));
  trustData.cbStruct = sizeof(trustData);
  trustData.pPolicyCallbackData = NULL;
  trustData.pSIPClientData = NULL;
  trustData.dwUIChoice = WTD_UI_NONE;
  trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
  trustData.dwUnionChoice = WTD_CHOICE_FILE;
  trustData.dwStateAction = 0;
  trustData.hWVTStateData = NULL;
  trustData.pwszURLReference = NULL;
  trustData.dwUIContext = 0;
  trustData.pFile = &fileInfo;
  LONG lStatus = WinVerifyTrust(NULL, &action, &trustData);
  int nResult;
  switch (lStatus) {
  case ERROR_SUCCESS:
    nResult = 0;
    break;
  case TRUST_E_NOSIGNATURE:
    nResult = GetLastError() == TRUST_E_NOSIGNATURE ? 1 : 2;
    break;
  case TRUST_E_EXPLICIT_DISTRUST:
  case TRUST_E_SUBJECT_NOT_TRUSTED:
  case CRYPT_E_SECURITY_SETTINGS:
    nResult = 3;
    break;
  default:
    nResult = 4;
    break;
  }
  delete[] pwszFile;
  return nResult;
}
#else
// Hyperion: off Windows there is no Authenticode check. Every file
// reads as unsigned, as one without a signature does on Windows.
BYTE GetCertificateOrganization(const char *pszFile, char *pszOrg, int nSize) {
  (void)pszFile;
  if (pszOrg && nSize > 0)
    pszOrg[0] = 0;
  return FALSE;
}

int VerifyTrust(const char *pszFile) {
  return (!pszFile || !*pszFile) ? 4 : 1;
}
#endif
