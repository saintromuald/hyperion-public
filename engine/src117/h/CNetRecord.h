// CNetRecord.h -- the network turn recorder object. net_record_init allocates 0x20
// bytes for it and writes every field but bReading, so +0x04 starts uninitialised in
// 1.17 as well. pCount points one past the block's command-count byte and pRead is the
// drain cursor. Each offset's assert names the instruction that fixes it.

#ifndef OC117_CNETRECORD_H
#define OC117_CNETRECORD_H

#include "platform.h"

class CNetRecord {
public:
  // 0x478340 (234 B), `ret Ch`. Not BYTE: the callee's one-byte read bounds the
  // width from below, and net_record_cmd pushes the whole dword with no narrowing.
  void sub_478340(int nStormId, const void *pCmd, DWORD nCmdLen);
  // 0x478640 (196 B), `ret 4h`. sub_478970 never writes EDX before the call,
  // so the recorder is the only register argument.
  int sub_478640(void *pFile);

  /*0x00*/ int   bRecording;
  /*0x04*/ int   bReading;
  /*0x08*/ BYTE *pBuf;
  /*0x0C*/ int   nLen;
  /*0x10*/ int   nCapacity;
  /*0x14*/ BYTE *pCount;
  /*0x18*/ DWORD dwFrame;
  /*0x1C*/ BYTE *pRead;
};

static_assert(offsetof(CNetRecord, bRecording) == 0x00, "cmp dword ptr [esi], 0 in sub_478340");
static_assert(offsetof(CNetRecord, bReading) == 0x04, "mov dword ptr [eax+4h], 0h in net_record_stop_playback");
static_assert(offsetof(CNetRecord, pBuf) == 0x08, "SMemFree(dword ptr [esi+8h]) in net_record_free");
OC_X86_LAYOUT(offsetof(CNetRecord, nLen) == 0x0C, "add eax, dword ptr [ecx+8h] in sub_478430");
OC_X86_LAYOUT(offsetof(CNetRecord, nCapacity) == 0x10, "cmp eax, ecx after add eax, 200h in net_record_grow");
OC_X86_LAYOUT(offsetof(CNetRecord, pCount) == 0x14, "mov byte ptr [ecx-1h], al in sub_4787E0");
OC_X86_LAYOUT(offsetof(CNetRecord, dwFrame) == 0x18, "cmp eax, dword ptr [0x52651C] in sub_478340");
OC_X86_LAYOUT(offsetof(CNetRecord, pRead) == 0x1C, "mov dword ptr [esi+1Ch], eax in net_record_read_turn");
OC_X86_LAYOUT(sizeof(CNetRecord) == 0x20, "push 20h / call SMemAlloc in net_record_init");

#endif  // OC117_CNETRECORD_H
