// net_record.cpp -- the network turn recorder: the per-block scratch object the
// drain fills, the fill and grow paths, and the save, load and teardown entry
// points. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CNetRecord.h"
#include "../h/CNetUnkn.h"
#include "../h/net_record.h"
#include "../h/net_cmds.h"     // GetCommandByteSize
#include "../h/saveload.h"     // compress, decompress
#include "../h/dlg_dlg.h"      // app_add_exit
#include "../h/Storm.h"        // SMemAlloc / SMemFree / SMemCopy
#include "../gen/globals117.h"
#include "../h/playerlimits.h"  // Hyperion: g_hxPlayers (stage D)
#include "../h/hxrules.h"       // Hyperion: HxRules
#include "../port/gameplay/port_trainer.h"  // Hyperion: PortTrainerCommand
#include "../port/lan/port_lanfeed.h"  // Hyperion: PortLanWatchLive

// The owner tag every SMemAlloc and SMemFree here passes, 1.17's string at
// 0x4EF2EC; the recorder object itself goes under operator new's and delete's
// tags.
#define OC_NET_RECORD_CPP "net_record.cpp"

// The globals and .rdata this TU reaches, dual-spelled, one #ifdef per name.
// g_ActiveStormID comes from gen/globals117.h.

// 0x52651C -- the frame counter, spelled as game.cpp and replay.cpp spell it.
#ifdef OC_DLL_MODE
#define oc_dwReplayFrame (*reinterpret_cast<unsigned int *>(OC_VA(0x52651Cu)))
#else
extern unsigned int oc_dwReplayFrame;
#endif

// 0x673D40 -- the single recorder object.
#ifdef OC_DLL_MODE
#define gpNetRecord_g (*reinterpret_cast<CNetRecord **>(OC_VA(0x673D40u)))
#else
extern CNetRecord *gpNetRecord_g;
#endif

// 0x4EF2FC -- the twelve opcodes the recorder drops, 1.17's byte table.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define s_abNoRecord (reinterpret_cast<const BYTE *>(OC_VA(0x4EF2FCu)))
#else
static const BYTE s_abNoRecord[12] = {
  0x37, 0x05, 0x06, 0x07, 0x08, 0x10, 0x11, 0x38, 0x39, 0x3A, 0x3B, 0x55
};
#endif

// The bodies, in oracle VA order.

// 0x4782D0 (33 B). Takes the two buffers the caller supplies and records that
// this object does not own them.
CNetUnkn::CNetUnkn(BYTE *pBytesIn, int *pnSizesIn, DWORD nCountIn)
{
  pBytes = pBytesIn;
  pnSizes = pnSizesIn;
  nCount = nCountIn;
  bOwned = 0;
}

// 0x478300 (63 B). Releases the two buffers, only when this object allocated
// them. net_record_expand inlines this body before it installs new ones.
CNetUnkn::~CNetUnkn()
{
  if (bOwned != 0) {
    SMemFree(pBytes, OC_NET_RECORD_CPP, 0xE5, 0);
    pBytes = 0;
    SMemFree(pnSizes, OC_NET_RECORD_CPP, 0xE8, 0);
    pnSizes = 0;
  }
}

// 0x478340 (234 B). Appends one command to the open block, opening a fresh
// block first when the frame moved on or when the block would overflow its
// byte-sized length.
void CNetRecord::sub_478340(int nStormId, const void *pCmd, DWORD nCmdLen)
{
  BYTE *pDst;
  unsigned int i;

  if (bRecording == 0)
    return;
  if (nCmdLen >= 0x100)
    return;

  for (i = 0; i < 12; i++) {
    if (*(const BYTE *)pCmd == s_abNoRecord[i])
      return;
  }

  net_record_grow((intptr_t)this);

  if (pCount != 0) {
    if ((intptr_t)pBuf + nLen - (intptr_t)pCount + 1 + (int)nCmdLen >= 0xFF)
      sub_4787E0((intptr_t)this);
  }

  // The volatile read keeps 1.17's order of the compare's two loads.
  if (*(volatile DWORD *)&this->dwFrame != oc_dwReplayFrame)
    sub_4787E0((intptr_t)this);

  pDst = pBuf + nLen;
  *pDst = (BYTE)nStormId;
  if (nCmdLen + 1 + nLen <= (DWORD)nCapacity) {
    nLen += 1;
    SMemCopy(pDst + 1, pCmd, nCmdLen);
    nLen += nCmdLen;
  }
}

