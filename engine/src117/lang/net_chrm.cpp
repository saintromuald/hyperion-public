// net_chrm.cpp -- the lobby packet layer: the save-slot table, the inbound
// message dispatch, and the outbound packet builders. Bodies are in oracle VA
// order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <string.h>        // strlen and memcpy

#include "../h/net_chrm.h"
#include "../h/net_cmds.h"    // net_send_message, IsValidPlayer, error_get_str
#include "../h/net_misc.h"    // net_drop_player, IsBNetGame
#include "../h/net_player.h"  // net_player_get_name
#include "../h/net_glue.h"    // IsGameCreator
#include "../h/gametype.h"    // IsScenarioGame
#include "../h/forces.h"      // GetForceName, SetForceName
#include "../h/gluChat.h"     // on_recv_chat_from, on_chrm_kicked
#include "../h/fd.h"          // on_fd_transfer
#include "../h/gds_local.h"   // psnprintf
#include "../h/Storm.h"       // SStrCopy, SStrPack
#include "../h/net_slot.h"    // get_selectable_races, set_selectable_races
#include "../h/Teams.h"       // IsTeamGame, GetNumTeams
#include "../h/CSprite.h"     // GetMaxPlayersPerTeam
#include "../h/saveload.h"    // sub_499850, sub_499860
#include "../h/net_chat_cmds.h"  // create_net_cmd_version, sub_470910
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.
// 0x672608 gbSaveSlot_g, eight entries, comes from gen/globals117.h.

// 0x4EF028 -- 1.17's "net_chrm.cpp" owner-tag string, IsGameCreator's file
// argument.
#ifdef OC_DLL_MODE
#define sgszNetChrmCpp (reinterpret_cast<const char *>(OC_VA(0x4EF028u)))
#else
extern const char sgszNetChrmCpp[1];
#endif

// 0x4EF038 -- "/astat %s", the Battle.net stats query on_recv_player_joined
// sends for the player who just arrived.
#ifdef OC_DLL_MODE
#define sgszAstatFmt (reinterpret_cast<const char *>(OC_VA(0x4EF038u)))
#else
extern const char sgszAstatFmt[1];
#endif

// 0x672610 -- the saved chat-room player types, one byte for each of 12 slots.
#ifdef OC_DLL_MODE
#define byte_672610 (reinterpret_cast<BYTE *>(OC_VA(0x672610u)))
#else
extern BYTE byte_672610[12];
#endif

// 0x5264B8 -- the per-player "show chat from this player" mask, a WORD tested
// against 1 << player.
#ifdef OC_DLL_MODE
#define word_5264B8 (*reinterpret_cast<WORD *>(OC_VA(0x5264B8u)))
#else
extern WORD word_5264B8;
#endif

// 0x673D48 -- the chat-room slot records, 12 of them, spelled as net_slot.cpp
// and forces.cpp spell them.
#ifndef OC_NET_CHRM_gNetPlayerData_g
#define OC_NET_CHRM_gNetPlayerData_g
#ifdef OC_DLL_MODE
#define gNetPlayerData_g (reinterpret_cast<PlayerData *>(OC_VA(0x673D48u)))
#else
extern PlayerTable<PlayerData, PlayerTagNetData> gNetPlayerData_g;
#endif
#endif

// 0x534890 -- the per-slot force bytes, spelled as forces.cpp spells them, and
// the four after them at 0x534898; the lobby-state packet carries all twelve.
#ifndef OC_FORCES_sgbPlayerForce_s
#define OC_FORCES_sgbPlayerForce_s
#ifdef OC_DLL_MODE
#define sgbPlayerForce_s (reinterpret_cast<BYTE *>(OC_VA(0x534890u)))
#else
extern BYTE sgbPlayerForce_s[8];
#endif
#endif

#ifndef OC_NET_CHRM_byte_534898
#define OC_NET_CHRM_byte_534898
#ifdef OC_DLL_MODE
#define byte_534898 (reinterpret_cast<BYTE *>(OC_VA(0x534898u)))
#else
extern BYTE byte_534898[4];
#endif
#endif

// 0x5264BC -- the map tileset, spelled as src117/lang/maphdr.cpp spells it.
// 0x526490 -- read here only as its low byte, the save-game flag sent back to
// the host; spelled as src117/lang/game.cpp and placebox.cpp spell it.
#ifndef dword_526490
#ifdef OC_DLL_MODE
#define dword_526490 (*reinterpret_cast<DWORD *>(OC_VA(0x526490u)))
#else
extern DWORD dword_526490;
#endif
#endif

