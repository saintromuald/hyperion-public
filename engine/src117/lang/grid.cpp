// grid.cpp -- CD-key storage, validation and the Brood War install check.
//
// The Storm owner tags are written out as "grid.cpp" plus a literal line, which
// is what the oracle compiled its __FILE__ and __LINE__ pair to.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/grid.h"
#include "../h/Storm.h"      // SMemAlloc, SMemFree, SStrLen, SStrCmp
#include "../h/gamedata.h"   // MemFileLoad
#include "../h/gds_local.h"  // psnprintf
#include "../h/glues.h"      // mem_free, IsSpawnedCopy
#include "../h/gluBN.h"      // PasswordInit
#include "../h/init.h"       // FileExists
#include "../h/net_cmds.h"   // get_valid_str_len
#include "../port/data/port_remastered.h"  // Hyperion: PortGameSource
#include "../h/netdesc.h"    // RetrieveKey, cleanup_key
#include "../h/snd.h"        // load_game_data
#include "../gen/globals117.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>            // Hyperion: _time64 (PasswordInit)

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

#ifndef OC_G_GBSPAWNEDCOPY
#define OC_G_GBSPAWNEDCOPY
#ifdef OC_DLL_MODE
#define gbSpawnedCopy_g (*reinterpret_cast<int *>(OC_VA(0x5085C8u)))
#else
extern int gbSpawnedCopy_g;
#endif
#endif

#ifndef OC_G_ISBROODWAR
#define OC_G_ISBROODWAR
#ifdef OC_DLL_MODE
#define isBroodwar (*reinterpret_cast<BOOL *>(OC_VA(0x66CAC0u)))
#else
extern BOOL isBroodwar;
#endif
#endif

#ifndef OC_G_SGPCDKEY
#define OC_G_SGPCDKEY
#ifdef OC_DLL_MODE
#define sgpCDKey (*reinterpret_cast<char **>(OC_VA(0x66CAC4u)))
#else
extern char *sgpCDKey;
#endif
#endif

#ifndef OC_G_SGPCDKEY_LEN
#define OC_G_SGPCDKEY_LEN
#ifdef OC_DLL_MODE
#define sgpCDKey_len (*reinterpret_cast<DWORD *>(OC_VA(0x66CAC8u)))
#else
extern DWORD sgpCDKey_len;
#endif
#endif

#ifndef OC_G_SGPOWNER
#define OC_G_SGPOWNER
#ifdef OC_DLL_MODE
#define sgpOwner (*reinterpret_cast<char **>(OC_VA(0x66CACCu)))
#else
extern char *sgpOwner;
#endif
#endif

#ifndef OC_G_SGPOWNER_LEN
#define OC_G_SGPOWNER_LEN
#ifdef OC_DLL_MODE
#define sgpOwner_len (*reinterpret_cast<DWORD *>(OC_VA(0x66CAD0u)))
#else
extern DWORD sgpOwner_len;
#endif
#endif

// The password buffer PasswordInit fills and DecodeFile keys off.
#define GRID_PASSLEN 20

// Hyperion: PasswordInit (0x448F40) is gluBN.cpp's in 1.17. The Battle.net
// screens are out of the port's build, and the key
// decoding below is its one caller left, so its body is here.
//
// Fill a buffer with nLen-1 random non-zero bytes and terminate it, then reseed
// the generator from the clock. The fixed 0x150B seed makes the password
// reproducible for the duration of the loop only.
void PasswordInit(char *pszBuf, int nLen)
{
  int i;

  OcSrand(0x150B);
  for (i = 0; i < nLen - 1;) {
    char c = static_cast<char>(OcRand());

    if (c != 0)
      pszBuf[i++] = c;
  }
  OcSrand(static_cast<unsigned int>(_time64(NULL)));
  pszBuf[nLen - 1] = '\0';
}

// Helpers the oracle inlines at every use.

// Decrypts one loaded key block in place and answers whether what came out is a
// usable string.
static int decode_key_block(char *pszBlock, DWORD dwLen, char *pszPass)
{
  PasswordInit(pszPass, GRID_PASSLEN);

  DWORD dwDec = DecodeFile(pszBlock, dwLen, reinterpret_cast<intptr_t>(pszPass));
  if (dwDec == 0 || dwDec >= dwLen)
    return 0;
  pszBlock[dwDec] = 0;
  return get_valid_str_len(pszBlock, 0x80, dwDec + 1, 0, 0);
}

// Loads one key file, dropping a block that came back empty. The two null
// returns share one exit, which is what the load's own null test branches to.
static void *load_key_file(const char *pszPath, DWORD *pdwLen)
{
  void *pv = load_game_data(pszPath, NULL, pdwLen, "grid.cpp", 0x6A);

  if (pv == NULL)
    return NULL;
  if (*pdwLen == 0) {
    mem_free(pv, "grid.cpp", 0x76);
    return NULL;
  }
  return pv;
}

// Zeroes a key string before handing the block back.
static void wipe_and_free(char *psz, int nLine)
{
  SecureZeroMemory(psz, SStrLen(psz));
  SMemFree(psz, "grid.cpp", nLine, 0);
}

