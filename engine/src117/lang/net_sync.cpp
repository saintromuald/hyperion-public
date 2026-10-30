// net_sync.cpp -- the turn-sync traffic: each machine folds its own copy of the
// game state into a hash, ships it, and compares, so a divergence is caught the
// turn it happens. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/net_sync.h"
#include "../h/gameloop.h"    // SaveMinimapCounts
#include "../h/minimap.h"     // sub_46C8E0, sub_46C900
#include "../h/net_rand.h"    // net_randsequence, set_rand_enabled
#include "../h/net_cmds.h"    // netmgr_queue_cmd
#include "../h/CUnit.h"
#include "../h/CSprite.h"     // SnapshotSightHash
#include "../gen/globals117.h"

#include <stdlib.h>           // _rotl, the hash mixing step
#include <string.h>           // memset

// The globals this TU reaches, dual-spelled, one #ifdef per name.
// gPlayerMinerals, gPlayerGas, gpSquareMap, gwMapWidth, gwMapHeight,
// gbMultiPlayerMode, gdwLocalMask, g_LocalNationID and g_ActiveNationID come
// from gen/globals117.h.

// 0x673F20 and 0x673F21 -- the two minimap sync bytes, adjacent and passed as a
// pair.
#ifdef OC_DLL_MODE
#define sgbMiniSyncVis_g (*reinterpret_cast<unsigned char *>(OC_VA(0x673F20u)))
#else
extern unsigned char sgbMiniSyncVis_g;
#endif

#ifdef OC_DLL_MODE
#define sgbMiniSyncSeq_g (*reinterpret_cast<unsigned char *>(OC_VA(0x673F21u)))
#else
extern unsigned char sgbMiniSyncSeq_g;
#endif

// 0x52651C -- the frame counter, spelled as game.cpp and replay.cpp spell it;
// gameloop.cpp's dword_52651C is the same slot.
#ifdef OC_DLL_MODE
#define oc_dwReplayFrame (*reinterpret_cast<unsigned int *>(OC_VA(0x52651Cu)))
#else
extern unsigned int oc_dwReplayFrame;
#endif

// 0x67266C -- spelled as CThingy.cpp spells it.
#ifdef OC_DLL_MODE
#ifndef gdwLocalBlackMask
#define gdwLocalBlackMask (*reinterpret_cast<DWORD *>(OC_VA(0x67266Cu)))
#endif
#else
extern DWORD gdwLocalBlackMask;
#endif

// 0x673F22 -- the record cursor into stru_673F28, wrapped at 16.
#ifdef OC_DLL_MODE
#define byte_673F22 (*reinterpret_cast<unsigned char *>(OC_VA(0x673F22u)))
#else
extern unsigned char byte_673F22;
#endif

// 0x673F23 -- the cursor into byte_674FE8, wrapped at byte_673F24.
#ifdef OC_DLL_MODE
#define byte_673F23 (*reinterpret_cast<unsigned char *>(OC_VA(0x673F23u)))
#else
extern unsigned char byte_673F23;
#endif

// 0x673F24 -- how many entries of byte_674FE8 are in use.
#ifdef OC_DLL_MODE
#define byte_673F24 (*reinterpret_cast<unsigned char *>(OC_VA(0x673F24u)))
#else
extern unsigned char byte_673F24;
#endif

// 0x673F28 -- the sixteen sync records.
#ifdef OC_DLL_MODE
#define stru_673F28 (reinterpret_cast<NETSYNCREC *>(OC_VA(0x673F28u)))
#else
extern NETSYNCREC stru_673F28[16] OC_UNALIGNED;
#endif

// 0x674FE8 -- the hash-kind table. sync_init clears 32 bytes of it, so the
// array is at least that long.
#ifdef OC_DLL_MODE
#define byte_674FE8 (reinterpret_cast<unsigned char *>(OC_VA(0x674FE8u)))
#else
extern unsigned char byte_674FE8[32] OC_UNALIGNED;
#endif

// 0x675008 -- the square-map row sub_47A290 hashes next, unsigned because
// 1.17's wrap test is an unsigned compare.
#ifdef OC_DLL_MODE
#define dword_675008 (*reinterpret_cast<unsigned int *>(OC_VA(0x675008u)))
#else
extern unsigned int dword_675008;
#endif

// The bodies, in oracle VA order.

// 0x47A250 (61 B). Folds both resource tables into a running hash, newest slot
// first, through one descending pointer into gPlayerGas as 1.17 walks them.
int sub_47A250()
{
  int nHash = 0;
  const int *pGas = &gPlayerGas.a[12];

  do {
    nHash ^= pGas[-13];
    --pGas;
    nHash = _rotl(nHash, 3);
    nHash ^= *pGas;
    nHash = _rotl(nHash, 3);
  } while (pGas != gPlayerGas.a);

  int nMixed = net_randsequence();

  nHash = _rotl(nHash, 3);
  nMixed = _rotl(nMixed ^ nHash, 3) ^ static_cast<int>(oc_dwReplayFrame);

  return _rotl(nMixed, 3);
}

// 0x47A290 (90 B). Folds one row of the square map into a hash, masking bit 30
// out of every cell, then advances the row cursor and wraps it at the map
// height. The count is an explicit pointer difference, as in 1.17.
int sub_47A290()
{
  int nHash = 0;
  unsigned int nRow = dword_675008;
  DWORD *pCell = gpSquareMap + gwMapWidth * nRow;
  DWORD *pEnd = pCell + gwMapWidth;
  int nCount = (int)(pEnd - pCell);

  while (nCount--) {
    nHash ^= *pCell++ & 0xBFFFFFFF;
    nHash = _rotl(nHash, 3);
  }

  nRow++;
  dword_675008 = nRow < gwMapHeight ? nRow : 0;
  return nHash;
}

