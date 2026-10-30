// bcrypt.h, off Windows: the SHA-1 hash the engine takes from BCrypt for replay
// and mod fingerprints, port_posix.cpp's own.
#pragma once
#include "windows.h"

typedef LONG NTSTATUS;
typedef void *BCRYPT_HANDLE;
typedef void *BCRYPT_ALG_HANDLE;
typedef void *BCRYPT_HASH_HANDLE;
#define BCRYPT_SHA1_ALG_HANDLE ((BCRYPT_ALG_HANDLE)0x00000031)
#define BCRYPT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#define BCRYPT_SHA1_ALGORITHM L"SHA1"
OC_EXTERN_C NTSTATUS __stdcall BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE *phAlgorithm, const wchar_t *pszAlgId,
                                                           const wchar_t *pszImplementation, ULONG dwFlags);
OC_EXTERN_C NTSTATUS __stdcall BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, ULONG dwFlags);
OC_EXTERN_C NTSTATUS __stdcall BCryptCreateHash(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_HASH_HANDLE *phHash,
                                                PUCHAR pbHashObject, ULONG cbHashObject, PUCHAR pbSecret,
                                                ULONG cbSecret, ULONG dwFlags);
OC_EXTERN_C NTSTATUS __stdcall BCryptHashData(BCRYPT_HASH_HANDLE hHash, PUCHAR pbInput, ULONG cbInput, ULONG dwFlags);
OC_EXTERN_C NTSTATUS __stdcall BCryptFinishHash(BCRYPT_HASH_HANDLE hHash, PUCHAR pbOutput, ULONG cbOutput,
                                                ULONG dwFlags);
OC_EXTERN_C NTSTATUS __stdcall BCryptDestroyHash(BCRYPT_HASH_HANDLE hHash);
OC_EXTERN_C NTSTATUS __stdcall BCryptHash(BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbSecret, ULONG cbSecret,
                                          PUCHAR pbInput, ULONG cbInput, PUCHAR pbOutput, ULONG cbOutput);