// 0x478430 (74 B). Walks every block from the head of the buffer to its end,
// stepping each command by its own size; nothing is produced. The count byte
// goes through a BYTE local, as in 1.17.
void sub_478430(void *pArg0)
{
  CNetRecord *p = (CNetRecord *)pArg0;
  BYTE *pCur = p->pBuf;
  BYTE bCount;
  int i;

  while (pCur < p->pBuf + p->nLen) {
    bCount = pCur[4];
    i = 0;
    pCur += 5;
    while (i < bCount) {
      ++pCur;
      int nSize = GetCommandByteSize((const net_cmd_select_t *)pCur);
      ++i;
      pCur += nSize;
      i += nSize;
    }
  }
}

// 0x478480 (307 B). Drains one frame's worth of commands into the caller's
// owner byte array and the scratch object's byte run, growing the scratch when
// it will not fit. The two entry guards are separate ifs, as in 1.17.
int net_record_read_turn(intptr_t nArg0, intptr_t nArg1, void *pArg2, intptr_t nArg3, void *pArg4)
{
  CNetRecord *p = (CNetRecord *)nArg0;
  CNetUnkn *pUnk = (CNetUnkn *)pArg4;
  DWORD dwSeen;
  BYTE *pDst;
  int *pnSizes;
  intptr_t nOff;
  int nIdx;
  BYTE bTotal;
  BYTE bRun;

  if (p->bReading == 0)
    return -1;
  if (p->pRead >= p->pBuf + p->nLen)
    return -1;

  // Hyperion: a record is another person's file, or a watched match's feed, so every read and
  // copy is held to the record and the buffers; a record that goes past them ends there, the
  // frames from it on not played. 1.17 trusted its own files.
  const BYTE *const pEnd = p->pBuf + p->nLen;
  const auto Broken = [p, pEnd]() {
    p->pRead = const_cast<BYTE *>(pEnd);
    return -1;
  };
  if (pEnd - p->pRead < 5)
    return Broken();

  dwSeen = *(DWORD *)p->pRead;
  nIdx = 0;
  pDst = pUnk->pBytes;
  pnSizes = pUnk->pnSizes;

  do {
    // A volatile read: 1.17 reloads the cursor here.
    BYTE *pCur = *(BYTE * volatile *)&p->pRead;
    if (pEnd - pCur < 5)
      return Broken();
    if (*(DWORD *)pCur != dwSeen) {
      *(int *)pArg2 = nIdx;
      return dwSeen;
    }
    pCur += 4;
    p->pRead = pCur;
    bTotal = *pCur;
    pCur++;
    p->pRead = pCur;
    nOff = pDst - pUnk->pBytes;
    if (bTotal + nOff >= pUnk->nCount) {
      if (!net_record_expand((intptr_t)pUnk, (intptr_t)pUnk, bTotal + nOff))
        return -1;
      pDst = pUnk->pBytes + nOff;
      pnSizes = pUnk->pnSizes;
    }
    bRun = 0;
    while (bRun < bTotal) {
      // Hyperion: the owners' array is 0x200 (net_mgr.cpp's byte_672BE8), the sizes' as many as
      // the scratch holds bytes, and a command is its owner, its opcode and its size in the record
      if (nIdx >= 0x200 || pEnd - p->pRead < 2)
        return Broken();
      if (nIdx >= static_cast<int>(pUnk->nCount)) {
        nOff = pDst - pUnk->pBytes;
        if (!net_record_expand((intptr_t)pUnk, (intptr_t)pUnk, nIdx + 0x100))
          return -1;
        pDst = pUnk->pBytes + nOff;
        pnSizes = pUnk->pnSizes;
      }
      ((BYTE *)nArg3)[nIdx] = 0xFF;
      pnSizes[nIdx] = 0;
      if (p->bReading != 0) {
        ((BYTE *)nArg3)[nIdx] = *p->pRead;
        p->pRead++;
        int nSize = GetCommandByteSize((const net_cmd_select_t *)p->pRead);
        if (nSize < 0 || nSize > pEnd - p->pRead)
          return Broken();
        if (nSize != 0) {
          nOff = pDst - pUnk->pBytes;
          if (nOff + nSize > static_cast<intptr_t>(pUnk->nCount)) {
            if (!net_record_expand((intptr_t)pUnk, (intptr_t)pUnk, nOff + nSize + 0x100))
              return -1;
            pDst = pUnk->pBytes + nOff;
            pnSizes = pUnk->pnSizes;
          }
          SMemCopy(pDst, p->pRead, nSize);
          pnSizes[nIdx] = nSize;
          p->pRead += nSize;
        }
      }
      pDst += pnSizes[nIdx];
      bRun += (BYTE)(pnSizes[nIdx] + 1);
      nIdx++;
    }
  } while (p->pRead < p->pBuf + p->nLen);

  // Hyperion: the last frame's commands, read to the buffer's end, are handed over
  // in a Hyperion game; 1.17 drops them, so its replays lose their last recorded frame
  // (stage D, engine/docs/replay-test.md). The next call finds the buffer read and ends.
  // A LAN match watched is read to the end of what has come at every frame, which holds whole
  // frames only (port_lanfeed.cpp).
  if ((HxRules() || PortLanWatchLive()) && nIdx != 0) {
    *(int *)pArg2 = nIdx;
    return dwSeen;
  }
  return -1;
}

