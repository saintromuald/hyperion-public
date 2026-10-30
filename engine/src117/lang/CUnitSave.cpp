// CUnitSave.cpp -- the unit table save: which slots are live, and the encoded records. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the oracle (WPO
// off) never does across a TU; that is what the __declspec(noinline) markers on some
// declarations are for, and why moving a body between TUs can change the code generated.

#include "../h/CUnitSave.h"
#include "../port/data/port_formats.h"  // Hyperion: PortSave117Size, PortSave117Put, PortSaveChunk
#include "../gen/save117.h"
#include <string.h>
#include <vector>
#include "../h/CUnit.h"
#include "../h/CUnitInit.h"         // sub_4266D0
#include "../h/CUnitProtoss.h"      // RestorePsiLinks
#include "../h/port_hxsave.h"       // Hyperion: HxSaveBeforePsiLinks
#include "../h/CUnitSpells.h"       // SaveCloakList
#include "../h/saveload.h"          // compress, pfwrite
#include "../h/Storm.h"             // SMemAlloc, SMemFree
#include "../gen/globals117.h"

// The bodies, in oracle VA order.

// Marks in pUsed the slot of every unit on the four lists, and of each listed
// unit's subunit.
void sub_431280(BYTE *pUsed, int nCount) {
  CUnit *apLists[4];
  memset(pUsed, 0, nCount);
  apLists[0] = CUnit::GetUsedList();
  apLists[1] = CUnit::GetDeadList();
  apLists[2] = CUnit::GetTransList();
  apLists[3] = CUnit::GetScannerList();
  for (unsigned int i = 0; i < 4; i++) {
    for (CUnit *pUnit = apLists[i]; pUnit; pUnit = pUnit->pNext) {
      CUnit *pSub = pUnit->subunit;
      if (pSub && (gulUnitTypeFlags[pSub->wType] & 0x10))
        pUsed[pSub - CUnit::Head()] = 1;
      pUsed[pUnit - CUnit::Head()] = 1;
    }
  }
}

// Encodes (nMode set) or decodes every marked unit in place, last slot first,
// and returns how many were marked.
int saveload_unitlist(int nMode, BYTE *pUsed, int nCount) {
  if (nMode)
    sub_431280(pUsed, nCount);
  int nUsed = 0;
  for (int i = g_pools.units; i--;) {
    if (pUsed[i]) {
      nUsed++;
      if (nMode)
        CUnit::Head()[i].EncodeToSave(1);
      else
        CUnit::Head()[i].DecodeFromSave(1);
    }
  }
  return nUsed;
}

// Rebuilds the links a loaded unit table needs.
BOOL sub_4313C0() {
  sub_4266D0();
  if (!HxSaveBeforePsiLinks())  // Hyperion: a Hyperion save's free lists
    return FALSE;
  RestorePsiLinks();
  return TRUE;
}

// Writes pUnit's id to the file.
BOOL write_unit_id(CUnit *pUnit, void *pFile) {
  DWORD dwId = pUnit->GetUnitID();
  return pfwrite(&dwId, sizeof(dwId), pFile);
}

// Writes the unit table: the slot map, then each live unit's index and encoded
// record, then the list heads and tails. The units are decoded again before
// any failure returns. pOut must be set before the memset for the register
// assignment to match.
BOOL CUnit::WriteSave(void *pFile) {
  std::vector<BYTE> vUsed(g_pools.units);  // Hyperion: on the stack in 1.17
  BYTE *abUsed = vUsed.data();
  int nUsed = saveload_unitlist(1, abUsed, g_pools.units);
  BOOL bOk = compress(abUsed, g_pools.units, pFile);
  if (bOk) {
    bOk = pfwrite(&nUsed, sizeof(nUsed), pFile);
    if (bOk && nUsed) {
      DWORD dwSize = nUsed * (sizeof(DWORD) + PortSave117Size(SAVE117_CUnit));  // Hyperion
      BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(dwSize, "CUnitSave.cpp", 0xA0, 0));
      BYTE *pOut = pBuf;
      memset(pBuf, 0, dwSize);
      for (unsigned int i = 0; i < g_pools.units; i++) {
        if (abUsed[i]) {
          *reinterpret_cast<DWORD *>(pOut) = i;
          pOut += sizeof(DWORD);
          CUnit *pUnit = &CUnit::Head()[i];
          pUnit->sub_41BF70(0);
          pOut = PortSave117Put(SAVE117_CUnit, pUnit, pOut);
          pUnit->sub_41BF70(1);
        }
      }
      bOk = PortSaveChunk(pBuf, dwSize, nUsed, pFile);
      SMemFree(pBuf, "CUnitSave.cpp", 0xB7, 0);
    }
  }
  saveload_unitlist(0, abUsed, g_pools.units);
  if (!bOk)
    return FALSE;
  if (!SaveCloakList(pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetUsedList(), pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetTransList(), pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetDeadList(), pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetScannerList(), pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetUsedListTail(), pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetTransListTail(), pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetDeadListTail(), pFile))
    return FALSE;
  if (!write_unit_id(CUnit::GetScannerListTail(), pFile))
    return FALSE;
  for (int i = 0; i < PlayerSlots(); i++) {  // Hyperion: every slot of a Hyperion game
    if (!write_unit_id(CUnit::GetPlayerList(i), pFile))
      return FALSE;
  }
  return TRUE;
}
