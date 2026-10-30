// gamedata.cpp -- the game data file layer: the archive-backed open, read and
// close wrappers, the asynchronous load queue, and the GRP, overlay and bitmap
// loaders. Functions are in oracle VA order.
//
// Eleven of them are declared in other headers (gamedata.h lists them), and the
// definitions here match those spellings.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gamedata.h"
#include "../h/glues.h"
#include "../h/snd.h"
#include "../h/CSprite.h"
#include "../h/event_Win.h"
#include "../h/save_dlg.h"
#include "../h/dlg_dlg.h"
#include "../h/Storm.h"
#include "../port/data/port_formats.h"  // Hyperion: PortGroupNative
#include "../port/platform/port_standalone.h"  // Hyperion: PortDataStore

#include <windows.h>
#include <string.h>

// The Win32 imports, called through 1.17's IAT: GetLastError (0x4EB17C),
// CloseHandle, WaitForMultipleObjects, Leave- and EnterCriticalSection, and
// Sleep (0x4EB2F8). windows.h declares them dllimport __stdcall; GetLastError
// is declared again as src117/lang/replay.cpp spells it.
extern "C" __declspec(dllimport) DWORD __stdcall GetLastError(void);

// Storm imports, spelled as the files that first declared them do (creep.cpp,
// maphdr.cpp, net_cmds.cpp, saveload.cpp). Storm exports them by ordinal only,
// so they are `extern "C"`, for the undecorated _Name@N symbol. SMemAlloc,
// SMemFree, SStrCopy and SStrPack come from src117/h/Storm.h.
extern "C" {
BOOL __stdcall SFileOpenFile(const char *pszFileName, HANDLE *phFile);
DWORD __stdcall SFileGetFileSize(HANDLE hFile, DWORD *pdwSizeHigh);
BOOL __stdcall SFileReadFile(HANDLE hFile, void *pBuffer, DWORD dwToRead,
                             DWORD *pdwRead, void *pOverlapped);
BOOL __stdcall SFileCloseFile(HANDLE hFile);
BOOL __stdcall SFileOpenFileEx(HANDLE hMpq, const char *pszFileName,
                               DWORD dwSearchScope, HANDLE *phFile);
DWORD __stdcall SErrGetLastError();
char *__stdcall SStrChrR(const char *pszStr, char chFind);
}

// Four Storm imports first used here, by ordinal: SBmpAllocLoadImage (325),
// SBmpLoadImage (323), SFileGetFileName (276) and SFileSetIoErrorMode (274),
// through the thunks at 0x4D00A6, 0x4D00AC, 0x4D00B8 and 0x4D00D6. All are
// __stdcall; the pointee types are Storm's convention, the widths 1.17's.
typedef void *(__stdcall *PFN_BMPALLOC)(int nSize);

extern "C" {
BOOL __stdcall SFileGetFileName(HANDLE hFile, char *pszBuffer, DWORD cchBuffer);
BOOL __stdcall SFileSetIoErrorMode(DWORD dwMode, void *pfnHandler);
BOOL __stdcall SBmpLoadImage(const char *pszFileName, void *pPalette,
                             void *pBuffer, DWORD dwBufferSize, DWORD *pdwWidth,
                             DWORD *pdwHeight, DWORD *pdwBpp);
// Hyperion: dwUnused is where Storm writes the depth, and the game passes 0;
// pointer-wide for the x64 build.
BOOL __stdcall SBmpAllocLoadImage(const char *pszFileName, void *pPalette,
                                  void **ppBuffer, DWORD *pdwWidth,
                                  DWORD *pdwHeight, DWORD_PTR dwUnused,
                                  DWORD dwBufferSize, PFN_BMPALLOC pfnAlloc);
}

// Game callees no other header declares.

// 0x4CEB90: ECX is the 0x104 path buffer, EDX the Win32 error code.
void __fastcall FileErrorDlg(const char *pszName, DWORD dwError);

