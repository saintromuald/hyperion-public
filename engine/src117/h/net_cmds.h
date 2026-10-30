#pragma once
// net_cmds.h -- the in-game network commands: the packet layouts, the emitters
// (`send_net_cmd_*`), the receivers (`recv_net_cmd_*`) and their TU's helpers, and
// the callees from other TUs (select.cpp, group_un.cpp, cheat.cpp, replay.cpp)
// that no header of their own declares yet.
//
// Include it after the platform types (BOOL, BYTE, WORD, DWORD), as order.h and
// CUnit.h require.

#include "CUnit.h"
#include "gametype.h"   // CheatsAllowed
#include "error.h"      // error_get_str
#include "selectlimits.h"  // SELECTION_MAX

// The packet layouts. #pragma pack(1) is the game's: send_net_cmd_Build puts a WORD
// at +1 and queues a length of 3, and the receivers read DWORDs at +1 and +6. The
// `mov edx, <n>` before each netmgr_queue_cmd is sizeof(cmd), and every size
// agrees with the game's per-opcode size table cmd_size_bytes_g (0x4EF048):
// [0x1F]=3 (Build), [0x14]=10 (RightClick), [0x15]=11 (Action), [0x06]=[0x07]=33.
#pragma pack(push, 1)

// Opcode only. The `push ecx` in these emitters is frame slack for the 1-byte
// local, not an argument -- nothing reads CL and the length is 1.
struct net_cmd_t          { BYTE bOpcode; };

// Opcode + one BYTE. Payload at +1: `mov byte ptr [ebp-3h], cl` over a struct
// based at [ebp-4h].
struct net_cmd_byte_t     { BYTE bOpcode; BYTE  bArg; };

// Opcode + one WORD. `mov word ptr [ebp-3h], cx`.
struct net_cmd_word_t     { BYTE bOpcode; WORD  wArg; };

// Opcode + one DWORD. `mov dword ptr [ebp-7h], ecx` over a struct at [ebp-8h].
struct net_cmd_dword_t    { BYTE bOpcode; DWORD dwArg; };

// Two WORDs -- minimap ping and liftoff, both carrying a map position.
struct net_cmd_point_t    { BYTE bOpcode; WORD wX; WORD wY; };

// send_net_cmd_Placebox, based at [ebp-8h]:
//   +0 `mov byte  ptr [ebp-8h], 0Ch`   opcode
//   +1 `mov byte  ptr [ebp-7h], cl`    BYTE
//   +2 `mov dword ptr [ebp-6h], eax`   point16_t, stored whole
//   +6 `mov word  ptr [ebp-2h], dx`    WORD
struct net_cmd_placebox_t { BYTE bOpcode; BYTE bOrderID; point16_t pos;
                            WORD wUnitType; };

// send_net_cmd_SetReplaySpeed, based at [ebp-0Ch]: +0 opcode, +1 BYTE (from the
// stack argument), +2 DWORD (ECX), +6 DWORD (EDX).
struct net_cmd_replay_t   { BYTE bOpcode; BYTE bPaused; DWORD dwSpeed;
                            DWORD dwTurnRate; };

// send_net_cmd_RightClick, based at [ebp-0Ch]: +0 opcode, WORDs at +1/+3/+5/+7,
// BYTE at +9.
struct net_cmd_rclick_t   { BYTE bOpcode; WORD wX; WORD wY; WORD wTargetID;
                            WORD wTargetType; BYTE bQueued; };

// send_net_cmd_Action -- RightClick plus the order id at +9, which moves the
// queued flag to +10.
struct net_cmd_action_t   { BYTE bOpcode; WORD wX; WORD wY; WORD wTargetID;
                            WORD wTargetType; BYTE bOrderID; BYTE bQueued; };

