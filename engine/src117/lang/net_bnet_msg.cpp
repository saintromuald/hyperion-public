// net_bnet_msg.cpp -- the Battle.net server and league message queue, and the
// chat slash commands that feed it. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/net_bnet_msg.h"
#include "../h/TSList.h"
#include "../h/Storm.h"         // SMemAlloc

#include <string.h>             // strlen and memcpy
#include <typeinfo>             // typeid -- SMemAlloc's owner tag
#if !defined(_MSC_VER)
#include "../h/rtti.h"  // Hyperion: the global type_info MSVC declares itself
#endif
#include "../h/net_cmds.h"   // map_msg_chat_msg
#include "../h/gluChat.h"    // on_recv_chat_from
#include "../h/net_chat_cmds.h"  // sub_4701F0, sub_470320, sub_4703D0
#include "../h/net_misc.h"       // IsBNetGame
#include "../h/gds_local.h"      // psnprintf
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x6662B8 -- gwGameMode, a WORD (game.cpp owns it).
#ifdef OC_DLL_MODE
#define gwGameMode (*reinterpret_cast<WORD *>(OC_VA(0x6662B8u)))
#else
extern WORD gwGameMode;
#endif

// 0x66A274 -- glGluesMode, a DWORD (glues.cpp owns it).
#ifdef OC_DLL_MODE
#define glGluesMode (*reinterpret_cast<DWORD *>(OC_VA(0x66A274u)))
#else
extern DWORD glGluesMode;
#endif

// 0x5264B8 -- the per-player "show chat from this player" mask, a WORD;
// net_chrm.cpp reaches the same slot.
#ifdef OC_DLL_MODE
#define word_5264B8 (*reinterpret_cast<WORD *>(OC_VA(0x5264B8u)))
#else
extern WORD word_5264B8;
#endif


// 0x66D0F8 -- gszLeagueName_g (League.cpp owns it). Only its first byte is
// read here, as a "joined a league" flag.
#ifndef OC_NET_BNET_MSG_gszLeagueName_g
#define OC_NET_BNET_MSG_gszLeagueName_g
#ifdef OC_DLL_MODE
#define gszLeagueName_g (reinterpret_cast<char *>(OC_VA(0x66D0F8u)))
#else
extern char gszLeagueName_g[1];
#endif
#endif

// 0x672594 -- the tick the last queued line was sent at.
#ifdef OC_DLL_MODE
#define dword_672594 (*reinterpret_cast<DWORD *>(OC_VA(0x672594u)))
#else
extern DWORD dword_672594;
#endif

// TBNETMESSAGE and its list specialization are in net_bnet_msg.h.

// 0x508D38 -- the message list, 12 bytes, its link at +4 as gBanList_g's is.
#ifdef OC_DLL_MODE
#define gMessageList_g (*reinterpret_cast<TBNETMESSAGELIST *>(OC_VA(0x508D38u)))
#else
extern TBNETMESSAGELIST gMessageList_g;
#endif

// 0x510970 -- 1.17's RTTI descriptor for TBNETMESSAGE, whose raw name tags the
// messages' allocations.
#ifdef OC_DLL_MODE
#define gTypeInfo_TBNETMESSAGE (*reinterpret_cast<type_info *>(OC_VA(0x510970u)))
#else
extern type_info gTypeInfo_TBNETMESSAGE;
#endif

// The bodies, in oracle VA order.

// Unlinks one message from whatever list holds it; inlined into Clear and
// DeleteNode.
static void UnlinkBNetMessage(TBNETMESSAGE *node)
{
  TSLink<TBNETMESSAGE> *pl = &node->link;
  TBNETMESSAGE *pprev = pl->prev;
  if (pprev != 0) {
    TBNETMESSAGE *pnext = pl->next;
    TSLink<TBNETMESSAGE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TBNETMESSAGE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<TBNETMESSAGE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = 0;
    pl->next = 0;
  }
}

// 0x46FC60. Unlinks and frees every queued message.
void TBNETMESSAGELIST::Clear(void)
{
  for (;;) {
    TBNETMESSAGE *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkBNetMessage(p);
    SMemFree(p, gTypeInfo_TBNETMESSAGE.raw_name(), -2, 0);
  }
}

