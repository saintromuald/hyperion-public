// vpacket.h -- declarations for src117/lang/vpacket.cpp: the voice packet stubs.
// Free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_VPACKET_H
#define OC117_VPACKET_H

#include "platform.h"

// voice_chat is the third entry of net_mgr.cpp's sgFnChatCmds_g table at
// 0x4EF2C0, so it takes that table's net_msg_handler_t signature: ECX, EDX and
// one stack word (`ret 4h`), returning the byte count consumed.
int voice_chat(int nPlayerId, char *pData, unsigned int nSize);

#endif  // OC117_VPACKET_H