// The globals this TU reaches, dual-spelled, one #ifdef per name:
//   0x6662C4  sgbLoadLowMemFile_g, the DWORD flag OpenMemVariantFile tests;
//   0x6662C8  gSFileCrit_g, a CRITICAL_SECTION;
//   0x6662E0  gszFailedAsyncFileName_g, char[0x104], the name the asynchronous
//             failure recorded, empty when there is none.

#ifndef OC117_W94A_SGBLOADLOWMEMFILE
#define OC117_W94A_SGBLOADLOWMEMFILE
#ifdef OC_DLL_MODE
#define sgbLoadLowMemFile_g (*reinterpret_cast<DWORD *>(OC_VA(0x6662C4u)))
#else
extern DWORD sgbLoadLowMemFile_g;
#endif
#endif

#ifndef OC117_W94A_GSFILECRIT
#define OC117_W94A_GSFILECRIT
#ifdef OC_DLL_MODE
#define gSFileCrit_g (*reinterpret_cast<CRITICAL_SECTION *>(OC_VA(0x6662C8u)))
#else
extern CRITICAL_SECTION gSFileCrit_g;
#endif
#endif

#ifndef OC117_W94A_GSZFAILEDASYNCFILENAME
#define OC117_W94A_GSZFAILEDASYNCFILENAME
#ifdef OC_DLL_MODE
#define gszFailedAsyncFileName_g (reinterpret_cast<char *>(OC_VA(0x6662E0u)))
#else
extern char gszFailedAsyncFileName_g[0x104];
#endif
#endif

// Types local to this TU.

// load_array_data's descriptor table, array_data_t, is in src117/h/gamedata.h.

// wait_object_loading's object array: 20-byte entries with the event handle at
// +0x10. The first four dwords are not used here.
struct gds_async_obj_t
{
  /*0x00*/ DWORD dwUnknown00;
  /*0x04*/ DWORD dwUnknown04;
  /*0x08*/ DWORD dwUnknown08;
  /*0x0C*/ DWORD dwUnknown0C;
  /*0x10*/ HANDLE hEvent;
};
static_assert(offsetof(gds_async_obj_t, hEvent) == 0x10, "gds_async_obj_t::hEvent");
OC_X86_LAYOUT(sizeof(gds_async_obj_t) == 0x14, "gds_async_obj_t size");

// alloc_load_bmp's output record: two WORDs, stored as words, and
// SBmpAllocLoadImage's buffer at +4.
struct bmp_load_out_t
{
  /*0x00*/ WORD wWidth;
  /*0x02*/ WORD wHeight;
  /*0x04*/ void *pBits;
};
static_assert(offsetof(bmp_load_out_t, wWidth) == 0x00, "bmp_load_out_t::wWidth");
static_assert(offsetof(bmp_load_out_t, wHeight) == 0x02, "bmp_load_out_t::wHeight");
OC_X86_LAYOUT(offsetof(bmp_load_out_t, pBits) == 0x04, "bmp_load_out_t::pBits");
OC_X86_LAYOUT(sizeof(bmp_load_out_t) == 0x08, "bmp_load_out_t size");

#include "../gen/globals117.h"

// The bodies, in oracle VA order.