// 0x4785C0 (121 B). Grows the record buffer by 0x2710 once it is within 0x200
// of full, carrying the open block's offset across the move. SMemCopy takes the
// old capacity, so the field is bumped after it.
void net_record_grow(intptr_t nArg0)
{
  CNetRecord *p = (CNetRecord *)nArg0;
  BYTE *pNew;
  BYTE *pNewCount;
  intptr_t nOff;

  if (p->nLen + 0x200 >= p->nCapacity) {
    nOff = (intptr_t)p->pCount;
    if (nOff != 0)
      nOff -= (intptr_t)p->pBuf;
    pNew = (BYTE *)SMemAlloc(p->nCapacity + 0x2710, OC_NET_RECORD_CPP, 0x2FA, 0);
    SMemCopy(pNew, p->pBuf, p->nCapacity);
    p->nCapacity += 0x2710;
    SMemFree(p->pBuf, OC_NET_RECORD_CPP, 0x2FD, 0);
    p->pBuf = pNew;
    if (nOff == 0)
      pNewCount = 0;
    else
      pNewCount = pNew + nOff;
    p->pCount = pNewCount;
  }
}

// Hyperion: room for nMore bytes past the record's length and the 0x200 a block's commands
// may take, in one move of the buffer. A record is held to 256 MB, a game of weeks: past that
// it is no game's, and the int lengths would wrap.
static BOOL net_record_reserve(CNetRecord *p, int nMore)
{
  const int kRecordMost = 256 << 20;
  if (nMore < 0 || p->nLen < 0 || nMore > kRecordMost - p->nLen)
    return FALSE;
  const int nNeed = p->nLen + nMore + 0x200;
  if (nNeed < p->nCapacity)
    return TRUE;
  const int nCapacity = nNeed + 0x2710;
  const intptr_t nOff = p->pCount != 0 ? p->pCount - p->pBuf : 0;
  BYTE *pNew = (BYTE *)SMemAlloc(nCapacity, OC_NET_RECORD_CPP, 0x2FA, 0);
  if (pNew == 0)
    return FALSE;
  SMemCopy(pNew, p->pBuf, p->nLen);
  SMemFree(p->pBuf, OC_NET_RECORD_CPP, 0x2FD, 0);
  p->pBuf = pNew;
  p->nCapacity = nCapacity;
  p->pCount = nOff != 0 ? pNew + nOff : 0;
  return TRUE;
}

