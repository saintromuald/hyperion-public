// code_map.cpp -- 1.17's code addresses that its data holds, and the engine's
// function for each (tools/code_map.py). The engine
// reads 1.17's data from the player's exe and sends each such address here
// (src117/port/platform/port_standalone.cpp); an address with no row goes to
// PortOriginalCodeReached. Written once by tools/code_map.py from the 32-bit
// build's hook tables; since then kept by hand, and
// tools/code_map.py --check lists what the data holds that has no row.
#include <cstring>
#include <new>
#include "Allowed.h"
#include "BNetGW.h"
#include "BlizzardError.h"
#include "CBullet.h"
#include "CFlingy.h"
#include "CImage.h"
#include "CLink.h"
#include "CLists.h"
#include "CNetRecord.h"
#include "CNetUnkn.h"
#include "COrder.h"
#include "CSprite.h"
#include "CTarget.h"
#include "CThingy.h"
#include "CUnit.h"
#include "CUnitBounds.h"
#include "CUnitBuild.h"
#include "CUnitCOP.h"
#include "CUnitCarrier.h"
#include "CUnitCloak.h"
#include "CUnitColor.h"
#include "CUnitCombat.h"
#include "CUnitFind.h"
#include "CUnitGUI.h"
#include "CUnitHarvest.h"
#include "CUnitIO.h"
#include "CUnitInit.h"
#include "CUnitInv.h"
#include "CUnitLClick.h"
#include "CUnitMisc.h"
#include "CUnitOrder.h"
#include "CUnitPBuild.h"
#include "CUnitPSpells.h"
#include "CUnitPath.h"
#include "CUnitPathBuild.h"
#include "CUnitPathCollide.h"
#include "CUnitPathUtil.h"
#include "CUnitProtoss.h"
#include "CUnitRClick.h"
#include "CUnitRank.h"
#include "CUnitSave.h"
#include "CUnitSearches.h"
#include "CUnitSpells.h"
#include "CUnitTBuild.h"
#include "CUnitTSpells.h"
#include "CUnitTerran.h"
#include "CUnitTransport.h"
#include "CUnitUpdate.h"
#include "CUnitUpdate2.h"
#include "CUnitUpgrade.h"
#include "CUnitZBuild.h"
#include "CUnitZSpells.h"
#include "CUnitZerg.h"
#include "CodeSign.h"
#include "CtxtHelp.h"
#include "DebugHelp.h"
#include "ErrorHandler.h"
#include "Exception.h"
#include "League.h"
#include "Logging.h"
#include "Module.h"
#include "NDebugHelp.h"
#include "Priority.h"
#include "SAI_Banker.h"
#include "SAI_Build.h"
#include "SAI_BuildWorkers.h"
#include "SAI_Main.h"
#include "SAI_Scripts.h"
#include "StatLB.h"
#include "Storm.h"
#include "SystemSurvey.h"
#include "TAI_AreaStrength.h"
#include "TAI_Captain.h"
#include "TAI_GuardPost.h"
#include "TReserve.h"
#include "Teams.h"
#include "TechTree.h"
#include "TrigAct.h"
#include "TrigCond.h"
#include "TrigEnum.h"
#include "TrigLB.h"
#include "VictDlg.h"
#include "_GAMENODE.h"
#include "ai_names.h"
#include "bad_file_names.h"
#include "banker.h"
#include "bmp.h"
#include "cellscrl.h"
#include "charactr.h"
#include "cheat.h"
#include "choke.h"
#include "chunk.h"
#include "clock.h"
#include "codec.h"
#include "collide.h"
#include "compress.h"
#include "count.h"
#include "crashy.h"
#include "crc32.h"
#include "credits.h"
#include "creep.h"
#include "cur.h"
#include "cursor.h"
#include "damage.h"
#include "debug.h"
#include "dlgFatal.h"
#include "dlg_btn.h"
#include "dlg_ctrl.h"
#include "dlg_dlg.h"
#include "dlg_dlgdraw.h"
#include "dlg_draw.h"
#include "dlg_edit.h"
#include "dlg_effects.h"
#include "dlg_flc.h"
#include "dlg_lbox.h"
#include "dlg_list.h"
#include "dlg_scrlbar.h"
#include "dlg_str.h"
#include "dlg_timer.h"
#include "error.h"
#include "event_Win.h"
#include "fd.h"
#include "fd_sessn.h"
#include "fd_user.h"
#include "filters.h"
#include "find.h"
#include "forces.h"
#include "game.h"
#include "game_evt.h"
#include "gamedata.h"
#include "gameloop.h"
#include "gamemap.h"
#include "gamesnd.h"
#include "gametype.h"
#include "gds_appaddex.h"
#include "gds_appcrit.h"
#include "gds_appexi.h"
#include "gds_appfat.h"
#include "gds_bmpcle.h"
#include "gds_bmpcop.h"
#include "gds_bmpcopre.h"
#include "gds_bmphlin.h"
#include "gds_bmpmas.h"
#include "gds_bmpmasre.h"
#include "gds_bmprec.h"
#include "gds_bmpvlin.h"
#include "gds_file.h"
#include "gds_font.h"
#include "gds_grp2bmpx.h"
#include "gds_grptobmp.h"
#include "gds_image.h"
#include "gds_inifile.h"
#include "gds_local.h"
#include "gds_log.h"
#include "gds_metabmp.h"
#include "gds_mou_win.h"
#include "gds_palette.h"
#include "gds_sys_Win.h"
#include "gds_vidblit.h"
#include "gds_vidinimo.h"
#include "gds_vidinimo_PC.h"
#include "gluBN.h"
#include "gluBNChat.h"
#include "gluBNCreate.h"
#include "gluBNIcon.h"
#include "gluBNJoin.h"
#include "gluBNOkCancel.h"
#include "gluBNProfile.h"
#include "gluBrfAct.h"
#include "gluBrfPort.h"
#include "gluChat.h"
#include "gluChatSlot.h"
#include "gluCmpgn.h"
#include "gluConn.h"
#include "gluCreat.h"
#include "gluError.h"
#include "gluExpCmpgn.h"
#include "gluGameMode.h"
#include "gluJoin.h"
#include "gluLoad.h"
#include "gluLogin.h"
#include "gluMain.h"
#include "gluMap.h"
#include "gluModem.h"
#include "gluPopup.h"
#include "gluRdy.h"
#include "gluRdyP.h"
#include "gluRdyT.h"
#include "gluRdyZ.h"
#include "gluScore.h"
#include "glues.h"
#include "grid.h"
#include "group.h"
#include "group_un.h"
#include "header.h"
#include "help.h"
#include "hp_float.h"
#include "idea.h"
#include "img.h"
#include "init.h"
#include "inspectorreport.h"
#include "light.h"
#include "los.h"
#include "main.h"
#include "map_load.h"
#include "maphdr.h"
#include "mapmsg.h"
#include "mask.h"
#include "menu_dlg.h"
#include "minimap.h"
#include "modeless.h"
#include "music.h"
#include "net_ban.h"
#include "net_bnet_msg.h"
#include "net_chat_cmds.h"
#include "net_chrm.h"
#include "net_cmds.h"
#include "net_error.h"
#include "net_glue.h"
#include "net_mgr.h"
#include "net_misc.h"
#include "net_player.h"
#include "net_rand.h"
#include "net_record.h"
#include "net_slot.h"
#include "net_strt.h"
#include "net_sync.h"
#include "net_time.h"
#include "netdesc.h"
#include "netdlg.h"
#include "okcancel.h"
#include "opt_dlgs.h"
#include "order.h"
#include "palette.h"
#include "pause.h"
#include "placebox.h"
#include "playflic.h"
#include "rclick.h"
#include "replay.h"
#include "repulse.h"
#include "sai.h"
#include "sai_ContourScan.h"
#include "sai_Contours.h"
#include "sai_ContoursCreate.h"
#include "sai_ContoursUtil.h"
#include "sai_LoadSave.h"
#include "sai_PathCreate.h"
#include "sai_PathFindArea.h"
#include "sai_PathFindLocal.h"
#include "sai_PathFindUtil.h"
#include "sai_TownBuilder.h"
#include "sai_debug.h"
#include "sai_influence.h"
#include "sai_path.h"
#include "save_dlg.h"
#include "save_dlg_list.h"
#include "saveload.h"
#include "scroll.h"
#include "select.h"
#include "sha.h"
#include "sha1.h"
#include "signature.h"
#include "snd.h"
#include "snd_dlg.h"
#include "snd_streamed.h"
#include "spd_dlg.h"
#include "statbtn.h"
#include "statcmd.h"
#include "statdata.h"
#include "statdraw.h"
#include "statfluf.h"
#include "statmisc.h"
#include "statport.h"
#include "statres.h"
#include "statselect.h"
#include "status.h"
#include "statwire.h"
#include "statwrite.h"
#include "target.h"
#include "textbox.h"
#include "tile2.h"
#include "tips.h"
#include "title.h"
#include "uiMulti.h"
#include "uiSingle.h"
#include "upgrade.h"
#include "usel.h"
#include "victory.h"
#include "videodlg.h"
#include "vpacket.h"
#include "../port/render/port_draw.h"  // Hyperion
#include "../port/platform/port_standalone.h"  // Hyperion: PortCodeRow, PortCodeMap

int __fastcall TryAddTrigger(void *pTrigger, int nFlags);
void __fastcall voice_squelch(DWORD dwStormID, BOOL bSquelch);  // vpacket.cpp
int __fastcall nullsub___shared_();                              // TrigCond.cpp