// load_array_data -- oracle 0x4442B0 (233 B). Reads a file whole and scatters
// it into a NULL-terminated table of {dest, count, elemsize} records, then frees
// the staging buffer. The failure path sets dwRemain = 0 and pBuf = NULL and
// falls into the same scatter loop.
void load_array_data(const char *pszFile, void *pArrayData)
{
  HANDLE hFile;
  BYTE *pSrc;
  void *pBuf;
  DWORD dwRemain;
  array_data_t *pEntry;
  void *pDest;

  if (!SFileOpenFileEx(NULL, pszFile, 0, &hFile))
  {
    FileErrorDlg(pszFile, SErrGetLastError());
    dwRemain = 0;
    pBuf = NULL;
  }
  else
  {
    // The size gets its own local and becomes the remainder only after the
    // close, as 1.17 orders it.
    const DWORD dwSize = GetFileSize_gds(reinterpret_cast<intptr_t>(hFile), 0);
    if (dwSize == 0)
      FileErrorDlg(pszFile, 0x18);
    pBuf = SMemAlloc(dwSize, "gamedata.cpp", 0x164, 0);
    ReadFile_gds(reinterpret_cast<intptr_t>(hFile), reinterpret_cast<intptr_t>(pBuf),
                 dwSize, 0);
    SFileCloseFile(hFile);
    dwRemain = dwSize;
  }

  pEntry = reinterpret_cast<array_data_t *>(pArrayData);
  pSrc = reinterpret_cast<BYTE *>(pBuf);
  while ((pDest = pEntry->pDest) != NULL)
  {
    DWORD dwBytes = pEntry->dwElemSize * pEntry->dwCount;
    if (dwBytes > dwRemain)
      break;
    // Hyperion: the column goes into its destination's own layout, where the
    // engine keeps it wider: a column of string ids that str_load_table_ex turns into
    // pointers in place. Elsewhere a memcpy, as it was.
    PortDataStore(pDest, pSrc, dwBytes);
    pSrc += dwBytes;
    dwRemain -= dwBytes;
    pEntry++;
  }
  if (dwRemain != 0)
    FileErrorDlg(pszFile, 0x18);
  SMemFree(pBuf, "gamedata.cpp", 0x192, 0);
}

// sub_4443A0 -- oracle 0x4443A0 (27 B). load_game_data with this TU's owner tag
// and a throwaway size slot; both register arguments are only passed on.
intptr_t sub_4443A0(intptr_t nArg0, intptr_t nArg1)
{
  DWORD dwSize;

  return reinterpret_cast<intptr_t>(
      load_game_data(reinterpret_cast<const char *>(nArg0),
                     reinterpret_cast<void *>(nArg1), &dwSize, "gamedata.cpp",
                     0x23D));
}

// gamedata_free_data -- oracle 0x4443C0 (8 B).
// Walks a descriptor table that has nothing left to free in 1.17, so the loop
// compiles to nothing.
void gamedata_free_data(void *pGameData)
{
  for (array_data_t *p = static_cast<array_data_t *>(pGameData); p->pDest; ++p) {
  }
}

// OpenMemVariantFile -- oracle 0x4443D0 (153 B). Opens pszFile, first trying a
// "\lowm" sibling directory when the low-memory flag is set: the leading
// directory up to the last backslash, then "\lowm", then the tail from that
// backslash. A name with no backslash falls back to pszFile.
intptr_t OpenMemVariantFile(intptr_t nArg0)
{
  char szPath[0x104];
  HANDLE hFile;
  const char *pszFile = reinterpret_cast<const char *>(nArg0);

  if (sgbLoadLowMemFile_g != 0)
  {
    const char *pszTail = SStrChrR(pszFile, '\\');
    if (pszTail == NULL)
      pszTail = pszFile;
    SStrCopy(szPath, pszFile, static_cast<size_t>(pszTail - pszFile) + 1);
    SStrPack(szPath, "\\lowm", 0x104);
    SStrPack(szPath, pszTail, 0x104);
    if (SFileOpenFile(szPath, &hFile))
      return reinterpret_cast<intptr_t>(hFile);
  }
  if (!SFileOpenFile(pszFile, &hFile))
    FileErrorDlg(pszFile, SErrGetLastError());
  return reinterpret_cast<intptr_t>(hFile);
}

// CheckAnyAsyncFailed -- oracle 0x444470 (82 B). The main loop's poll of the
// asynchronous-IO failure slot: if sfile_io_err_cb recorded a name, it takes a
// copy under the lock and raises the dialog with error 0x0F outside it.
void CheckAnyAsyncFailed()
{
  char szName[0x104];

  if (gszFailedAsyncFileName_g[0] != 0)
  {
    EnterCriticalSection(&gSFileCrit_g);
    SStrCopy(szName, gszFailedAsyncFileName_g, 0x104);
    LeaveCriticalSection(&gSFileCrit_g);
    FileErrorDlg(szName, 0x0F);
  }
}