// 0x478640 (196 B). Reads a recorded buffer back from a file: its length
// first, then the bytes, growing the buffer until the length fits. The grow
// loop is net_record_grow inlined, so that body sits above this one.
int CNetRecord::sub_478640(void *pFile)
{
  CNetRecord *p = this;
  int nOk;

  nOk = decompress(&p->nLen, 4, pFile);
  if (nOk == 0)
    return 0;
  if (p->nLen > 0) {
    // Hyperion: one move to the length the file gives, held to a record's most, where 1.17
    // grew 0x2710 at a time to any length
    const int nWanted = p->nLen;
    p->nLen = 0;
    if (!net_record_reserve(p, nWanted))
      return 0;
    p->nLen = nWanted;
    nOk = decompress(p->pBuf, p->nLen, pFile);
    if (nOk == 0)
      return 0;
  }
  return nOk;
}

// 0x478710 (202 B). Reallocates the scratch object to hold nArg2 command bytes,
// copies what is already there, then drops the old buffers.
BOOL net_record_expand(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2)
{
  CNetUnkn *p = (CNetUnkn *)nArg0;
  BYTE *pNewBytes;
  int *pnNewSizes;

  pNewBytes = (BYTE *)SMemAlloc((DWORD)nArg2, OC_NET_RECORD_CPP, 0xC7, 0);
  if (pNewBytes == 0)
    return FALSE;

  pnNewSizes = (int *)SMemAlloc((DWORD)(nArg2 * 4), OC_NET_RECORD_CPP, 0xCA, 0);
  if (pnNewSizes == 0) {
    SMemFree(pNewBytes, 0, 0, 0);
    return FALSE;
  }

  SMemCopy(pNewBytes, p->pBytes, p->nCount);
  SMemCopy(pnNewSizes, p->pnSizes, p->nCount * 4);
  p->~CNetUnkn();
  p->pBytes = pNewBytes;
  p->pnSizes = pnNewSizes;
  p->nCount = (DWORD)nArg2;
  p->bOwned = 1;
  return TRUE;
}

// 0x4787E0 (72 B). Closes the open block by back-filling its length byte, then
// stamps a fresh block header with the current frame, read back out of dwFrame
// as 1.17 does.
void sub_4787E0(intptr_t nArg0)
{
  CNetRecord *p = (CNetRecord *)nArg0;
  BYTE *pCnt = p->pCount;
  intptr_t nUsed;

  if (pCnt != 0) {
    nUsed = (intptr_t)p->pBuf - (intptr_t)pCnt + p->nLen;
    if (nUsed == 0)
      p->nLen -= 5;
    else
      pCnt[-1] = (BYTE)nUsed;
    p->pCount = 0;
  }
  p->dwFrame = oc_dwReplayFrame;
  *(DWORD *)(p->pBuf + p->nLen) = p->dwFrame;
  p->nLen += 5;
  p->pCount = p->pBuf + p->nLen;
}

// 0x478830 (20 B). Records one locally issued command under the active Storm
// id, passed whole.
void net_record_cmd(intptr_t nArg0, int nArg1)
{
  PortTrainerCommand(static_cast<int>(g_ActiveStormID), (const void *)nArg0, static_cast<unsigned>(nArg1));  // Hyperion
  gpNetRecord_g->sub_478340(g_ActiveStormID, (const void *)nArg0, nArg1);
}

// 0x478850 (58 B). Drops the recorder object and its buffer. net_record_init
// inlines this body and app_add_exit takes its address.
void net_record_free()
{
  CNetRecord *pRec = gpNetRecord_g;

  if (pRec != 0) {
    SMemFree(pRec->pBuf, OC_NET_RECORD_CPP, 0x192, 0);
    SMemFree(pRec, "delete", -1, 0);
    gpNetRecord_g = 0;
  }
}

// 0x478890 (23 B). Drains one frame off the recorder. nArg1 goes in
// net_record_read_turn's EDX slot, which that callee never reads.
int sub_478890(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2)
{
  return net_record_read_turn((intptr_t)gpNetRecord_g, nArg1, (void *)nArg0, nArg1, (void *)nArg2);
}

