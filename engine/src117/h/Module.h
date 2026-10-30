// Module.h -- declarations for src117/lang/Module.cpp: walking a loaded module's
// PE headers to find its CodeView debug directory. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_MODULE_H
#define OC117_MODULE_H

#include "platform.h"
#include "Storm.h"   // the one <windows.h>: IMAGE_OPTIONAL_HEADER32 / 64

// 0x404D70. ECX addresses the optional header's magic word. Returns 1 for PE32
// (0x10B), 2 for PE32+ (0x20B) and 0 otherwise; GetModuleDebugInfo uses all 32
// bits.
int CheckPE32(const WORD *pwMagic);

// 0x404DA0. ECX is the optional header, EDX receives the debug directory's RVA
// and the stack slot its size. Both outs are written before the magic is
// checked, so a caller reads them after a false return too. BYTE, not bool: the
// magic failure clears only AL and the other exits set all 32 bits.
BYTE GetDebugDirectoryRVA(const IMAGE_OPTIONAL_HEADER32 *pOptionalHeader,
                          DWORD *pdwRva, DWORD *pdwSize);

// The record GetModuleDebugInfo fills, one per loaded module, which
// FormatModuleDebugInfo prints with the format at 0x4FDAF0. The type's name is
// invented; the layout is 1.17's.
struct MODULE_DEBUG_INFO {
  /*0x000*/ void  *pImageBase;
  /*0x004*/ DWORD dwSizeOfImage;
  /*0x008*/ char  szModuleName[0x100];
  /*0x108*/ char  szPdbPath[0x100];
  /*0x208*/ DWORD dwSignature;
  /*0x20C*/ GUID  guidSignature;
  /*0x21C*/ DWORD dwAge;
  /*0x220*/ DWORD dwTimeDateStamp;
  /*0x224*/ DWORD dwBuildStamp;

  MODULE_DEBUG_INFO();
};

// The fields EnumModuleData::EnumModuleData (0x403340) clears per element, in
// its order, the string terminators last. The GUID goes through a reference,
// which gives the array loop its second pointer at &Data3. ExecuteEnumCallback
// inlines the same stores for its local record.
inline MODULE_DEBUG_INFO::MODULE_DEBUG_INFO()
{
  pImageBase = NULL;
  dwSizeOfImage = 0;
  dwSignature = 0;
  GUID &guid = guidSignature;
  guid.Data1 = 0;
  guid.Data2 = 0;
  guid.Data3 = 0;
  guid.Data4[0] = 0;
  guid.Data4[1] = 0;
  guid.Data4[2] = 0;
  guid.Data4[3] = 0;
  guid.Data4[4] = 0;
  guid.Data4[5] = 0;
  guid.Data4[6] = 0;
  guid.Data4[7] = 0;
  dwAge = 0;
  dwTimeDateStamp = 0;
  dwBuildStamp = 0;
  szModuleName[0] = 0;
  szPdbPath[0] = 0;
}

static_assert(offsetof(MODULE_DEBUG_INFO, pImageBase)     == 0x000, "GetModuleDebugInfo 0x404ED0: mov dword ptr [edx], ecx");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, dwSizeOfImage)  == 0x004, "ctor 0x403340: mov dword ptr [edx+4h], 0h");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, szModuleName)   == 0x008, "ctor 0x403340: mov byte ptr [.. +8h], 0h");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, szPdbPath)      == 0x108, "ExtractDebugInfo 0x405500: lea eax, [ebx+108h] into strncpy_s(.., 100h, ..)");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, dwSignature)    == 0x208, "ExtractDebugInfo<CV_INFO_PDB20>: mov [ebx+208h], [esi+8h]");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, guidSignature)  == 0x20C, "ExtractDebugInfo: movdqu xmmword ptr [ebx+20Ch], xmm0");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, dwAge)          == 0x21C, "ExtractDebugInfo: mov [ebx+21Ch], [esi+14h]");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, dwTimeDateStamp)== 0x220, "GetModuleDebugInfo 0x404ED0: mov [ebx+220h], [edi+8h]");
OC_X86_LAYOUT(offsetof(MODULE_DEBUG_INFO, dwBuildStamp)   == 0x224, "GetModuleDebugInfo 0x404ED0: mov [ebx+224h], eax");
OC_X86_LAYOUT(sizeof(MODULE_DEBUG_INFO) == 0x228, "ExecuteEnumCallback 0x405010: imul eax, dword ptr [ebx], 228h");

