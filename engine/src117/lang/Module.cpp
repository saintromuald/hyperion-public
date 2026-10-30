// Hyperion: not built. The game's crash reporter is gone from the port and the
// port's own (port_crash.cpp) stays; the file is kept so that later changes to
// the reconstruction still apply. An address of its functions in 1.17's data
// goes to PortOriginalCodeReached.
//
// Module.cpp -- walking a loaded module's PE headers to reach its CodeView debug
// directory. Bodies are in oracle VA order.

#include "../h/Module.h"
#include "../h/EnumModuleData.h"
#include "../gen/globals117.h"

// CheckPE32 -- oracle 0x404D70 (40 B). Which optional-header layout a module
// uses.
int CheckPE32(const WORD *pwMagic)
{
  WORD wMagic = *pwMagic;

  if (wMagic == 0x10B)
    return 1;

  return wMagic == 0x20B ? 2 : 0;
}

// GetDebugDirectoryRVA -- oracle 0x404DA0 (143 B), with CheckPE32 inlined as
// nKind. A BYTE return over an int helper: the magic failure sets only AL, the
// helper's exits all 32 bits. The size is stored in both tail arms, as in 1.17.
static __forceinline int DebugDirTail(const IMAGE_OPTIONAL_HEADER32 *pOptionalHeader,
                                      DWORD *pdwRva, DWORD *pdwSize, int nKind)
{
  *pdwRva = 0;
  *pdwSize = 0xFFFFFFFF;

  DWORD dwSize;

  if (nKind == 2) {
    const IMAGE_OPTIONAL_HEADER64 *pOpt64 =
        reinterpret_cast<const IMAGE_OPTIONAL_HEADER64 *>(pOptionalHeader);

    if (pOpt64->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
      return 0;

    *pdwRva = pOpt64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
    dwSize = pOpt64->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
  } else {
    if (pOptionalHeader->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG)
      return 0;

    *pdwRva = pOptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
    dwSize = pOptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
  }

  if (*pdwRva == 0) {
    *pdwSize = dwSize;
    if (dwSize != 0)
      return 0;
  } else {
    *pdwSize = dwSize;
    // Volatile: 1.17 reloads the size it just stored.
    if (*static_cast<volatile DWORD *>(pdwSize) < sizeof(IMAGE_DEBUG_DIRECTORY))
      return 0;
  }

  return 1;
}

BYTE GetDebugDirectoryRVA(const IMAGE_OPTIONAL_HEADER32 *pOptionalHeader,
                          DWORD *pdwRva, DWORD *pdwSize)
{
  WORD wMagic = pOptionalHeader->Magic;
  int nKind;

  if (wMagic == 0x10B)
    nKind = 1;
  else if (wMagic == 0x20B)
    nKind = 2;
  else
    return 0;

  return DebugDirTail(pOptionalHeader, pdwRva, pdwSize, nKind);
}

// GetFirstDebugInfo -- oracle 0x404E30 (156 B). The first CodeView entry, NB10
// or RSDS, that yields a PDB name.
bool GetFirstDebugInfo(const BYTE *pImageBase, const IMAGE_DEBUG_DIRECTORY *pDirectory,
                       DWORD cbDirectory, MODULE_DEBUG_INFO *pInfo)
{
  DWORD nEntries = cbDirectory / sizeof(IMAGE_DEBUG_DIRECTORY);

  for (DWORD i = 0; i < nEntries; i++) {
    const BYTE *pData = pImageBase + pDirectory[i].AddressOfRawData;

    if (pDirectory[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW)
      continue;

    DWORD cbData = pDirectory[i].SizeOfData;

    if (cbData < 4)
      continue;

    if (IsBadReadPtr(pData, cbData))
      continue;

    bool bGot;

    if (*reinterpret_cast<const DWORD *>(pData) == 0x3031424E) {
      bGot = ExtractDebugInfo<CV_INFO_PDB20>(
          reinterpret_cast<const CV_INFO_PDB20 *>(pData), pInfo);
    } else if (*reinterpret_cast<const DWORD *>(pData) == 0x53445352) {
      bGot = ExtractDebugInfo<CV_INFO_PDB70>(
          reinterpret_cast<const CV_INFO_PDB70 *>(pData), pInfo);
    } else {
      continue;
    }

    if (bGot)
      return true;
  }

  return false;
}

// GetModuleDebugInfo -- oracle 0x404ED0 (316 B). The header checks nest, each
// scope's `return false` last, as in 1.17. The build stamp is accumulated in the
// field, not a local; e_res2[6..9] holds Blizzard's own stamp.
bool GetModuleDebugInfo(const BYTE *pImageBase, MODULE_DEBUG_INFO *pInfo)
{
  pInfo->pImageBase = const_cast<BYTE *>(pImageBase);

  const IMAGE_DOS_HEADER *pDos =
      reinterpret_cast<const IMAGE_DOS_HEADER *>(pImageBase);

  if (!IsBadReadPtr(pImageBase, 0x40) && pDos->e_magic == IMAGE_DOS_SIGNATURE) {
    const IMAGE_NT_HEADERS32 *pNt =
        reinterpret_cast<const IMAGE_NT_HEADERS32 *>(pImageBase + pDos->e_lfanew);

    if (!IsBadReadPtr(pNt, 4) && pNt->Signature == IMAGE_NT_SIGNATURE &&
        !IsBadReadPtr(&pNt->FileHeader, sizeof(IMAGE_FILE_HEADER)) &&
        !IsBadReadPtr(&pNt->OptionalHeader, pNt->FileHeader.SizeOfOptionalHeader) &&
        CheckPE32(&pNt->OptionalHeader.Magic) != 0) {
      pInfo->dwSizeOfImage = pNt->OptionalHeader.SizeOfImage;
      pInfo->dwTimeDateStamp = pNt->FileHeader.TimeDateStamp;

      if (pDos->e_res2[8] == 0xB74F && pDos->e_res2[9] == 0x2D98) {
        pInfo->dwBuildStamp = pDos->e_res2[6] + (pDos->e_res2[7] << 16);

        if (pInfo->dwBuildStamp != 0)
          pInfo->dwBuildStamp *= 0xFDCA75BB;
      }

      DWORD dwRva = 0;
      DWORD cbDirectory;
      BYTE bFound;

      // cbOut is scoped to the block, so its stack slot can be reused.
      {
        DWORD cbOut = 0;

        bFound = GetDebugDirectoryRVA(&pNt->OptionalHeader, &dwRva, &cbOut);
        cbDirectory = cbOut;
      }

      if (bFound && dwRva != 0 && cbDirectory != 0) {
        const BYTE *pDirectory = pImageBase + dwRva;

        if (!IsBadReadPtr(pDirectory, cbDirectory) &&
            cbDirectory >= sizeof(IMAGE_DEBUG_DIRECTORY)) {
          GetFirstDebugInfo(pImageBase,
                            reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(pDirectory),
                            cbDirectory, pInfo);
        }
      }

      return true;
    }

    return false;
  }

  return false;
}

// ExecuteEnumCallback -- oracle 0x405010 (352 B). Narrows the module name to its
// basename, fills the record and appends it to the collector. The clamp compares
// unsigned, against sizeof - 1.
#ifndef OC_PAYLOAD
bool ExecuteEnumCallback(int nUnused, EnumModuleData *pData, HMODULE hModule,
                         const WCHAR *pwszPath, int cchPath)
{
  MODULE_DEBUG_INFO info;

  int nChars = WideCharToMultiByte(CP_UTF8, 0, pwszPath, cchPath,
                                   info.szModuleName, 0x100, NULL, NULL);

  if (nChars < 0)
    info.szModuleName[0] = 0;
  else if (nChars > sizeof(info.szModuleName) - 1)
    info.szModuleName[sizeof(info.szModuleName) - 1] = 0;
  else
    info.szModuleName[nChars] = 0;

  const char *pszName = info.szModuleName;
  const char *pszBase = NULL;
  const char *pszHit;

  if ((pszHit = strrchr(pszName, '/')) != NULL && pszHit > pszBase)
    pszBase = pszHit;

  if ((pszHit = strrchr(pszName, '\\')) != NULL && pszHit > pszBase)
    pszBase = pszHit;

  if ((pszHit = strrchr(pszName, ':')) != NULL && pszHit > pszBase)
    pszBase = pszHit;

  if (pszBase != NULL)
    pszBase++;
  else
    pszBase = pszName;

  memcpy(info.szModuleName, pszBase, sizeof(info.szModuleName));

  if (!GetModuleDebugInfo(reinterpret_cast<const BYTE *>(hModule), &info))
    return true;

  if (pData == NULL)
    return false;

  pData->aItems[pData->nCount] = info;
  pData->nCount++;

  if (pData->nCount < 0x100)
    return true;

  return false;
}
#endif

// ExtractDebugInfo<CV_INFO_PDB20> -- oracle 0x405500 (108 B). An NB10 record:
// the PDB path, age and timestamp signature; the GUID is cleared.
template <>
bool ExtractDebugInfo<CV_INFO_PDB20>(const CV_INFO_PDB20 *pRecord, MODULE_DEBUG_INFO *pInfo)
{
  if (IsBadReadPtr(pRecord, sizeof(*pRecord)))
    return false;

  if (IsBadStringPtrA(pRecord->PdbFileName, MAX_PATH))
    return false;

  strncpy_s(pInfo->szPdbPath, sizeof(pInfo->szPdbPath), pRecord->PdbFileName, (size_t)-1);
  pInfo->dwAge = pRecord->Age;
  pInfo->dwSignature = pRecord->Signature;
  memset(&pInfo->guidSignature, 0, sizeof(pInfo->guidSignature));
  return true;
}

// ExtractDebugInfo<CV_INFO_PDB70> -- oracle 0x405570 (101 B). An RSDS record:
// the PDB path, age and GUID signature.
template <>
bool ExtractDebugInfo<CV_INFO_PDB70>(const CV_INFO_PDB70 *pRecord, MODULE_DEBUG_INFO *pInfo)
{
  if (IsBadReadPtr(pRecord, sizeof(*pRecord)))
    return false;

  if (IsBadStringPtrA(pRecord->PdbFileName, MAX_PATH))
    return false;

  strncpy_s(pInfo->szPdbPath, sizeof(pInfo->szPdbPath), pRecord->PdbFileName, (size_t)-1);
  pInfo->dwAge = pRecord->Age;
  pInfo->guidSignature = pRecord->Signature;
  return true;
}