// 0x47A2F0 (15 B). Hands the two sync bytes to the minimap comparer as a pair,
// by a tail jump.
void SaveMinimapCounts()
{
  sub_46C8E0(&sgbMiniSyncVis_g, &sgbMiniSyncSeq_g);
}

// 0x47A300 (95 B). Queues this machine's sync report for the current slot: a
// seven-byte command carrying the slot's turn, its sequence nibble and three of
// its record bytes.
void packet_sync()
{
  NETSYNCPKT pkt;
  BYTE bSlot = byte_673F22;
  const NETSYNCREC *pRec = &stru_673F28[bSlot];

  pkt.bCmd = 0x37;
  pkt.bSeq = (BYTE)((bSlot << 4) | byte_674FE8[byte_673F23]);
  pkt.wTurn = pRec->wTurn;
  pkt.b04 = pRec->b03;
  pkt.b05 = (BYTE)pRec->dw08;
  pkt.b06 = pRec->b05;
  netmgr_queue_cmd(&pkt, 7);
}

// 0x47A360 (147 B). Folds one kind of game state into a 16-bit hash with the
// random generator disabled so the fold cannot perturb it. The mask xor's
// operand order is 1.17's.
WORD GetVersionSyncHash(BYTE bKind)
{
  unsigned int nHash;
  int nWasEnabled = set_rand_enabled(0);

  switch (bKind) {
  case 2:
    nHash = (unsigned int)sub_47A250();
    break;
  case 1:
    {
      CUnit *pUnit;
      nHash = 0;
      pUnit = CUnit::GetUsedList();
      while (pUnit != NULL) {
        unsigned int nUnit = (unsigned int)pUnit->GetHash();
        pUnit = pUnit->pNext;
        nHash ^= nUnit;
        nHash = _rotl(nHash, 3);
      }
    }
    break;
  default:
    nHash = byte_673F22;
    break;
  }

  if (nWasEnabled != 0)
    set_rand_enabled(1);

  nHash ^= (unsigned int)sub_47A290() ^
           ((gdwLocalMask ^ gdwLocalBlackMask) << (8 - g_LocalNationID));

  return (WORD)(nHash ^ (nHash >> 16));
}

// 0x47A400 (139 B). Clears the sync records and seeds the kind table, then
// takes the first sample. Both clear sizes are literals: in the DLL form the
// two arrays are pointers.
void sync_init()
{
  byte_673F22 = 0;
  byte_673F23 = 0;
  byte_673F24 = 0;
  memset(byte_674FE8, 0, 32);
  memset(stru_673F28, 0, 0x10C0);
  sgbMiniSyncVis_g = 0;
  sgbMiniSyncSeq_g = 0;
  sub_46C900();
  dword_675008 = 0;

  if (gbMultiPlayerMode != 0) {
    byte_674FE8[byte_673F24++] = 1;
    byte_674FE8[byte_673F24++] = 2;
  }

  UpdateGameHash();
}

// 0x47A490 (127 B). Advances both cursors, hashes the kind they now select and
// files the result in the next sync record. Each cursor is incremented in place
// on the global, as in 1.17.
void UpdateGameHash()
{
  BYTE bKind;
  NETSYNCREC *pRec;
  WORD wHash;

  if (++byte_673F22 >= 16)
    byte_673F22 = 0;

  if (++byte_673F23 >= byte_673F24)
    byte_673F23 = 0;

  bKind = byte_674FE8[byte_673F23];
  wHash = GetVersionSyncHash(bKind);

  pRec = &stru_673F28[byte_673F22];
  pRec->b04 = sgbMiniSyncVis_g;
  pRec->b05 = sgbMiniSyncSeq_g;
  pRec->wTurn = wHash;
  pRec->bSeq = bKind;
  SnapshotSightHash(&pRec->b03, &pRec->dw08, pRec->abRun);
}

// 0x47A510 (83 B). Accepts an incoming sync report only if it checksums and its
// turn and sequence still agree with the slot's own record. The slot guard
// cannot fail, since a byte shifted right by four is at most fifteen; 1.17 has
// it anyway.
BOOL net_recv_sync(const NETSYNCPKT *pPkt)
{
  BYTE bSeq = pPkt->bSeq;
  BYTE bSlot = (BYTE)(bSeq >> 4);

  if (bSlot >= 16)
    return 0;

  if (sub_47A570(bSlot, pPkt)) {
    if (stru_673F28[bSlot].wTurn == pPkt->wTurn
        && stru_673F28[bSlot].bSeq == (bSeq & 0x0F))
      return 1;
  }

  return 0;
}

// 0x47A570 (145 B). Checks one sync report against the slot's record: the local
// player's bit of the flag byte must agree, and the checksum must match a
// rotate-xor fold over up to fourteen record bytes.
int sub_47A570(int nSlot, const NETSYNCPKT *pPkt)
{
  if (((stru_673F28[nSlot].b04 >> g_ActiveNationID) ^ pPkt->b06) & 1)
    return 0;

  unsigned int nFirst = pPkt->b05;
  unsigned int nLast = nFirst + 13;

  if (nLast >= 0x100)
    nLast = 0xFF;

  int nMask = 1 << g_ActiveNationID;
  unsigned int nHash = 0;
  const BYTE *p = &stru_673F28[nSlot].abRun[nFirst];

  for (unsigned int i = nFirst; i <= nLast; i++) {
    nHash ^= *p++ & nMask;
    nHash = _rotl(nHash, 1);
  }

  nHash ^= nHash >> 16;
  return pPkt->b04 == (BYTE)(nHash ^ (nHash >> 8));
}
