// charactr.cpp -- single-player character file I/O. Bodies are in oracle VA order.

#include "../h/charactr.h"

#include <string.h>  // strrchr, memset
#include <time.h>    // _time64

#include "../h/Storm.h"     // SStrCopy / SStrPack
#include "../h/fd_user.h"   // pfopen / pfclose / CreateDirectoriesRecursive
#include "../h/gds_file.h"  // pfseek
#include "../h/gluCmpgn.h"  // ReadCharacterFile, seated there
#include "../h/net_cmds.h"  // is_bad_filename
#include "../h/saveload.h"  // compress / decompress
#include "../gen/globals117.h"

// SFileGetUserDataPath, Storm.dll ordinal 296 (thunk 0x4D006A), spelled exactly as
// src117/lang/gds_log.cpp and src117/lang/replay.cpp spell it. The sheet's name
// for 296 is SFileGetFileCompressedSize, which became SFileGetUserDataPath in 1.17.
extern "C" {
// Hyperion: as port_stormfile.cpp defines it
DWORD  __stdcall SFileGetUserDataPath(char *pszBuf, DWORD cchBuf, DWORD dwFlags);
}

// The on-disk character record. 0x70 bytes: memset(pRec, 0, 0x70) in
// CreateNewCharacterFile and ReadCharacterFile, and WriteCharacterFile
// compresses exactly 0x70 from its base.
struct charrec_t {
  /*0x00*/ DWORD dwMagic;     // compared against 0x112233 in ReadCharacterFile
  /*0x04*/ BYTE bExpansion;   // `setne al` on gbMultiPlayerMode, stored byte
  /*0x05*/ BYTE abPad[3];     // nothing in .text reaches 0x05..0x07
  /*0x08*/ DWORD dwStamp;     // `mov [esi+8], eax` straight off _time64
  /*0x0C*/ char szName[0x19]; // SStrCopy capacity 0x19 at +0x0C
  /*0x25*/ BYTE abRest[0x4B]; // to 0x70; ReadCharacterFile zeroes 0x34..0x6F
};
static_assert(offsetof(charrec_t, dwMagic) == 0x00, "charrec_t::dwMagic");
static_assert(offsetof(charrec_t, bExpansion) == 0x04, "charrec_t::bExpansion");
static_assert(offsetof(charrec_t, dwStamp) == 0x08, "charrec_t::dwStamp");
static_assert(offsetof(charrec_t, szName) == 0x0C, "charrec_t::szName");
static_assert(sizeof(charrec_t) == 0x70, "charrec_t: memset(pRec, 0, 0x70)");

// The bodies, in oracle VA order.

// 0x40D4F0. Build the per-user path for one character and delete it. The
// extension is picked with a `cmove`, so both spellings are materialised first
// and neither arm branches.
int __fastcall DeleteCharacterFile(const char *pszName)
{
  char szPath[0x104];

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, "characters\\", 0x104);
  SStrPack(szPath, pszName, 0x104);
  SStrPack(szPath, gbMultiPlayerMode != 0 ? ".mpc" : ".spc", 0x104);
  return DeleteFileA(szPath);
}

// 0x40D570. Walk the character directory and hand each readable record to the
// callback. A name of 0x19 characters or more is skipped, which is the same
// capacity SStrCopy uses for charrec_t::szName.
void __fastcall EnumCharacterFiles(PFN_ENUMCHARACTER pfn)
{
  char szPath[0x104];
  WIN32_FIND_DATAA fd;
  charrec_t rec;

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, "characters\\", 0x104);
  SStrPack(szPath, "*", 0x104);
  SStrPack(szPath, gbMultiPlayerMode != 0 ? ".mpc" : ".spc", 0x104);

  HANDLE hFind = FindFirstFileA(szPath, &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if ((fd.dwFileAttributes & 0x16) == 0) {
        const char *pDot = strrchr(static_cast<const char *>(fd.cFileName), '.');
        if (pDot != NULL)
          *const_cast<char *>(pDot) = 0;
        if (strlen(fd.cFileName) < 0x19 &&
            ReadCharacterFile(fd.cFileName, &rec) != 0)
          pfn(&rec);
      }
    } while (FindNextFileA(hFind, &fd) != 0);
    FindClose(hFind);
  }
}

// 0x40D680. Read one character record, falling back to a freshly initialised
// one whenever the file is missing, truncated or has the wrong magic. The short
// read is a second decompress of 0x34 bytes with the tail zeroed, which is how
// an older, smaller record is upgraded in place.
__declspec(noinline) int ReadCharacterFile(char *pszName, void *pOut)
{
  char szPath[0x104];
  charrec_t *pRec = static_cast<charrec_t *>(pOut);
  int nOk = 0;

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, "characters\\", 0x104);
  SStrPack(szPath, pszName, 0x104);
  SStrPack(szPath, gbMultiPlayerMode != 0 ? ".mpc" : ".spc", 0x104);

  void *pFile = pfopen(szPath, 1);
  if (pFile != NULL) {
    if (decompress(pRec, 0x70, pFile) == 0) {
      if (pfseek(pFile, 0, 0) != 0)
        goto close;
      if (decompress(pRec, 0x34, pFile) == 0)
        goto close;
      memset(&pRec->abRest[0x0F], 0, 0x3C);
    }
    if (pRec->dwMagic == 0x112233) {
      if (_stricmp(pszName, pRec->szName) != 0)
        SStrCopy(pRec->szName, pszName, 0x19);
      nOk = 1;
    }
  close:
    pfclose(pFile);
    if (nOk != 0)
      return nOk;
  }

  memset(pRec, 0, 0x70);
  SStrCopy(pRec->szName, pszName, 0x19);
  pRec->dwStamp = static_cast<DWORD>(_time64(NULL));
  pRec->bExpansion = gbMultiPlayerMode != 0;
  pRec->dwMagic = 0x112233;
  return nOk;
}

// 0x40D7C0. Initialise a record for a new name and write it. The length handed
// to is_bad_filename is an inline strlen plus one, counting the terminator.
int __fastcall CreateNewCharacterFile(const char *pszName, void *pRecOut)
{
  charrec_t *pRec = static_cast<charrec_t *>(pRecOut);

  if (is_bad_filename(pszName, (DWORD)(strlen(pszName) + 1), 0) != 0)
    return 0;

  memset(pRec, 0, 0x70);
  SStrCopy(pRec->szName, pszName, 0x19);
  pRec->dwStamp = static_cast<DWORD>(_time64(NULL));
  pRec->bExpansion = gbMultiPlayerMode != 0;
  pRec->dwMagic = 0x112233;
  return WriteCharacterFile(pRec);
}

// 0x40D830. Create the directory, then compress the record into its file. The
// path is built twice because the first build is consumed by the directory
// pass, which walks it destructively.
int __fastcall WriteCharacterFile(void *pRec)
{
  char szPath[0x104];
  charrec_t *p = static_cast<charrec_t *>(pRec);

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, "characters\\", 0x104);
  if (!CreateDirectoriesRecursive(szPath))
    return 0;

  SFileGetUserDataPath(szPath, 0x104, 1);
  SStrPack(szPath, "characters\\", 0x104);
  SStrPack(szPath, p->szName, 0x104);
  SStrPack(szPath, gbMultiPlayerMode != 0 ? ".mpc" : ".spc", 0x104);

  void *pFile = pfopen(szPath, 0x16);
  if (pFile == NULL)
    return 0;

  int nWritten = compress(p, 0x70, pFile);
  if (pfclose(pFile) != 0)
    nWritten = 0;
  return nWritten;
}
