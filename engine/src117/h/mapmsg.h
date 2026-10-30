// mapmsg.h -- declarations for src117/lang/mapmsg.cpp, the on-screen map message
// line: the text banner triggers and chat write across the top of the play area.
// Free functions are __fastcall through /Gr and are declared without the
// keyword.
//
// Five of this TU's functions are declared by the headers of the TUs that call
// them and are not repeated here: map_msg_chat_msg and wrap_map_msg
// (net_cmds.h), map_msg_target_self (order.h), map_msg_default (TrigAct.h) and
// map_msg_trigger_abort (victory.h).

#ifndef OC117_MAPMSG_H
#define OC117_MAPMSG_H

#include "platform.h"

// 0x46A760 (138 B). ECX only, read as 32 bits; no caller reads EAX.
void invalidate_slot(int nSlot);

// 0x46A7F0 (105 B), 0x46ACD0 (346 B) and 0x46AE30 (25 B). Nullary.
void map_msg_init_font();
void map_msg_draw();
void map_msg_init();

// 0x46AB40 (334 B). `ret 8h`: ECX is the text, EDX is read as 8 bits, and the
// two stack words are a 4-byte deadline and a 1-byte source flag.
void map_msg(const char *pszText, BYTE bColor, DWORD dwDeadline, BYTE bSource);

// 0x46AE70 (77 B). ECX only, the text; wrap_map_msg discards EAX.
void map_msg_target(const char *pszText);

// 0x46AFB0 (181 B). Nullary; EAX is never set.
void map_msg_update();

// 0x46B070 (50 B). maphdr.cpp declares it the same way.
void __fastcall map_msg_error(const char *pszMsg, BYTE bType);

// Hyperion: the lines on screen now, top first, up to nMax; the live channel reports what
// a sandbox command made the game say, such as a building it could not place (port_control.cpp).
int MapMsgLines(const char **ppszLines, int nMax);

#endif  // OC117_MAPMSG_H