// Save/load, based at [ebp-24h]: +0 opcode, +1 DWORD, +5 a char buffer whose
// capacity is the `push 1Ch` handed to SStrCopy. Total 33, as cmd_size_bytes_g
// records for opcodes 6 and 7. The length passed to netmgr_queue_cmd is not
// sizeof; see the bodies.
struct net_cmd_gamefile_t { BYTE bOpcode; DWORD dwTimestamp; char szName[0x1C]; };

// The receive side.

// The variable-length select family (opcodes 9/10/11), as GetCommandByteSize
// sees it: an opcode, a count, and a 16-bit unit index per selected unit.
struct net_cmd_select_t   { BYTE bOpcode; BYTE bCount; };

// The same family seen from the receive side, where the trailing array is real:
// WORD unit indexes from +2. recv_net_cmd_SelectDeltaDel rejects a count above 12
// before it reads an element, so 12 is the capacity.
struct net_cmd_selectdelta_t { BYTE bOpcode; BYTE bCount; WORD wUnits[SELECTION_MAX]; };

// Hyperion: the commands of a game of wide unit ids: 1.17's six that carry a unit id, with
// the opcode's top bit set and each id in a DWORD. 1.17's opcodes stop at 0x75. They are
// read in any game; only a game past 2,047 units sends them.
#define NET_CMD_WIDE_IDS 0x80
// 0x89, 0x8A, 0x8B: Select, SelectDeltaAdd, SelectDeltaDel, 2 + 4 bytes a unit.
struct net_cmd_selectdelta_w_t { BYTE bOpcode; BYTE bCount; DWORD dwUnits[SELECTION_MAX]; };
// 0x94: RightClick, 12 bytes.
struct net_cmd_rclick_w_t { BYTE bOpcode; WORD wX; WORD wY; DWORD dwTargetID;
                            WORD wTargetType; BYTE bQueued; };
// 0x95: Action, 13 bytes.
struct net_cmd_action_w_t { BYTE bOpcode; WORD wX; WORD wY; DWORD dwTargetID;
                            WORD wTargetType; BYTE bOrderID; BYTE bQueued; };
// 0xA9: ExitTransport, net_cmd_dword_t, 5 bytes.

// Hyperion: the commands that carry an order, a tech or an upgrade past 1.17's byte, a
// mod's, each id in a WORD. They are read in any game; only an id from 256
// on is sent in them, so a game of 1.17's ids records 1.17's commands.
// 0x96: Action, with the target's id in a DWORD as 0x95's, 14 bytes.
struct net_cmd_action_o_t { BYTE bOpcode; WORD wX; WORD wY; DWORD dwTargetID;
                            WORD wTargetType; WORD wOrderID; BYTE bQueued; };
// 0x8D: Placebox, 9 bytes.
struct net_cmd_placebox_o_t { BYTE bOpcode; WORD wOrderID; point16_t pos; WORD wUnitType; };
// 0x97: Research, 0x98: Upgrade, net_cmd_word_t, 3 bytes.

// Hyperion: 0x8C, a selection command of a game of Hyperion's selection (HX_OPT_SELECTION,
// selectlimits.h), which selects up to 255 units: one of 1.17's three
// operations (bOp's low bits, 0x09 Select, 0x0A SelectDeltaAdd, 0x0B SelectDeltaDel) in chunks
// of up to 60 units, a DWORD each, so that a chunk fits a replay's block (CNetRecord::sub_478340
// keeps a command under 253 bytes). SELECT_CHUNK_NEXT marks a chunk after the operation's first
// and SELECT_CHUNK_MORE one that more follow. The count is the byte after the opcode, as in
// 1.17's: 3 + 4 bytes a unit.
#define NET_CMD_SELECT_CHUNK 0x8C
#define SELECT_CHUNK_UNITS 60
#define SELECT_CHUNK_NEXT 0x40
#define SELECT_CHUNK_MORE 0x80
struct net_cmd_selectchunk_t { BYTE bOpcode; BYTE bCount; BYTE bOp; DWORD dwUnits[SELECT_CHUNK_UNITS]; };