#ifndef g_MapTileset
#ifdef OC_DLL_MODE
#define g_MapTileset (*reinterpret_cast<WORD *>(OC_VA(0x5264BCu)))
#else
extern WORD g_MapTileset;
#endif
#endif

// Storm.dll imports, extern "C" so the names do not mangle.
// SNetSendServerChatCommand is declared in Storm.h.
extern "C" {
BOOL __stdcall SNetGetPlayerName(DWORD dwPlayerId, char *pszBuffer,
                                 DWORD dwBufferSize);
}

// The wire packets, TU-local because nothing in src117/h takes one. Each size
// is the length its builder hands net_send_message.

#pragma pack(push, 1)

// Opcode 0x4E, the kick and its reason.
struct net_chrm_kick_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ BYTE bReason;
};

// Opcode 0x4F, one file-transfer chunk: a length and up to 0x200 bytes; the
// length sent is the count plus 3.
struct net_chrm_fd_t {
  /*0x00*/ BYTE  bOpcode;
  /*0x01*/ WORD  wLen;
  /*0x03*/ BYTE  data[0x200];
};

// Opcodes 0x4C and 0x4D, a chat or whisper line of up to 0x78 bytes.
struct net_chrm_chat_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ char szText[0x78];
};

// Opcode 0x4A, the whole lobby state, 0x3F bytes.
struct net_chrm_state_t {
  /*0x00*/ BYTE bOpcode;
  /*0x01*/ WORD wTileset;
  /*0x03*/ WORD wMapWidth;
  /*0x05*/ WORD wMapHeight;
  /*0x07*/ BYTE bType[12];
  /*0x13*/ BYTE bRace[12];
  /*0x1F*/ BYTE bSlotType[12];
  /*0x2B*/ BYTE bForce[12];
  /*0x37*/ BYTE bRaces[8];
};

#pragma pack(pop)

static_assert(offsetof(net_chrm_kick_t, bOpcode) == 0x00, "mov byte ptr [ebp-4h], 4Eh");
static_assert(sizeof(net_chrm_kick_t) == 2, "packet_kick pushes 2 as the length");
static_assert(offsetof(net_chrm_kick_t, bReason) == 0x01, "mov byte ptr [ebp-3h], dl");

static_assert(offsetof(net_chrm_fd_t, bOpcode) == 0x00, "mov byte ptr [ebp-204h], 4Fh");
static_assert(sizeof(net_chrm_fd_t) == 0x203, "the 204h frame less the alignment byte");
static_assert(offsetof(net_chrm_fd_t, wLen) == 0x01, "mov word ptr [ebp-203h], si");
static_assert(offsetof(net_chrm_fd_t, data) == 0x03, "lea eax, dword ptr [ebp-201h]");

static_assert(offsetof(net_chrm_forces_t, bOpcode) == 0x00, "mov byte ptr [ebp-7Ch], 4Bh");
static_assert(sizeof(net_chrm_forces_t) == 0x79, "packet_forces pushes 79h");
static_assert(offsetof(net_chrm_forces_t, szNames) == 0x01, "the last GetForceName dest is ebp-7Bh");

static_assert(offsetof(net_chrm_chat_t, bOpcode) == 0x00, "mov byte ptr [ebp-7Ch], 4Ch");
static_assert(sizeof(net_chrm_chat_t) == 0x79, "1 + the 78h SStrCopy capacity");
static_assert(offsetof(net_chrm_chat_t, szText) == 0x01, "lea eax, dword ptr [ebp-7Bh]");

static_assert(sizeof(net_chrm_state_t) == 0x3F, "packet_lobby_info pushes 3Fh");
static_assert(offsetof(net_chrm_state_t, bOpcode) == 0x00, "mov byte ptr [ebp-44h], 4Ah");
static_assert(offsetof(net_chrm_state_t, wTileset) == 0x01, "mov word ptr [ebp-43h], ax");
static_assert(offsetof(net_chrm_state_t, wMapWidth) == 0x03, "mov word ptr [ebp-41h], ax");
static_assert(offsetof(net_chrm_state_t, wMapHeight) == 0x05, "mov word ptr [ebp-3Fh], ax");
static_assert(offsetof(net_chrm_state_t, bType) == 0x07, "mov byte ptr [ebp+edx-3Dh], cl");
static_assert(offsetof(net_chrm_state_t, bRace) == 0x13, "mov byte ptr [ebp+edx-31h], cl");
static_assert(offsetof(net_chrm_state_t, bSlotType) == 0x1F, "movq qword ptr [ebp-25h], xmm0");
static_assert(offsetof(net_chrm_state_t, bForce) == 0x2B, "mov dword ptr [ebp-19h], eax");
static_assert(offsetof(net_chrm_state_t, bRaces) == 0x37, "lea ecx, dword ptr [ebp-0Dh]");

