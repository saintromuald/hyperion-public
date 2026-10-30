// port_stormfile.cpp -- Storm's file functions, the port's own: the SFile functions 1.17
// imports, with Storm's names and shapes, doing what storm.dll's do, read from its code. The
// MPQs are read by StormLib (port_stormlib.cpp); tools/porttest/storm_test.cpp compares the
// two. The search order, the scopes and the reads are docs/modules/data.md's "A file request,
// end to end", the Dda streams its "Sound".
//
// Kept from storm.dll besides: every open archive is searched, whatever flags it was opened
// with; '/' in a name is not '\', in StormLib's hash as in Storm's; SFileGetFileName and
// SFileGetArchiveName are one function. Not kept: data Storm cannot decompress is still read
// as StormLib reads it, and zlib and bzip2 sectors, which no 1.17 archive has, are read too.
// A read with a request is served at once, its event signalled (LoadGroups waits on it). The
// Dda streams are read on a thread of their own (DdaThread), which shares state under FileLock.
//
// One archive is no MPQ: a StarCraft: Remastered install (port_remastered.cpp), opened by the
// name PORT_REMASTERED_ARCHIVE, whose files are read whole into memory when opened.
#include <windows.h>
#include <SDL3/SDL.h>  // SDL_free, for Remastered's files
#include "port_dsound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <string>
#include "port_stormapi.h"
#include "../platform/port_paths.h"  // PortLogFile
#include "port_stormlib.h"
#include "port_remastered.h"  // PortRemasteredLoad, PORT_REMASTERED_ARCHIVE

#include "port_hxm.h"  // PortHxmIsMap, PortHxmCompiled
#include "port_moddata.h"  // ModDataFilePath

static const DWORD SERR_INVALID = ERROR_INVALID_PARAMETER;
static const DWORD SERR_NOT_FOUND = ERROR_FILE_NOT_FOUND;
static const DWORD SERR_FILE_INVALID = 0x3EE;
static const DWORD SERR_NO_HEADER = 0x8510006C;
static const DWORD SERR_NOT_ON_CD = ERROR_INVALID_DRIVE;
static const DWORD SERR_DDA_NOT_WAVE = ERROR_INVALID_DATA;
static const DWORD SERR_DDA_NOT_ARCHIVED = 0x8510006F;
static const DWORD SERR_DDA_NO_SOUND = 0x85100071;
static const DWORD SERR_DDA_NO_STREAM = 0x85100072;
static const DWORD MPQ_FLAG_COMPRESSED = 0x0000FF00;
static const DWORD MPQ_FLAG_ENCRYPTED = 0x00010000;

static const DWORD RECORD_ARCHIVE = 0x48435241;  // "ARCH"
static const DWORD RECORD_FILE = 0x454C4946;     // "FILE"

// What an archive and a file have in common: the name SFileGetFileName and
// SFileGetArchiveName write, at the same place in both, as in storm.dll.
struct SFileRecord {
  DWORD dwKind;
  char szName[MAX_PATH];
};

struct SFileArchive : SFileRecord {
  SFileArchive *pNext;  // in search order
  DWORD dwPriority;
  void *hMpq;           // StormLib's
  int nRefs;            // the list's, while open, and each open file's
  // storm.dll's kind of archive: 0 serves a read by request at once, 1 (flag 2,
  // or a Dda stream begun on one of its files) and 2 (on a CD) on its worker
  DWORD dwReadKind;
  bool bRemastered;     // Remastered's install: no hMpq, and its files in memory
};

struct SFileFile : SFileRecord {
  HANDLE hDisk;         // a file on disk, or INVALID_HANDLE_VALUE
  SFileArchive *pArchive;
  void *hMpqFile;       // StormLib's, for a file in an archive
  DWORD dwPosition;
  BYTE *pMemory;        // a file of Remastered's, read whole (SDL_malloc's), or NULL
  DWORD cbMemory;
};

// A Dda stream. dwEnd is the data chunk's size, which storm.dll compares with
// positions in the file, where the data starts at dwStart; so a stream ends that
// many bytes before the data does, and this keeps that, since SFileDdaGetPos
// gives the game the difference.
struct DdaStream {
  DdaStream *pNext;
  SFileFile *pFile;
  DWORD dwState;           // DDA_FILLING, DDA_PRIMED, DDA_PLAYING or DDA_DRAINED
  DWORD dwWrite;           // where in the buffer the next piece goes
  DWORD cbSilence;         // silence written after the data, for SFileDdaGetPos
  DWORD dwStart, dwEnd;
  bool bLoop;
  BYTE bSilence;           // 0x80 for 8-bit samples, 0 otherwise
  LONG lVolume, lPan;
  IDirectSoundBuffer *pBuffer;
  DWORD cbBuffer;
};

typedef int(__stdcall *PFN_IOERROR)(const char *pszName, DWORD dwError, DWORD dwRetry);

static CRITICAL_SECTION g_fileLock;
static bool g_bFileLockReady;
static SFileArchive *g_pArchives;
static WORD g_wLocale;
static DWORD g_dwIoErrorMode = 2;
static PFN_IOERROR g_pfnIoError;
static char g_szAppDataDir[MAX_PATH];

static void FileLock(void)
{
  // The first call comes from the game's start-up, before any thread of its own.
  if (!g_bFileLockReady) {
    InitializeCriticalSection(&g_fileLock);
    g_bFileLockReady = true;
  }
  EnterCriticalSection(&g_fileLock);
}

static void FileUnlock(void)
{
  LeaveCriticalSection(&g_fileLock);
}

// The folder of the running exe with a backslash, where Storm looks for files on
// disk: the program's folder (on a Mac, the one Hyperion.app is in).
static void FileBasePath(char *pszPath)
{
  SStrPrintf(pszPath, MAX_PATH, "%s", PortProgramFolder());
}

// Whether Storm takes a name as a path of its own: from the root, or with a
// drive or a server.
static bool FileIsAbsolute(const char *pszName)
{
  return pszName[0] == '\\' || strstr(pszName, ":\\") != NULL || strstr(pszName, "\\\\") != NULL;
}