// recv_net_cmd_GroupUnits: `movzx eax, byte ptr [eax+1h]` (action) and
// `mov cl, byte ptr [eax+2h]` (group).
struct net_cmd_group_t    { BYTE bOpcode; BYTE bAction; BYTE bGroup; };

// recv_net_cmd_LoadGame: `mov ecx, dword ptr [ecx+1h]` and `lea edx, [ecx+5h]`.
// The name is the packet's variable-length tail; [1] is the minimum that lets
// the tail be addressed, not a claim about its length.
struct net_cmd_loadgame_t { BYTE bOpcode; DWORD dwArg; char szName[1]; };

// recv_net_cmd_SetReplaySpeed: byte at +1, dword at +2, dword at +6.
struct net_cmd_replayspeed_t { BYTE bOpcode; BYTE bPaused; DWORD dwSpeed; DWORD dwMult; };

// The per-player slot record, 36 bytes: recv_net_cmd_Cheat forms
// `0x6726D3 + 36*g_ActiveHumanID`, so the stride is 36 and szName is at +11.
struct PlayerData {
  /*0x00*/ DWORD dwPlayerID;
  /*0x04*/ DWORD dwStormID;
  /*0x08*/ BYTE  nType;
  /*0x09*/ BYTE  nRace;
  /*0x0A*/ BYTE  nTeam;
  /*0x0B*/ char  szName[25];
};

#pragma pack(pop)

// Callees from other TUs. All __fastcall, which /Gr makes the default; it is
// spelled out because these declare the ABI of code in other TUs.

// oracle 0x476C80. The family sink: every emitter ends here. The packet arrives
// in ECX and the length in EDX, with no stack argument. `const void *` because
// the callers pass fourteen different packet types.
void __fastcall netmgr_queue_cmd(const void *pCmd, DWORD dwLen);

// oracle 0x47CB60, 6 B -- `mov eax, dword ptr [0x67517C]` / `ret`. Nullary; the
// result is only tested, so BOOL.
BOOL __fastcall IsGamePaused();

// oracle 0x477800, 73 B -- ends `setz al` over a dword compare against
// g_LocalStormID. Nullary for the same reason.
BOOL __fastcall IsGameHost();

// oracle 0x477E00 (net_misc.cpp). One BYTE argument: recv_net_cmd_Quit sets only
// CL and the callee's first use is `movzx eax, cl`.
void __fastcall sub_477E00(BYTE bPlayer);

// oracle 0x444E00 (gameloop.cpp). A 32-bit argument though the callee uses only
// CX: the call site emits `mov ecx, 5`, where a WORD parameter gives `mov cx, 5`.
void __fastcall gameloop_init(int nMode);

// load_game (0x498BD0) is declared in saveload.h, its own TU's header.

// oracle 0x481040 (replay.cpp) is declared in replay.h, which net_cmds.cpp
// includes.

// oracle 0x4BE950 -- a one-byte `ret` stub, declared as two symbols. AllowedErr
// has two identical case blocks that both branch there, which the compiler would
// have merged had they called one function; the linker's /OPT:ICF folded the two
// functions onto one address. voice_squelch is the survivor's name; the other
// has none.
void __fastcall voice_squelch(DWORD dwStormID, BOOL bSquelch);
void __fastcall nullsub_folded_with_voice_squelch(DWORD dwHumanID, BOOL bFlag);

// oracle 0x40D9D0 (cheat.cpp). DWORD bitfield in ECX, char* in EDX: the callee
// does `mov edx, ecx` then compares against the old value at [0x5432FC] and
// stores it back there.
void __fastcall cheat_set_bits(DWORD dwFlags, const char *pszWho);

// oracle 0x49CB10 / 0x49CB20 (select.cpp). No arguments; each returns the next
// selected unit in EAX or NULL (`xor eax,eax; ret` on the exhausted path).
CUnit * __fastcall select_get_first();
CUnit * __fastcall select_get_next();
// Hyperion: the walk above over one unit of the selection, while a command that one unit
// of several carries out runs (select.cpp); NULL ends it.
void SelectOnlySet(CUnit *pUnit);
bool SelectOnlyActive();