typedef int (__fastcall *AlwaysFalseFn)(void);
typedef void (__fastcall *RleDrawFn)(int, int, const void *, const void *, ULONG_PTR);
typedef void (__fastcall *VoiceSquelchFn)(DWORD, BOOL);

// Constructors, destructors and virtual members, by their decorated names; off MSVC by
// their Itanium names, the deleting destructor for the scalar deleting one.
#if defined(_MSC_VER)
extern "C" void __cdecl oc_hook_45AFA0(void);
#pragma comment(linker, "/alternatename:oc_hook_45AFA0=\?\?_G\?$TSHashTable@UHASHENTRY@@VHASHKEY_CONSTSTRI@@@@UEAAPEAXI@Z")
extern "C" void __cdecl oc_hook_45C790(void);
#pragma comment(linker, "/alternatename:oc_hook_45C790=\?Destroy@\?$TSHashTable@UHASHENTRY@@VHASHKEY_CONSTSTRI@@@@UEAAXXZ")
extern "C" void __cdecl oc_hook_45CF40(void);
#pragma comment(linker, "/alternatename:oc_hook_45CF40=\?InternalDelete@\?$TSHashTable@UHASHENTRY@@VHASHKEY_CONSTSTRI@@@@UEAAXPEAUHASHENTRY@@@Z")
extern "C" void __cdecl oc_hook_45CF70(void);
#pragma comment(linker, "/alternatename:oc_hook_45CF70=\?InternalNew@\?$TSHashTable@UHASHENTRY@@VHASHKEY_CONSTSTRI@@@@UEAAPEAUHASHENTRY@@PEAV\?$TSExplicitList@UHASHENTRY@@$0NNNNNNNN@@@KK@Z")
#else
#if defined(__APPLE__)
#define OC_ITANIUM(name) __asm__("_" name)
#else
#define OC_ITANIUM(name) __asm__(name)
#endif
extern "C" void oc_hook_45AFA0(void) OC_ITANIUM("_ZN11TSHashTableI9HASHENTRY17HASHKEY_CONSTSTRIED0Ev");
extern "C" void oc_hook_45C790(void) OC_ITANIUM("_ZN11TSHashTableI9HASHENTRY17HASHKEY_CONSTSTRIE7DestroyEv");
extern "C" void oc_hook_45CF40(void) OC_ITANIUM("_ZN11TSHashTableI9HASHENTRY17HASHKEY_CONSTSTRIE14InternalDeleteEPS0_");
extern "C" void oc_hook_45CF70(void) OC_ITANIUM("_ZN11TSHashTableI9HASHENTRY17HASHKEY_CONSTSTRIE11InternalNewEP14TSExplicitListIS0_Lj3722304989EEjj");
#endif

