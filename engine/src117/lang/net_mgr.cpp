// net_mgr.cpp -- the network turn manager. Bodies are in oracle VA order, and
// that order decides two bodies' shapes: net_init_sync precedes net_mgr_init,
// whose body is net_init_sync inlined plus a tail jump, and GetTurnPackets
// precedes ProgressNetwork, which still calls it.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>             // memset

#include "../h/net_mgr.h"      // our declarations, and net_cmds.h through them
#include "../h/gluRdy.h"       // packet_briefing_start, sub_475D10, sub_476B20
#include "../h/net_player.h"   // leave_game -- packet_briefing_start reaches it
#include "../h/net_sync.h"      // packet_sync, sync_init, UpdateGameHash
#include "../h/net_time.h"      // sub_47A810
#include "../h/net_chat_cmds.h" // sub_470910
#include "../h/net_record.h"   // net_record_cmd
#include "../h/net_rand.h"     // set_rand_enabled
#include "../port/render/port_overlay.h"  // Hyperion: PortOverlayAction
#include "../port/replays/port_replay.h"  // Hyperion: PortReplayTurnsUse, PortReplayTurnsKeep
#include "../h/game_evt.h"    // game_event_reset
#include "../h/Storm.h"        // SStrCopy, SStrPack
#include "../h/victory.h"       // psnprintf
#include "../h/net_error.h"     // net_error_popup -- SendTurns
#include "../h/CNetUnkn.h"      // CNetUnkn -- net_mgr_replay_turns
#include "../h/replay.h"        // replay_get_total_frames, sub_480E00 -- net_mgr_replay_turns
#include "../port/hud/port_hudstate.h"  // Hyperion: the HUD's data layer

// KERNEL32!GetTickCount through the IAT slot 0x4EB2FC. __declspec(dllimport),
// because GetTurnPackets calls it indirectly (`call dword ptr [0x4EB2FC]`).
extern "C" __declspec(dllimport) DWORD __stdcall GetTickCount(void);

// The network-message handler, from netmgr_process_messages' indirect call:
// __fastcall with the player id in ECX, the data in EDX and the remaining byte
// count on the stack, returning the bytes it consumed.
typedef int(__fastcall *net_msg_handler_t)(int nPlayerId, char *pData,
                                           unsigned int nSize);

#include "../gen/globals117.h"

// The arms below sit after #include "../gen/globals117.h", under a bare
// #ifdef OC_DLL_MODE: that header spells an array global's dll-mode arm as a
// pointer, and whichever #define comes last wins, so in the other order every
// sizeof over an array global would be 4 in the payload.

// The globals this TU reaches that gen/globals117.h does not; gdwNetLatency_g,
// sgbBriefingState_g, sgdwPlayerStatus_g, sgbPlyrEndBriefing_g, g_ActiveHumanID,
// gPlayback, gPlayerData and gPlayerSlots_g come from it. Each type is the
// access width 1.17 uses.

// dword_508D68 -- DWORD @0x508D68. netmgr_replay_init stores -1 to it.
#ifdef OC_DLL_MODE
#define dword_508D68 (*reinterpret_cast<DWORD *>(OC_VA(0x508D68u)))
#else
extern DWORD dword_508D68;
#endif

// glGluesMode -- DWORD @0x66A274 (glues.cpp). ProgressNetwork tests it four
// times, each `cmp dword ptr [0x66A274], 19h`.
#ifdef OC_DLL_MODE
#define glGluesMode (*reinterpret_cast<DWORD *>(OC_VA(0x66A274u)))
#else
extern DWORD glGluesMode;
#endif

// dword_672658 -- DWORD @0x672658 (net_cmds.cpp): ProgressNetwork increments it
// and net_init_sync zeroes it.
#ifdef OC_DLL_MODE
#define dword_672658 (*reinterpret_cast<DWORD *>(OC_VA(0x672658u)))
#else
extern DWORD dword_672658;
#endif

// byte_672665 -- BYTE @0x672665 (net_data.cpp): GetTurnPackets writes 1 and 0
// and ProgressNetwork tests it, one byte on every access.
#ifdef OC_DLL_MODE
#define byte_672665 (*reinterpret_cast<BYTE *>(OC_VA(0x672665u)))
#else
extern BYTE byte_672665;
#endif

// dword_672980 -- DWORD @0x672980; netmgr_replay_init zeroes it.
#ifdef OC_DLL_MODE
#define dword_672980 (*reinterpret_cast<DWORD *>(OC_VA(0x672980u)))
#else
extern DWORD dword_672980;
#endif

// sgbSyncTurnEnable -- DWORD @0x67298C. Four bytes on every access, though its
// name says byte.
#ifdef OC_DLL_MODE
#define sgbSyncTurnEnable (*reinterpret_cast<DWORD *>(OC_VA(0x67298Cu)))
#else
extern DWORD sgbSyncTurnEnable;
#endif

// sgpTurns -- char *[8] @0x672994: GetTurnPackets passes it to
// net_receive_turns' `char **ppData`; sgdwTurnLens follows at +0x20.
#ifdef OC_DLL_MODE
#define sgpTurns (*reinterpret_cast<char *(*)[8]>(OC_VA(0x672994u)))
#else
extern char *sgpTurns[8];
#endif