// oracle 0x4643C0 / 0x464200 (group_un.cpp). The group is a BYTE:
// recv_net_cmd_GroupUnits loads only CL, and both callees read exactly that byte.
// group_modify's second argument is 32-bit (`mov edx, 1h`).
void __fastcall group_modify(BYTE nGroup, BOOL bAdd);
void __fastcall group_recall(BYTE nGroup);

// The receivers' shared tail and their permission predicates.

// oracle 0x4A7ED0 (15 B). Every receiver that changes something a button
// reflects ends `pop <reg> / jmp status_set_update`; it takes nothing and returns
// nothing.
void __fastcall status_set_update();

// oracle 0x4B38A0 (6 B). Reads the failure code the *Allowed predicates leave
// behind. `call get_allowed_err / mov ecx, eax / jmp AllowedErr` hands EAX
// straight to AllowedErr's int parameter, which is what fixes the return width.
int __fastcall get_allowed_err();

// oracle 0x4B38B0 (7 B) -- get_allowed_err's setter, which unit_can_make_type
// calls. One 32-bit argument in ECX; EAX is written on no path to a `ret`.
void __fastcall set_allowed_err(int nCode);

// The four `*Allowed` predicates (ResearchAllowed, TechUseAllowed, UpgradeAllowed,
// TTAllowed) are declared in statbtn.h, which net_cmds.cpp includes. They return
// int, not BOOL: every receiver tests `cmp eax, 1h`, other values being failure
// codes for get_allowed_err. TTAllowed's first parameter is 16 bits:
// recv_net_cmd_Build loads it with a bare `mov cx, word ptr [edi+1h]`.

// The rest of the receivers' callees from other TUs. IsLadderGame and
// CanChangeVision are in gametype.h, unpause_game and AllianceUpdateUnits in
// TrigAct.h, psnprintf in victory.h and SStrLen in Storm.h.

// 0x47F680, `ret 0Ch`: five parameters. The two register ones are 32-bit, not
// the packet's WORD: recv_net_cmd_RightClick widens them before the call. The
// queued flag is a BYTE; the body reads it only as `cmp byte ptr [ebp+10h], 0`.
void __fastcall sub_47F680(int nX, int nY, CUnit *pTarget, int nTargetType,
                           BYTE bQueued);

// 0x47D9C0-family order dispatcher, `ret 10h`: six parameters, RightClick's five
// plus the order id. The register pair is again 32-bit, but sign-extended
// (`movsx ecx, word ptr [esi+1h]`) where RightClick's is zero-extended.
void __fastcall select_target(int nX, int nY, CUnit *pTarget, int nTargetType,
                              order_id_t nOrderID, int nQueued);

// 0x47DA50 / 0x47D5F0, both `ret 4h`: three parameters. The register widths
// differ: the call sites write only CL for the order id and only DX for the unit
// type. placebox_validate's result is tested for non-zero; placebox_query_place's
// is never read.
int  __fastcall placebox_validate(order_id_t bOrderID, WORD wUnitType, CUnit *pUnit);
// recv_net_cmd_Placebox loads the unit type as a WORD (`mov dx`), leaving EDX's
// top half undefined, and placebox_query_place passes the whole EDX on as an int:
// a latent defect in 1.17, kept. C linkage lets placebox.cpp define the int view.
// Hyperion: the unit type is an int, as placebox.cpp's definition reads it,
// which the x64 build needs; a unit type is the same widened.
extern "C" void __fastcall placebox_query_place(order_id_t bOrderID, int nUnitType,
                                                point16_t pos);
// The same function as a C++ declaration, which the generator reads.
void __fastcall placebox_query_place(order_id_t bOrderID, int nUnitType,
                                     point16_t pos);