// 0x4634E0. Hashes the decrypted CD key. A block that will not decrypt is freed
// and the hash is taken over a null pointer.
int sub_4634E0()
{
  char szPass[GRID_PASSLEN];
  char *pszKey = static_cast<char *>(SMemAlloc(sgpCDKey_len, "grid.cpp", 0x42, 0));

  memcpy(pszKey, sgpCDKey, sgpCDKey_len);
  if (!decode_key_block(pszKey, sgpCDKey_len, szPass)) {
    SMemFree(pszKey, "grid.cpp", 0x4D, 0);
    pszKey = NULL;
  }

  int nHash = SStrHash(pszKey, 0, 0);
  if (pszKey)
    wipe_and_free(pszKey, 0x5C);
  return nHash;
}

// 0x4635B0.
BOOL IsBroodwar()
{
  return isBroodwar;
}

// 0x4635C0.
int IsSpawnedCopy()
{
  return gbSpawnedCopy_g;
}

// 0x4635D0. Hands back decrypted copies of the CD key and the owner name. An
// out-parameter that does not decrypt is freed and set to null.
void RetrieveKey(DWORD_PTR *pdwKeyLo, DWORD_PTR *pdwKeyHi)
{
  char szPass[GRID_PASSLEN];

  if (pdwKeyLo) {
    *pdwKeyLo = reinterpret_cast<DWORD_PTR>(SMemAlloc(sgpCDKey_len, "grid.cpp", 0x42, 0));
    memcpy(reinterpret_cast<void *>(*pdwKeyLo), sgpCDKey, sgpCDKey_len);
  }
  if (pdwKeyHi) {
    *pdwKeyHi = reinterpret_cast<DWORD_PTR>(SMemAlloc(sgpOwner_len, "grid.cpp", 0x46, 0));
    memcpy(reinterpret_cast<void *>(*pdwKeyHi), sgpOwner, sgpOwner_len);
  }
  if (pdwKeyLo) {
    if (!decode_key_block(reinterpret_cast<char *>(*pdwKeyLo), sgpCDKey_len, szPass)) {
      SMemFree(reinterpret_cast<void *>(*pdwKeyLo), "grid.cpp", 0x4D, 0);
      *pdwKeyLo = 0;
    }
  }
  if (pdwKeyHi) {
    if (!decode_key_block(reinterpret_cast<char *>(*pdwKeyHi), sgpOwner_len, szPass)) {
      SMemFree(reinterpret_cast<void *>(*pdwKeyHi), "grid.cpp", 0x50, 0);
      *pdwKeyHi = 0;
    }
  }
}

// 0x463710. Answers whether the named file decrypts, under the CD key, to the
// text handed in.
int check_genuine(const char *pszFile, const char *pszExpect)
{
  DWORD dwSize;
  int nGenuine = 0;
  char *pszKey = NULL;

  void *pvFile = MemFileLoad(pszFile, 0, &dwSize, 1, 0, "grid.cpp", 0x89);
  if (pvFile) {
    if (dwSize != 0) {
      RetrieveKey(reinterpret_cast<DWORD_PTR *>(&pszKey), 0);
      if (pszKey) {
        DWORD dwDec = DecodeFile(pvFile, dwSize, reinterpret_cast<intptr_t>(pszKey));
        if (dwDec <= dwSize)
          nGenuine = (SStrCmp(static_cast<char *>(pvFile), pszExpect, dwDec) == 0);
      }
    }
    SMemFree(pvFile, "grid.cpp", 0xA0, 0);
  }
  if (pszKey)
    wipe_and_free(pszKey, 0x5C);
  return nGenuine;
}

// 0x4637D0. Releases the raw key files init_cdkey loaded.
void free_cdkey()
{
  if (sgpCDKey) {
    mem_free(sgpCDKey, "grid.cpp", 0xD5);
    sgpCDKey = NULL;
  }
  if (sgpOwner) {
    mem_free(sgpOwner, "grid.cpp", 0xD9);
    sgpOwner = NULL;
  }
}

// 0x463820. Wipes and releases the two decrypted strings RetrieveKey handed out.
void cleanup_key(DWORD_PTR dwKeyLo, DWORD_PTR dwKeyHi)
{
  if (dwKeyLo)
    wipe_and_free(reinterpret_cast<char *>(dwKeyLo), 0x5C);
  if (dwKeyHi)
    wipe_and_free(reinterpret_cast<char *>(dwKeyHi), 0x61);
}

// 0x463880. Fills the cell IsBroodwar reads.
void CheckIsBroodwar()
{
  isBroodwar = FileExists(reinterpret_cast<intptr_t>("rez\\epilogX.txt"), 0);
}

// 0x4638A0. Loads both key files and decides whether this is a spawned copy. A
// file that loads to zero length is dropped.
void init_cdkey()
{
  char szPath[0x104];

  // Hyperion: a StarCraft: Remastered install has no key files and is the whole game,
  // never a spawned copy; its fonts need no key (port_remastered.cpp)
  if (PortGameSource() == PORT_SOURCE_REMASTERED) {
    gbSpawnedCopy_g = 0;
    return;
  }

  psnprintf(szPath, 0x104, "%s\\%s.gid", "font", "font");
  sgpCDKey = static_cast<char *>(load_key_file(szPath, &sgpCDKey_len));

  psnprintf(szPath, 0x104, "%s\\%s.clh", "font", "font");
  sgpOwner = static_cast<char *>(load_key_file(szPath, &sgpOwner_len));

  psnprintf(szPath, 0x104, "%s\\%s.ccd", "font", "font");
  gbSpawnedCopy_g = !check_genuine(szPath, "sgubon");
}