// InitFileErrorMode -- oracle 0x4444D0 (13 B). Installs sfile_io_err_cb as
// Storm's IO-error handler.
void InitFileErrorMode()
{
  SFileSetIoErrorMode(1, reinterpret_cast<void *>(sfile_io_err_cb));
}

// GameLoadImage -- oracle 0x4444E0 (51 B).
void GameLoadImage(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3, intptr_t nArg4,
                   intptr_t nArg5, intptr_t nArg6)
{
  if (!SBmpLoadImage(reinterpret_cast<const char *>(nArg0),
                     reinterpret_cast<void *>(nArg1),
                     reinterpret_cast<void *>(nArg2), nArg3,
                     reinterpret_cast<DWORD *>(nArg4),
                     reinterpret_cast<DWORD *>(nArg5),
                     reinterpret_cast<DWORD *>(nArg6)))
  {
    FileErrorDlg(reinterpret_cast<const char *>(nArg0), SErrGetLastError());
  }
}

// CloseFile -- oracle 0x444520 (7 B).
void CloseFile(intptr_t hFile)
{
  SFileCloseFile(reinterpret_cast<HANDLE>(hFile));
}

// GetFileSize_gds -- oracle 0x444530 (102 B). ReadGroupFile, LoadGroupFile and
// LoadOverlayFile have it inlined, with its 0x104 name buffer.
int GetFileSize_gds(intptr_t nArg0, intptr_t nArg1)
{
  char szName[0x104];
  DWORD dwSize;
  DWORD dwError;

  dwSize = SFileGetFileSize(reinterpret_cast<HANDLE>(nArg0),
                            reinterpret_cast<DWORD *>(nArg1));
  if (dwSize == 0xFFFFFFFFu)
  {
    dwError = GetLastError();
    if (!SFileGetFileName(reinterpret_cast<HANDLE>(nArg0), szName, 0x104))
      SStrCopy(szName, "*unknown*", 0x104);
    FileErrorDlg(szName, dwError);
  }
  return dwSize;
}

// OpenFile -- oracle 0x4445A0 (66 B). nArg2 is a quiet flag: when it is set,
// "file not found" (2) and Storm's 0x3EE raise no dialog; every other error
// still does.
BOOL OpenFile(intptr_t nArg0, intptr_t nArg1, int nArg2)
{
  DWORD dwError;

  if (SFileOpenFile(reinterpret_cast<const char *>(nArg0),
                    reinterpret_cast<HANDLE *>(nArg1)))
  {
    return 1;
  }
  dwError = SErrGetLastError();
  if (nArg2 == 0 || (dwError != 2 && dwError != 0x3EE))
    FileErrorDlg(reinterpret_cast<const char *>(nArg0), dwError);
  return 0;
}

// OpenFileEx -- oracle 0x4445F0 (72 B). OpenFile with SFileOpenFileEx (an
// archive handle and a search scope) in place of SFileOpenFile.
BOOL OpenFileEx(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3, int nArg4)
{
  DWORD dwError;

  if (SFileOpenFileEx(reinterpret_cast<HANDLE>(nArg3),
                      reinterpret_cast<const char *>(nArg0), nArg4,
                      reinterpret_cast<HANDLE *>(nArg1)))
  {
    return 1;
  }
  dwError = SErrGetLastError();
  if (nArg2 == 0 || (dwError != 2 && dwError != 0x3EE))
    FileErrorDlg(reinterpret_cast<const char *>(nArg0), dwError);
  return 0;
}