// 0x49C870. Two register args, no stack arg, plain `ret`: it opens
// `mov edi, ecx / mov esi, edx` and null-tests the first as an object. The
// result is compared `cmp eax, 1h / jle`, i.e. a signed int, not a BOOL.
int  __fastcall sub_49C870(CUnit *pUnit, int nPlayer);

// 0x464360. No parameters: nothing is set up at the call site and the callee
// loads g_ActiveHumanID itself. Plain `ret`, result never read.
void __fastcall sub_464360();

// 0x4771E0. Nullary for the same reason; returns a Storm id in EAX, compared
// dword-wise against g_ActiveStormID (`mov eax, dword ptr [0x508D58]` on its
// own early-out path is the same quantity).
DWORD __fastcall sub_4771E0();

// 0x476E20, `ret 0Ch`: five parameters. ECX is the string; EDX and the first
// stack slot are two independent lengths, the body taking the smaller.
int  __fastcall get_valid_str_len(const char *pszStr, DWORD dwLen, DWORD dwCap,
                                  int nFlagA, int nFlagB);

// 0x408300, `ret 4h`: three parameters. ECX is the string, EDX SStrLen's result,
// and the third the `push 0h` hoisted above the SStrLen call. The name's polarity
// looks inverted and is 1.17's: the save goes ahead when this returns non-zero.
int  __fastcall is_bad_filename(const char *pszStr, DWORD dwLen, int nFlags);

// 0x498D20 saveload_write_save_data, `ret 4h`: the packet's DWORD, the name and a
// literal 0. It is declared in saveload.h, its own TU's header.

// 0x46FD90 (net_bnet_msg.cpp), 730 B. One argument in ECX: the body opens with a
// '/' test, the slash-command dispatcher. The only consumer tests EAX.
int __fastcall sub_46FD90(const char *pszText);

// 0x4780A0 (net_player.cpp), 32 B. One BYTE argument: the body opens
// `cmp cl, 8h` and the call site sets only CL (`mov cl, bl` off an int loop
// counter). Returns `setz al` over a table byte compare.
BOOL __fastcall IsValidPlayer(BYTE bPlayer);

// 0x474780 (net_cmds.cpp), 165 B. Three arguments, the third callee-cleaned
// (`ret 4h` and the caller does not adjust ESP). The body forwards all three
// straight into SNetSendMessage(playerId, data, length).
void __fastcall net_send_message(int nPlayer, const void *pData, int nLen);

// 0x4CD040 (gds_palette.cpp), 36 B. `ret 4h`: three arguments. The first is a
// BYTE: the call site emits `xor cl, cl` and the body reads `movzx eax, cl`. The
// second is read as `movzx eax, dx` and times 4 is a memcpy length; the third is
// the source.
void __fastcall vid_restore_palette(BYTE bFirst, DWORD dwCount,
                                    const void *pPalette);

// oracle 0x4CCF40 (169 B, gds_palette.cpp). Nullary; recv_net_cmd_PauseOn calls
// it beside vid_fade.
void __fastcall vid_desaturate();


// 0x46AC90 (mapmsg.cpp), 57 B. ECX the string, EDX the display time, 0x1B58 when
// zero (a full-width test, so 32-bit). Not map_msg_default (0x46AEC0, TrigAct.h),
// which has the same shape at a different address.
void __fastcall map_msg_chat_msg(const char *pszText, int nTime);

// 0x46A860 (498 B), mapmsg.cpp; net_misc.cpp's net_drop_player calls it. ECX is a
// pointer, EDX a 32-bit value compared against -1, and `ret 4h` adds one stack
// word. EAX is written on no path to a `ret`.
void wrap_map_msg(void *pArg0, int nArg1, int nArg2);

// The bodies in src117/lang/net_cmds.cpp; AllowedErr (0x472210), defined there,
// is not declared here. A register parameter's width cannot be read off the
// callee, so those take the packet field's; a stack parameter's can, and is four
// bytes even where the field is one (`mov eax, dword ptr [ebp+10h]` then
// `mov byte ptr [ebp-3h], al`). Spellings order.cpp declared first are kept.