// sgdwTurnLens -- unsigned int[8] @0x6729B4. unsigned int, not DWORD:
// net_receive_turns takes `unsigned int *`, and DWORD is unsigned long.
// net_init_sync clears its 0x20 bytes.
#ifdef OC_DLL_MODE
#define sgdwTurnLens (*reinterpret_cast<unsigned int (*)[8]>(OC_VA(0x6729B4u)))
#else
extern unsigned int sgdwTurnLens[8] OC_UNALIGNED;
#endif

// sgdwBytesInCmdQueue -- DWORD @0x6729D4, read and written 4 bytes at a time.
#ifdef OC_DLL_MODE
#define sgdwBytesInCmdQueue (*reinterpret_cast<DWORD *>(OC_VA(0x6729D4u)))
#else
extern DWORD sgdwBytesInCmdQueue;
#endif

// sgbCmdQueue_g -- BYTE[0x200] @0x6729D8. Only element 0 is touched here; 0x200
// is its storage, up to dword_672BD8.
#ifdef OC_DLL_MODE
#define sgbCmdQueue_g (*reinterpret_cast<BYTE (*)[0x200]>(OC_VA(0x6729D8u)))
#else
extern BYTE sgbCmdQueue_g[0x200] OC_UNALIGNED;
#endif

// dword_6737E8 -- DWORD @0x6737E8, GetTurnPackets' GetTickCount stamp.
#ifdef OC_DLL_MODE
#define dword_6737E8 (*reinterpret_cast<DWORD *>(OC_VA(0x6737E8u)))
#else
extern DWORD dword_6737E8;
#endif

// sgFnChatCmds_g -- the .rdata network-message dispatch table @0x4EF2B8, which
// netmgr_process_messages indexes and calls. Declared here because its element
// type must be complete; 3 entries are reachable (`cmp al, 3h / jnb`).
#ifdef OC_DLL_MODE
#define sgFnChatCmds_g (reinterpret_cast<const net_msg_handler_t *>(OC_VA(0x4EF2B8u)))
#else
extern const net_msg_handler_t sgFnChatCmds_g[3] OC_UNALIGNED;
#endif

// dword_6737EC -- DWORD @0x6737EC, sub_476B20's tick stamp; gen/globals117.h
// does not carry it.
#ifdef OC_DLL_MODE
#define dword_6737EC (*reinterpret_cast<DWORD *>(OC_VA(0x6737ECu)))
#else
extern DWORD dword_6737EC;
#endif

// The recorded-turn buffers net_mgr_replay_turns hands CNetUnkn and sub_478890:
// the storm ids, the turn bytes and their sizes; 0x200 is the byte buffer's
// extent.
#ifdef OC_DLL_MODE
#define byte_672BE8 (reinterpret_cast<BYTE *>(OC_VA(0x672BE8u)))
#define byte_672DE8 (reinterpret_cast<BYTE *>(OC_VA(0x672DE8u)))
#define dword_672FE8 (reinterpret_cast<int *>(OC_VA(0x672FE8u)))
#else
extern BYTE byte_672BE8[];
extern BYTE byte_672DE8[0x200] OC_UNALIGNED;
extern int dword_672FE8[];
#endif

// oc_dwReplayFrame -- the frame counter @0x52651C, spelled as replay.cpp
// spells it.
#ifdef OC_DLL_MODE
#define oc_dwReplayFrame (*reinterpret_cast<unsigned int *>(OC_VA(0x52651Cu)))
#else
extern unsigned int oc_dwReplayFrame;
#endif

// gdwDefTurnsInTransit -- DWORD @0x672660 (net_data.cpp), spelled as
// net_chat_cmds.cpp spells it. SendTurns' turn budget.
#ifdef OC_DLL_MODE
#define gdwDefTurnsInTransit (*reinterpret_cast<DWORD *>(OC_VA(0x672660u)))
#else
extern DWORD gdwDefTurnsInTransit;
#endif

// gdwMaxPacketSize -- DWORD @0x67265C (net_data.cpp), spelled as net_glue.cpp
// spells it. The command queue's capacity.
#ifdef OC_DLL_MODE
#define gdwMaxPacketSize (*reinterpret_cast<DWORD *>(OC_VA(0x67265Cu)))
#else
extern DWORD gdwMaxPacketSize;
#endif

// word_5264B8 -- WORD @0x5264B8, a per-player bit mask tested here as
// `1 << storm id`; every access is `word ptr`.
#ifdef OC_DLL_MODE
#define word_5264B8 (*reinterpret_cast<WORD *>(OC_VA(0x5264B8u)))
#else
extern WORD word_5264B8;
#endif

// gbInPacketProcess -- BYTE @0x672984. net_mgr_process_turns sets it to 1 around
// its work and back to 0.
#ifdef OC_DLL_MODE
#define gbInPacketProcess (*reinterpret_cast<BYTE *>(OC_VA(0x672984u)))
#else
extern BYTE gbInPacketProcess;
#endif

// byte_672985 -- BYTE @0x672985, tested and cleared.
#ifdef OC_DLL_MODE
#define byte_672985 (*reinterpret_cast<BYTE *>(OC_VA(0x672985u)))
#else
extern BYTE byte_672985;
#endif

// sgdwNumSync_g -- DWORD @0x672988, cleared and compared in
// net_mgr_process_turns.
#ifdef OC_DLL_MODE
#define sgdwNumSync_g (*reinterpret_cast<DWORD *>(OC_VA(0x672988u)))
#else
extern DWORD sgdwNumSync_g;
#endif