static void* addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI___Destroy() { return (void*)static_cast<void (__cdecl *)(void)>(&oc_hook_45C790); }
static void* addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI___InternalDelete() { return (void*)static_cast<void (__cdecl *)(void)>(&oc_hook_45CF40); }
static void* addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI___InternalNew() { return (void*)static_cast<void (__cdecl *)(void)>(&oc_hook_45CF70); }
static void* addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI____scalar_deleting_destructor_() { return (void*)static_cast<void (__cdecl *)(void)>(&oc_hook_45AFA0); }
static void* addr_act_alliance() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_alliance); }
static void* addr_act_center_view() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_center_view); }
static void* addr_act_create_unit() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_create_unit); }
static void* addr_act_defeat() { return (void*)static_cast<int (__fastcall *)(void)>(&act_defeat); }
static void* addr_act_disable_escape() { return (void*)static_cast<int (__fastcall *)(void)>(&act_disable_escape); }
static void* addr_act_doodad() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_doodad); }
static void* addr_act_draw() { return (void*)static_cast<int (__fastcall *)(void)>(&act_draw); }
static void* addr_act_enable_escape() { return (void*)static_cast<int (__fastcall *)(void)>(&act_enable_escape); }
static void* addr_act_give() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_give); }
static void* addr_act_invincible() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_invincible); }
static void* addr_act_kill() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_kill); }
static void* addr_act_kill_atloc() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_kill_atloc); }
static void* addr_act_leaderboard() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_leaderboard); }
static void* addr_act_leaderboard_cpu() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_leaderboard_cpu); }
static void* addr_act_mission_obj() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_mission_obj); }
static void* addr_act_move_loc() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_move_loc); }
static void* addr_act_move_unit() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_move_unit); }
static void* addr_act_mute() { return (void*)static_cast<int (__fastcall *)(void)>(&act_mute); }
static void* addr_act_next_mission() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_next_mission); }
static void* addr_act_none() { return (void*)static_cast<int (__fastcall *)(void)>(&act_none); }
static void* addr_act_order() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_order); }
static void* addr_act_pause() { return (void*)static_cast<int (__fastcall *)(void)>(&act_pause); }
static void* addr_act_ping() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_ping); }
static void* addr_act_portrait() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_portrait); }
static void* addr_act_preserve() { return (void*)static_cast<int (__fastcall *)(void)>(&act_preserve); }
static void* addr_act_remove() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_remove); }
static void* addr_act_remove_atloc() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_remove_atloc); }
static void* addr_act_run_ai() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_run_ai); }
static void* addr_act_run_ai_atloc() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_run_ai_atloc); }
static void* addr_act_set_deaths() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_deaths); }
static void* addr_act_set_hangar() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_hangar); }
static void* addr_act_set_resource() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_resource); }
static void* addr_act_set_score() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_score); }
static void* addr_act_set_switch() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_switch); }
static void* addr_act_set_timer() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_timer); }
static void* addr_act_set_unit_energy() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_unit_energy); }
static void* addr_act_set_unit_hp() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_unit_hp); }
static void* addr_act_set_unit_res() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_unit_res); }
static void* addr_act_set_unit_shields() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_set_unit_shields); }
static void* addr_act_start_timer() { return (void*)static_cast<int (__fastcall *)(void)>(&act_start_timer); }
static void* addr_act_stop_timer() { return (void*)static_cast<int (__fastcall *)(void)>(&act_stop_timer); }
static void* addr_act_text() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_text); }
static void* addr_act_transmission() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_transmission); }
static void* addr_act_unmute() { return (void*)static_cast<int (__fastcall *)(void)>(&act_unmute); }
static void* addr_act_unpause() { return (void*)static_cast<int (__fastcall *)(void)>(&act_unpause); }
static void* addr_act_victory() { return (void*)static_cast<int (__fastcall *)(void)>(&act_victory); }
static void* addr_act_wait() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_wait); }
static void* addr_act_wav() { return (void*)static_cast<int (__fastcall *)(TrigAction *)>(&act_wav); }
static void* addr_allied_victory_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&allied_victory_fcn); }
static void* addr_bf_always() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_always); }
static void* addr_bf_battery() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_battery); }
static void* addr_bf_build_fighter_ok() { return (void*)static_cast<int (__fastcall *)(int, int, CUnit *)>(&bf_build_fighter_ok); }
static void* addr_bf_building_ok() { return (void*)static_cast<int (__fastcall *)(WORD, int, CUnit *)>(&bf_building_ok); }
static void* addr_bf_burrowed() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_burrowed); }
static void* addr_bf_can_attack() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_attack); }
static void* addr_bf_can_attack_unburrowed() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_attack_unburrowed); }
static void* addr_bf_can_burrow() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_burrow); }
static void* addr_bf_can_cloak() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_cloak); }
static void* addr_bf_can_cloak_hero() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_cloak_hero); }
static void* addr_bf_can_make_archon() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_make_archon); }
static void* addr_bf_can_make_archon_need2() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_make_archon_need2); }
static void* addr_bf_can_make_darchon() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_make_darchon); }
static void* addr_bf_can_move_patrol() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_move_patrol); }
static void* addr_bf_can_move_patrol_unburrowed() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_move_patrol_unburrowed); }
static void* addr_bf_can_move_stop() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_move_stop); }
static void* addr_bf_can_move_stop_unburrowed() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_move_stop_unburrowed); }
static void* addr_bf_can_siege() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_siege); }
static void* addr_bf_can_tank_move() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_can_tank_move); }
static void* addr_bf_empty_silo() { return (void*)static_cast<int (__fastcall *)(WORD, int, CUnit *)>(&bf_empty_silo); }
static void* addr_bf_has_interceptor() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_has_interceptor); }
static void* addr_bf_has_mines() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_has_mines); }
static void* addr_bf_has_nuke() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_has_nuke); }
static void* addr_bf_has_scarab() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_has_scarab); }
static void* addr_bf_has_spell() { return (void*)static_cast<int (__fastcall *)(tech_id_t, int, CUnit *)>(&bf_has_spell); }
static void* addr_bf_hatch_rally() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_hatch_rally); }
static void* addr_bf_is_cloaked() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_is_cloaked); }
static void* addr_bf_is_hero_cloaked() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_is_hero_cloaked); }
static void* addr_bf_is_ship() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_is_ship); }
static void* addr_bf_is_sieged() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_is_sieged); }
static void* addr_bf_larva_sel() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_larva_sel); }
static void* addr_bf_lifted() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_lifted); }
static void* addr_bf_liftoff_ok() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_liftoff_ok); }
static void* addr_bf_load() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_load); }
static void* addr_bf_load_drone() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_load_drone); }
static void* addr_bf_load_scv() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_load_scv); }
static void* addr_bf_make_lurker() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_make_lurker); }
static void* addr_bf_no_exit() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_no_exit); }
static void* addr_bf_no_load() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_no_load); }
static void* addr_bf_no_load_drone() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_no_load_drone); }
static void* addr_bf_no_load_scv() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_no_load_scv); }
static void* addr_bf_not_comp() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_not_comp); }
static void* addr_bf_protoss_build_advanced() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_protoss_build_advanced); }
static void* addr_bf_protoss_build_basic() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_protoss_build_basic); }
static void* addr_bf_rally() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_rally); }
static void* addr_bf_replay_play() { return (void*)static_cast<BOOL (__stdcall *)(int)>(&bf_replay_play); }
static void* addr_bf_replay_slowdown() { return (void*)static_cast<BOOL (__stdcall *)(int)>(&bf_replay_slowdown); }
static void* addr_bf_replay_speedup() { return (void*)static_cast<BOOL (__stdcall *)(int)>(&bf_replay_speedup); }
static void* addr_bf_research() { return (void*)static_cast<int (__fastcall *)(tech_id_t, int, CUnit *)>(&bf_research); }
static void* addr_bf_scv_attack_orders() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_scv_attack_orders); }
static void* addr_bf_stop_addon() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_stop_addon); }
static void* addr_bf_stop_build() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_stop_build); }
static void* addr_bf_stop_research() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_stop_research); }
static void* addr_bf_stop_unit() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_stop_unit); }
static void* addr_bf_stop_upgrade() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_stop_upgrade); }
static void* addr_bf_terran_build_advanced() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_terran_build_advanced); }
static void* addr_bf_terran_build_basic() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_terran_build_basic); }
static void* addr_bf_tower() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_tower); }
static void* addr_bf_trans() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_trans); }
static void* addr_bf_trans_has_passenger() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_trans_has_passenger); }
static void* addr_bf_upgrade() { return (void*)static_cast<int (__fastcall *)(upgrade_id_t, int, CUnit *)>(&bf_upgrade); }
static void* addr_bf_zerg_build_advanced() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_zerg_build_advanced); }
static void* addr_bf_zerg_build_basic() { return (void*)static_cast<int (__fastcall *)(BYTE, int, CUnit *)>(&bf_zerg_build_basic); }
static void* addr_big_menu_btn_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&big_menu_btn_fcn); }
static void* addr_brfact_hide() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_hide); }
static void* addr_brfact_message() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_message); }
static void* addr_brfact_objectives() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_objectives); }
static void* addr_brfact_show() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_show); }
static void* addr_brfact_talking() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_talking); }
static void* addr_brfact_transmission() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_transmission); }
static void* addr_brfact_tutorial() { return (void*)static_cast<BOOL (*)(void)>(&brfact_tutorial); }
static void* addr_brfact_wait() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_wait); }
static void* addr_brfact_wav() { return (void*)static_cast<BOOL (*)(TrigAction *)>(&brfact_wav); }
static void* addr_btn_ctrl_checkbox() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&btn_ctrl_checkbox); }
static void* addr_btn_ctrl_pushbutton() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&btn_ctrl_pushbutton); }
static void* addr_btn_ctrl_radiobutton() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&btn_ctrl_radiobutton); }
static void* addr_cond_accumulate() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_accumulate); }
static void* addr_cond_bring() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_bring); }
static void* addr_cond_control() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_control); }
static void* addr_cond_deaths() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_deaths); }
static void* addr_cond_elapsed() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_elapsed); }
static void* addr_cond_highest_score() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_highest_score); }
static void* addr_cond_kill() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_kill); }
static void* addr_cond_least_control() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_least_control); }
static void* addr_cond_least_control_atloc() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_least_control_atloc); }
static void* addr_cond_least_kills() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_least_kills); }
static void* addr_cond_least_resource() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_least_resource); }
static void* addr_cond_lowest_score() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_lowest_score); }
static void* addr_cond_most_control() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_most_control); }
static void* addr_cond_most_control_atloc() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_most_control_atloc); }
static void* addr_cond_most_kills() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_most_kills); }
static void* addr_cond_most_resource() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_most_resource); }
static void* addr_cond_opponents() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_opponents); }
static void* addr_cond_score() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_score); }
static void* addr_cond_switch() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_switch); }
static void* addr_cond_timer() { return (void*)static_cast<int (*)(TrigCondition *)>(&cond_timer); }
static void* addr_ctrl_brief_portrait_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&ctrl_brief_portrait_fcn); }
static void* addr_ctrl_btn_ok_ctrl() { return (void*)static_cast<BOOL (*)(void *, void *)>(&ctrl_btn_ok_ctrl); }
static void* addr_ctrl_btnglow_flc() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&ctrl_btnglow_flc); }
static void* addr_ctrl_cmpgn_flc_conditional() { return (void*)static_cast<int (*)(intptr_t, intptr_t)>(&ctrl_cmpgn_flc_conditional); }
static void* addr_ctrl_dlg() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&ctrl_dlg); }
static void* addr_ctrl_edit_msgtosend_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&ctrl_edit_msgtosend_ctrl_fcn); }
static void* addr_ctrl_flc() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&ctrl_flc); }
static void* addr_ctrl_flc_only_mouseover() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&ctrl_flc_only_mouseover); }
static void* addr_ctrl_listbox_msgs_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&ctrl_listbox_msgs_ctrl_fcn); }
static void* addr_ctrl_mapinfo_text() { return (void*)static_cast<BOOL (*)(void *, void *)>(&ctrl_mapinfo_text); }
static void* addr_ctrl_rect() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&ctrl_rect); }
static void* addr_ctrl_text() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&ctrl_text); }
static void* addr_ctrl_xcampaign_flc_conditional() { return (void*)static_cast<int (*)(intptr_t, intptr_t)>(&ctrl_xcampaign_flc_conditional); }
static void* addr_draw_checkbox() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_checkbox); }
static void* addr_draw_combo() { return (void*)static_cast<void (__fastcall *)(lbox_t *, int, int, rect16_t *)>(&draw_combo); }
static void* addr_draw_ctext() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_ctext); }
static void* addr_draw_dlg() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_dlg); }
static void* addr_draw_edit() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_edit); }
static void* addr_draw_flc() { return (void*)static_cast<void (*)(ctrl_t *, int, int, rect16_t *)>(&draw_flc); }
static void* addr_draw_hscroll() { return (void*)static_cast<void (*)(ctrl_t *, int, int, rect16_t *)>(&draw_hscroll); }
static void* addr_draw_lbox() { return (void*)static_cast<void (__fastcall *)(lbox_t *, int, int, rect16_t *)>(&draw_lbox); }
static void* addr_draw_ltext() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_ltext); }
static void* addr_draw_pushbutton() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_pushbutton); }
static void* addr_draw_radiobutton() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_radiobutton); }
static void* addr_draw_rect() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_rect); }
static void* addr_draw_rtext() { return (void*)static_cast<void (__fastcall *)(ctrl_t *, int, int, rect16_t *)>(&draw_rtext); }
static void* addr_draw_vscroll() { return (void*)static_cast<void (*)(ctrl_t *, int, int, rect16_t *)>(&draw_vscroll); }
static void* addr_edit_ctrl_edit() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&edit_ctrl_edit); }
static void* addr_evt_target_off() { return (void*)static_cast<void (__fastcall *)(void)>(&evt_target_off); }
static void* addr_filter_checkbox_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&filter_checkbox_fcn); }
static void* addr_gluCreate_computerSlotDropDown_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluCreate_computerSlotDropDown_ctrl_fcn); }
static void* addr_gluCreate_gameTypeDropDown_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluCreate_gameTypeDropDown_ctrl_fcn); }
static void* addr_gluCreate_listbox_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluCreate_listbox_ctrl_fcn); }
static void* addr_gluCreate_raceDropDown_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluCreate_raceDropDown_ctrl_fcn); }
static void* addr_gluCreate_selfSlotDropDown_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluCreate_selfSlotDropDown_ctrl_fcn); }
static void* addr_gluCreate_speedScroll_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluCreate_speedScroll_ctrl_fcn); }
static void* addr_gluCreate_subTypeDropDown_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluCreate_subTypeDropDown_ctrl_fcn); }
static void* addr_gluJoin_gamelist_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluJoin_gamelist_ctrl_fcn); }
static void* addr_gluLoad_listbox_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluLoad_listbox_ctrl_fcn); }
static void* addr_gluLogin_listbox_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&gluLogin_listbox_ctrl_fcn); }
static void* addr_gluScore_flc_saveReplay_ctrl_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&gluScore_flc_saveReplay_ctrl_fcn); }
static void* addr_gluScore_player_inset_ctrl_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&gluScore_player_inset_ctrl_fcn); }
static void* addr_gluScore_player_scorebox_ctrl_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&gluScore_player_scorebox_ctrl_fcn); }
static void* addr_gluScore_tab_ctrl_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&gluScore_tab_ctrl_fcn); }
static void* addr_glupedit_edit_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&glupedit_edit_fcn); }
static void* addr_help_listbox_ctrl_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&help_listbox_ctrl_fcn); }
static void* addr_lbox_ctrl_combo() { return (void*)static_cast<int (__fastcall *)(lbox_t *, dlgevt_t *)>(&lbox_ctrl_combo); }
static void* addr_lbox_ctrl_list() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&lbox_ctrl_list); }
static void* addr_maphdr_COLR() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_COLR); }
static void* addr_maphdr_DIM() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_DIM); }
static void* addr_maphdr_ERA() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_ERA); }
static void* addr_maphdr_FORC() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, void *)>(&maphdr_FORC); }
static void* addr_maphdr_MASK() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_MASK); }
static void* addr_maphdr_MBRF() { return (void*)static_cast<int (__stdcall *)(void *, int, int)>(&maphdr_MBRF); }
static void* addr_maphdr_MRGN() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_MRGN); }
static void* addr_maphdr_MRGN_Ext() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_MRGN_Ext); }
static void* addr_maphdr_MTXM() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_MTXM); }
static void* addr_maphdr_OWNR() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_OWNR); }
static void* addr_maphdr_PTEC() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_PTEC); }
static void* addr_maphdr_PTEC_Bwar() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_PTEC_Bwar); }
static void* addr_maphdr_PUNI() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_PUNI); }
static void* addr_maphdr_SIDE() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_SIDE); }
static void* addr_maphdr_SPRP() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, void *)>(&maphdr_SPRP); }
static void* addr_maphdr_STR() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_STR); }
static void* addr_maphdr_TECS() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_TECS); }
static void* addr_maphdr_TECS_Bwar() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_TECS_Bwar); }
static void* addr_maphdr_THG2() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_THG2); }
static void* addr_maphdr_TRIG() { return (void*)static_cast<int (__stdcall *)(void *, int, int)>(&maphdr_TRIG); }
static void* addr_maphdr_TYPE() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_TYPE); }
static void* addr_maphdr_UNIS() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UNIS); }
static void* addr_maphdr_UNIS_Bwar() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UNIS_Bwar); }
static void* addr_maphdr_UNIT() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UNIT); }
static void* addr_maphdr_UPGR() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UPGR); }
static void* addr_maphdr_UPGR_Bwar() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UPGR_Bwar); }
static void* addr_maphdr_UPGS() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UPGS); }
static void* addr_maphdr_UPGS_Bwar() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UPGS_Bwar); }
static void* addr_maphdr_UPRP() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_UPRP); }
static void* addr_maphdr_VCOD() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, int)>(&maphdr_VCOD); }
static void* addr_maphdr_VER() { return (void*)static_cast<BOOL (__stdcall *)(void *, int, void *)>(&maphdr_VER); }
static void* addr_mask_update_air() { return (void*)static_cast<void (*)(int, int, mask_tbl_t *, DWORD *, DWORD, DWORD)>(&mask_update_air); }
static void* addr_mask_update_air_clipped() { return (void*)static_cast<void (*)(int, int, mask_tbl_t *, DWORD *, DWORD, DWORD)>(&mask_update_air_clipped); }
static void* addr_mask_update_ground() { return (void*)static_cast<void (*)(int, int, mask_tbl_t *, DWORD *, DWORD, DWORD)>(&mask_update_ground); }
static void* addr_mask_update_ground_clipped() { return (void*)static_cast<void (*)(int, int, mask_tbl_t *, DWORD *, DWORD, DWORD)>(&mask_update_ground_clipped); }
static void* addr_menu_btn_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&menu_btn_fcn); }
static void* addr_minimap_btn_ctrl_fcn() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&minimap_btn_ctrl_fcn); }
static void* addr_minimap_image_ctrl_fcn() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&minimap_image_ctrl_fcn); }
static void* addr_net_dlg_radio_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&net_dlg_radio_fcn); }
static void* addr_order_attack() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_attack); }
static void* addr_order_attack_unit() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_attack_unit); }
static void* addr_order_bldg_liftoff() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_bldg_liftoff); }
static void* addr_order_build() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_build); }
static void* addr_order_build_fighter() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_build_fighter); }
static void* addr_order_burrow() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_burrow); }
static void* addr_order_cancel_addon() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_cancel_addon); }
static void* addr_order_cancel_land() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_cancel_land); }
static void* addr_order_cancel_nuke() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_cancel_nuke); }
static void* addr_order_cancel_research() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_cancel_research); }
static void* addr_order_cancel_upgrade() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_cancel_upgrade); }
static void* addr_order_carrier_attack() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_carrier_attack); }
static void* addr_order_carrier_move() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_carrier_move); }
static void* addr_order_carrier_stop() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_carrier_stop); }
static void* addr_order_cloak() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_cloak); }
static void* addr_order_deburrow() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_deburrow); }
static void* addr_order_decloak() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_decloak); }
static void* addr_order_harvest() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_harvest); }
static void* addr_order_heal() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_heal); }
static void* addr_order_hold_pos() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_hold_pos); }
static void* addr_order_kill() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_kill); }
static void* addr_order_kill_egg() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_kill_egg); }
static void* addr_order_land() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_land); }
static void* addr_order_morph() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_morph); }
static void* addr_order_move() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_move); }
static void* addr_order_nuke_paint() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_nuke_paint); }
static void* addr_order_nydus_exit() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_nydus_exit); }
static void* addr_order_patrol() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_patrol); }
static void* addr_order_pickup() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_pickup); }
static void* addr_order_place() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_place); }
static void* addr_order_place_addon() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_place_addon); }
static void* addr_order_place_cop() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_place_cop); }
static void* addr_order_place_protoss() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_place_protoss); }
static void* addr_order_rally() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_rally); }
static void* addr_order_reaver_attack() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_reaver_attack); }
static void* addr_order_recharge() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_recharge); }
static void* addr_order_repair() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_repair); }
static void* addr_order_replay_play() { return (void*)static_cast<void (*)(void)>(&order_replay_play); }
static void* addr_order_replay_slowdown() { return (void*)static_cast<void (*)(void)>(&order_replay_slowdown); }
static void* addr_order_replay_speedup() { return (void*)static_cast<void (*)(void)>(&order_replay_speedup); }
static void* addr_order_research() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_research); }
static void* addr_order_return() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_return); }
static void* addr_order_select_larva() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_select_larva); }
static void* addr_order_siege() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_siege); }
static void* addr_order_stim() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_stim); }
static void* addr_order_stop() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_stop); }
static void* addr_order_stop_build() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_stop_build); }
static void* addr_order_summon_archon() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_summon_archon); }
static void* addr_order_summon_darchon() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_summon_darchon); }
static void* addr_order_tank_mode() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_tank_mode); }
static void* addr_order_tech() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_tech); }
static void* addr_order_tower_attack() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_tower_attack); }
static void* addr_order_trilobyte_stop() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_trilobyte_stop); }
static void* addr_order_unload() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_unload); }
static void* addr_order_upgrade() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_upgrade); }
static void* addr_order_upgrade_bldg() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_upgrade_bldg); }
static void* addr_order_zerg_place() { return (void*)static_cast<void (__fastcall *)(WORD, BYTE)>(&order_zerg_place); }
static void* addr_rclick_query_attacker() { return (void*)static_cast<int (*)(CUnit *, int, int, CUnit **, WORD)>(&rclick_query_attacker); }
static void* addr_rclick_query_mover() { return (void*)static_cast<int (*)(CUnit *, int, int, CUnit **, WORD)>(&rclick_query_mover); }
static void* addr_rclick_query_none() { return (void*)static_cast<int (*)(CUnit *, int, int, CUnit **, WORD)>(&rclick_query_none); }
static void* addr_rclick_query_scv() { return (void*)static_cast<int (*)(CUnit *, int, int, CUnit **, WORD)>(&rclick_query_scv); }
static void* addr_rclick_query_tower() { return (void*)static_cast<int (*)(CUnit *, int, int, CUnit **, WORD)>(&rclick_query_tower); }
static void* addr_rclick_query_worker() { return (void*)static_cast<int (*)(CUnit *, int, int, CUnit **, WORD)>(&rclick_query_worker); }
static void* addr_replay_lb_hide() { return (void*)static_cast<BOOL (*)(void)>(&replay_lb_hide); }
static void* addr_replay_lb_show() { return (void*)static_cast<BOOL (*)(char *)>(&replay_lb_show); }
static void* addr_replaydlg_btn_delete_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&replaydlg_btn_delete_fcn); }
static void* addr_replaydlg_edit_name_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&replaydlg_edit_name_fcn); }
static void* addr_replaydlg_listbox_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&replaydlg_listbox_fcn); }
static void* addr_rle_advance_cloak() { return (void*)static_cast<void (__fastcall *)(CImage *)>(&rle_advance_cloak); }
static void* addr_rle_advance_decloak() { return (void*)static_cast<void (__fastcall *)(CImage *)>(&rle_advance_decloak); }
static void* addr_rle_advance_warp_fade() { return (void*)static_cast<void (__fastcall *)(CImage *)>(&rle_advance_warp_fade); }
static void* addr_rle_draw_cloak() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_cloak); }
static void* addr_rle_draw_cloak_hflip() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_cloak_hflip); }
static void* addr_rle_draw_cloaked() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_cloaked); }
static void* addr_rle_draw_cloaked_hflip() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_cloaked_hflip); }
static void* addr_rle_draw_greenbox() { return (void*)static_cast<void (__fastcall *)(int, int, FRAME *, rle_clip_t *, BYTE)>(&rle_draw_greenbox); }
static void* addr_rle_draw_hpfloat() { return (void*)static_cast<void (*)(int, int, int, hpbar_t *, CUnit *)>(&rle_draw_hpfloat); }
static void* addr_rle_draw_invis() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_invis); }
static void* addr_rle_draw_invis_hflip() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_invis_hflip); }
static void* addr_rle_draw_playerside() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, BYTE)>(&rle_draw_playerside); }
static void* addr_rle_draw_playerside_hflip() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, BYTE)>(&rle_draw_playerside_hflip); }
static void* addr_rle_draw_select() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, BYTE)>(&rle_draw_select); }
static void* addr_rle_draw_shift() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_shift); }
static void* addr_rle_draw_shift_hflip() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_shift_hflip); }
static void* addr_rle_draw_texture() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_texture); }
static void* addr_rle_draw_texture_hflip() { return (void*)static_cast<void (__fastcall *)(int, int, void *, void *, DWORD)>(&rle_draw_texture_hflip); }
static void* addr_rle_draw_warp_fade() { return (void*)static_cast<void (__fastcall *)(int, int, FRAME *, rle_clip_t *, BYTE)>(&rle_draw_warp_fade); }
static void* addr_rle_draw_warp_fade_hflip() { return (void*)static_cast<void (__fastcall *)(int, int, FRAME *, rle_clip_t *, BYTE)>(&rle_draw_warp_fade_hflip); }
static void* addr_save_edit_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&save_edit_fcn); }
static void* addr_save_listbox_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&save_listbox_fcn); }
static void* addr_scroll_ctrl_scroll() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&scroll_ctrl_scroll); }
static void* addr_snd_checkbox_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&snd_checkbox_fcn); }
static void* addr_snd_scrollbar_fcn() { return (void*)static_cast<void (*)(ctrl_t *, dlgevt_t *)>(&snd_scrollbar_fcn); }
static void* addr_speed_checkbox_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&speed_checkbox_fcn); }
static void* addr_speed_scrollbar_fcn() { return (void*)static_cast<void (*)(ctrl_t *, dlgevt_t *)>(&speed_scrollbar_fcn); }
static void* addr_statF10_btn_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&statF10_btn_ctrl_fcn); }
static void* addr_statcmd_btn_ctrl_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&statcmd_btn_ctrl_fcn); }
static void* addr_statcmd_replay_progress_fcn() { return (void*)static_cast<void (*)(ctrl_t *, dlgevt_t *)>(&statcmd_replay_progress_fcn); }
static void* addr_statcmd_set_ctrl() { return (void*)static_cast<void (*)(int)>(&statcmd_set_ctrl); }
static void* addr_statdata_icon_btn_ctrl_fcn() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&statdata_icon_btn_ctrl_fcn); }
static void* addr_statdata_progressbar_ctrl_fcn() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&statdata_progressbar_ctrl_fcn); }
static void* addr_statdata_queue_arrow_ctrl_fcn() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&statdata_queue_arrow_ctrl_fcn); }
static void* addr_statdata_select_btn_ctrl_fcn() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&statdata_select_btn_ctrl_fcn); }
static void* addr_statdata_transport_btn_ctrl_fcn() { return (void*)static_cast<int (__fastcall *)(ctrl_t *, dlgevt_t *)>(&statdata_transport_btn_ctrl_fcn); }
static void* addr_statdraw_draw_archon() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_archon); }
static void* addr_statdraw_draw_bldg() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_bldg); }
static void* addr_statdraw_draw_carrier() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_carrier); }
static void* addr_statdraw_draw_egg() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_egg); }
static void* addr_statdraw_draw_none() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_none); }
static void* addr_statdraw_draw_overlord() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_overlord); }
static void* addr_statdraw_draw_powerup() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_powerup); }
static void* addr_statdraw_draw_transport() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_transport); }
static void* addr_statdraw_draw_unit() { return (void*)static_cast<void (__fastcall *)(ctrl_t *)>(&statdraw_draw_unit); }
static void* addr_statdraw_query_archon() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_archon); }
static void* addr_statdraw_query_bldg() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_bldg); }
static void* addr_statdraw_query_carrier() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_carrier); }
static void* addr_statdraw_query_egg() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_egg); }
static void* addr_statdraw_query_overlord() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_overlord); }
static void* addr_statdraw_query_powerup() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_powerup); }
static void* addr_statdraw_query_transport() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_transport); }
static void* addr_statdraw_query_unit() { return (void*)static_cast<int (__fastcall *)(void)>(&statdraw_query_unit); }
static void* addr_statport_portrait_ctrl_fcn() { return (void*)static_cast<BOOL (*)(intptr_t, intptr_t)>(&statport_portrait_ctrl_fcn); }
static void* addr_statres_resource_ctrl_fcn() { return (void*)static_cast<BOOL (*)(void *, void *)>(&statres_resource_ctrl_fcn); }
static void* addr_statwire_evn_ctrl() { return (void*)static_cast<BOOL (*)(void *, void *)>(&statwire_evn_ctrl); }
static void* addr_sub_442960() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&sub_442960); }
static void* addr_sub_442AF0() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&sub_442AF0); }
static void* addr_sub_475B20() { return (void*)static_cast<int (*)(int, intptr_t, int)>(&sub_475B20); }
static void* addr_sub_475C60() { return (void*)static_cast<int (*)(int, intptr_t, int)>(&sub_475C60); }
static void* addr_tips_checkbox_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&tips_checkbox_fcn); }
static void* addr_tips_next_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&tips_next_fcn); }
static void* addr_video_dlg_colorcycle_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&video_dlg_colorcycle_fcn); }
static void* addr_video_dlg_gamma_slider_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&video_dlg_gamma_slider_fcn); }
static void* addr_video_dlg_portraits_radio_fcn() { return (void*)static_cast<int (*)(ctrl_t *, dlgevt_t *)>(&video_dlg_portraits_radio_fcn); }
static void* addr_vision_checkbox_fcn() { return (void*)static_cast<BOOL (*)(ctrl_t *, dlgevt_t *)>(&vision_checkbox_fcn); }
static void* addr_voice_chat() { return (void*)static_cast<int (*)(int, char *, unsigned int)>(&voice_chat); }

