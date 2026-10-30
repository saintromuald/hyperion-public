// game.h -- declarations for src117/lang/game.cpp: starting, restarting and
// tearing down a game session.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GAME_H
#define OC117_GAME_H

#include "platform.h"
#include "gamemap.h"    // gamemap_init, gamemap_free -- gamemap.cpp owns both
#include "dialog.h"   // dlg_dword_t, the button-handler parameter type
#include "gds_palette.h"  // palentry_t, PFN_FIND_PEN

// CPalette::find_closest_palette_entry -- 0x4666C0, 146 B. game_init passes its
// address to init_pens as a PFN_FIND_PEN (gds_palette.h), so the class is
// declared here until its own TU has a header. The return is int, not BYTE:
// minimap_init joins it with an int without widening it.
class CPalette {
public:
  static int __fastcall find_closest_palette_entry(void *pPalette,
                                                  palentry_t entry);
};

// The callees game.cpp reaches that no other header declares.

// 0x414400, 322 B.
void count_init();



// 0x469ED0, 678 B.
void maphdr_data_init();

// 0x46B560, 60 B.
BOOL LoadMapData();

// 0x49CB90, 39 B. It tail-jumps net_cmds_reset_select.
void select_init();

// 0x40B060, 125 B.
void cell_init();

// 0x46B5C0, 331 B. ECX, EDX and one stack dword (`ret 4h`).
BOOL PreloadMap(char *pszMap, void *pSlots, int nFlag);

// 0x499C50, 558 B.
BOOL sub_499C50();

// 0x4074A0 / 0x41D690 / 0x461E00 / 0x477610 / 0x463EA0 / 0x4B2830, all
// nullary; group_un_init tail-jumps net_cmds_init.
void AssignAINames();
void permute_colors_by_slot_map();
void alliance_init();
void group_un_init();
void target_init();
// gametype_init_resources and gametype_load_triggers are seated in gametype.h.

// 0x41D570 / 0x4BE240, nullary.
void initPlayerColorMap();
void victory_init();

// 0x478240, 41 B. The seed in ECX.
void net_srand(int nSeed);

// dlg_grp_free is seated in src117/h/dlg_dlg.h.

// 0x4662C0 / 0x466470, 360 B and 17 B. league_write_replay takes its one
// argument on the stack (`ret 4h`).
BOOL __stdcall league_write_replay(char *pszPath);
intptr_t league_get_game_result();

// 0x476F80, 210 B.
void net_end_game();

// 0x445460 / 0x445470, 15 B and 169 B. Their .rdata strings name gameloop.cpp,
// so they belong to that TU.
void sub_445460();
void sub_445470();

// game.cpp's own functions, in oracle VA order.

// 0x443680, 266 B.
void game_init_theme();

// 0x443900, 108 B.
BOOL sub_443900();

// 0x443970, 540 B. On success it returns CreateGame_SinglePlayer's result, so
// int and not BOOL.
int sub_443970();

// 0x443B90, 80 B.
BOOL SinglePlayerMeleeInitGame();

// 0x443C70 / 0x443D20, 173 B and 79 B. Each returns CreateGame_SinglePlayer's
// result on success, so int and not BOOL.
int sub_443C70();
int sub_443D20();

// 0x443BE0, 143 B.
void game_slots_init();

// 0x443DB0, 358 B. The flag in ECX.
void free_game(int nWriteReplay);

// 0x443D70, 51 B. It always returns 1.
BOOL free_game_minimap();

// 0x443F20, 18 B. The button-click sound handler new_game hands to
// snd_set_btn_click, so its parameter is PFN_BTNCLICKSND's; `ret 4h`.
void __stdcall sub_443F20(dlg_dword_t dwSound);

// 0x443F40, 743 B.
BOOL new_game();

// 0x444230, 126 B. It tail-jumps status_load_strings.
void game_ui_init();

// 0x443790, 177 B.
BOOL game_init();

// 0x443850, 168 B.
BOOL game_minimap_init();

#endif  // OC117_GAME_H