// 0x46FCC0, `ret 4h`. Unlinks and frees one message; returns its successor.
TBNETMESSAGE *TBNETMESSAGELIST::DeleteNode(TBNETMESSAGE *pNode)
{
  TSLink<TBNETMESSAGE> *pl = pNode ? &pNode->link : &link;
  TBNETMESSAGE *pRet = pl->next;
  if ((intptr_t)pRet <= 0)
    pRet = 0;
  UnlinkBNetMessage(pNode);
  SMemFree(pNode, gTypeInfo_TBNETMESSAGE.raw_name(), -2, 0);
  return pRet;
}


// 0x46FD30 (10 B). Drops every queued message.
void ClearBNetMessages()
{
  gMessageList_g.Clear();
}

// Placement construction of one node: both link fields zeroed.
static __forceinline TBNETMESSAGE *InitBNetMessage(TBNETMESSAGE *q)
{
  q->link.prev = NULL;
  q->link.next = NULL;
  return q;
}

// Unlinks a link from whatever list holds it: UnlinkBNetMessage's body on a
// link, forced inline. CreateBNetMessage passes the list's own link when the
// allocation failed.
static __forceinline void UnlinkBNetLink(TSLink<TBNETMESSAGE> *pl)
{
  TBNETMESSAGE *pprev = pl->prev;
  if (pprev != 0) {
    TBNETMESSAGE *pnext = pl->next;
    TSLink<TBNETMESSAGE> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<TBNETMESSAGE> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<TBNETMESSAGE> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = 0;
    pl->next = 0;
  }
}

// 0x470070 (209 B). Queues one line for Battle.net. The TSList
// allocate-and-append is inlined, including the arm that unlinks the list's own
// link when the allocation failed; that unlink goes through UnlinkBNetLink, as
// 1.17's registers have it.
void CreateBNetMessage(const char *pszText)
{
  if (!IsBNetGame())
    return;

  size_t nLen = strlen(pszText);
  if (nLen == 0)
    return;
  ++nLen;
  if (nLen > 0x80)
    return;

  void *pv = SMemAlloc(sizeof(TBNETMESSAGE), gTypeInfo_TBNETMESSAGE.raw_name(), -2, 8);
  TBNETMESSAGE *pNode = pv ? InitBNetMessage(static_cast<TBNETMESSAGE *>(pv)) : NULL;
  TSLink<TBNETMESSAGE> *pLink = pNode ? &pNode->link : &gMessageList_g.link;

  UnlinkBNetLink(pLink);
  {
    TBNETMESSAGE *pIns = gMessageList_g.link.prev;

    pLink->prev = pIns;
    pLink->next = pIns->link.next;
    pIns->link.next = pNode;
    gMessageList_g.link.prev = reinterpret_cast<TBNETMESSAGE *>(pLink);
  }
  memcpy(pNode->szText, pszText, nLen);
}

// Echoes one squelch or unsquelch line locally. Inlined into sub_46FD90's
// shared tail; its early returns let that tail fall into CreateBNetMessage.
static void chat_notify(const char *pszMsg)
{
  WORD wMode = gwGameMode;

  if (wMode == 4) {
    if (glGluesMode != 3)
      return;
    on_recv_chat_from(-1, reinterpret_cast<intptr_t>(pszMsg));
    wMode = gwGameMode;
  }

  if (wMode == 3)
    map_msg_chat_msg(pszMsg, 0);
}