// The emitters, in oracle address order.
//
// send_net_cmd_Action's bOrderID is a BYTE: usel_click_target pushes a dword
// read of a byte-only-written local, which is right only if the callee reads one
// byte. nQueued stays int: its caller widens with `movzx eax, byte ptr [0x665E68]`.
// Hyperion: an order_id_t, and an order past 1.17's byte goes in 0x96.
void __fastcall send_net_cmd_Action(WORD wX, WORD wY, CUnit *pTarget,
                                    int nTargetType, order_id_t bOrderID, int nQueued);
void __fastcall send_net_cmd_Build(WORD wUnitType);
void __fastcall send_net_cmd_BuildFighter();
void __fastcall send_net_cmd_Burrow(BYTE bQueued);
void __fastcall send_net_cmd_CancelNuke();
void __fastcall send_net_cmd_Cheat(DWORD dwFlags);
void __fastcall send_net_cmd_Cloak(BYTE bQueued);
void __fastcall send_net_cmd_Deburrow(BYTE bQueued);
void __fastcall send_net_cmd_Decloak(BYTE bQueued);
void __fastcall send_net_cmd_ExitTransport(CUnit *pUnit);
void __fastcall send_net_cmd_HoldPosition(BYTE bQueued);
void __fastcall send_net_cmd_Kill();
void __fastcall send_net_cmd_KillEgg();
void __fastcall send_net_cmd_Liftoff();
void __fastcall send_net_cmd_LoadGame(DWORD dwTimestamp, const char *pszName);
// The coordinates are 32-bit: the one caller widens each with
// `movzx r32, word ptr` before the call, and the callee only narrows them.
void __fastcall send_net_cmd_MinimapPing(int nX, int nY);
void __fastcall send_net_cmd_Morph(WORD wUnitType);
void __fastcall send_net_cmd_PauseOff();
void __fastcall send_net_cmd_PauseOn();
void __fastcall send_net_cmd_Placebox(order_id_t bOrderID, WORD wUnitType, point16_t pos);
void __fastcall send_net_cmd_Research(tech_id_t bTechId);
void __fastcall send_net_cmd_RestartGame();
void __fastcall send_net_cmd_Return(BYTE bQueued);
// x and y are int: rclick_action widens each with `movzx r32, word ptr`, and
// the body only reads cx/dx, so either width fits it.
void __fastcall send_net_cmd_RightClick(int nX, int nY, CUnit *pTarget,
                                        int nTargetType, int nQueued);
void __fastcall send_net_cmd_SaveGame(const char *pszName);
void __fastcall send_net_cmd_SetAllies(DWORD dwAllies);
void __fastcall send_net_cmd_SetFog(WORD wVisionFlags);
// bPaused is a BYTE: the callee loads the whole slot and uses AL, which fits
// either width, and order_replay_play decides it by widening a `sete al` result
// at the push.
void __fastcall send_net_cmd_SetReplaySpeed(DWORD dwSpeed, DWORD dwTurnRate,
                                            BYTE bPaused);

// The one parameter here that is 32-bit: the callee stores a byte, but
// speed_dlg_cmd passes `movsx ecx, ax`, which a BYTE or 16-bit parameter would
// not emit. int, as dialog_accel bounds it to the seven game speeds 0..6.
void __fastcall send_net_cmd_SetSpeed(int nSpeed);

void __fastcall send_net_cmd_SiegeMode(BYTE bQueued);
void __fastcall send_net_cmd_Stim();
void __fastcall send_net_cmd_Stop(BYTE bQueued);
void __fastcall send_net_cmd_StopAddon();
void __fastcall send_net_cmd_StopBuild(WORD wSlot);
void __fastcall send_net_cmd_StopResearch();
void __fastcall send_net_cmd_StopUpgrade();
void __fastcall send_net_cmd_Stop_Carrier();
void __fastcall send_net_cmd_Stop_Reaver();
void __fastcall send_net_cmd_SummonArchon();
void __fastcall send_net_cmd_SummonDArchon();
void __fastcall send_net_cmd_SwitchTankMode(BYTE bQueued);
void __fastcall send_net_cmd_UnloadAll(BYTE bQueued);
void __fastcall send_net_cmd_Upgrade(upgrade_id_t bUpgradeId);
void __fastcall send_net_cmd_UpgradeBldg(WORD wUnitType);