// The base path and pszName as a full path; FALSE when it would not fit, where
// storm.dll goes on with what its buffer held.
static bool FileUnderBase(const char *pszName, char *pszPath)
{
  char szBase[MAX_PATH], szJoined[2 * MAX_PATH];
  FileBasePath(szBase);
  SStrPrintf(szJoined, sizeof szJoined, "%s%s", szBase, pszName);
  const DWORD n = GetFullPathNameA(szJoined, MAX_PATH, pszPath, NULL);
  return n != 0 && n < MAX_PATH;
}

static bool FileIsPlainFile(const char *pszPath)
{
  const DWORD dwAttributes = GetFileAttributesA(pszPath);
  return dwAttributes != INVALID_FILE_ATTRIBUTES && !(dwAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

// storm.dll's test for an archive that must be on a CD (SFileOpenArchive's flag
// 1): a fingerprint of the drive, as it takes it.
static bool FileIsOnCd(const char *pszPath)
{
  char szRoot[4] = {pszPath[0], pszPath[0] ? pszPath[1] : '\0', 0, 0};
  if (szRoot[1])
    szRoot[2] = pszPath[2];
  const DWORD dwType = GetDriveTypeA(szRoot);
  DWORD dwFsFlags = 0;
  char szFsName[MAX_PATH] = {0};
  if (!GetVolumeInformationA(szRoot, NULL, 0, NULL, NULL, &dwFsFlags, szFsName, MAX_PATH))
    return false;
  DWORD dwSectorsPerCluster, dwBytesPerSector, dwFreeClusters, dwClusters;
  if (!GetDiskFreeSpaceA(szRoot, &dwSectorsPerCluster, &dwBytesPerSector, &dwFreeClusters, &dwClusters))
    return false;
  DWORD dwFsName;
  memcpy(&dwFsName, szFsName, 4);
  DWORD h = (dwFsFlags & 4) ^ dwFsName ^ dwBytesPerSector ^ dwFreeClusters ^ dwType;
  h ^= h >> 16;
  return static_cast<WORD>(h) == 0x1F00 || static_cast<WORD>(h) == 0x0805;
}

static void FileReleaseArchive(SFileArchive *pArchive)
{
  if (--pArchive->nRefs > 0)
    return;
  if (pArchive->hMpq)
    StormLibCloseArchive(pArchive->hMpq);
  SMemFree(pArchive, "SFile", 0, 0);
}

// A read of the disk, with the I/O error mode Storm has: 0 gives up, 1 asks the
// callback whether to try again, 2 asks the player, and more tries again.
static bool FileAskRetry(const char *pszName, DWORD dwError, DWORD *pdwRetry)
{
  switch (g_dwIoErrorMode) {
  case 0:
    return false;
  case 1:
    return g_pfnIoError && g_pfnIoError(pszName, dwError, (*pdwRetry)++) != 0;
  case 2: {
    char szText[MAX_PATH + 64];
    SStrPrintf(szText, sizeof szText, "Error %u reading %s.", dwError, pszName);
    return MessageBoxA(NULL, szText, "Hyperion", MB_RETRYCANCEL | MB_ICONERROR) == IDRETRY;
  }
  default:
    return true;
  }
}

static DWORD FileArchivedSize(SFileFile *pFile)
{
  return pFile->pMemory ? pFile->cbMemory : StormLibFileSize(pFile->hMpqFile);
}

// cb bytes of a file in an archive from dwPosition, or what is left of it.
static DWORD FileReadArchived(SFileFile *pFile, DWORD dwPosition, void *pBuffer, DWORD cb)
{
  const DWORD cbFile = FileArchivedSize(pFile);
  if (dwPosition >= cbFile)
    return 0;
  if (cb > cbFile - dwPosition)
    cb = cbFile - dwPosition;
  if (pFile->pMemory) {
    memcpy(pBuffer, pFile->pMemory + dwPosition, cb);
    return cb;
  }
  DWORD dwRetry = 0;
  for (;;) {
    DWORD cbRead = 0, dwError = 0;
    StormLibSeekFile(pFile->hMpqFile, dwPosition);
    if (StormLibReadFile(pFile->hMpqFile, pBuffer, cb, &cbRead, &dwError))
      return cbRead;
    // Data that does not decompress: storm.dll leaves those bytes as they were
    // and goes on, and so does this.
    if (dwError == ERROR_FILE_CORRUPT)
      return cb;
    if (!FileAskRetry(pFile->pArchive->szName, dwError, &dwRetry))
      return cbRead;
  }
}

// A file on disk; *pcbRead is what came, and TRUE only when all of cb did.
static BOOL FileReadDisk(SFileFile *pFile, void *pBuffer, DWORD cb, DWORD *pcbRead)
{
  DWORD dwRetry = 0;
  for (;;) {
    DWORD cbRead = 0;
    if (ReadFile(pFile->hDisk, pBuffer, cb, &cbRead, NULL)) {
      *pcbRead = cbRead;
      return cbRead == cb;
    }
    const DWORD dwError = GetLastError();
    if (!FileAskRetry(pFile->szName, dwError, &dwRetry)) {
      *pcbRead = cbRead;
      return FALSE;
    }
  }
}

// The scope SFileOpenFile and SFileLoadFile pass: none while an archive is
// open, the disk when none is. (storm.dll's ordinal 263 can set more; 1.17
// does not import it.)
static DWORD FileDefaultScope(void)
{
  return g_pArchives ? 0 : 1;
}

// The Dda streams.

static const DWORD DDA_PIECE = 0x4000;       // what is written ahead of the play cursor at a time
static const DWORD DDA_MIN_BUFFER = 0x8000;
static const LONG DDA_UNCHANGED = 0x7FFFFFFF; // a volume or pan left as it is
static const DWORD DDA_FLAG_FREQUENCY = 0x20;
static const DWORD DDA_FLAG_PAN = 0x40;
static const DWORD DDA_FLAG_LOOP = 0x40000;
static const DWORD DDA_PERIOD_MS = 50;

enum { DDA_FILLING, DDA_PRIMED, DDA_PLAYING, DDA_DRAINED };

static IDirectSound *g_pDirectSound;
static DdaStream *g_pStreams;
static HANDLE g_hDdaThread;
static HANDLE g_hDdaWake;
static BYTE g_abDdaPiece[DDA_PIECE];

// Under the lock.
static void DdaEnd(SFileFile *pFile)
{
  for (DdaStream **pp = &g_pStreams; *pp;) {
    DdaStream *pStream = *pp;
    if (pStream->pFile != pFile) {
      pp = &pStream->pNext;
      continue;
    }
    *pp = pStream->pNext;
    pStream->pBuffer->Release();
    SMemFree(pStream, "SFileDda", 0, 0);
  }
}

static DdaStream *DdaFind(SFileFile *pFile)
{
  DdaStream *pStream = g_pStreams;
  while (pStream && pStream->pFile != pFile)
    pStream = pStream->pNext;
  return pStream;
}

// Moves a file's position as storm.dll's parser does: back no further than the
// start, and on no further than the last byte.
static void DdaSeek(SFileFile *pFile, LONG lDistance)
{
  if (lDistance < 0 && pFile->dwPosition < static_cast<DWORD>(-lDistance))
    pFile->dwPosition = 0;
  else
    pFile->dwPosition += lDistance;
  const DWORD dwLast = FileArchivedSize(pFile) - 1;
  if (pFile->dwPosition > dwLast)
    pFile->dwPosition = dwLast;
}

// Reads chunk headers from the file's position on until dwId's, skipping each
// other chunk by its size; the chunk's size and where its body starts.
static bool DdaFindChunk(SFileFile *pFile, DWORD dwId, DWORD *pcbChunk, DWORD *pdwBody)
{
  DWORD adwHeader[2];
  if (!SFileReadFile(pFile, adwHeader, 8, NULL, NULL))
    return false;
  while (adwHeader[0] != dwId) {
    DdaSeek(pFile, static_cast<LONG>(adwHeader[1]));
    if (pFile->dwPosition == static_cast<DWORD>(-1))
      return false;
    if (!SFileReadFile(pFile, adwHeader, 8, NULL, NULL))
      return false;
  }
  *pcbChunk = adwHeader[1];
  DdaSeek(pFile, 0);
  *pdwBody = pFile->dwPosition;
  return *pdwBody != static_cast<DWORD>(-1);
}

// cb bytes of the file from dwPosition into the buffer at dwOffset. A piece
// that comes up short, of a stream that does not loop, is made up to 16 KB, or
// to the buffer's end, with silence. The buffer starts playing once the piece
// reaches the buffer's end or the stream's.
static void DdaFill(DdaStream *pStream, DWORD dwPosition, DWORD dwOffset, DWORD cb)
{
  if (cb == 0)
    return;
  const DWORD cbRead = FileReadArchived(pStream->pFile, dwPosition, g_abDdaPiece, cb);
  DWORD cbWindow = pStream->cbBuffer - dwOffset;
  if (cbWindow > DDA_PIECE)
    cbWindow = DDA_PIECE;
  const bool bPad = cbRead < cbWindow && !pStream->bLoop;
  void *pv = NULL;
  DWORD cbLocked = 0;
  if (pStream->pBuffer->Lock(dwOffset, bPad ? cbWindow : cb, &pv, &cbLocked, NULL, NULL, 0) == DS_OK && pv) {
    memcpy(pv, g_abDdaPiece, cbRead);
    if (bPad)
      memset(static_cast<BYTE *>(pv) + cbRead, pStream->bSilence, cbWindow - cbRead);
    pStream->pBuffer->Unlock(pv, cbLocked, NULL, 0);
  }
  if (pStream->dwState == DDA_PRIMED &&
      (dwPosition + cb >= pStream->cbBuffer || dwPosition - pStream->dwStart + cb >= pStream->dwEnd)) {
    pStream->pBuffer->Play(0, 0, DSBPLAY_LOOPING);
    pStream->dwState = DDA_PLAYING;
  }
}

// Bytes played since the buffer was last written to, from what the buffer says.
static DWORD DdaPlayedAhead(DdaStream *pStream)
{
  DWORD dwPlay = 0, dwWriteCursor = 0;
  pStream->pBuffer->GetCurrentPosition(&dwPlay, &dwWriteCursor);
  return (dwPlay - pStream->dwWrite) % pStream->cbBuffer;
}

// One pass of storm.dll's worker over a stream: the first fill, a piece where
// 16 KB have played, and at the data's end a loop back or silence. Whether it
// wrote anything or moved on to another state.
static bool DdaServiceStream(DdaStream *pStream)
{
  SFileFile *pFile = pStream->pFile;
  bool bMoved = false;
  if (pStream->dwState == DDA_DRAINED || (pStream->dwState == DDA_PLAYING && pFile->dwPosition >= pStream->dwEnd)) {
    if (pStream->bLoop) {
      pFile->dwPosition = pStream->dwStart;
    } else {
      if (DdaPlayedAhead(pStream) >= DDA_PIECE) {
        DWORD cb = pStream->cbBuffer - pStream->dwWrite;
        if (cb > DDA_PIECE)
          cb = DDA_PIECE;
        void *pv = NULL;
        DWORD cbLocked = cb;
        if (pStream->pBuffer->Lock(pStream->dwWrite, cb, &pv, &cbLocked, NULL, NULL, 0) == DS_OK) {
          if (pv)
            memset(pv, pStream->bSilence, cbLocked);
          pStream->pBuffer->Unlock(pv, cbLocked, NULL, 0);
          pStream->cbSilence += cbLocked;
          pStream->dwWrite += cbLocked;
          if (pStream->dwWrite >= pStream->cbBuffer)
            pStream->dwWrite -= pStream->cbBuffer;
          bMoved = true;
        }
      }
      bMoved |= pStream->dwState != DDA_DRAINED;
      pStream->dwState = DDA_DRAINED;
    }
  }
  if (pStream->dwState == DDA_PLAYING) {
    if (DdaPlayedAhead(pStream) >= DDA_PIECE) {
      DWORD cb = pStream->cbBuffer - pStream->dwWrite;
      if (cb > DDA_PIECE)
        cb = DDA_PIECE;
      if (cb >= pStream->dwEnd - pFile->dwPosition)
        cb = pStream->dwEnd - pFile->dwPosition;
      DdaFill(pStream, pFile->dwPosition, pStream->dwWrite, cb);
      pFile->dwPosition += cb;
      pStream->dwWrite += pStream->bLoop ? cb : DDA_PIECE;
      if (pStream->dwWrite >= pStream->cbBuffer)
        pStream->dwWrite -= pStream->cbBuffer;
      // a stream that loops on no data writes nothing, and storm.dll's worker
      // goes round it for ever
      bMoved |= cb != 0;
    }
  } else if (pStream->dwState == DDA_FILLING) {
    const DWORD cbFirst = pStream->dwEnd < pStream->cbBuffer ? pStream->dwEnd : pStream->cbBuffer;
    pStream->dwState = DDA_PRIMED;
    DWORD dwOffset = 0;
    while (dwOffset < cbFirst) {
      DWORD cb = cbFirst - dwOffset;
      if (cb > DDA_PIECE)
        cb = DDA_PIECE;
      DdaFill(pStream, pFile->dwPosition, dwOffset, cb);
      pFile->dwPosition += cb;
      dwOffset += cb;
    }
    pStream->dwWrite = dwOffset % pStream->cbBuffer;
    bMoved = true;
  }
  return bMoved;
}

extern "C" {

// Brings every stream up to its play cursor. The streams' thread calls it, and
// so can a test that has just moved a cursor.
void __cdecl FileDdaService(void)
{
  FileLock();
  for (DdaStream *pStream = g_pStreams; pStream; pStream = pStream->pNext) {
    // storm.dll's worker takes one pass at a time and comes back at once while it has
    // pieces to read; each pass that goes on writes a byte at least, so the bound is a guard
    for (DWORD n = pStream->cbBuffer + 4; n && DdaServiceStream(pStream); --n) {
    }
  }
  FileUnlock();
}

}  // extern "C"

// The streams' thread, started with the first stream and never stopped, as
// storm.dll's worker is not; with no stream it sleeps until one begins.
static DWORD WINAPI DdaThread(void *)
{
  for (;;) {
    WaitForSingleObject(g_hDdaWake, g_pStreams ? DDA_PERIOD_MS : INFINITE);
    FileDdaService();
  }
}

// A new archive, in the search by its priority. Under the lock.
static SFileArchive *FileListArchive(const char *pszPath, DWORD dwPriority, void *hMpq, DWORD dwReadKind,
                                     bool bRemastered)
{
  SFileArchive *pArchive = static_cast<SFileArchive *>(SMemAlloc(sizeof(SFileArchive), "SFile", 0, 8));
  pArchive->dwKind = RECORD_ARCHIVE;
  SStrCopy(pArchive->szName, pszPath, MAX_PATH);
  pArchive->dwPriority = dwPriority;
  pArchive->hMpq = hMpq;
  pArchive->nRefs = 1;
  pArchive->dwReadKind = dwReadKind;
  pArchive->bRemastered = bRemastered;
  // before the first archive of no higher priority, so the newest of equals first
  SFileArchive **ppAt = &g_pArchives;
  while (*ppAt && (*ppAt)->dwPriority > dwPriority)
    ppAt = &(*ppAt)->pNext;
  pArchive->pNext = *ppAt;
  *ppAt = pArchive;
  return pArchive;
}

extern "C" {

// Ordinal 266.
BOOL __stdcall SFileOpenArchive(const char *pszName, DWORD dwPriority, DWORD dwFlags, HANDLE *phMpq)
{
  if (phMpq)
    *phMpq = NULL;
  if (!phMpq || !pszName || !*pszName) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  FileLock();
  if (strcmp(pszName, PORT_REMASTERED_ARCHIVE) == 0) {
    *phMpq = FileListArchive(pszName, dwPriority, NULL, 0, true);
    FileUnlock();
    return TRUE;
  }
  char szPath[MAX_PATH];
  SStrCopy(szPath, pszName, MAX_PATH);
  if (!FileIsPlainFile(szPath) && !FileIsAbsolute(szPath))
    FileUnderBase(pszName, szPath);
  // A Hyperion map opens as the MPQ it compiles to (port_hxm.cpp), which is also the
  // name the archive keeps, since a raw read (SFileOpenFileEx's scope 4) reopens it.
  if (PortHxmIsMap(szPath)) {
    char szCompiled[MAX_PATH];
    if (!PortHxmCompiled(szPath, szCompiled, MAX_PATH)) {
      FileUnlock();
      SErrSetLastError(SERR_NO_HEADER);
      return FALSE;
    }
    SStrCopy(szPath, szCompiled, MAX_PATH);
  }
  const bool bOnCd = FileIsOnCd(szPath);
  if ((dwFlags & 1) && !bOnCd) {
    FileUnlock();
    SErrSetLastError(SERR_NOT_ON_CD);
    return FALSE;
  }
  // Storm opens the file itself first, and a failure there is Win32's alone.
  HANDLE hProbe = CreateFileA(szPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (hProbe == INVALID_HANDLE_VALUE) {
    FileUnlock();
    return FALSE;
  }
  CloseHandle(hProbe);
  DWORD dwError = 0;
  void *hMpq = StormLibOpenArchive(szPath, &dwError);
  if (!hMpq) {
    FileUnlock();
    SErrSetLastError(SERR_NO_HEADER);
    return FALSE;
  }
  *phMpq = FileListArchive(szPath, dwPriority, hMpq, bOnCd ? 2 : (dwFlags >> 1) & 1, false);
  FileUnlock();
  return TRUE;
}

// Ordinal 252. The archive leaves the search at once and closes when its last
// open file does. Always TRUE, as in storm.dll.
BOOL __stdcall SFileCloseArchive(HANDLE hMpq)
{
  SFileArchive *pArchive = static_cast<SFileArchive *>(hMpq);
  if (!pArchive || pArchive->dwKind != RECORD_ARCHIVE)
    return TRUE;
  FileLock();
  for (SFileArchive **pp = &g_pArchives; *pp; pp = &(*pp)->pNext)
    if (*pp == pArchive) {
      *pp = pArchive->pNext;
      break;
    }
  FileReleaseArchive(pArchive);
  FileUnlock();
  return TRUE;
}

// HYPERION_FILE_LOG=1: each name the game asks for, the first time, with where it was found
// (the disk, an archive's name, or "-" for nowhere), a line each in hyperion_files.log in
// the data folder (the list of what a StarCraft: Remastered install has to give).
// Called under FileLock.
static void FileLogOpen(const char *pszName, DWORD dwScope, const char *pszFrom)
{
  static int s_nOn = -1;
  static FILE *s_f = NULL;
  static std::set<std::string> *s_pSeen = NULL;
  if (s_nOn < 0) {
    const char *psz = getenv("HYPERION_FILE_LOG");
    s_nOn = psz && *psz && *psz != '0';
    if (s_nOn) {
      s_f = fopen(PortLogFile("hyperion_files.log"), "a");
      s_pSeen = new std::set<std::string>;
      s_nOn = s_f != NULL;
    }
  }
  if (!s_nOn)
    return;
  std::string key(pszName);
  for (char &c : key)
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c == '/' ? '\\' : c;
  if (!s_pSeen->insert(key).second)
    return;
  const char *pszLeaf = strrchr(pszFrom, '\\');
  fprintf(s_f, "%s\t%u\t%s\n", pszName, static_cast<unsigned>(dwScope), pszLeaf ? pszLeaf + 1 : pszFrom);
  fflush(s_f);
}

// Ordinal 268.
BOOL __stdcall SFileOpenFileEx(HANDLE hMpq, const char *pszName, DWORD dwScope, HANDLE *phFile)
{
  if (phFile)
    *phFile = NULL;
  if (!phFile || !pszName || !*pszName) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  FileLock();

  // the disk: a mod's sound or video in its folder (port_moddata.h); else the name's last part
  // in the base folder, then the name itself
  char szPath[MAX_PATH];
  bool bOnDisk = ModDataFilePath(pszName, szPath, sizeof szPath) && FileIsPlainFile(szPath);
  if (!bOnDisk && (dwScope & 2)) {
    const char *pszLeaf = strrchr(pszName, '\\');
    bOnDisk = FileUnderBase(pszLeaf ? pszLeaf + 1 : pszName, szPath) && FileIsPlainFile(szPath);
  }
  if (!bOnDisk && (dwScope & 1)) {
    if (FileIsAbsolute(pszName)) {
      SStrCopy(szPath, pszName, MAX_PATH);
      bOnDisk = FileIsPlainFile(szPath);
    } else {
      bOnDisk = FileUnderBase(pszName, szPath) && FileIsPlainFile(szPath);
    }
  }
  if (bOnDisk) {
    HANDLE hDisk = CreateFileA(szPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hDisk != INVALID_HANDLE_VALUE) {
      FileLogOpen(pszName, dwScope, "disk");
      if (dwScope & 4) {
        *phFile = hDisk;
      } else {
        SFileFile *pFile = static_cast<SFileFile *>(SMemAlloc(sizeof(SFileFile), "SFile", 0, 8));
        pFile->dwKind = RECORD_FILE;
        SStrCopy(pFile->szName, pszName, MAX_PATH);
        pFile->hDisk = hDisk;
        *phFile = pFile;
      }
      FileUnlock();
      return TRUE;
    }
  }

  // the archives: the one given, or each in search order
  SFileArchive *pArchive = NULL;
  void *hMpqFile = NULL;
  BYTE *pMemory = NULL;
  DWORD cbMemory = 0;
  SFileArchive *pOnly = static_cast<SFileArchive *>(hMpq);
  for (SFileArchive *p = pOnly ? pOnly : g_pArchives; p; p = pOnly ? NULL : p->pNext) {
    if (p->bRemastered) {
      unsigned long cb = 0;
      pMemory = static_cast<BYTE *>(PortRemasteredLoad(pszName, &cb));
      if (pMemory) {
        cbMemory = static_cast<DWORD>(cb);
        pArchive = p;
        break;
      }
      continue;
    }
    DWORD dwError = 0;
    hMpqFile = StormLibOpenFile(p->hMpq, pszName, g_wLocale, &dwError);
    if (hMpqFile) {
      pArchive = p;
      break;
    }
  }
  if (!hMpqFile && !pMemory) {
    FileLogOpen(pszName, dwScope, "-");
    FileUnlock();
    SErrSetLastError(SERR_NOT_FOUND);
    return FALSE;
  }
  FileLogOpen(pszName, dwScope, pArchive->szName);
  if ((dwScope & 4) && pMemory) {
    // a file of Remastered's has no file of its own to hand out
    SDL_free(pMemory);
    FileUnlock();
    SErrSetLastError(SERR_FILE_INVALID);
    return FALSE;
  }
  if (dwScope & 4) {
    // the archive's own file, at the data, for data stored as it is
    DWORD dwFlags, dwOffset;
    StormLibFileLayout(pArchive->hMpq, hMpqFile, &dwFlags, &dwOffset);
    StormLibCloseFile(hMpqFile);
    if (dwFlags & (MPQ_FLAG_COMPRESSED | MPQ_FLAG_ENCRYPTED)) {
      FileUnlock();
      SErrSetLastError(SERR_FILE_INVALID);
      return FALSE;
    }
    HANDLE hRaw = CreateFileA(pArchive->szName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    FileUnlock();
    if (hRaw == INVALID_HANDLE_VALUE)
      return FALSE;
    SetFilePointer(hRaw, static_cast<LONG>(dwOffset), NULL, FILE_BEGIN);
    *phFile = hRaw;
    return TRUE;
  }
  SFileFile *pFile = static_cast<SFileFile *>(SMemAlloc(sizeof(SFileFile), "SFile", 0, 8));
  pFile->dwKind = RECORD_FILE;
  SStrCopy(pFile->szName, pszName, MAX_PATH);
  pFile->hDisk = INVALID_HANDLE_VALUE;
  pFile->pArchive = pArchive;
  pFile->hMpqFile = hMpqFile;
  pFile->pMemory = pMemory;
  pFile->cbMemory = cbMemory;
  ++pArchive->nRefs;
  *phFile = pFile;
  FileUnlock();
  return TRUE;
}

// Ordinal 267.
BOOL __stdcall SFileOpenFile(const char *pszName, HANDLE *phFile)
{
  return SFileOpenFileEx(NULL, pszName, FileDefaultScope(), phFile);
}

// Ordinal 253. Always TRUE, as in storm.dll.
BOOL __stdcall SFileCloseFile(HANDLE hFile)
{
  SFileFile *pFile = static_cast<SFileFile *>(hFile);
  if (!pFile || pFile->dwKind != RECORD_FILE)
    return TRUE;
  FileLock();
  // storm.dll's stream holds its file open and plays on; the game ends a stream
  // before it closes the file, and this ends it with the file.
  DdaEnd(pFile);
  if (pFile->hDisk != INVALID_HANDLE_VALUE)
    CloseHandle(pFile->hDisk);
  if (pFile->hMpqFile)
    StormLibCloseFile(pFile->hMpqFile);
  if (pFile->pMemory)
    SDL_free(pFile->pMemory);
  if (pFile->pArchive)
    FileReleaseArchive(pFile->pArchive);
  pFile->dwKind = 0;
  SMemFree(pFile, "SFile", 0, 0);
  FileUnlock();
  return TRUE;
}

// Ordinal 269. pOverlapped is Storm's request: the position to read from at +8
// and an event to signal at +0x10, and the file's own position stays. From an
// archive of kind 0 it is read as any read is, and the event signalled; from a
// file on disk or another archive, storm.dll hands it to its worker and returns
// TRUE with nothing in *pdwRead, and this reads it at once and does the same.
BOOL __stdcall SFileReadFile(HANDLE hFile, void *pBuffer, DWORD cb, DWORD *pdwRead, void *pOverlapped)
{
  if (pdwRead)
    *pdwRead = 0;
  SFileFile *pFile = static_cast<SFileFile *>(hFile);
  if (!pBuffer || !pFile) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  BYTE *pRequest = static_cast<BYTE *>(pOverlapped);
  HANDLE hEvent = pRequest ? *reinterpret_cast<HANDLE *>(pRequest + 0x10) : NULL;
  if (cb == 0) {
    if (hEvent)
      SetEvent(hEvent);
    return TRUE;
  }
  FileLock();
  if (pFile->hDisk != INVALID_HANDLE_VALUE) {
    DWORD cbRead = 0;
    BOOL bOk;
    if (pRequest) {
      SetFilePointer(pFile->hDisk, *reinterpret_cast<LONG *>(pRequest + 8), NULL, FILE_BEGIN);
      FileReadDisk(pFile, pBuffer, cb, &cbRead);
      bOk = TRUE;
      cbRead = 0;
    } else {
      bOk = FileReadDisk(pFile, pBuffer, cb, &cbRead);
    }
    FileUnlock();
    if (pdwRead)
      *pdwRead = cbRead;
    if (hEvent)
      SetEvent(hEvent);
    return bOk;
  }
  if (pRequest && pFile->pArchive->dwReadKind != 0) {
    FileReadArchived(pFile, *reinterpret_cast<DWORD *>(pRequest + 8), pBuffer, cb);
    FileUnlock();
    if (hEvent)
      SetEvent(hEvent);
    return TRUE;
  }
  DWORD cbRead;
  if (pRequest) {
    cbRead = FileReadArchived(pFile, *reinterpret_cast<DWORD *>(pRequest + 8), pBuffer, cb);
  } else {
    cbRead = FileReadArchived(pFile, pFile->dwPosition, pBuffer, cb);
    pFile->dwPosition += cbRead;
  }
  FileUnlock();
  if (pdwRead)
    *pdwRead = cbRead;
  if (hEvent)
    SetEvent(hEvent);
  if (cbRead != cb) {
    SErrSetLastError(ERROR_HANDLE_EOF);
    return FALSE;
  }
  return TRUE;
}

// Ordinal 265. A file in an archive has no high half.
DWORD __stdcall SFileGetFileSize(HANDLE hFile, DWORD *pdwHigh)
{
  if (pdwHigh)
    *pdwHigh = 0;
  SFileFile *pFile = static_cast<SFileFile *>(hFile);
  if (!pFile)
    return INVALID_FILE_SIZE;
  if (pFile->hDisk != INVALID_HANDLE_VALUE)
    return GetFileSize(pFile->hDisk, pdwHigh);
  return FileArchivedSize(pFile);
}

// Ordinal 264. A file on disk has no archive, and that is TRUE.
BOOL __stdcall SFileGetFileArchive(HANDLE hFile, HANDLE *phMpq)
{
  if (!phMpq || !hFile) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  *phMpq = static_cast<SFileFile *>(hFile)->pArchive;
  return TRUE;
}

// Ordinals 276 and 275, one function in storm.dll: a file's name as it was
// opened, or an archive's path as it was opened.
BOOL __stdcall SFileGetFileName(HANDLE h, char *pszBuffer, DWORD cchBuffer)
{
  if (!pszBuffer || !h) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  *pszBuffer = '\0';
  SStrCopy(pszBuffer, static_cast<SFileRecord *>(h)->szName, cchBuffer);
  return TRUE;
}

BOOL __stdcall SFileGetArchiveName(HANDLE hMpq, char *pszBuffer, DWORD cchBuffer)
{
  return SFileGetFileName(hMpq, pszBuffer, cchBuffer);
}

// Ordinal 281. The block comes from SMemAlloc with cbExtra more bytes, zeroed,
// past the data; *pcbData does not count them.
BOOL __stdcall SFileLoadFileEx(HANDLE hMpq, const char *pszName, void **ppData, DWORD *pcbData, DWORD cbExtra,
                               DWORD dwScope, void *pOverlapped)
{
  if (!ppData) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  *ppData = NULL;
  if (!pszName) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  if (pcbData)
    *pcbData = 0;
  HANDLE hFile;
  // without scope 4, whose raw handle is not the SFile one read below
  if (!SFileOpenFileEx(hMpq, pszName, dwScope & ~4u, &hFile))
    return FALSE;
  SFileFile *pFile = static_cast<SFileFile *>(hFile);
  const DWORD cb = pFile->hDisk != INVALID_HANDLE_VALUE ? GetFileSize(pFile->hDisk, NULL)
                                                        : FileArchivedSize(pFile);
  void *pData = SMemAlloc(cbExtra + cb, "SFile", 0, 0);
  if (SFileReadFile(hFile, pData, cb, NULL, pOverlapped)) {
    if (cbExtra)
      SMemZero(static_cast<BYTE *>(pData) + cb, cbExtra);
    *ppData = pData;
    if (pcbData)
      *pcbData = cb;
  } else if (pData) {
    SMemFree(pData, "SFile", 0, 0);
  }
  SFileCloseFile(hFile);
  return *ppData != NULL;
}

// Ordinal 279.
BOOL __stdcall SFileLoadFile(const char *pszName, void **ppData, DWORD *pcbData, DWORD cbExtra, void *pOverlapped)
{
  return SFileLoadFileEx(NULL, pszName, ppData, pcbData, cbExtra, FileDefaultScope(), pOverlapped);
}

// Ordinal 280.
BOOL __stdcall SFileUnloadFile(void *pData)
{
  if (!pData) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  SMemFree(pData, "SFile", 0, 0);
  return TRUE;
}

// Ordinal 295. The game's folder, with the backslash when bSlash is given; the length
// written. storm.dll gives the running exe's folder, which in 1.17's install is the game's;
// Hyperion's may be elsewhere: HYPERION_GAME, which hyperion.cpp sets to the folder it found
// (the exe's when none is named), so the MPQs, maps and saves are read where it says. The last
// error is left as it was, as storm.dll leaves it, though the variable may be missing.
BOOL __stdcall SFileGetInstallPath(char *pszBuffer, DWORD cchBuffer, DWORD bSlash)
{
  char szPath[MAX_PATH + 1];
  const DWORD dwErr = GetLastError();
  DWORD n = GetEnvironmentVariableA("HYPERION_GAME", szPath, MAX_PATH);
  SetLastError(dwErr);
  if (n > 0 && n < MAX_PATH) {
    while (n > 0 && (szPath[n - 1] == '\\' || szPath[n - 1] == '/'))
      --n;
    if (bSlash)
      szPath[n++] = '\\';
    szPath[n] = '\0';
  } else {
    SStrCopy(szPath, PortProgramFolder(), MAX_PATH);
    n = lstrlenA(szPath);
    if (!bSlash && n > 0)
      szPath[n - 1] = '\0';
  }
  return static_cast<BOOL>(reinterpret_cast<UINT_PTR>(SStrCopy(pszBuffer, szPath, cchBuffer)));
}

// Ordinal 296. The folder SFileSetAppDataDir gave, with or without its
// backslash. As in storm.dll, the length returned is the one before a
// backslash is taken off, and the backslash is added within cchBuffer less
// the folder's length.
DWORD __stdcall SFileGetUserDataPath(char *pszBuffer, DWORD cchBuffer, DWORD bSlash)
{
  const int n = SStrPrintf(pszBuffer, cchBuffer, "%s", g_szAppDataDir);
  const bool bHasSlash = n > 0 && pszBuffer[n - 1] == '\\';
  if (bSlash) {
    if (bHasSlash)
      return static_cast<DWORD>(n);
    return static_cast<DWORD>(reinterpret_cast<UINT_PTR>(SStrPack(pszBuffer, "\\", cchBuffer - n)));
  }
  if (bHasSlash)
    pszBuffer[n - 1] = '\0';
  return static_cast<DWORD>(n);
}

// Ordinal 297. The folder is kept as given; the length kept is returned.
DWORD __stdcall SFileSetAppDataDir(const char *pszPath)
{
  return static_cast<DWORD>(reinterpret_cast<UINT_PTR>(SStrCopy(g_szAppDataDir, pszPath, MAX_PATH)));
}

// Ordinal 272. Returns what it was given.
DWORD __stdcall SFileSetLocale(DWORD dwLocale)
{
  g_wLocale = static_cast<WORD>(dwLocale);
  return dwLocale;
}

// Ordinal 274. Mode 0 fails a read that fails, 1 asks pfnCallback
// (int __stdcall (name, error, retry)) whether to try again, 2 (the default)
// asks the player, and more try again. Always TRUE.
BOOL __stdcall SFileSetIoErrorMode(DWORD dwMode, void *pfnCallback)
{
  g_dwIoErrorMode = dwMode;
  g_pfnIoError = reinterpret_cast<PFN_IOERROR>(pfnCallback);
  return TRUE;
}

// The Dda functions.

// Ordinal 260. The game keeps the DirectSound; storm.dll takes no reference.
BOOL __stdcall SFileDdaInitialize(IDirectSound *pDirectSound)
{
  if (!pDirectSound) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  g_pDirectSound = pDirectSound;
  return TRUE;
}

// Ordinal 256. Ends every stream and lets the DirectSound go. Always TRUE.
BOOL __stdcall SFileDdaDestroy(void)
{
  FileLock();
  while (g_pStreams)
    DdaEnd(g_pStreams->pFile);
  g_pDirectSound = NULL;
  FileUnlock();
  return TRUE;
}

// Ordinal 255. A WAV file of an archive, from dwStart bytes into its data, into
// a DirectSound buffer of cbBuffer rounded up to 16 KB and at least 32 KB.
// dwFlags 0x20 and 0x40 give the buffer frequency and pan control, and 0x40000
// loops the sound. A volume or pan of 0 or 0x7FFFFFFF leaves the buffer's.
// The seventh argument is not read, as in storm.dll.
BOOL __stdcall SFileDdaBeginEx(HANDLE hFile, DWORD cbBuffer, DWORD dwFlags, DWORD dwStart, LONG lVolume, LONG lPan,
                               DWORD)
{
  SFileFile *pFile = static_cast<SFileFile *>(hFile);
  if (!cbBuffer || !pFile) {
    SErrSetLastError(SERR_INVALID);
    return FALSE;
  }
  if (!g_pDirectSound) {
    SErrSetLastError(SERR_DDA_NO_SOUND);
    return FALSE;
  }
  FileLock();
  DdaEnd(pFile);
  if (pFile->hDisk != INVALID_HANDLE_VALUE || !pFile->pArchive) {
    FileUnlock();
    SErrSetLastError(SERR_DDA_NOT_ARCHIVED);
    return FALSE;
  }
  // from now on the archive's reads by request go to storm.dll's worker, and
  // SFileReadFile answers them as it does for such an archive
  if (pFile->pArchive->dwReadKind == 0)
    pFile->pArchive->dwReadKind = 1;
  cbBuffer = (cbBuffer + DDA_PIECE - 1) & ~(DDA_PIECE - 1);
  if (cbBuffer < DDA_MIN_BUFFER)
    cbBuffer = DDA_MIN_BUFFER;

  // RIFF, WAVE, the fmt chunk's first 16 bytes, then the data chunk. A read
  // that fails leaves its own error.
  pFile->dwPosition = 0;
  DWORD adwRiff[3];
  if (!SFileReadFile(pFile, adwRiff, sizeof adwRiff, NULL, NULL)) {
    FileUnlock();
    return FALSE;
  }
  DWORD cbFormat, dwFormat, cbData, dwData;
  if (adwRiff[0] != 0x46464952 || adwRiff[2] != 0x45564157 || !DdaFindChunk(pFile, 0x20746D66, &cbFormat, &dwFormat) ||
      cbFormat < 16) {
    FileUnlock();
    SErrSetLastError(SERR_DDA_NOT_WAVE);
    return FALSE;
  }
  PCMWAVEFORMAT format;
  if (!SFileReadFile(pFile, &format, sizeof format, NULL, NULL)) {
    FileUnlock();
    return FALSE;
  }
  DdaSeek(pFile, static_cast<LONG>(cbFormat - 16));
  if (pFile->dwPosition == static_cast<DWORD>(-1)) {
    FileUnlock();
    return FALSE;
  }
  if (!DdaFindChunk(pFile, 0x61746164, &cbData, &dwData)) {
    FileUnlock();
    SErrSetLastError(SERR_DDA_NOT_WAVE);
    return FALSE;
  }

  WAVEFORMATEX wfx;
  memcpy(&wfx, &format, sizeof format);
  wfx.cbSize = 0;
  DSBUFFERDESC1 desc;
  memset(&desc, 0, sizeof desc);
  desc.dwSize = sizeof desc;
  desc.dwFlags = DSBCAPS_CTRLVOLUME | (dwFlags & DDA_FLAG_FREQUENCY ? DSBCAPS_CTRLFREQUENCY : 0) |
                 (dwFlags & DDA_FLAG_PAN ? DSBCAPS_CTRLPAN : 0);
  desc.dwBufferBytes = cbBuffer;
  desc.lpwfxFormat = &wfx;
  IDirectSoundBuffer *pBuffer = NULL;
  g_pDirectSound->CreateSoundBuffer(reinterpret_cast<LPCDSBUFFERDESC>(&desc), &pBuffer, NULL);
  if (!pBuffer) {
    FileUnlock();
    return FALSE;
  }
  if (lVolume != 0 && lVolume != DDA_UNCHANGED)
    pBuffer->SetVolume(lVolume);
  if (lPan != 0 && lPan != DDA_UNCHANGED)
    pBuffer->SetPan(lPan);
  if (dwStart)
    DdaSeek(pFile, static_cast<LONG>(dwStart));

  DdaStream *pStream = static_cast<DdaStream *>(SMemAlloc(sizeof(DdaStream), "SFileDda", 0, 8));
  pStream->pFile = pFile;
  pStream->dwState = DDA_FILLING;
  pStream->dwStart = dwData + (dwStart < cbData - dwData ? dwStart : cbData - dwData);
  pStream->dwEnd = cbData;
  pStream->bLoop = (dwFlags & DDA_FLAG_LOOP) != 0;
  pStream->bSilence = wfx.wBitsPerSample == 8 ? 0x80 : 0;
  pStream->lVolume = lVolume;
  pStream->lPan = lPan;
  pStream->pBuffer = pBuffer;
  pStream->cbBuffer = cbBuffer;
  pStream->pNext = g_pStreams;
  g_pStreams = pStream;
  if (!g_hDdaWake)
    g_hDdaWake = CreateEventA(NULL, FALSE, FALSE, NULL);
  if (!g_hDdaThread)
    g_hDdaThread = CreateThread(NULL, 0, DdaThread, NULL, 0, NULL);
  FileUnlock();
  SetEvent(g_hDdaWake);
  return TRUE;
}

// Ordinal 257. Stops and frees the file's stream, if it has one. Always TRUE.
BOOL __stdcall SFileDdaEnd(HANDLE hFile)
{
  FileLock();
  DdaEnd(static_cast<SFileFile *>(hFile));
  FileUnlock();
  return TRUE;
}

// Ordinal 258. How far the stream has played and where it ends, in bytes of its
// data; 0 until it starts playing.
BOOL __stdcall SFileDdaGetPos(HANDLE hFile, DWORD *pdwPosition, DWORD *pdwEnd)
{
  if (pdwPosition)
    *pdwPosition = 0;
  if (pdwEnd)
    *pdwEnd = 0;
  FileLock();
  DdaStream *pStream = DdaFind(static_cast<SFileFile *>(hFile));
  if (!pStream) {
    FileUnlock();
    SErrSetLastError(SERR_DDA_NO_STREAM);
    return FALSE;
  }
  const DWORD cbTotal = pStream->dwEnd - pStream->dwStart;
  DWORD dwPosition = 0;
  if (pStream->dwState == DDA_PLAYING || pStream->dwState == DDA_DRAINED) {
    DWORD dwPlay = 0, dwWriteCursor = 0;
    pStream->pBuffer->GetCurrentPosition(&dwPlay, &dwWriteCursor);
    // what has gone into the buffer, and the play cursor in the same lap or the
    // one before
    const DWORD cbWritten = pStream->pFile->dwPosition + pStream->cbSilence - pStream->dwStart;
    dwPosition = cbWritten / pStream->cbBuffer * pStream->cbBuffer + dwPlay;
    if (dwPosition >= cbWritten)
      dwPosition = dwPosition > pStream->cbBuffer ? dwPosition - pStream->cbBuffer : 0;
    if (dwPosition >= cbTotal)
      dwPosition = cbTotal;
  }
  if (pdwPosition)
    *pdwPosition = dwPosition;
  if (pdwEnd)
    *pdwEnd = cbTotal;
  FileUnlock();
  return TRUE;
}

// Ordinal 261. DirectSound's hundredths of a decibel; 0x7FFFFFFF, or what the
// stream has already, leaves one be.
BOOL __stdcall SFileDdaSetVolume(HANDLE hFile, LONG lVolume, LONG lPan)
{
  FileLock();
  DdaStream *pStream = DdaFind(static_cast<SFileFile *>(hFile));
  if (!pStream) {
    FileUnlock();
    SErrSetLastError(SERR_DDA_NO_STREAM);
    return FALSE;
  }
  if (lVolume != DDA_UNCHANGED && lVolume != pStream->lVolume) {
    pStream->lVolume = lVolume;
    pStream->pBuffer->SetVolume(lVolume);
  }
  if (lPan != DDA_UNCHANGED && lPan != pStream->lPan) {
    pStream->lPan = lPan;
    pStream->pBuffer->SetPan(lPan);
  }
  FileUnlock();
  return TRUE;
}

}  // extern "C"