// dword_672BD8 -- DWORD @0x672BD8, set to 0 and 1 and compared.
#ifdef OC_DLL_MODE
#define dword_672BD8 (*reinterpret_cast<DWORD *>(OC_VA(0x672BD8u)))
#else
extern DWORD dword_672BD8;
#endif

// Four more, typed exactly as gen/globals117.h types them, so that the two
// spellings cannot conflict.

#ifdef OC_DLL_MODE
#define gdwNetLatency_g      (*reinterpret_cast<DWORD *>(OC_VA(0x672990u)))
#else
extern DWORD gdwNetLatency_g;
#endif

#ifdef OC_DLL_MODE
#define sgbBriefingState_g   (*reinterpret_cast<BYTE *>(OC_VA(0x672986u)))
#else
extern BYTE sgbBriefingState_g;
#endif

#ifdef OC_DLL_MODE
#define sgdwPlayerStatus_g   (*reinterpret_cast<DWORD (*)[8]>(OC_VA(0x672670u)))
#else
extern DWORD sgdwPlayerStatus_g[8];
#endif

#ifdef OC_DLL_MODE
#define sgbPlyrEndBriefing_g (*reinterpret_cast<BYTE (*)[12]>(OC_VA(0x672BDCu)))
#else
extern BYTE sgbPlyrEndBriefing_g[12];
#endif

// The bodies, in oracle VA order.

// 0x475AC0 (85 B). Pulls one turn's worth of packets for all eight slots and
// reports whether the game may step. The timeout is a tick difference compared
// unsigned against 2500 ms, so it survives GetTickCount wrapping.
int GetTurnPackets()
{
  DWORD dwNow = GetTickCount();

  if (net_receive_turns(0, 8, sgpTurns, sgdwTurnLens, sgdwPlayerStatus_g)) {
    dword_6737E8 = dwNow;
    byte_672665 = 1;
    return 1;
  }

  int nInTime = dwNow - dword_6737E8 < 0x9C4;
  byte_672665 = 0;
  return nInTime;
}


// 0x475B20 (17 B). A pass-through into sub_475B40 with a trailing 0; under /Gr
// the two register arguments are already where the callee wants them.
// Hyperion: the data pointer is an intptr_t and the bytes used are returned, as
// net_msg_handler_t calls it: at 64 bits an int cut the pointer, and the dispatcher read
// the count from whatever the return register held.
int sub_475B20(int nArg0, intptr_t nArg1, int nArg2)
{
  return sub_475B40(nArg0, nArg1, nArg2, 0);
}


// 0x475B40. Receives one chat packet: validates sender and text, then records
// and shows it (nArg3 == 0) or prefixes the sender's name and routes it to the
// message area. Returns the bytes consumed, 0 on a rejection; `int`, because
// every non-zero exit sets EAX though both forwarders discard it.
int sub_475B40(int nArg0, intptr_t nArg1, int nArg2, int nArg3)
{
  char szMsg[0x52];
  char szLine[0x100];

  if (!IsValidPlayer(static_cast<BYTE>(nArg0)))
    return 0;

  DWORD dwLen = static_cast<DWORD>(nArg2);
  if (dwLen < 2)
    return 0;

  const char *pszText = reinterpret_cast<const char *>(nArg1) + 2;
  int nLen = get_valid_str_len(pszText, 0x50, dwLen - 2, 0, 0);
  if (nLen == 0)
    return 0;

  int nRet = nLen + 2;
  // Two arms with the same return: as one `||`, the length leaves EAX early
  // and the `mov cl, bl` before net_player_get_name is dropped.
  if ((word_5264B8 & (1 << nArg0)) == 0)
    return nRet;
  else if (g_StormToHumanID[nArg0] >= 8)
    return nRet;

  if (nArg3 == 0)
  {
    SStrCopy(&szMsg[2], pszText, 0x50);
    DWORD dwSlot = g_StormToHumanID[nArg0];
    szMsg[0] = 0x5C;
    szMsg[1] = static_cast<char>(dwSlot);
    net_record_cmd(reinterpret_cast<intptr_t>(szMsg), 0x52);
    wrap_map_msg(const_cast<char *>(pszText), dwSlot, 0);
    return nRet;
  }

  psnprintf(szLine, 0x100, error_get_str(0x50),
            net_player_get_name(static_cast<BYTE>(nArg0)));
  SStrPack(szLine, " ", 0x100);
  SStrPack(szLine, pszText, 0x100);
  map_msg_chat_msg(szLine, 0);
  return nRet;
}



// 0x475C60 (17 B). sub_475B20's twin, passing 1 where its sibling passes 0.
// Hyperion: as sub_475B20, the pointer whole and the count returned
int sub_475C60(int nArg0, intptr_t nArg1, int nArg2)
{
  return sub_475B40(nArg0, nArg1, nArg2, 1);
}


// 0x475C80 (55 B). A descending scan, 7..0, over two parallel 8-entry tables
// with an early-out.
int sub_475C80()
{
  int nSlot;

  if (sgbBriefingState_g != 2)
    return 0;

  nSlot = 8;
  do {
    --nSlot;
    if ((sgdwPlayerStatus_g[nSlot] & 0x10000) != 0 && sgbPlyrEndBriefing_g[nSlot] == 0)
      return 0;
  } while (nSlot != 0);

  sgbBriefingState_g = 0;
  return 1;
}