// The bodies, in oracle VA order.

// Marks a player as having no save slot.
void ClearSaveSlot(BYTE bPlayer)
{
  gbSaveSlot_g[bPlayer] = 8;
}

// Sends one file-transfer chunk. The payload is copied into the packet rather
// than sent from the caller's buffer, and the length word is the same count.
void __stdcall FDTransmission(void *pMsg, int nLen, char cPlayer)
{
  net_chrm_fd_t pkt;

  pkt.bOpcode = 0x4F;
  pkt.wLen = static_cast<WORD>(nLen);
  memcpy(pkt.data, pMsg, nLen);
  net_send_message(static_cast<BYTE>(cPlayer), &pkt, nLen + 3);
}

// The save slot a player is using, or 8 for none.
BYTE GetSaveSlot(BYTE bPlayer)
{
  return gbSaveSlot_g[bPlayer];
}

// Pulls one lobby packet off the wire and routes it. Every arm answers with the
// opcode it handled, an unroutable packet with 0x52, and an empty queue with
// 0x51. The cases are in 1.17's block order; 0x4C and 0x4D share one arm.
int net_process_lobby_msg(intptr_t nArg0)
{
  int nPlayer;
  unsigned int nLen;
  char *pData;

  if (!net_receive_message(&nPlayer, &pData, &nLen))
    return 0x51;

  switch (static_cast<BYTE>(pData[0])) {
    case 0x4A:
      if (nLen != 0x3F)
        break;
      on_recv_player_data(reinterpret_cast<intptr_t>(pData), nPlayer);
      return 0x4A;

    case 0x4B:
      if (nLen != 0x79)
        break;
      on_recv_force_names(reinterpret_cast<const net_chrm_forces_t *>(pData), nPlayer);
      return 0x4B;

    case 0x49:
      if (nLen != 5)
        break;
      on_recv_player_joined(pData, nPlayer);
      return 0x49;

    case 0x4C:
    case 0x4D: {
      char *pMsg;

      if (nLen < 1)
        break;
      if (!get_valid_str_len(pData + 1, 0x78, nLen - 1, 0, 0))
        break;

      // pMsg and nFrom are 1.17's reloads after the calls.
      pMsg = pData;
      if (static_cast<BYTE>(pMsg[0]) == 0x4C) {
        int nFrom = nPlayer;

        if (IsValidPlayer(static_cast<BYTE>(nFrom)) &&
            (word_5264B8 & (1 << nFrom)) != 0)
          on_recv_chat_from(nFrom, reinterpret_cast<intptr_t>(pMsg + 1));
      } else {
        on_recv_whisper_from(reinterpret_cast<intptr_t>(pMsg), nPlayer);
      }
      return 0x4C;
    }

    case 0x4E:
      if (nLen != 2)
        break;
      if (nPlayer == 0)
        on_chrm_kicked(reinterpret_cast<net_chrm_kick_t *>(pData)->bReason);
      return 0x4E;

    case 0x4F: {
      net_chrm_fd_t *pFd = reinterpret_cast<net_chrm_fd_t *>(pData);

      on_fd_transfer(pFd->data, pFd->wLen, nPlayer);
      return 0x4F;
    }

    case 0x50:
      return 0x50;
  }

  return 0x52;
}

// Resets every save slot, counting down.
void InitSaveSlots()
{
  int i = 8;

  do {
    --i;
    gbSaveSlot_g[i] = 8;
  } while (i != 0);
}

// Pushes the saved player types back into the chat-room slot records.
void sub_4719E0()
{
  int i = 12;

  do {
    --i;
    gNetPlayerData_g[i].nType = byte_672610[i];
  } while (i != 0);
}

// Saves the chat-room slot records' player types away again, through a local
// pointer as 1.17 does.
void sub_471A10()
{
  BYTE *pbSaved = byte_672610;
  int i = 12;

  do {
    --i;
    pbSaved[i] = gNetPlayerData_g[i].nType;
  } while (i != 0);
}

// Claims a save slot for a player. With nArg2 set the claim is unconditional;
// otherwise the slot must be free and the player must not already hold one.
int sub_471A40(BYTE bArg0, BYTE bArg1, int nArg2)
{
  if (nArg2 == 0) {
    int i = 8;

    do {
      --i;
      if (gbSaveSlot_g[i] == bArg1)
        return 0;
    } while (i != 0);

    if (gbSaveSlot_g[bArg0] != 8)
      return 0;
  }

  gbSaveSlot_g[bArg0] = bArg1;
  return 1;
}