// 0x4788B0 (182 B). Replaces the recorder object with a fresh one and registers
// the teardown. The allocation under the "new" tag and the null check around
// the stores are `new CNetRecord`, inlined.
void net_record_init()
{
  CNetRecord *pRec;

  net_record_free();
  pRec = (CNetRecord *)SMemAlloc(sizeof(CNetRecord), "new", -1, 0);
  if (pRec != 0) {
    pRec->nCapacity = 0xC350;
    pRec->pBuf = (BYTE *)SMemAlloc(0xC350, OC_NET_RECORD_CPP, 0x184, 0);
    pRec->nLen = 0;
    pRec->pCount = 0;
    pRec->dwFrame = 0xFFFFFFFF;
    pRec->bRecording = 1;
    pRec->pRead = 0;
    gpNetRecord_g = pRec;
    app_add_exit(net_record_free);
    return;
  }
  gpNetRecord_g = 0;
  app_add_exit(net_record_free);
}

// 0x478970 (13 B). Hands the recorder and a file to the loader.
int sub_478970(void *pFile)
{
  return gpNetRecord_g->sub_478640(pFile);
}

// 0x478980 (98 B). Closes the open block, walks what was recorded, then writes
// the length and the bytes out through the compressor.
int net_record_write(intptr_t nArg0)
{
  CNetRecord *p = gpNetRecord_g;
  int nSize;
  int nOk;

  if (p->pCount != 0) {
    p->pCount[-1] = (BYTE)(p->nLen + (intptr_t)p->pBuf - (intptr_t)p->pCount);
    p->pCount = 0;
  }
  sub_478430(p);
  nSize = p->nLen;
  nOk = compress(&nSize, 4, (void *)nArg0);
  if (nOk == 0)
    return 0;
  if (p->nLen > 0) {
    nOk = compress(p->pBuf, p->nLen, (void *)nArg0);
    if (nOk == 0)
      return 0;
  }
  return nOk;
}

// 0x4789F0 (31 B). Resets the drain cursor to the head of the recorded buffer
// and flips the recorder from writing to reading.
void net_record_start_playback()
{
  CNetRecord *pRec;

  gpNetRecord_g->bRecording = 0;
  pRec = gpNetRecord_g;
  pRec->bReading = 1;
  pRec->pRead = pRec->pBuf;
}

// 0x478A10 (13 B). Stops reading.
void net_record_stop_playback()
{
  gpNetRecord_g->bReading = 0;
}

// Hyperion: a LAN match's record as it is played (port_lanfeed.cpp).
//
// net_record_live gives the record being made: its bytes, its length, and the block still
// open, its offset (-1 when none is) and frame. The open block's count is written only when
// the next one opens, so a reader takes it from the length.
int net_record_live(const BYTE **ppBuf, int *pnOpenAt, DWORD *pdwOpenFrame)
{
  CNetRecord *p = gpNetRecord_g;
  if (p == 0 || p->bRecording == 0) {
    *ppBuf = 0;
    *pnOpenAt = -1;
    *pdwOpenFrame = 0;
    return 0;
  }
  *ppBuf = p->pBuf;
  *pnOpenAt = p->pCount != 0 ? static_cast<int>(p->pCount - p->pBuf) - 5 : -1;
  *pdwOpenFrame = p->dwFrame;
  return p->nLen;
}

// net_record_append adds blocks to a record being played back, its reading carried across a
// move of its buffer; the frames it holds are read as a replay's are (net_record_read_turn).
BOOL net_record_append(const void *pData, int nBytes)
{
  CNetRecord *p = gpNetRecord_g;
  if (p == 0 || nBytes < 0)
    return FALSE;
  const intptr_t nRead = p->pRead != 0 ? p->pRead - p->pBuf : -1;
  if (!net_record_reserve(p, nBytes))
    return FALSE;
  if (nRead >= 0)
    p->pRead = p->pBuf + nRead;
  SMemCopy(p->pBuf + p->nLen, pData, nBytes);
  p->nLen += nBytes;
  return TRUE;
}