// 0x475CC0 (75 B). The argument is tested twice, before and after the
// leave_game call. The queued command is one byte with a length of 1.
void packet_briefing_start(int nStart)
{
  BYTE bCmd;

  if (nStart)
    leave_game(3);

  if (sgbBriefingState_g == 1) {
    if (nStart) {
      sgbBriefingState_g = 0;
    } else {
      bCmd = 0x54;
      netmgr_queue_cmd(&bCmd, 1);
      sgbBriefingState_g = 2;
    }
  }
}

// Marks the briefing done, clears the eight end-of-briefing flags and sends turns.
// The fill is a memset of 8 of the 12 bytes; the shipped form lowers it to two dword stores.
void sub_475D10()
{
  sgbBriefingState_g = 1;
  memset(sgbPlyrEndBriefing_g, 0, 8);
  SendTurns();
}

// 0x475D30 (1,585 B). Walks one slot's turn buffer and dispatches every in-game
// command, charging each command's length against the bytes left and abandoning
// the buffer once the count goes negative. In playback, commands outside a small
// allowed set are skipped without advancing the cursor, as 1.17 does.
void net_process_game_turn(char *pData, unsigned int nSize, int nFlag)
{
  const BYTE *pCmd = reinterpret_cast<const BYTE *>(pData);
  int nLeft = static_cast<int>(nSize);

  while (nLeft != 0)
  {
    int nPlayback = gPlayback;
    BYTE cmd = *pCmd;
    int nBytes;
    int nText;

    if (nPlayback != 0 && nFlag == 0)
    {
      switch (cmd)
      {
      case 0x05: case 0x08: case 0x10: case 0x11: case 0x37:
      case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x55:
      case 0x56:
        break;

      default: {
        // Hyperion: a command of no size ends the turn, which 1.17 walked forever
        const int nSkip = GetCommandByteSize(reinterpret_cast<const net_cmd_select_t *>(pCmd));
        if (nSkip <= 0)
          goto done;
        nLeft -= nSkip;
        if (nLeft < 0)
          goto done;
        continue;
      }
      }
    }

    switch (cmd)
    {
    case 0x05:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      ++sgdwNumSync_g;
      break;

    case 0x37:
      nBytes = 7;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      if (nPlayback == 0 &&
          !net_recv_sync(reinterpret_cast<const NETSYNCPKT *>(pCmd)))
      {
        dword_672BD8 = 1;
        sgdwPlayerStatus_g[g_ActiveStormID] = 0x10000;
      }
      ++sgdwNumSync_g;
      break;

    case 0x06:
      nLeft -= 5;
      if (nLeft < 0)
        goto done;
      nText = get_valid_str_len(reinterpret_cast<const char *>(pCmd) + 5,
                               0x1C, nLeft, 0, 0);
      if (nText == 0)
        goto done;
      nBytes = nText + 5;
      nLeft -= nText;
      recv_net_cmd_SaveGame(reinterpret_cast<const net_cmd_gamefile_t *>(pCmd));
      break;

    case 0x07:
      nLeft -= 5;
      if (nLeft < 0)
        goto done;
      nText = get_valid_str_len(reinterpret_cast<const char *>(pCmd) + 5,
                               0x1C, nLeft, 0, 0);
      if (nText == 0)
        goto done;
      nBytes = nText + 5;
      nLeft -= nText;
      recv_net_cmd_LoadGame(reinterpret_cast<const net_cmd_loadgame_t *>(pCmd));
      break;

    case 0x08:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_RestartGame();
      break;

    case 0x09:
      nLeft -= 2;
      if (nLeft < 0)
        goto done;
      nBytes = pCmd[1] * 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      nBytes += 2;
      recv_net_cmd_Select(reinterpret_cast<const net_cmd_select_t *>(pCmd));
      break;

    case 0x0A:
      nLeft -= 2;
      if (nLeft < 0)
        goto done;
      nBytes = pCmd[1] * 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      nBytes += 2;
      recv_net_cmd_SelectDeltaAdd(reinterpret_cast<const net_cmd_selectdelta_t *>(pCmd));
      break;

    case 0x0B:
      nLeft -= 2;
      if (nLeft < 0)
        goto done;
      nBytes = pCmd[1] * 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      nBytes += 2;
      recv_net_cmd_SelectDeltaDel(reinterpret_cast<const net_cmd_selectdelta_t *>(pCmd));
      break;

    case 0x0C:
      nBytes = 8;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Placebox(reinterpret_cast<const net_cmd_placebox_t *>(pCmd));
      break;

    case 0x15:
      nBytes = 0x0B;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Action(reinterpret_cast<const net_cmd_action_t *>(pCmd));
      break;

    case 0x58:
      nBytes = 5;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_MinimapPing(reinterpret_cast<const net_cmd_point_t *>(pCmd));
      break;

    case 0x14:
      nBytes = 0x0A;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_RightClick(reinterpret_cast<const net_cmd_rclick_t *>(pCmd));
      break;

    case 0x0D:
      nBytes = 3;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SetFog(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    case 0x0E:
      nBytes = 5;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SetAllies(reinterpret_cast<const net_cmd_dword_t *>(pCmd));
      break;

    case 0x0F:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SetSpeed(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x10:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_PauseOn();
      break;

    case 0x11:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_PauseOff();
      break;

    case 0x12:
      nBytes = 5;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Cheat(reinterpret_cast<const net_cmd_dword_t *>(pCmd));
      break;

    case 0x13:
      nBytes = 3;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_GroupUnits(reinterpret_cast<const net_cmd_group_t *>(pCmd));
      break;

    case 0x18:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Kill();
      break;

    case 0x19:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_KillEgg();
      break;

    case 0x1A:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Stop(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x1B:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Stop_Carrier();
      break;

    case 0x1C:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Stop_Reaver();
      break;

    case 0x1D:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Nothing();
      break;

    case 0x1E:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Return(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x1F:
      nBytes = 3;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Build(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    case 0x20:
      nBytes = 3;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_StopBuild(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    case 0x30:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Research(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x31:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_StopResearch(reinterpret_cast<const net_cmd_t *>(pCmd));
      break;

    case 0x32:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Upgrade(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x33:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_StopUpgrade(reinterpret_cast<const net_cmd_t *>(pCmd));
      break;

    case 0x2C:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Burrow(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x2D:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Deburrow(reinterpret_cast<const net_cmd_t *>(pCmd));
      break;

    case 0x21:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Cloak(reinterpret_cast<const net_cmd_t *>(pCmd));
      break;

    case 0x22:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Decloak(reinterpret_cast<const net_cmd_t *>(pCmd));
      break;

    case 0x23:
      nBytes = 3;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Morph(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    case 0x35:
      nBytes = 3;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_UpgradeBldg(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    case 0x25:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SwitchTankMode(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x26:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SiegeMode(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x28:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_UnloadAll(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x29:
      nBytes = 3;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_ExitTransport(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    // Hyperion: the commands of wide unit ids (h/net_cmds.h)
    case 0x89: case 0x8A: case 0x8B:
      nLeft -= 2;
      if (nLeft < 0)
        goto done;
      nBytes = pCmd[1] * 4;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      nBytes += 2;
      if (cmd == 0x89)
        recv_net_cmd_SelectWide(reinterpret_cast<const net_cmd_selectdelta_w_t *>(pCmd));
      else if (cmd == 0x8A)
        recv_net_cmd_SelectDeltaAddWide(reinterpret_cast<const net_cmd_selectdelta_w_t *>(pCmd));
      else
        recv_net_cmd_SelectDeltaDelWide(reinterpret_cast<const net_cmd_selectdelta_w_t *>(pCmd));
      break;

    // Hyperion: a chunk of a selection command of Hyperion's selection (stage S)
    case NET_CMD_SELECT_CHUNK:
      nLeft -= 3;
      if (nLeft < 0)
        goto done;
      nBytes = pCmd[1] * 4;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      nBytes += 3;
      recv_net_cmd_SelectChunk(reinterpret_cast<const net_cmd_selectchunk_t *>(pCmd));
      break;

    case 0x94:
      nBytes = sizeof(net_cmd_rclick_w_t);
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_RightClickWide(reinterpret_cast<const net_cmd_rclick_w_t *>(pCmd));
      break;

    case 0x95:
      nBytes = sizeof(net_cmd_action_w_t);
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_ActionWide(reinterpret_cast<const net_cmd_action_w_t *>(pCmd));
      break;

    // Hyperion: the commands of an order past 1.17's byte (h/net_cmds.h)
    case 0x96:
      nBytes = sizeof(net_cmd_action_o_t);
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_ActionOrder(reinterpret_cast<const net_cmd_action_o_t *>(pCmd));
      break;

    case 0x8D:
      nBytes = sizeof(net_cmd_placebox_o_t);
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_PlaceboxOrder(reinterpret_cast<const net_cmd_placebox_o_t *>(pCmd));
      break;

    case 0x97:
      nBytes = sizeof(net_cmd_word_t);
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_ResearchWord(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    case 0x98:
      nBytes = sizeof(net_cmd_word_t);
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_UpgradeWord(reinterpret_cast<const net_cmd_word_t *>(pCmd));
      break;

    case 0xA9:
      nBytes = sizeof(net_cmd_dword_t);
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_ExitTransportWide(reinterpret_cast<const net_cmd_dword_t *>(pCmd));
      break;

    case 0x2A:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SummonArchon();
      break;

    case 0x5A:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SummonDArchon();
      break;

    case 0x2B:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_HoldPosition(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x2E:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_CancelNuke();
      break;

    case 0x2F:
      nBytes = 5;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Liftoff(reinterpret_cast<const net_cmd_point_t *>(pCmd));
      break;

    case 0x27:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_BuildFighter();
      break;

    case 0x34:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_StopAddon(reinterpret_cast<const net_cmd_t *>(pCmd));
      break;

    case 0x36:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Stim();
      break;

    case 0x38:
    case 0x39:
      nBytes = 1;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_VoiceToggle(reinterpret_cast<const net_cmd_t *>(pCmd));
      break;

    case 0x3A:
    case 0x3B:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_VoiceSquelch(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x55:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      net_recv_set_latency(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x56:
      nBytes = 0x0A;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_SetReplaySpeed(reinterpret_cast<const net_cmd_replayspeed_t *>(pCmd));
      break;

    case 0x57:
      nBytes = 2;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      recv_net_cmd_Quit(reinterpret_cast<const net_cmd_byte_t *>(pCmd));
      break;

    case 0x5C:
      nBytes = 0x52;
      nLeft -= nBytes;
      if (nLeft < 0)
        goto done;
      wrap_map_msg(const_cast<BYTE *>(pCmd) + 2, pCmd[1], nFlag);
      break;

    case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40:
    case 0x41: case 0x42: case 0x43: case 0x44: case 0x48:
    case 0x5B:
      goto done;

    default:
      goto done;
    }

    net_record_cmd(reinterpret_cast<intptr_t>(pCmd), nBytes);
    PortHudCommand(pCmd);  // Hyperion: the HUD's actions a minute (port_hudstate.cpp)
    pCmd += nBytes;
  }

done:
  ;
}

// 0x476500 (154 B). Sends empty turns until the number in transit reaches
// the default plus, with sync on, the latency; each carries whatever the
// command queue holds, or a lone 5 when it is empty.
void SendTurns()
{
  DWORD dwTurns;
  DWORD dwMax;

  if (!SNetGetTurnsInTransit(&dwTurns)) {
    net_error_popup(0x51, "net_mgr.cpp", 0x8F5, 1);
    return;
  }

  dwMax = gdwDefTurnsInTransit;
  if (sgbSyncTurnEnable)
    dwMax += gdwNetLatency_g;

  while (dwTurns < dwMax) {
    dwTurns++;
    if (sgdwBytesInCmdQueue == 0) {
      sgbCmdQueue_g[0] = 5;
      sgdwBytesInCmdQueue = 1;
    }
    net_send_turn(reinterpret_cast<char *>(sgbCmdQueue_g), sgdwBytesInCmdQueue);
    sgdwBytesInCmdQueue = 0;
    if (sgbSyncTurnEnable)
      packet_sync();
  }
}


// 0x4765A0 (67 B). Resets the turn manager's own state: five scalar stores and
// a 32-byte clear. The clear's count is spelled out, not sizeof(sgdwTurnLens),
// which would be 4 if gen/globals117.h ever spelled the array as a pointer.
void net_init_sync()
{
  gdwNetLatency_g = 0;
  sgbBriefingState_g = 0;
  sgbSyncTurnEnable = 0;
  dword_672658 = 0;
  sgdwBytesInCmdQueue = 0;
  memset(sgdwTurnLens, 0, 8 * sizeof(unsigned int));
}


// 0x4765F0 (6 B). `mov eax, [0x672990] / ret`; net_mgr.h says why it returns
// int.
int net_get_latency()
{
  return gdwNetLatency_g;
}


// 0x476600 (73 B). net_init_sync's body inlined, then a tail jump into
// sub_470910 with `mov cl, 1h`.
void net_mgr_init()
{
  net_init_sync();
  sub_470910(1);
}


// 0x476650 (437 B). Runs the pre-game lobby command stream for every slot still
// in the game. Each arm charges its command's length against the slot's
// remaining bytes and gives up on the slot once the count goes negative, so every
// short-count exit shares a target with the default. The case order is 1.17's.
void net_mgr_process_lobby_turns()
{
  g_ActiveStormID = 8;
  do
  {
    int nSlot = g_ActiveStormID - 1;
    g_ActiveStormID = nSlot;

    if ((sgdwPlayerStatus_g[nSlot] & 0x20000) != 0)
    {
      int nLeft = static_cast<int>(sgdwTurnLens[nSlot]);
      const BYTE *pCmd = reinterpret_cast<const BYTE *>(sgpTurns[nSlot]);

      while (nLeft != 0)
      {
        switch (*pCmd)
        {
        case 0x05:
          if (--nLeft < 0)
            goto done;
          ++pCmd;
          break;
        case 0x3C:
          if (--nLeft < 0)
            goto done;
          recv_net_cmd_StartGame(pCmd, nSlot);
          ++pCmd;
          break;
        case 0x3D:
          nLeft -= 2;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_download_progress(pCmd, nSlot);
          pCmd += 2;
          break;
        case 0x3E:
          nLeft -= 6;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_modify_slot(pCmd, nSlot);
          pCmd += 6;
          break;
        case 0x3F:
          nLeft -= 8;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_version(pCmd, nSlot);
          pCmd += 8;
          break;
        case 0x40:
          nLeft -= 0x12;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_version_verify(pCmd, nSlot, 0);
          pCmd += 0x12;
          break;
        case 0x41:
          nLeft -= 3;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_change_race(pCmd, nSlot);
          pCmd += 3;
          break;
        case 0x42:
          nLeft -= 2;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_change_team(pCmd, nSlot);
          pCmd += 2;
          break;
        case 0x43:
          nLeft -= 2;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_change_force(pCmd, nSlot);
          pCmd += 2;
          break;
        case 0x44:
          nLeft -= 3;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_change_slot(pCmd, nSlot);
          pCmd += 3;
          break;
        case 0x45:
          nLeft -= 3;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_swap_slots(pCmd, nSlot);
          pCmd += 3;
          break;
        case 0x48:
          nLeft -= 0x0D;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_seed_init(pCmd, nSlot);
          pCmd += 0x0D;
          break;
        case 0x5B:
          if (--nLeft < 0)
            goto done;
          recv_net_cmd_make_public(pCmd, nSlot);
          ++pCmd;
          break;
        case 0x54:
          if (--nLeft < 0)
            goto done;
          sgbPlyrEndBriefing_g[g_ActiveStormID] = 1;
          ++pCmd;
          break;
        case 0x3A:
        case 0x3B:
          nLeft -= 2;
          if (nLeft < 0)
            goto done;
          recv_net_cmd_VoiceSquelch(
              reinterpret_cast<const net_cmd_byte_t *>(pCmd));
          pCmd += 2;
          break;
        default:
          goto done;
        }
      }
    done:
      ;
    }
  } while (g_ActiveStormID != 0);

  g_ActiveStormID = 8;
}


// 0x4768A0 (258 B). Runs one game turn for every slot still in the game, walking
// the eight slots downward, then restores the active-player triple to the local
// player's. g_ActiveStormID is both the published slot and the loop's value, and
// a callee can move it, so only the two paths that ran a turn re-read it.
void net_mgr_process_turns()
{
  set_rand_enabled(1);
  gbInPacketProcess = 1;

  int nSlot = 8;
  dword_672BD8 = 0;
  do
  {
    g_ActiveStormID = --nSlot;
    if ((sgdwPlayerStatus_g[nSlot] & 0x20000) != 0)
    {
      g_ActiveNationID = g_StormToNationID[nSlot];
      g_ActiveHumanID = g_StormToHumanID[nSlot];
      sgdwNumSync_g = 0;
      net_process_game_turn(sgpTurns[nSlot], sgdwTurnLens[nSlot], 0);

      if (gPlayback == 0 && sgdwNumSync_g != 1)
      {
        nSlot = g_ActiveStormID;
        dword_672BD8 = 1;
        sgdwPlayerStatus_g[nSlot] = 0x10000;
      }
      else
      {
        // The read is spelled out in BOTH arms on purpose: one read above the
        // `if` is CSE'd into a single load and costs the loop's rel8 back edge.
        nSlot = g_ActiveStormID;
      }
    }
  } while (nSlot != 0);

  g_ActiveStormID = -1;
  if (dword_672BD8 != 0)
    sub_47A630();

  g_ActiveNationID = g_LocalNationID;
  g_ActiveHumanID = g_LocalHumanID;
  g_ActiveStormID = g_LocalStormID;
  gbInPacketProcess = 0;
  set_rand_enabled(0);

  if (byte_672985 != 0)
  {
    byte_672985 = 0;
    game_event_reset(0);
  }
}


// 0x4769B0 (355 B). Replays the recorded turns that fall due this frame. The
// frame and the two counts compare signed (`jl` / `jle`) although both globals
// are declared DWORD, hence the casts. Each turn runs as the player whose
// storm id recorded it, and the local ids are put back afterwards.
void net_mgr_replay_turns()
{
  int nFrame;
  int i;
  int nOffset;
  int j;
  char *pTurn;
  int nSize;

  if (!sgbSyncTurnEnable)
    return;

  nFrame = oc_dwReplayFrame;
  if (nFrame >= replay_get_total_frames()) {
    sub_480E00(1);
    do_victory_dlg();
    return;
  }
  if (nFrame < static_cast<int>(dword_508D68))
    return;

  CNetUnkn turns(byte_672DE8, dword_672FE8, 0x200);
  // Hyperion: a frame read one call early keeps the buffer it grew, so
  // it is dispatched whole instead of from the static arrays' stale tail
  // (src117/port/replays/port_replay.cpp).
  PortReplayTurnsUse(&turns);

  if (nFrame > static_cast<int>(dword_508D68)) {
    dword_508D68 = sub_478890(reinterpret_cast<intptr_t>(&dword_672980),
                              reinterpret_cast<intptr_t>(byte_672BE8),
                              reinterpret_cast<intptr_t>(&turns));
    PortReplayTurnsKeep(&turns);
    if (static_cast<int>(dword_508D68) == -1 || nFrame < static_cast<int>(dword_508D68))
      return;
  }

  if (dword_672980 == 0)
    return;

  set_rand_enabled(1);
  gbInPacketProcess = 1;
  for (i = 0, nOffset = 0; i < static_cast<int>(dword_672980); i++) {
    for (j = 0; j < 8; j++) {
      if (gPlayerData[j].dwStormID == byte_672BE8[i]) {
        g_ActiveHumanID = gPlayerData[j].dwPlayerID;
        g_ActiveNationID = gPlayerData[j].dwPlayerID;
        break;
      }
    }
    // Both loads precede the store: `mov edx, [ebx+esi*4]` comes before it.
    pTurn = reinterpret_cast<char *>(turns.pBytes);
    nSize = turns.pnSizes[i];
    sgdwNumSync_g = 0;
    net_process_game_turn(pTurn + nOffset, nSize, 1);
    nOffset += turns.pnSizes[i];
  }
  g_ActiveNationID = g_LocalNationID;
  g_ActiveHumanID = g_LocalHumanID;
  dword_672980 = 0;
  g_ActiveStormID = g_LocalStormID;
  gbInPacketProcess = 0;
  set_rand_enabled(0);
}

// 0x476B20 (89 B). A 250 ms-throttled network pump with an out-parameter. The
// throttle compare is unsigned, so the tick stamp is a DWORD. The plain
// `if (byte_672665 != 0) dword_6737EC = dwNow;` is if-converted to a cmovnz over
// an unconditional store.
int sub_476B20(int *pnOut)
{
  DWORD dwNow;

  *pnOut = 0;

  dwNow = GetTickCount();
  if (dwNow - dword_6737EC < 250)
    return 1;

  ProgressNetwork();

  if (byte_672665 != 0)
    dword_6737EC = dwNow;

  *pnOut = sub_475C80();
  return glGluesMode != 0x19;
}

// 0x476B80 (117 B). The receive pump: drain every queued network message, and
// within each message run the .rdata handler for each packet until the buffer is
// spent. The locals are address-taken by net_receive_message. The inner loop
// exits on zero bytes left, an opcode of 3 or more, or a handler that consumed
// nothing.
void netmgr_process_messages()
{
  unsigned int nSize;
  char *pData;
  int nPlayerId;

  while (net_receive_message(&nPlayerId, &pData, &nSize)) {
    while (nSize != 0) {
      BYTE bOpcode = static_cast<BYTE>(*pData);
      if (bOpcode >= 3)
        break;

      int nUsed = sgFnChatCmds_g[bOpcode](nPlayerId, pData, nSize);
      if (nUsed == 0)
        break;

      pData += nUsed;
      nSize -= nUsed;
    }
  }
}


// 0x476C00 (122 B). One network step of the game loop. glGluesMode == 0x19 means
// the glue layer has torn the game down, and the body re-tests it between every
// pair of calls, because any of them can be what tears it down. GetTurnPackets
// is called, not inlined. The counter is incremented through a local pointer,
// which keeps the payload build's read-modify-write.
BOOL ProgressNetwork()
{
  if (glGluesMode == 0x19)
    return 0;

  if (!GetTurnPackets()) {
    if (sgbSyncTurnEnable != 0)
      sub_47A810();
  }

  if (byte_672665 == 0)
    return 0;
  if (glGluesMode == 0x19)
    return 0;

  SendTurns();

  if (glGluesMode == 0x19)
    return 0;

  if (sgbSyncTurnEnable != 0)
    net_mgr_process_turns();
  else
    net_mgr_process_lobby_turns();

  if (glGluesMode == 0x19)
    return 0;

  DWORD *pdwTurn = &dword_672658;
  ++*pdwTurn;

  if (sgbSyncTurnEnable != 0)
    UpdateGameHash();

  return 1;
}


// 0x476C80 (137 B). Appends one command to the outgoing queue, first flushing a
// full queue as a turn when the turn pipeline has room; single-player drops what
// does not fit. The queue store is written in both arms; MSVC merges them.
void __fastcall netmgr_queue_cmd(const void *pCmd, DWORD dwLen)
{
  DWORD dwTurns;
  DWORD dwUsed = sgdwBytesInCmdQueue;

  PortOverlayAction(pCmd);  // Hyperion: Settings' actions per minute counts it
  if (dwUsed + dwLen > gdwMaxPacketSize) {
    if (gwGameMode == 4)
      return;
    if (!SNetGetTurnsInTransit(&dwTurns)) {
      net_error_popup(0x51, "net_mgr.cpp", 0xE1, 1);
      return;
    }
    if (dwTurns >= 0x10 - gdwDefTurnsInTransit)
      return;
    netmgr_send_turn();
    dwUsed = sgdwBytesInCmdQueue;
  }
  memcpy(&sgbCmdQueue_g[dwUsed], pCmd, dwLen);
  dwUsed += dwLen;
  if (dwUsed & 1) sgdwBytesInCmdQueue = dwUsed; else sgdwBytesInCmdQueue = dwUsed;
}

// 0x476D10 (103 B). Receives the "latency changed" packet, clamps the new value
// to 0..2, stores it and announces who did it with string 0x45 + the value. The
// psnprintf call is written once per gPlayback arm (the live game names the
// player from gPlayerData, a replay from gPlayerSlots_g), and g_ActiveHumanID is
// read into a local before the latency store, which keeps 1.17's order.
void net_recv_set_latency(const net_cmd_byte_t *pCmd)
{
  char szBuf[0x80];

  DWORD dwLatency = pCmd->bArg;
  if (dwLatency > 2)
    dwLatency = 2;

  DWORD dwId = g_ActiveHumanID;
  gdwNetLatency_g = dwLatency;

  if (gPlayback)
    psnprintf(szBuf, 0x80, error_get_str(dwLatency + 0x45),
              gPlayerSlots_g[dwId].szName);
  else
    psnprintf(szBuf, 0x80, error_get_str(dwLatency + 0x45),
              gPlayerData[dwId].szName);

  map_msg_chat_msg(szBuf, 0);
}


// 0x476D80 (21 B). Two global stores and a bare `ret`.
void netmgr_replay_init()
{
  dword_508D68 = static_cast<DWORD>(-1);
  dword_672980 = 0;
}


// 0x476DA0 (62 B). Flushes the outgoing command queue as one turn. An empty
// queue is not skipped: it is sent as a single opcode-5 byte, because the turn
// itself is the synchronisation event. The length lives in a local, and the
// final store is written in both arms so the sync guard is read before it.
void netmgr_send_turn()
{
  DWORD dwLen = sgdwBytesInCmdQueue;

  if (dwLen == 0) {
    dwLen = 1;
    sgbCmdQueue_g[0] = 5;
    sgdwBytesInCmdQueue = dwLen;
  }

  net_send_turn(reinterpret_cast<char *>(sgbCmdQueue_g), dwLen);

  if (sgbSyncTurnEnable != 0) {
    sgdwBytesInCmdQueue = 0;
    packet_sync();
    return;
  }
  sgdwBytesInCmdQueue = 0;
}


// 0x476DE0 (28 B). Queues the 2-byte latency packet, storing the argument
// before the opcode, as the send_net_cmd_* family in net_cmds.cpp does.
void netmgr_set_latency(int nLatency)
{
  net_cmd_byte_t cmd;
  cmd.bArg = (BYTE)nLatency;
  cmd.bOpcode = 0x55;
  netmgr_queue_cmd(&cmd, sizeof(cmd));
}


// 0x476E00 (21 B). Enables or disables turn sync. The 4-byte store is
// unconditional; only the two calls are guarded, the second a tail jump.
void netmgr_set_sync(int nEnabled)
{
  sgbSyncTurnEnable = nEnabled;

  if (nEnabled != 0) {
    sync_init();
    packet_sync();
  }
}