// ReadFile_gds -- oracle 0x444640 (264 B). nArg3's own stack slot is
// SFileReadFile's bytes-read out-slot, while the value it held is kept for the
// two tests after, as 1.17 does it.
void ReadFile_gds(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3)
{
  char szName[0x104];
  DWORD_PTR dwOverlapped = static_cast<DWORD_PTR>(nArg3);  // Hyperion: DWORD cut the pointer at 64 bits
  DWORD dwError;

  if (!SFileReadFile(reinterpret_cast<HANDLE>(nArg0),
                     reinterpret_cast<void *>(nArg1),
                     static_cast<DWORD>(nArg2),
                     reinterpret_cast<DWORD *>(&nArg3),
                     reinterpret_cast<void *>(dwOverlapped)))
  {
    if (dwOverlapped != 0 && GetLastError() == 0x3E5)
      return;
    if (GetLastError() == 0x26)
    {
      if (!SFileGetFileName(reinterpret_cast<HANDLE>(nArg0), szName, 0x104))
        SStrCopy(szName, "*unknown*", 0x104);
      FileErrorDlg(szName, 0x18);
    }
    else
    {
      dwError = GetLastError();
      if (!SFileGetFileName(reinterpret_cast<HANDLE>(nArg0), szName, 0x104))
        SStrCopy(szName, "*unknown*", 0x104);
      FileErrorDlg(szName, dwError);
    }
  }
  if (dwOverlapped == 0 && nArg3 != nArg2)
  {
    if (!SFileGetFileName(reinterpret_cast<HANDLE>(nArg0), szName, 0x104))
      SStrCopy(szName, "*unknown*", 0x104);
    FileErrorDlg(szName, 0x18);
  }
}

// wait_object_loading -- oracle 0x444750 (189 B). Pumps the dialog dispatcher
// while up to 0x3F (63) event handles from a 20-byte-stride object array are
// outstanding, the WaitForMultipleObjects batch bound, then closes every event
// and its Storm file handle.
void wait_object_loading(void *pArg0, intptr_t nArg1, int nArg2)
{
  HANDLE ahWait[0x3F];
  gds_async_obj_t *pObj = reinterpret_cast<gds_async_obj_t *>(pArg0);
  HANDLE *phFiles = reinterpret_cast<HANDLE *>(nArg1);
  int nDone = 0;
  int nWait;
  int i;

  while (nDone < nArg2)
  {
    for (nWait = 0; static_cast<DWORD>(nWait) < 0x3F;)
    {
      HANDLE hEvent;
      if (nWait + nDone >= nArg2)
        break;
      hEvent = pObj[nWait + nDone].hEvent;
      if (hEvent == NULL)
        nDone++;
      else
        ahWait[nWait++] = hEvent;
    }
    do
    {
      RunEvtDispatcher(1);
      img_update();
    } while (WaitForMultipleObjects(nWait, ahWait, TRUE, 50) == WAIT_TIMEOUT);
    nDone += nWait;
  }

  for (i = 0; i < nArg2; i++)
  {
    if (pObj[i].hEvent != NULL)
    {
      CloseHandle(pObj[i].hEvent);
      if (phFiles[i] != NULL)
        SFileCloseFile(phFiles[i]);
    }
  }
  RunEvtDispatcher(1);
  img_update();
}

// AllocCB -- oracle 0x444810 (27 B). SBmpAllocLoadImage's allocator callback,
// passed by alloc_load_bmp, with this TU's own file and line as the owner tag.
void *__stdcall AllocCB(int nArg0)
{
  // Hyperion: returned, as Storm reads it (h/gamedata.h)
  return SMemAlloc(nArg0, "gamedata.cpp", 0x1B3, 0);
}

// sfile_io_err_cb -- oracle 0x444830 (87 B). Storm's IO-error handler. Retry
// counts 1..3 back off 10 ms and report handled; a count of 0 reports handled
// without sleeping; above 3 it records the failing name for CheckAnyAsyncFailed
// and gives up. The bound is unsigned, which the cast below keeps.
BOOL __stdcall sfile_io_err_cb(intptr_t nArg0, int nArg1, int nArg2)
{
  if (static_cast<DWORD>(nArg2) == 0)
    return 1;
  if (static_cast<DWORD>(nArg2) <= 3)
  {
    Sleep(10);
    return 1;
  }
  if (gszFailedAsyncFileName_g[0] == 0)
  {
    EnterCriticalSection(&gSFileCrit_g);
    SStrCopy(gszFailedAsyncFileName_g, reinterpret_cast<const char *>(nArg0),
             0x104);
    LeaveCriticalSection(&gSFileCrit_g);
  }
  return 0;
}

