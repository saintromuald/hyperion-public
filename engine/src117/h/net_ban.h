// net_ban.h -- declarations for src117/lang/net_ban.cpp: the multiplayer ban
// list, a Storm intrusive list of names the chat room refuses to let back in.
// The free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_NET_BAN_H
#define OC117_NET_BAN_H

#include "platform.h"

#include "TSList.h"

struct TBAN;

// The ban list, a specialisation so that its Clear has 1.17's name.
#pragma pack(push, 1)
template <> struct TSList<TBAN,TSGetLink<TBAN> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TBAN> link;
  // 0x46FBB0. Unlinks and frees every ban.
  void Clear(void);
};
#pragma pack(pop)

// 0x46FC50. Drops every ban: a tail jump into Clear on the list at 0x508D2C.
void ClearBans();

// 0x46FB10. Takes the player's name, not an index: the 0x24-byte node it
// allocates holds the name at +8, where IsPlayerBanned compares. Every path
// returns an explicit value, though BanPlayer discards it.
BOOL AddBannedPlayer(const char *pszName);

// 0x46FC10. Compares the name against each node's +8 with _stricmp.
BOOL IsPlayerBanned(const char *pszName);

#endif  // OC117_NET_BAN_H