static PortCodeRow g_rows[383];

const PortCodeRow *__cdecl PortCodeMap(ULONG *pCount)
{
  static bool s_init;
  if (!s_init) {
    s_init = true;
    g_rows[0] = { 0x405E1Eu, (void*)static_cast<RleDrawFn>(&PortRleDrawNormal) };
    g_rows[1] = { 0x4063B5u, (void*)static_cast<RleDrawFn>(&PortRleDrawInvis) };
    g_rows[2] = { 0x406533u, (void*)static_cast<RleDrawFn>(&PortRleDrawEmp) };
    g_rows[3] = { 0x4067F6u, (void*)static_cast<RleDrawFn>(&PortRleDrawEffect) };
    g_rows[4] = { 0x406950u, (void*)static_cast<RleDrawFn>(&PortRleDrawShadow) };
    g_rows[5] = { 0x406C09u, (void*)static_cast<RleDrawFn>(&PortRleDrawInvisHflip) };
    g_rows[6] = { 0x406D8Eu, (void*)static_cast<RleDrawFn>(&PortRleDrawEmpHflip) };
    g_rows[7] = { 0x406F03u, (void*)static_cast<RleDrawFn>(&PortRleDrawEffectHflip) };
    g_rows[8] = { 0x4071C0u, (void*)static_cast<RleDrawFn>(&PortRleDrawNormalHflip) };
    g_rows[9] = { 0x40730Eu, (void*)static_cast<RleDrawFn>(&PortRleDrawShadowHflip) };
    g_rows[10] = { 0x40E570u, addr_rle_advance_cloak() };
    g_rows[11] = { 0x40E5F0u, addr_rle_advance_decloak() };
    g_rows[12] = { 0x40E650u, addr_rle_advance_warp_fade() };
    g_rows[13] = { 0x40E6C0u, addr_rle_draw_invis() };
    g_rows[14] = { 0x40E700u, addr_rle_draw_invis_hflip() };
    g_rows[15] = { 0x40E740u, addr_rle_draw_cloak() };
    g_rows[16] = { 0x40E780u, addr_rle_draw_cloak_hflip() };
    g_rows[17] = { 0x40E7C0u, addr_rle_draw_cloaked() };
    g_rows[18] = { 0x40E7E0u, addr_rle_draw_cloaked_hflip() };
    g_rows[19] = { 0x40E800u, addr_rle_draw_texture() };
    g_rows[20] = { 0x40E880u, addr_rle_draw_texture_hflip() };
    g_rows[21] = { 0x40E900u, addr_rle_draw_select() };
    g_rows[22] = { 0x40E950u, addr_rle_draw_playerside() };
    g_rows[23] = { 0x40E990u, addr_rle_draw_playerside_hflip() };
    g_rows[24] = { 0x40E9D0u, addr_rle_draw_greenbox() };
    g_rows[25] = { 0x40EA60u, addr_rle_draw_shift() };
    g_rows[26] = { 0x40EAC0u, addr_rle_draw_shift_hflip() };
    g_rows[27] = { 0x40EB20u, addr_rle_draw_warp_fade() };
    g_rows[28] = { 0x40EB40u, addr_rle_draw_warp_fade_hflip() };
    g_rows[29] = { 0x441980u, addr_allied_victory_fcn() };
    g_rows[30] = { 0x441A60u, addr_filter_checkbox_fcn() };
    g_rows[31] = { 0x4426D0u, addr_vision_checkbox_fcn() };
    g_rows[32] = { 0x442960u, addr_sub_442960() };
    g_rows[33] = { 0x442AF0u, addr_sub_442AF0() };
    g_rows[34] = { 0x44FCD0u, addr_brfact_wait() };
    g_rows[35] = { 0x44FD10u, addr_brfact_wav() };
    g_rows[36] = { 0x44FDA0u, addr_brfact_message() };
    g_rows[37] = { 0x44FDC0u, addr_brfact_objectives() };
    g_rows[38] = { 0x44FDE0u, addr_brfact_show() };
    g_rows[39] = { 0x44FE00u, addr_brfact_hide() };
    g_rows[40] = { 0x44FE10u, addr_brfact_talking() };
    g_rows[41] = { 0x44FE30u, addr_brfact_transmission() };
    g_rows[42] = { 0x44FEC0u, addr_brfact_tutorial() };
    g_rows[43] = { 0x44FFD0u, addr_ctrl_brief_portrait_fcn() };
    g_rows[44] = { 0x4508F0u, addr_ctrl_mapinfo_text() };
    g_rows[45] = { 0x450D50u, addr_ctrl_btn_ok_ctrl() };
    g_rows[46] = { 0x451310u, addr_ctrl_edit_msgtosend_ctrl_fcn() };
    g_rows[47] = { 0x4513D0u, addr_ctrl_listbox_msgs_ctrl_fcn() };
    g_rows[48] = { 0x453FE0u, addr_ctrl_cmpgn_flc_conditional() };
    g_rows[49] = { 0x4559D0u, addr_gluCreate_listbox_ctrl_fcn() };
    g_rows[50] = { 0x456120u, addr_gluCreate_selfSlotDropDown_ctrl_fcn() };
    g_rows[51] = { 0x4561D0u, addr_gluCreate_raceDropDown_ctrl_fcn() };
    g_rows[52] = { 0x456290u, addr_gluCreate_computerSlotDropDown_ctrl_fcn() };
    g_rows[53] = { 0x4565D0u, addr_gluCreate_speedScroll_ctrl_fcn() };
    g_rows[54] = { 0x456780u, addr_gluCreate_subTypeDropDown_ctrl_fcn() };
    g_rows[55] = { 0x4569D0u, addr_gluCreate_gameTypeDropDown_ctrl_fcn() };
    g_rows[56] = { 0x458D80u, addr_ctrl_xcampaign_flc_conditional() };
    g_rows[57] = { 0x459550u, addr_gluJoin_gamelist_ctrl_fcn() };
    g_rows[58] = { 0x459D40u, addr_gluLoad_listbox_ctrl_fcn() };
    g_rows[59] = { 0x45A590u, addr_gluLogin_listbox_ctrl_fcn() };
    g_rows[60] = { 0x45AFA0u, addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI____scalar_deleting_destructor_() };
    g_rows[61] = { 0x45C790u, addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI___Destroy() };
    g_rows[62] = { 0x45CF40u, addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI___InternalDelete() };
    g_rows[63] = { 0x45CF70u, addr_TSHashTable_HASHENTRY_HASHKEY_CONSTSTRI___InternalNew() };
    g_rows[64] = { 0x45FCC0u, addr_glupedit_edit_fcn() };
    g_rows[65] = { 0x461F70u, addr_gluScore_player_inset_ctrl_fcn() };
    g_rows[66] = { 0x4620C0u, addr_gluScore_flc_saveReplay_ctrl_fcn() };
    g_rows[67] = { 0x462E40u, addr_gluScore_player_scorebox_ctrl_fcn() };
    g_rows[68] = { 0x463350u, addr_gluScore_tab_ctrl_fcn() };
    g_rows[69] = { 0x464810u, addr_help_listbox_ctrl_fcn() };
    g_rows[70] = { 0x464D20u, addr_rle_draw_hpfloat() };
    g_rows[71] = { 0x467E50u, addr_maphdr_VCOD() };
    g_rows[72] = { 0x467F40u, addr_maphdr_UNIS_Bwar() };
    g_rows[73] = { 0x4680D0u, addr_maphdr_UNIS() };
    g_rows[74] = { 0x468250u, addr_maphdr_UPGS_Bwar() };
    g_rows[75] = { 0x468340u, addr_maphdr_UPGS() };
    g_rows[76] = { 0x468430u, addr_maphdr_TECS_Bwar() };
    g_rows[77] = { 0x468500u, addr_maphdr_TECS() };
    g_rows[78] = { 0x4685D0u, addr_maphdr_PUNI() };
    g_rows[79] = { 0x468660u, addr_maphdr_UPGR_Bwar() };
    g_rows[80] = { 0x468750u, addr_maphdr_UPGR() };
    g_rows[81] = { 0x468840u, addr_maphdr_PTEC_Bwar() };
    g_rows[82] = { 0x468930u, addr_maphdr_PTEC() };
    g_rows[83] = { 0x468A20u, addr_maphdr_TYPE() };
    g_rows[84] = { 0x468A30u, addr_maphdr_VER() };
    g_rows[85] = { 0x468A60u, addr_maphdr_SIDE() };
    g_rows[86] = { 0x468AB0u, addr_maphdr_OWNR() };
    g_rows[87] = { 0x468B00u, addr_maphdr_ERA() };
    g_rows[88] = { 0x468B60u, addr_maphdr_COLR() };
    g_rows[89] = { 0x468BA0u, addr_maphdr_THG2() };
    g_rows[90] = { 0x468CE0u, addr_maphdr_STR() };
    g_rows[91] = { 0x468D50u, addr_maphdr_MRGN() };
    g_rows[92] = { 0x468D80u, addr_maphdr_MRGN_Ext() };
    g_rows[93] = { 0x468DB0u, addr_maphdr_UPRP() };
    g_rows[94] = { 0x468DE0u, addr_maphdr_TRIG() };
    g_rows[95] = { 0x468E80u, addr_maphdr_MBRF() };
    g_rows[96] = { 0x468F30u, addr_maphdr_UNIT() };
    g_rows[97] = { 0x469130u, addr_maphdr_DIM() };
    g_rows[98] = { 0x469170u, addr_maphdr_MTXM() };
    g_rows[99] = { 0x469260u, addr_maphdr_MASK() };
    g_rows[100] = { 0x4692F0u, addr_maphdr_SPRP() };
    g_rows[101] = { 0x469320u, addr_maphdr_FORC() };
    g_rows[102] = { 0x46C570u, addr_mask_update_air() };
    g_rows[103] = { 0x46C5B0u, addr_mask_update_air_clipped() };
    g_rows[104] = { 0x46C620u, addr_mask_update_ground() };
    g_rows[105] = { 0x46C6A0u, addr_mask_update_ground_clipped() };
    g_rows[106] = { 0x46C7A0u, addr_menu_btn_fcn() };
    g_rows[107] = { 0x46C7D0u, addr_big_menu_btn_fcn() };
    g_rows[108] = { 0x46D0E0u, addr_minimap_btn_ctrl_fcn() };
    g_rows[109] = { 0x46D6A0u, addr_minimap_image_ctrl_fcn() };
    g_rows[110] = { 0x46FA90u, addr_net_dlg_radio_fcn() };
    g_rows[111] = { 0x475B20u, addr_sub_475B20() };
    g_rows[112] = { 0x475C60u, addr_sub_475C60() };
    g_rows[113] = { 0x47C310u, addr_order_attack() };
    g_rows[114] = { 0x47C360u, addr_order_attack_unit() };
    g_rows[115] = { 0x47C370u, addr_order_bldg_liftoff() };
    g_rows[116] = { 0x47C380u, addr_order_build() };
    g_rows[117] = { 0x47C390u, addr_order_build_fighter() };
    g_rows[118] = { 0x47C3A0u, addr_order_burrow() };
    g_rows[119] = { 0x47C3B0u, addr_order_cancel_addon() };
    g_rows[120] = { 0x47C3C0u, addr_order_cancel_research() };
    g_rows[121] = { 0x47C3D0u, addr_order_cancel_upgrade() };
    g_rows[122] = { 0x47C3E0u, addr_order_cancel_nuke() };
    g_rows[123] = { 0x47C3F0u, addr_order_carrier_attack() };
    g_rows[124] = { 0x47C400u, addr_order_carrier_move() };
    g_rows[125] = { 0x47C410u, addr_order_carrier_stop() };
    g_rows[126] = { 0x47C420u, addr_order_cloak() };
    g_rows[127] = { 0x47C440u, addr_order_deburrow() };
    g_rows[128] = { 0x47C450u, addr_order_decloak() };
    g_rows[129] = { 0x47C460u, addr_order_pickup() };
    g_rows[130] = { 0x47C490u, addr_order_harvest() };
    g_rows[131] = { 0x47C4A0u, addr_order_heal() };
    g_rows[132] = { 0x47C4B0u, addr_order_hold_pos() };
    g_rows[133] = { 0x47C4C0u, addr_order_kill() };
    g_rows[134] = { 0x47C4D0u, addr_order_kill_egg() };
    g_rows[135] = { 0x47C4E0u, addr_order_land() };
    g_rows[136] = { 0x47C4F0u, addr_order_morph() };
    g_rows[137] = { 0x47C520u, addr_order_move() };
    g_rows[138] = { 0x47C530u, addr_order_nuke_paint() };
    g_rows[139] = { 0x47C540u, addr_order_nydus_exit() };
    g_rows[140] = { 0x47C550u, addr_order_patrol() };
    g_rows[141] = { 0x47C560u, addr_order_place() };
    g_rows[142] = { 0x47C590u, addr_order_place_addon() };
    g_rows[143] = { 0x47C5C0u, addr_order_place_cop() };
    g_rows[144] = { 0x47C5D0u, addr_order_place_protoss() };
    g_rows[145] = { 0x47C600u, addr_order_rally() };
    g_rows[146] = { 0x47C610u, addr_order_reaver_attack() };
    g_rows[147] = { 0x47C620u, addr_order_recharge() };
    g_rows[148] = { 0x47C630u, addr_order_repair() };
    g_rows[149] = { 0x47C640u, addr_order_return() };
    g_rows[150] = { 0x47C660u, addr_order_research() };
    g_rows[151] = { 0x47C670u, addr_order_select_larva() };
    g_rows[152] = { 0x47C700u, addr_order_siege() };
    g_rows[153] = { 0x47C710u, addr_order_stim() };
    g_rows[154] = { 0x47C760u, addr_order_stop() };
    g_rows[155] = { 0x47C770u, addr_order_stop_build() };
    g_rows[156] = { 0x47C780u, addr_order_summon_archon() };
    g_rows[157] = { 0x47C790u, addr_order_summon_darchon() };
    g_rows[158] = { 0x47C7A0u, addr_order_tank_mode() };
    g_rows[159] = { 0x47C7B0u, addr_order_tech() };
    g_rows[160] = { 0x47C9C0u, addr_order_tower_attack() };
    g_rows[161] = { 0x47C9D0u, addr_order_trilobyte_stop() };
    g_rows[162] = { 0x47C9E0u, addr_order_unload() };
    g_rows[163] = { 0x47CA10u, addr_order_upgrade() };
    g_rows[164] = { 0x47CA20u, addr_order_upgrade_bldg() };
    g_rows[165] = { 0x47CA50u, addr_order_zerg_place() };
    g_rows[166] = { 0x47EF40u, addr_rclick_query_none() };
    g_rows[167] = { 0x47EF50u, addr_rclick_query_attacker() };
    g_rows[168] = { 0x47F010u, addr_rclick_query_tower() };
    g_rows[169] = { 0x47F070u, addr_rclick_query_mover() };
    g_rows[170] = { 0x47F180u, addr_rclick_query_scv() };
    g_rows[171] = { 0x47F2C0u, addr_rclick_query_worker() };
    g_rows[172] = { 0x47FD60u, addr_replay_lb_show() };
    g_rows[173] = { 0x47FE10u, addr_replay_lb_hide() };
    g_rows[174] = { 0x480E50u, addr_order_replay_slowdown() };
    g_rows[175] = { 0x480EA0u, addr_bf_replay_slowdown() };
    g_rows[176] = { 0x480EE0u, addr_order_replay_speedup() };
    g_rows[177] = { 0x480F30u, addr_bf_replay_speedup() };
    g_rows[178] = { 0x480F70u, addr_order_replay_play() };
    g_rows[179] = { 0x480F90u, addr_bf_replay_play() };
    g_rows[180] = { 0x481160u, addr_replaydlg_btn_delete_fcn() };
    g_rows[181] = { 0x481250u, addr_replaydlg_edit_name_fcn() };
    g_rows[182] = { 0x481320u, addr_replaydlg_listbox_fcn() };
    g_rows[183] = { 0x49B4D0u, addr_save_edit_fcn() };
    g_rows[184] = { 0x49B500u, addr_save_listbox_fcn() };
    g_rows[185] = { 0x49ED40u, addr_snd_checkbox_fcn() };
    g_rows[186] = { 0x49EFC0u, addr_snd_scrollbar_fcn() };
    g_rows[187] = { 0x49F1E0u, addr_speed_scrollbar_fcn() };
    g_rows[188] = { 0x49F3C0u, addr_speed_checkbox_fcn() };
    g_rows[189] = { 0x49F410u, addr_bf_always() };
    g_rows[190] = { 0x49F420u, addr_bf_building_ok() };
    g_rows[191] = { 0x49F460u, addr_bf_build_fighter_ok() };
    g_rows[192] = { 0x49F4C0u, addr_bf_can_move_patrol() };
    g_rows[193] = { 0x49F500u, addr_bf_can_move_stop() };
    g_rows[194] = { 0x49F530u, addr_bf_can_attack() };
    g_rows[195] = { 0x49F570u, addr_bf_tower() };
    g_rows[196] = { 0x49F5D0u, addr_bf_stop_unit() };
    g_rows[197] = { 0x49F5F0u, addr_bf_stop_addon() };
    g_rows[198] = { 0x49F600u, addr_bf_larva_sel() };
    g_rows[199] = { 0x49F640u, addr_bf_rally() };
    g_rows[200] = { 0x49F650u, addr_bf_hatch_rally() };
    g_rows[201] = { 0x49F690u, addr_bf_not_comp() };
    g_rows[202] = { 0x49F6B0u, addr_bf_stop_upgrade() };
    g_rows[203] = { 0x49F6D0u, addr_bf_stop_research() };
    g_rows[204] = { 0x49F6F0u, addr_bf_no_load() };
    g_rows[205] = { 0x49F720u, addr_bf_load() };
    g_rows[206] = { 0x49F750u, addr_bf_trans() };
    g_rows[207] = { 0x49F790u, addr_bf_trans_has_passenger() };
    g_rows[208] = { 0x49F7C0u, addr_bf_research() };
    g_rows[209] = { 0x49F7D0u, addr_bf_has_spell() };
    g_rows[210] = { 0x49F7E0u, addr_bf_has_mines() };
    g_rows[211] = { 0x49F840u, addr_bf_upgrade() };
    g_rows[212] = { 0x49F850u, addr_bf_can_cloak() };
    g_rows[213] = { 0x49F8C0u, addr_bf_is_cloaked() };
    g_rows[214] = { 0x49F930u, addr_bf_can_cloak_hero() };
    g_rows[215] = { 0x49F9A0u, addr_bf_is_hero_cloaked() };
    g_rows[216] = { 0x49FA10u, addr_bf_can_siege() };
    g_rows[217] = { 0x49FA60u, addr_bf_is_sieged() };
    g_rows[218] = { 0x49FAB0u, addr_bf_can_tank_move() };
    g_rows[219] = { 0x49FAE0u, addr_bf_has_nuke() };
    g_rows[220] = { 0x49FB00u, addr_bf_battery() };
    g_rows[221] = { 0x49FB40u, addr_bf_is_ship() };
    g_rows[222] = { 0x49FB60u, addr_bf_lifted() };
    g_rows[223] = { 0x49FB90u, addr_bf_liftoff_ok() };
    g_rows[224] = { 0x49FBF0u, addr_bf_can_burrow() };
    g_rows[225] = { 0x49FC70u, addr_bf_burrowed() };
    g_rows[226] = { 0x49FCE0u, addr_bf_make_lurker() };
    g_rows[227] = { 0x49FCF0u, addr_bf_can_move_patrol_unburrowed() };
    g_rows[228] = { 0x49FD20u, addr_bf_can_attack_unburrowed() };
    g_rows[229] = { 0x49FD70u, addr_bf_no_load_drone() };
    g_rows[230] = { 0x49FDC0u, addr_bf_load_drone() };
    g_rows[231] = { 0x49FE10u, addr_bf_can_move_stop_unburrowed() };
    g_rows[232] = { 0x49FE60u, addr_bf_no_exit() };
    g_rows[233] = { 0x49FE80u, addr_bf_zerg_build_basic() };
    g_rows[234] = { 0x49FF20u, addr_bf_zerg_build_advanced() };
    g_rows[235] = { 0x49FFB0u, addr_bf_has_interceptor() };
    g_rows[236] = { 0x49FFF0u, addr_bf_has_scarab() };
    g_rows[237] = { 0x4A0030u, addr_bf_can_make_archon_need2() };
    g_rows[238] = { 0x4A0050u, addr_bf_can_make_archon() };
    g_rows[239] = { 0x4A0090u, addr_bf_can_make_darchon() };
    g_rows[240] = { 0x4A00D0u, addr_bf_protoss_build_basic() };
    g_rows[241] = { 0x4A0180u, addr_bf_protoss_build_advanced() };
    g_rows[242] = { 0x4A0230u, addr_bf_stop_build() };
    g_rows[243] = { 0x4A0260u, addr_bf_scv_attack_orders() };
    g_rows[244] = { 0x4A0290u, addr_bf_no_load_scv() };
    g_rows[245] = { 0x4A02F0u, addr_bf_load_scv() };
    g_rows[246] = { 0x4A0350u, addr_bf_terran_build_basic() };
    g_rows[247] = { 0x4A0410u, addr_bf_terran_build_advanced() };
    g_rows[248] = { 0x4A0490u, addr_bf_empty_silo() };
    g_rows[249] = { 0x4A04B0u, addr_evt_target_off() };
    g_rows[250] = { 0x4A04C0u, addr_order_cancel_land() };
    g_rows[251] = { 0x4A0660u, addr_statcmd_btn_ctrl_fcn() };
    g_rows[252] = { 0x4A09F0u, addr_statcmd_replay_progress_fcn() };
    g_rows[253] = { 0x4A11E0u, addr_statcmd_set_ctrl() };
    g_rows[254] = { 0x4A1D40u, addr_statdata_progressbar_ctrl_fcn() };
    g_rows[255] = { 0x4A21D0u, addr_statdata_select_btn_ctrl_fcn() };
    g_rows[256] = { 0x4A2350u, addr_statdata_queue_arrow_ctrl_fcn() };
    g_rows[257] = { 0x4A2830u, addr_statdata_icon_btn_ctrl_fcn() };
    g_rows[258] = { 0x4A2940u, addr_statdata_transport_btn_ctrl_fcn() };
    g_rows[259] = { 0x4A3160u, addr_statdraw_draw_archon() };
    g_rows[260] = { 0x4A3310u, addr_statdraw_draw_bldg() };
    g_rows[261] = { 0x4A3540u, addr_statdraw_draw_carrier() };
    g_rows[262] = { 0x4A3840u, addr_statdraw_draw_egg() };
    g_rows[263] = { 0x4A3F00u, addr_statdraw_draw_none() };
    g_rows[264] = { 0x4A3FD0u, addr_statdraw_draw_overlord() };
    g_rows[265] = { 0x4A4030u, addr_statdraw_draw_powerup() };
    g_rows[266] = { 0x4A4AF0u, addr_statdraw_draw_transport() };
    g_rows[267] = { 0x4A4E40u, addr_statdraw_draw_unit() };
    g_rows[268] = { 0x4A5160u, addr_statdraw_query_archon() };
    g_rows[269] = { 0x4A5180u, addr_statdraw_query_bldg() };
    g_rows[270] = { 0x4A51F0u, addr_statdraw_query_carrier() };
    g_rows[271] = { 0x4A52A0u, addr_statdraw_query_egg() };
    g_rows[272] = { 0x4A53A0u, addr_statdraw_query_overlord() };
    g_rows[273] = { 0x4A53F0u, addr_statdraw_query_powerup() };
    g_rows[274] = { 0x4A5410u, addr_statdraw_query_transport() };
    g_rows[275] = { 0x4A55E0u, addr_statdraw_query_unit() };
    g_rows[276] = { 0x4A6500u, addr_statF10_btn_ctrl_fcn() };
    g_rows[277] = { 0x4A6D30u, addr_statport_portrait_ctrl_fcn() };
    g_rows[278] = { 0x4A7790u, addr_statres_resource_ctrl_fcn() };
    g_rows[279] = { 0x4A82E0u, addr_statwire_evn_ctrl() };
    g_rows[280] = { 0x4B50E0u, addr_tips_checkbox_fcn() };
    g_rows[281] = { 0x4B52A0u, addr_tips_next_fcn() };
    g_rows[282] = { 0x4B5620u, addr_act_portrait() };
    g_rows[283] = { 0x4B5660u, addr_act_mute() };
    g_rows[284] = { 0x4B5690u, addr_act_unmute() };
    g_rows[285] = { 0x4B56B0u, addr_act_leaderboard_cpu() };
    g_rows[286] = { 0x4B56F0u, addr_act_move_loc() };
    g_rows[287] = { 0x4B5840u, addr_act_move_unit() };
    g_rows[288] = { 0x4B5900u, addr_act_next_mission() };
    g_rows[289] = { 0x4B5930u, addr_act_doodad() };
    g_rows[290] = { 0x4B59C0u, addr_act_invincible() };
    g_rows[291] = { 0x4B5A50u, addr_act_give() };
    g_rows[292] = { 0x4B5B00u, addr_act_set_unit_hp() };
    g_rows[293] = { 0x4B5BB0u, addr_act_set_unit_energy() };
    g_rows[294] = { 0x4B5C60u, addr_act_set_unit_shields() };
    g_rows[295] = { 0x4B5D10u, addr_act_set_unit_res() };
    g_rows[296] = { 0x4B5DC0u, addr_act_set_hangar() };
    g_rows[297] = { 0x4B5E70u, addr_act_stop_timer() };
    g_rows[298] = { 0x4B5E80u, addr_act_start_timer() };
    g_rows[299] = { 0x4B5E90u, addr_act_alliance() };
    g_rows[300] = { 0x4B5ED0u, addr_act_disable_escape() };
    g_rows[301] = { 0x4B5EE0u, addr_act_enable_escape() };
    g_rows[302] = { 0x4B5EF0u, addr_act_victory() };
    g_rows[303] = { 0x4B5F20u, addr_act_defeat() };
    g_rows[304] = { 0x4B5F70u, addr_act_draw() };
    g_rows[305] = { 0x4B5FC0u, addr_act_preserve() };
    g_rows[306] = { 0x4B5FD0u, addr_act_wait() };
    g_rows[307] = { 0x4B6020u, addr_act_pause() };
    g_rows[308] = { 0x4B6080u, addr_act_unpause() };
    g_rows[309] = { 0x4B60E0u, addr_act_transmission() };
    g_rows[310] = { 0x4B6240u, addr_act_wav() };
    g_rows[311] = { 0x4B6300u, addr_act_text() };
    g_rows[312] = { 0x4B6350u, addr_act_center_view() };
    g_rows[313] = { 0x4B64D0u, addr_act_create_unit() };
    g_rows[314] = { 0x4B6550u, addr_act_set_deaths() };
    g_rows[315] = { 0x4B65A0u, addr_act_order() };
    g_rows[316] = { 0x4B6670u, addr_act_mission_obj() };
    g_rows[317] = { 0x4B6690u, addr_act_set_switch() };
    g_rows[318] = { 0x4B6720u, addr_act_set_timer() };
    g_rows[319] = { 0x4B6760u, addr_act_run_ai() };
    g_rows[320] = { 0x4B6780u, addr_act_run_ai_atloc() };
    g_rows[321] = { 0x4B67C0u, addr_act_leaderboard() };
    g_rows[322] = { 0x4B6840u, addr_act_kill() };
    g_rows[323] = { 0x4B6870u, addr_act_kill_atloc() };
    g_rows[324] = { 0x4B68C0u, addr_act_remove() };
    g_rows[325] = { 0x4B68F0u, addr_act_remove_atloc() };
    g_rows[326] = { 0x4B6940u, addr_act_set_resource() };
    g_rows[327] = { 0x4B6990u, addr_act_set_score() };
    g_rows[328] = { 0x4B69E0u, addr_act_ping() };
    g_rows[329] = { 0x4B8E70u, addr_act_none() };
    g_rows[330] = { 0x4B8E80u, (void*)static_cast<AlwaysFalseFn>(&nullsub___shared_) };
    g_rows[331] = { 0x4B8E90u, addr_cond_timer() };
    g_rows[332] = { 0x4B8ED0u, addr_cond_control() };
    g_rows[333] = { 0x4B8F20u, addr_cond_bring() };
    g_rows[334] = { 0x4B8F80u, addr_cond_accumulate() };
    g_rows[335] = { 0x4B8FD0u, addr_cond_kill() };
    g_rows[336] = { 0x4B9020u, addr_cond_score() };
    g_rows[337] = { 0x4B9070u, addr_cond_most_control() };
    g_rows[338] = { 0x4B90E0u, addr_cond_most_control_atloc() };
    g_rows[339] = { 0x4B9160u, addr_cond_most_kills() };
    g_rows[340] = { 0x4B91C0u, addr_cond_highest_score() };
    g_rows[341] = { 0x4B9220u, addr_cond_most_resource() };
    g_rows[342] = { 0x4B9280u, addr_cond_switch() };
    g_rows[343] = { 0x4B92E0u, addr_cond_elapsed() };
    g_rows[344] = { 0x4B9320u, addr_cond_opponents() };
    g_rows[345] = { 0x4B9360u, addr_cond_deaths() };
    g_rows[346] = { 0x4B93B0u, addr_cond_least_control() };
    g_rows[347] = { 0x4B9420u, addr_cond_least_control_atloc() };
    g_rows[348] = { 0x4B94A0u, addr_cond_least_kills() };
    g_rows[349] = { 0x4B9510u, addr_cond_lowest_score() };
    g_rows[350] = { 0x4B9580u, addr_cond_least_resource() };
    g_rows[351] = { 0x4BE5F0u, addr_video_dlg_colorcycle_fcn() };
    g_rows[352] = { 0x4BE820u, addr_video_dlg_portraits_radio_fcn() };
    g_rows[353] = { 0x4BE8D0u, addr_video_dlg_gamma_slider_fcn() };
    g_rows[354] = { 0x4BE940u, addr_voice_chat() };
    g_rows[355] = { 0x4BE950u, (void*)static_cast<VoiceSquelchFn>(&voice_squelch) };
    g_rows[356] = { 0x4C1470u, addr_ctrl_dlg() };
    g_rows[357] = { 0x4C17E0u, addr_ctrl_rect() };
    g_rows[358] = { 0x4C1860u, addr_ctrl_text() };
    g_rows[359] = { 0x4C37D0u, addr_draw_rtext() };
    g_rows[360] = { 0x4C37F0u, addr_draw_dlg() };
    g_rows[361] = { 0x4C3860u, addr_draw_pushbutton() };
    g_rows[362] = { 0x4C3890u, addr_draw_radiobutton() };
    g_rows[363] = { 0x4C39B0u, addr_draw_checkbox() };
    g_rows[364] = { 0x4C3B20u, addr_draw_rect() };
    g_rows[365] = { 0x4C3B40u, addr_draw_edit() };
    g_rows[366] = { 0x4C3BA0u, addr_draw_ltext() };
    g_rows[367] = { 0x4C3BC0u, addr_draw_ctext() };
    g_rows[368] = { 0x4C63B0u, addr_lbox_ctrl_combo() };
    g_rows[369] = { 0x4C6480u, addr_lbox_ctrl_list() };
    g_rows[370] = { 0x4C6700u, addr_draw_combo() };
    g_rows[371] = { 0x4C6740u, addr_draw_lbox() };
    g_rows[372] = { 0x4C74A0u, addr_btn_ctrl_checkbox() };
    g_rows[373] = { 0x4C7500u, addr_btn_ctrl_pushbutton() };
    g_rows[374] = { 0x4C7540u, addr_btn_ctrl_radiobutton() };
    g_rows[375] = { 0x4C7880u, addr_edit_ctrl_edit() };
    g_rows[376] = { 0x4C8150u, addr_draw_flc() };
    g_rows[377] = { 0x4C82B0u, addr_ctrl_flc_only_mouseover() };
    g_rows[378] = { 0x4C82F0u, addr_ctrl_flc() };
    g_rows[379] = { 0x4C83B0u, addr_ctrl_btnglow_flc() };
    g_rows[380] = { 0x4C8D10u, addr_draw_hscroll() };
    g_rows[381] = { 0x4C9060u, addr_draw_vscroll() };
    g_rows[382] = { 0x4C9630u, addr_scroll_ctrl_scroll() };
  }
  *pCount = 383;
  return g_rows;
}