// Takes the four force names out of a 0x4B packet. Only a non-creator in a
// scenario game applies them, and only from the host.
void on_recv_force_names(const net_chrm_forces_t *pkt, int nPlayer)
{
  int i;

  if (IsGameCreator(sgszNetChrmCpp, 0x120))
    return;
  if (nPlayer != 0)
    return;
  if (!IsScenarioGame(NULL))
    return;

  i = 4;
  do {
    --i;
    SetForceName(i + 1, pkt->szNames[i]);
  } while (i != 0);
}

// Installs the whole lobby state a 0x4A packet carries. Only a non-creator
// applies it, and only from the host. dwPlayerID is the loop's first store, and
// the save flag goes through an int local, as in 1.17.
void on_recv_player_data(intptr_t nArg0, int nArg1)
{
  const net_chrm_state_t *pPkt =
      reinterpret_cast<const net_chrm_state_t *>(nArg0);
  WORD wLocal;
  BYTE bSaveSlot;
  BYTE bSaveFlag;
  int i;

  if (IsGameCreator(sgszNetChrmCpp, 0xD5))
    return;
  if (nArg1 != 0)
    return;

  g_MapTileset = pPkt->wTileset;
  gwMapWidth = pPkt->wMapWidth;
  gwMapHeight = pPkt->wMapHeight;

  PlayerClear(gNetPlayerData_g);  // Hyperion: and Hyperion's slots

  i = 12;
  do {
    --i;
    gNetPlayerData_g[i].dwPlayerID = i;
    gNetPlayerData_g[i].dwStormID = -1;
    gNetPlayerData_g[i].nType = pPkt->bType[i];
    gNetPlayerData_g[i].nRace = pPkt->bRace[i];
    gNetPlayerData_g[i].nTeam = pPkt->bForce[i];
  } while (i != 0);

  if (IsTeamGame()) {
    int nSlot = 0;
    int nPerTeam = GetMaxPlayersPerTeam();
    int nTeam;

    for (nTeam = 0; nTeam < GetNumTeams(); ++nTeam) {
      int n;

      if (nSlot >= 8)
        break;

      for (n = 0; n < nPerTeam; ++n) {
        if (nSlot >= 8)
          break;
        gNetPlayerData_g[nSlot].nTeam = static_cast<BYTE>(nTeam + 1);
        ++nSlot;
      }
    }
  }

  memcpy(byte_672610, pPkt->bSlotType, 12);
  memcpy(sgbPlayerForce_s, pPkt->bForce, 8);
  memcpy(byte_534898, pPkt->bForce + 8, 4);
  set_selectable_races(const_cast<BYTE *>(pPkt->bRaces), 8);

  if (gpLoadGameFile != 0)
    bSaveSlot = static_cast<BYTE>(sub_499850());
  else
    bSaveSlot = 0;

  {
    int nTmp = static_cast<BYTE>(dword_526490);

    bSaveFlag = static_cast<BYTE>(gpLoadGameFile != 0 ? nTmp : 0);
  }

  sub_477FB0(&wLocal);
  create_net_cmd_version(wLocal, 1, 5, bSaveSlot, bSaveFlag, sub_499860(), 0);
  sub_470910(3);
}

// Announces an arriving player in the chat room and asks Battle.net for their
// record. A player Storm cannot name is dropped silently.
void on_recv_player_joined(void *pArg0, int nArg1)
{
  const net_chrm_req_lobby_t *pkt =
      reinterpret_cast<const net_chrm_req_lobby_t *>(pArg0);
  char szMsg[0x91];
  char szName[0x19];
  DWORD dwStormID;

  if (nArg1 != 0)
    return;

  dwStormID = pkt->nValue;
  if (dwStormID >= 8)
    return;
  if (dwStormID == g_LocalStormID)
    return;
  if (!SNetGetPlayerName(dwStormID, szName, sizeof(szName)))
    return;
  if (szName[0] == 0)
    return;

  psnprintf(szMsg, sizeof(szMsg), error_get_str(0x4A), szName);
  on_recv_chat_from(8, reinterpret_cast<intptr_t>(szMsg));

  if (IsBNetGame()) {
    psnprintf(szMsg, sizeof(szMsg), sgszAstatFmt, szName);
    SNetSendServerChatCommand(szMsg);
  }
}