// load_game_data -- oracle 0x444890 (156 B). The whole-file loader. With a
// pBuffer it reads into it; with NULL it allocates with this TU's owner tag, and
// the caller's pszTag and nLine are ignored, as in 1.17.
void *load_game_data(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine)
{
  HANDLE hFile;
  DWORD dwSize;

  if (!SFileOpenFileEx(NULL, pszFile, 0, &hFile))
  {
    FileErrorDlg(pszFile, SErrGetLastError());
    if (pdwSize != NULL)
      *pdwSize = 0;
    return NULL;
  }
  dwSize = GetFileSize_gds(reinterpret_cast<intptr_t>(hFile), 0);
  if (pdwSize != NULL)
    *pdwSize = dwSize;
  if (dwSize == 0)
    FileErrorDlg(pszFile, 0x18);
  if (pBuffer == NULL)
    pBuffer = SMemAlloc(dwSize, "gamedata.cpp", 0xD2, 0);
  ReadFile_gds(reinterpret_cast<intptr_t>(hFile), reinterpret_cast<intptr_t>(pBuffer),
               dwSize, 0);
  SFileCloseFile(hFile);
  return pBuffer;
}

// alloc_load_bmp -- oracle 0x444930 (89 B). glue_load_cd_dlg installs it as
// dlg_load's PFN_DLGMEASURE.
void alloc_load_bmp(const void *pData, void *pOut, DWORD_PTR dwZero, const void *pA, DWORD dwB)
{
  DWORD dwWidth;
  DWORD dwHeight;
  void *pBits;
  bmp_load_out_t *pDest = reinterpret_cast<bmp_load_out_t *>(pOut);

  if (!SBmpAllocLoadImage(reinterpret_cast<const char *>(pData),
                          reinterpret_cast<void *>(dwZero), &pBits, &dwWidth,
                          &dwHeight, 0, 0, AllocCB))
  {
    FileErrorDlg(reinterpret_cast<const char *>(pData), SErrGetLastError());
  }
  pDest->wWidth = static_cast<WORD>(dwWidth);
  pDest->wHeight = static_cast<WORD>(dwHeight);
  pDest->pBits = pBits;
}

// MemFileLoad -- oracle 0x444990 (197 B). load_game_data with the caller's own
// owner tag passed to SMemAlloc, and nFlags making both the open failure and an
// empty file quiet (an empty file closes the handle and returns NULL instead of
// raising 0x18).
void *__fastcall MemFileLoad(const char *pszFile, DWORD_PTR dwArg2, DWORD *pdwSize,
                             int nFlags, int nScope, const char *pszOwnerFile,
                             int nOwnerLine)
{
  HANDLE hFile;
  DWORD dwSize;
  void *pBuffer = reinterpret_cast<void *>(dwArg2);
  DWORD dwError;

  if (!SFileOpenFileEx(NULL, pszFile, nScope, &hFile))
  {
    dwError = SErrGetLastError();
    if (nFlags == 0 || (dwError != 2 && dwError != 0x3EE))
      FileErrorDlg(pszFile, dwError);
    if (pdwSize != NULL)
      *pdwSize = 0;
    return NULL;
  }
  dwSize = GetFileSize_gds(reinterpret_cast<intptr_t>(hFile), 0);
  if (pdwSize != NULL)
    *pdwSize = dwSize;
  if (dwSize == 0)
  {
    if (nFlags != 0)
    {
      SFileCloseFile(hFile);
      return NULL;
    }
    FileErrorDlg(pszFile, 0x18);
  }
  if (pBuffer == NULL)
    pBuffer = SMemAlloc(dwSize, pszOwnerFile, nOwnerLine, 0);
  ReadFile_gds(reinterpret_cast<intptr_t>(hFile), reinterpret_cast<intptr_t>(pBuffer),
               dwSize, 0);
  SFileCloseFile(hFile);
  return pBuffer;
}