// The two CodeView records GetFirstDebugInfo dispatches on, NB10 and RSDS.
struct CV_INFO_PDB20 {
  /*0x00*/ DWORD CvSignature;
  /*0x04*/ LONG  Offset;
  /*0x08*/ DWORD Signature;
  /*0x0C*/ DWORD Age;
  /*0x10*/ char  PdbFileName[1];
};

struct CV_INFO_PDB70 {
  /*0x00*/ DWORD CvSignature;
  /*0x04*/ GUID  Signature;
  /*0x14*/ DWORD Age;
  /*0x18*/ char  PdbFileName[1];
};

static_assert(offsetof(CV_INFO_PDB20, CvSignature) == 0x00, "0x405500: the NB10 tag the caller matched");
static_assert(offsetof(CV_INFO_PDB20, Offset)      == 0x04, "0x405500: the dword between the tag and the signature");
static_assert(offsetof(CV_INFO_PDB20, Signature)   == 0x08, "0x405500: mov [ebx+208h], [esi+8h]");
static_assert(offsetof(CV_INFO_PDB20, Age)         == 0x0C, "0x405500: mov [ebx+21Ch], [esi+0Ch]");
static_assert(offsetof(CV_INFO_PDB20, PdbFileName) == 0x10, "0x405500: lea edi, [esi+10h]");
static_assert(sizeof(CV_INFO_PDB20) == 0x14, "0x405500: push 14h into IsBadReadPtr");
static_assert(offsetof(CV_INFO_PDB70, CvSignature) == 0x00, "0x405570: the RSDS tag the caller matched");
static_assert(offsetof(CV_INFO_PDB70, Signature)   == 0x04, "0x405570: movdqu xmm0, xmmword ptr [esi+4h]");
static_assert(offsetof(CV_INFO_PDB70, Age)         == 0x14, "0x405570: mov [ebx+21Ch], [esi+14h]");
static_assert(offsetof(CV_INFO_PDB70, PdbFileName) == 0x18, "0x405570: lea edi, [esi+18h]");
static_assert(sizeof(CV_INFO_PDB70) == 0x1C, "0x405570: push 1Ch into IsBadReadPtr");

// 0x405500 / 0x405570, ECX the record and EDX the output, one specialization per
// record.
template <class T>
bool ExtractDebugInfo(const T *pRecord, MODULE_DEBUG_INFO *pInfo);
template <> bool ExtractDebugInfo<CV_INFO_PDB20>(const CV_INFO_PDB20 *pRecord, MODULE_DEBUG_INFO *pInfo);
template <> bool ExtractDebugInfo<CV_INFO_PDB70>(const CV_INFO_PDB70 *pRecord, MODULE_DEBUG_INFO *pInfo);

// 0x404E30. ECX the module base, EDX the debug directory, then its byte count
// and the record to fill. Returns whether one entry yielded a PDB name.
bool GetFirstDebugInfo(const BYTE *pImageBase, const IMAGE_DEBUG_DIRECTORY *pDirectory,
                       DWORD cbDirectory, MODULE_DEBUG_INFO *pInfo);

// 0x404ED0. ECX a loaded module's base, EDX the record to fill. True once the PE
// headers check out, whether or not a CodeView record was found.
bool GetModuleDebugInfo(const BYTE *pImageBase, MODULE_DEBUG_INFO *pInfo);

struct EnumModuleData;

// 0x405010. ECX is never read and no caller sets it. EDX is the collector, then
// the module handle and its wide path with a length.
bool ExecuteEnumCallback(int nUnused, EnumModuleData *pData, HMODULE hModule,
                         const WCHAR *pwszPath, int cchPath);


#endif  // OC117_MODULE_H
