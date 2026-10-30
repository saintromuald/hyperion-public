// oracle_image.h -- the 1.17 exe for the differential tests: read from the
// player's copy at run time, checked against the oracle's SHA-1, and the file
// offset of a VA. Nothing of Blizzard's is in the repo.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char ORACLE_SHA1[] = "f7e2c31dcb69752e1729825de62317879c32732e";

static bool Sha1Hex(const BYTE *p, DWORD n, char out[41])
{
  HCRYPTPROV prov = 0;
  HCRYPTHASH hash = 0;
  BYTE digest[20];
  DWORD cb = sizeof digest;
  bool ok = CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
            CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash) && CryptHashData(hash, p, n, 0) &&
            CryptGetHashParam(hash, HP_HASHVAL, digest, &cb, 0);
  if (hash) CryptDestroyHash(hash);
  if (prov) CryptReleaseContext(prov, 0);
  for (int i = 0; ok && i < 20; ++i) sprintf(out + 2 * i, "%02x", digest[i]);
  return ok;
}

// The file offset of a VA, from the section table.
static long FileOffset(const BYTE *image, uint32_t va)
{
  const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
  const IMAGE_NT_HEADERS32 *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(image + dos->e_lfanew);
  uint32_t rva = va - nt->OptionalHeader.ImageBase;
  const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
  for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + sec->SizeOfRawData)
      return (long)(rva - sec->VirtualAddress + sec->PointerToRawData);
  }
  return -1;
}

// Reads the exe at path and checks that it is the oracle. Prints why not and
// returns NULL when it cannot be read or is another build.
static BYTE *LoadOracle(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("cannot open %s\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long cb = ftell(f);
  fseek(f, 0, SEEK_SET);
  BYTE *image = static_cast<BYTE *>(malloc(cb));
  fread(image, 1, cb, f);
  fclose(f);
  char sha[41] = {0};
  if (!Sha1Hex(image, (DWORD)cb, sha) || strcmp(sha, ORACLE_SHA1) != 0) {
    printf("%s is not the 1.17 oracle (sha1 %s)\n", path, sha);
    free(image);
    return NULL;
  }
  printf("oracle %s\n", sha);
  return image;
}