// Shows a whisper. The sender's bit in the mask must be set: a squelched
// player's whisper is dropped before any formatting.
void on_recv_whisper_from(intptr_t nArg0, int nArg1)
{
  char szMsg[0x104];

  if (!IsValidPlayer(static_cast<BYTE>(nArg1)))
    return;
  if ((word_5264B8 & (1 << nArg1)) == 0)
    return;

  psnprintf(szMsg, sizeof(szMsg), error_get_str(0x50),
            net_player_get_name(static_cast<BYTE>(nArg1)));
  SStrPack(szMsg, " ", sizeof(szMsg));
  SStrPack(szMsg, reinterpret_cast<const char *>(nArg0 + 1), sizeof(szMsg));
  on_recv_chat_from(-1, reinterpret_cast<intptr_t>(szMsg));
}

// Kicks a player: the same 0x4E packet three times, then the drop.
void packet_kick(DWORD dwStormID, BYTE bReason)
{
  net_chrm_kick_t pkt;

  pkt.bOpcode = 0x4E;
  pkt.bReason = bReason;
  net_send_message(dwStormID, &pkt, sizeof(pkt));
  net_send_message(dwStormID, &pkt, sizeof(pkt));
  net_send_message(dwStormID, &pkt, sizeof(pkt));
  net_drop_player(dwStormID, 0, 3);
}

// Broadcasts a chat-room line. sub_46FD90 is the slash-command hook: a handled
// command is not sent on.
void packet_chat(const char *pszText)
{
  net_chrm_chat_t pkt;
  size_t nLen;

  if (sub_46FD90(pszText))
    return;

  nLen = strlen(pszText) + 1;
  if (nLen > 0x78)
    return;

  pkt.bOpcode = 0x4C;
  SStrCopy(pkt.szText, pszText, 0x78);
  net_send_message(-1, &pkt, static_cast<int>(nLen + 1));
}

// Sends the four force names to one player.
void packet_forces(int nPlayer)
{
  net_chrm_forces_t pkt;
  int i = 4;

  pkt.bOpcode = 0x4B;
  do {
    --i;
    GetForceName(i + 1, pkt.szNames[i], 0x1E);
  } while (i != 0);

  net_send_message(nPlayer, &pkt, sizeof(pkt));
}

// Sends a joining player the whole lobby state, then the end-of-state marker.
// The slot types go as one 12-byte copy, as in 1.17.
void packet_lobby_info(int nArg0)
{
  net_chrm_state_t pkt;
  BYTE bEnd;
  int i = 12;

  pkt.wTileset = g_MapTileset;
  pkt.wMapWidth = gwMapWidth;
  pkt.wMapHeight = gwMapHeight;
  pkt.bOpcode = 0x4A;

  do {
    --i;
    pkt.bType[i] = gNetPlayerData_g[i].nType;
    pkt.bRace[i] = gNetPlayerData_g[i].nRace;
  } while (i != 0);

  memcpy(pkt.bSlotType, byte_672610, 12);
  memcpy(pkt.bForce, sgbPlayerForce_s, 8);
  memcpy(pkt.bForce + 8, byte_534898, 4);
  get_selectable_races(pkt.bRaces, 8);
  net_send_message(nArg0, &pkt, sizeof(pkt));

  bEnd = 0x50;
  net_send_message(nArg0, &bEnd, 1);
}

// Whispers one player, then echoes the line locally with the recipient's name.
void packet_chat2(const char *pszText, int nPlayer)
{
  net_chrm_chat_t pkt;
  char szMsg[0x104];
  size_t nLen;

  if (!IsValidPlayer(static_cast<BYTE>(nPlayer)))
    return;

  nLen = strlen(pszText) + 1;
  if (nLen > 0x78)
    return;

  pkt.bOpcode = 0x4D;
  SStrCopy(pkt.szText, pszText, 0x78);
  net_send_message(nPlayer, &pkt, static_cast<int>(nLen + 1));

  psnprintf(szMsg, sizeof(szMsg), error_get_str(0x4F),
            net_player_get_name(static_cast<BYTE>(nPlayer)));
  SStrPack(szMsg, " ", sizeof(szMsg));
  SStrPack(szMsg, pkt.szText, sizeof(szMsg));
  on_recv_chat_from(-1, reinterpret_cast<intptr_t>(szMsg));
}

// Asks the host to resend the lobby state. The packet goes out on the broadcast
// player id, not on the one the argument names.
void packet_request_lobby(int nArg0)
{
  net_chrm_req_lobby_t pkt;

  pkt.bOpcode = 0x49;
  pkt.nValue = nArg0;
  net_send_message(-1, &pkt, sizeof(pkt));
}