// ReadGroupFile -- oracle 0x444A60 (174 B). GetFileSize_gds is inlined here, and
// its 0x104 buffer makes the frame 0x10C.
GROUP *__fastcall ReadGroupFile(const char *pszFile, intptr_t nFlags,
                                const char *pszTag, int nLine)
{
  HANDLE hFile;
  DWORD dwSize;
  void *pBuf;

  hFile = reinterpret_cast<HANDLE>(OpenMemVariantFile(
      reinterpret_cast<intptr_t>(pszFile)));
  dwSize = GetFileSize_gds(reinterpret_cast<intptr_t>(hFile), 0);
  if (nFlags != 0)
    *reinterpret_cast<DWORD *>(nFlags) = dwSize;
  if (dwSize == 0)
    FileErrorDlg(pszFile, 0x18);
  pBuf = SMemAlloc(dwSize, pszTag, nLine, 0);
  ReadFile_gds(reinterpret_cast<intptr_t>(hFile), reinterpret_cast<intptr_t>(pBuf),
               dwSize, 0);
  SFileCloseFile(hFile);
  // Hyperion: the group in the engine's layout, a frame table of its own at 64
  // bits; on x86 the file as it was read.
  return PortGroupNative(reinterpret_cast<GROUP *>(pBuf));
}

// LoadGroupFile -- oracle 0x444B10 (177 B). ReadGroupFile with a caller-owned
// buffer: nArg1 points at a buffer slot filled only while it is NULL, and the
// handle is returned still open. GetFileSize_gds is inlined here too.
intptr_t LoadGroupFile(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3, intptr_t nArg4,
                  int nArg5)
{
  HANDLE hFile;
  DWORD dwSize;
  void **ppBuf = reinterpret_cast<void **>(nArg1);

  hFile = reinterpret_cast<HANDLE>(OpenMemVariantFile(nArg0));
  dwSize = GetFileSize_gds(reinterpret_cast<intptr_t>(hFile), 0);
  if (nArg2 != 0)
    *reinterpret_cast<DWORD *>(nArg2) = dwSize;
  if (dwSize == 0)
    FileErrorDlg(reinterpret_cast<const char *>(nArg0), 0x18);
  if (*ppBuf == NULL)
    *ppBuf = SMemAlloc(dwSize, reinterpret_cast<const char *>(nArg4), nArg5, 0);
  ReadFile_gds(reinterpret_cast<intptr_t>(hFile), reinterpret_cast<intptr_t>(*ppBuf),
               dwSize, nArg3);
  return reinterpret_cast<intptr_t>(hFile);
}

// LoadOverlayFile -- oracle 0x444BD0 (204 B). LoadGroupFile with a plain
// SFileOpenFile in place of the low-memory search, GetFileSize_gds inlined as
// well.
intptr_t LoadOverlayFile(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3, intptr_t nArg4,
                    int nArg5)
{
  HANDLE hFile;
  DWORD dwSize;
  void **ppBuf = reinterpret_cast<void **>(nArg1);

  if (!SFileOpenFile(reinterpret_cast<const char *>(nArg0), &hFile))
  {
    FileErrorDlg(reinterpret_cast<const char *>(nArg0), SErrGetLastError());
  }
  dwSize = GetFileSize_gds(reinterpret_cast<intptr_t>(hFile), 0);
  if (nArg2 != 0)
    *reinterpret_cast<DWORD *>(nArg2) = dwSize;
  if (dwSize == 0)
    FileErrorDlg(reinterpret_cast<const char *>(nArg0), 0x18);
  if (*ppBuf == NULL)
    *ppBuf = SMemAlloc(dwSize, reinterpret_cast<const char *>(nArg4), nArg5, 0);
  ReadFile_gds(reinterpret_cast<intptr_t>(hFile), reinterpret_cast<intptr_t>(*ppBuf),
               dwSize, nArg3);
  return reinterpret_cast<intptr_t>(hFile);
}

// mem_free -- oracle 0x444CA0 (23 B). The file and line-tagged free.
void mem_free(void *pMem, const char *pszFile, int nLine)
{
  if (pMem != NULL)
    SMemFree(pMem, pszFile, nLine, 0);
}