// The receivers and helpers, in oracle address order. Receivers take the packet
// in ECX; the thirteen with no parameter read no register and
// net_process_game_turn sets no ECX before calling them. net_cmds_init and
// net_init_ids are defined in net_cmds.cpp though their rows name group_un.cpp
// and game.cpp. AllianceUpdateUnits (0x4721C0), also defined there, is declared
// in TrigAct.h.
CUnit * __fastcall FindClosestMergeTarget(CUnit *pUnit, CUnit **ppUnits,
                                          int nCount, int nUnitType);
int  __fastcall GetCommandByteSize(const net_cmd_select_t *pCmd);
BYTE __fastcall GetSelectedOwner();
void __fastcall sub_472100(CUnit *pUnit);
void __fastcall net_cmds_reset_select();
void __fastcall net_cmds_init();
void __fastcall recv_net_cmd_Action(const net_cmd_action_t *pCmd);
void __fastcall recv_net_cmd_Build(const net_cmd_word_t *pCmd);
void __fastcall recv_net_cmd_BuildFighter();
void __fastcall recv_net_cmd_Burrow(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_CancelNuke();
void __fastcall recv_net_cmd_Cheat(const net_cmd_dword_t *pCmd);
void __fastcall recv_net_cmd_Cloak(const net_cmd_t *pCmd);
void __fastcall recv_net_cmd_Deburrow(const net_cmd_t *pCmd);
void __fastcall recv_net_cmd_Decloak(const net_cmd_t *pCmd);
void __fastcall recv_net_cmd_ExitTransport(const net_cmd_word_t *pCmd);
void __fastcall recv_net_cmd_GroupUnits(const net_cmd_group_t *pCmd);
void __fastcall recv_net_cmd_HoldPosition(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_Kill();
void __fastcall recv_net_cmd_KillEgg();
void __fastcall recv_net_cmd_Liftoff(const net_cmd_point_t *pCmd);
void __fastcall recv_net_cmd_LoadGame(const net_cmd_loadgame_t *pCmd);
void __fastcall recv_net_cmd_MinimapPing(const net_cmd_point_t *pCmd);
void __fastcall recv_net_cmd_Morph(const net_cmd_word_t *pCmd);
void __fastcall recv_net_cmd_Nothing();
void __fastcall recv_net_cmd_PauseOff();
void __fastcall recv_net_cmd_PauseOn();
void __fastcall recv_net_cmd_Placebox(const net_cmd_placebox_t *pCmd);
void __fastcall recv_net_cmd_Quit(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_Research(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_RestartGame();
void __fastcall recv_net_cmd_Return(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_RightClick(const net_cmd_rclick_t *pCmd);
void __fastcall recv_net_cmd_SaveGame(const net_cmd_gamefile_t *pCmd);
void __fastcall recv_net_cmd_Select(const net_cmd_select_t *pCmd);
void __fastcall recv_net_cmd_SelectDeltaAdd(const net_cmd_selectdelta_t *pCmd);
void __fastcall recv_net_cmd_SelectDeltaDel(const net_cmd_selectdelta_t *pCmd);
// Hyperion: the commands of wide unit ids, and their sizes: 0 for an
// opcode not theirs.
int NetCmdWideSize(const BYTE *pCmd);
void recv_net_cmd_SelectWide(const net_cmd_selectdelta_w_t *pCmd);
void recv_net_cmd_SelectDeltaAddWide(const net_cmd_selectdelta_w_t *pCmd);
void recv_net_cmd_SelectDeltaDelWide(const net_cmd_selectdelta_w_t *pCmd);
void recv_net_cmd_SelectChunk(const net_cmd_selectchunk_t *pCmd);
void recv_net_cmd_RightClickWide(const net_cmd_rclick_w_t *pCmd);
void recv_net_cmd_ActionWide(const net_cmd_action_w_t *pCmd);
void recv_net_cmd_ActionOrder(const net_cmd_action_o_t *pCmd);
void recv_net_cmd_PlaceboxOrder(const net_cmd_placebox_o_t *pCmd);
void recv_net_cmd_ResearchWord(const net_cmd_word_t *pCmd);
void recv_net_cmd_UpgradeWord(const net_cmd_word_t *pCmd);
void recv_net_cmd_ExitTransportWide(const net_cmd_dword_t *pCmd);
void __fastcall recv_net_cmd_SetAllies(const net_cmd_dword_t *pCmd);
void __fastcall recv_net_cmd_SetFog(const net_cmd_word_t *pCmd);
void __fastcall recv_net_cmd_SetReplaySpeed(const net_cmd_replayspeed_t *pCmd);
void __fastcall recv_net_cmd_SetSpeed(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_SiegeMode(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_Stim();
void __fastcall recv_net_cmd_Stop(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_StopAddon(const net_cmd_t *pCmd);
void __fastcall recv_net_cmd_StopBuild(const net_cmd_word_t *pCmd);
void __fastcall recv_net_cmd_StopResearch(const net_cmd_t *pCmd);
void __fastcall recv_net_cmd_StopUpgrade(const net_cmd_t *pCmd);
void __fastcall recv_net_cmd_Stop_Carrier();
void __fastcall recv_net_cmd_Stop_Reaver();
void __fastcall recv_net_cmd_SummonArchon();
void __fastcall recv_net_cmd_SummonDArchon();
void __fastcall recv_net_cmd_SwitchTankMode(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_UnloadAll(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_Upgrade(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_UpgradeBldg(const net_cmd_word_t *pCmd);
void __fastcall recv_net_cmd_VoiceSquelch(const net_cmd_byte_t *pCmd);
void __fastcall recv_net_cmd_VoiceToggle(const net_cmd_t *pCmd);
// oracle 0x478020 (45 B) / 0x4781A0 (19 B). The per-player record accessors
// recv_net_cmd_VoiceToggle is built from. get reads ECX as a BYTE, writes a WORD
// through EDX and returns 1 or 0; set writes only DX, so its second parameter is
// 16 bits. The table, a validity BYTE at 0x673818 and a flags WORD at 0x67381A
// with a stride of 34, has no name.
BOOL __fastcall net_player_get_flags(BYTE bPlayerId, WORD *pwFlags);
void __fastcall net_player_set_flags(BYTE bPlayerId, WORD wFlags);
void __fastcall net_send_string(const char *pszText);
void __fastcall net_init_ids();

// The network transport layer and the two remaining emitters. Under /Gr `ret 4`
// means three parameters and `ret 0Ch` five.
void __fastcall net_send_turn(char *pData, unsigned int nSize);
BOOL __fastcall net_receive_message(int *pnPlayerId, char **ppData,
                                    unsigned int *pnSize);                 // ret 4h
BOOL __fastcall net_receive_turns(int nFirstPlayer, int nPlayers, char **ppData,
                                  unsigned int *pnSize, DWORD *pdwPlayerBits); // ret Ch
void __fastcall send_net_cmd_Select(int nCount, CUnit **ppUnits);

// nCount is an int that the body narrows: the slot is read whole and then
// `movzx eax, al`, where a BYTE parameter is one `movzx eax, byte ptr [ebp+0Ch]`.
void __fastcall send_net_cmd_GroupUnits(BYTE bAction, BYTE bGroup,
                                        CUnit **ppUnits, int nCount);      // ret 8h

// net_error_show_error (0x474A70) is seated in net_error.h, its owner.