// 0x46FD90 (730 B). The slash-command hook the chat-send path runs first. A line
// that is not a command answers 0 and is sent as typed; anything matched here
// answers 1 and is consumed. The squelch and unsquelch arms converge on one
// tail, which the notify helper above keeps a single copy.
int __fastcall sub_46FD90(const char *pszText)
{
  const char *pszName;
  DWORD nSlot;
  char *pszRest;
  WORD wStrId;

  if (pszText[0] != '/')
    return 0;

  if (sub_470320(pszText + 1, "squelch ", 8, &nSlot, 0) != NULL ||
      sub_470320(pszText + 1, "ignore ", 7, &nSlot, 0) != NULL) {
    DWORD nId = nSlot;

    voice_squelch(gPlayerData[nId].dwStormID, TRUE);
    word_5264B8 &= ~static_cast<WORD>(1 << gPlayerData[nId].dwStormID);
    pszName = (gPlayback != 0 && gwGameMode == 3) ? gPlayerSlots_g[nId].szName
                                                  : gPlayerData[nId].szName;
    wStrId = 0x4D;
  } else if (sub_470320(pszText + 1, "unsquelch ", 10, &nSlot, 0) != NULL ||
             sub_470320(pszText + 1, "unignore ", 9, &nSlot, 0) != NULL) {
    DWORD nId = nSlot;

    voice_squelch(gPlayerData[nId].dwStormID, FALSE);
    word_5264B8 |= static_cast<WORD>(1 << gPlayerData[nId].dwStormID);
    pszName = (gPlayback != 0 && gwGameMode == 3) ? gPlayerSlots_g[nId].szName
                                                  : gPlayerData[nId].szName;
    wStrId = 0x4E;
  } else if ((pszRest = sub_4703D0(pszText + 1, "reply ", 6, &nSlot)) != NULL ||
             (pszRest = sub_4703D0(pszText + 1, "r ", 2, &nSlot)) != NULL) {
    char szCmd[0x110];

    if (IsBNetGame()) {
      psnprintf(szCmd, 0x110, "/w %s %s", gReplyPlayerName_g, pszRest);
      CreateBNetMessage(szCmd);
      return 1;
    }

    if (nSlot >= 8)
      return 1;

    sub_4701F0(pszRest, gPlayerData[nSlot].dwStormID);
    return 1;
  } else {
    // The Battle.net arm jumps to the send below rather than repeating it, as
    // 1.17 does.
    if (IsBNetGame())
      goto send;

    if ((pszRest = sub_470320(pszText + 1, "whisper ", 8, &nSlot, 1)) != NULL ||
        (pszRest = sub_470320(pszText + 1, "msg ", 4, &nSlot, 1)) != NULL ||
        (pszRest = sub_470320(pszText + 1, "w ", 2, &nSlot, 1)) != NULL ||
        (pszRest = sub_470320(pszText + 1, "m ", 2, &nSlot, 1)) != NULL)
      sub_4701F0(pszRest, gPlayerData[nSlot].dwStormID);

    return 1;
  }

  {
    char szMsg[0x104];

    psnprintf(szMsg, 0x104, error_get_str(wStrId), pszName);
    chat_notify(szMsg);
  }

send:
  // Four copies of one call that MSVC merges, kept for 1.17's register choice.
  if (glGluesMode != 3) {
    if (gPlayback != 0)
      CreateBNetMessage(pszText);
    else
      CreateBNetMessage(pszText);
  } else {
    if (gPlayback != 0)
      CreateBNetMessage(pszText);
    else
      CreateBNetMessage(pszText);
  }
  return 1;
}


// SNETEVENT's layout; the struct is in net_bnet_msg.h.
OC_X86_LAYOUT(sizeof(SNETEVENT) == 0x10, "four dwords");
static_assert(offsetof(SNETEVENT, pszData) == 0x08, "mov ecx, [ecx+8h]");

// 0x46FD40 (69 B). The Battle.net server-message handler Storm calls through
// SNetRegisterEventHandler(4, ...). In the chat-room arm a null text skips the
// call; the sender id -1 means "no player".
void __stdcall snet_servermessage_cb(SNETEVENT *pEvent)
{
  if (gwGameMode == 3) {
    map_msg_chat_msg(pEvent->pszData, 0);
  } else if (gwGameMode == 4 && glGluesMode == 3) {
    char *pszText = pEvent->pszData;

    if (pszText != NULL)
      on_recv_chat_from(-1, reinterpret_cast<intptr_t>(pszText));
  }
}

// 0x470150 (159 B). Sends the oldest queued line, at most one every two
// seconds. A league member's /league line goes only to the league service; a
// league member's /stats line goes to both.
void sub_470150()
{
  if (!IsBNetGame())
    return;

  TBNETMESSAGE *pNode = gMessageList_g.link.next;
  if ((intptr_t)pNode <= 0)
    return;

  DWORD dwNow = GetTickCount();
  if (dwNow - dword_672594 < 2000)
    return;

  dword_672594 = dwNow;
  if (gszLeagueName_g[0] != 0) {
    if (_strnicmp(pNode->szText, "/league", 7) == 0) {
      SNetSendLeagueCommand(pNode->szText, 0);
      gMessageList_g.DeleteNode(pNode);
      return;
    }
    if (_strnicmp(pNode->szText, "/stats", 6) == 0)
      SNetSendLeagueCommand(pNode->szText, 0);
  }
  SNetSendServerChatCommand(pNode->szText);
  gMessageList_g.DeleteNode(pNode);
}
