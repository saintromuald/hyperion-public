// gamesnd.h -- declarations for src117/lang/gamesnd.cpp: the game's sound cues,
// positional sounds, acknowledgements and error sounds, and the sound callees
// it reaches in other TUs.
#pragma once

#include "platform.h"   // WORD, and <stddef.h> offsetof
#include "pos.h"        // Hyperion: upos_t (stage E)

// snd_pos_t -- the map point a positional sound is played at, a 4-byte object
// passed by value as in 1.17: x is the low half of the pushed dword, y the high.
struct snd_pos_t {
  /*0x00*/ upos_t x;   // `mov word ptr [ebp-4h], ax` <- CSprite::wPosX
  /*0x02*/ upos_t y;   // `mov word ptr [ebp-2h], ax` <- CSprite::wPosY
};
// Hyperion: on x64 the two are positions of 4 bytes (h/pos.h, stage E)
OC_X86_LAYOUT(sizeof(snd_pos_t)      == 4,    "snd_pos_t     pushed as ONE dword: `push dword ptr [ebp-4h]`");
static_assert(offsetof(snd_pos_t, x) == 0x00, "snd_pos_t::x  `mov word ptr [ebp-4h], ax`, the LOW half of the pushed dword");
OC_X86_LAYOUT(offsetof(snd_pos_t, y) == 0x02, "snd_pos_t::y  `mov word ptr [ebp-2h], ax`, the HIGH half");

// gamesnd_play_mtx -- 0x446A20 (268 B). Two register parameters and two stack
// ones, as CImage::PlaySnd calls it. a2 and a4 are the literals 1 and 0 there,
// so what they mean is not known.
void __fastcall gamesnd_play_mtx(int nSound, int a2, snd_pos_t pos, int a4);

// gamesnd_screendist_pan -- 0x446360 (117 B). The pan for a signed screen X;
// CUnit::GetPan tail-jumps into it.
int __fastcall gamesnd_screendist_pan(int nScreenX);

// 0x446B70 gamesnd_research_complete (178 B) and 0x447060
// gamesnd_upgrade_finished (181 B), the completion cues, each taking the unit.
class CUnit;
void __fastcall gamesnd_research_complete(CUnit *pUnit);
void __fastcall gamesnd_upgrade_finished(CUnit *pUnit);

// The rest of gamesnd.cpp. Some of this TU's names are declared in its callers'
// headers (TrigAct.h, CUnitTerran.h, CUnitBuild.h, CUnitHarvest.h,
// CUnitCombat.h, CUnitTSpells.h, order.h) and are not restated here.

// rect16_t is reached only through a pointer, so a forward declaration does.
struct rect16_t;

// Callees in other TUs.

// 0x49DB30 snd_get_duration (10 B), of a sound id.
int __fastcall snd_get_duration(int nSoundId);

// 0x49E780 sub_49E780 (64 B). It never reads ECX, but every caller loads a sound
// id there, so the id stays the first parameter.
int __fastcall sub_49E780(int nSoundId, int nVolume, int nPan, CUnit *pUnit);

// 0x4A7200 statport_talking_portrait (241 B): the unit, a deadline in
// milliseconds, and three stack words, 1, 0x11 and the sound id.
void __fastcall statport_talking_portrait(CUnit *pUnit, int nDeadlineMs, int a3, int a4, int nSoundId);

// 0x442B60 sub_442B60 is declared in CUnitTerran.h, which gamesnd.cpp includes;
// a second spelling here would be a second overload.

// 0x49E3F0 sub_49E3F0 (112 B), void, since gamesnd_reset tail-jumps into it.
void __fastcall sub_49E3F0(intptr_t nArg0);

// gamesnd.cpp's own bodies.

// 0x4460D0 (73 B), taking the unit.
void __fastcall gamesnd_update_attacknotify_timers(CUnit *pUnit);

// 0x4463E0 (100 B): the volume for a map point, both coordinates signed.
int __fastcall gamesnd_screendist_volume(int nMapX, int nMapY);

// 0x446570 (106 B) / 0x4465E0 (106 B), each taking the unit. 0x446650,
// gamesnd_create_man, is declared in CUnitBuild.h.
void __fastcall gamesnd_placebox_error(CUnit *pUnit);
void __fastcall gamesnd_land_error(CUnit *pUnit);

// 0x446450 (132 B) / 0x4464E0 (132 B): the unit and a status string id.
// gamesnd_placebox_error_msg's id is a WORD because its caller, sub_47D290,
// passes a 16-bit register unextended.
void __fastcall gamesnd_placebox_error_msg(CUnit *pUnit, WORD wStringId);
void __fastcall gamesnd_build_error(CUnit *pUnit, int nStringId);

// 0x446120 (205 B) / 0x446280 (219 B), each taking the unit. void: EAX is set
// on some paths only, and no caller reads it.
void __fastcall gamesnd_ack(CUnit *pUnit);
void __fastcall gamesnd_build_complete(CUnit *pUnit);

// 0x446E90 (98 B). The unit type is a WORD because the one caller, sub_47D290,
// passes a 16-bit register unextended.
void __fastcall gamesnd_placebox(WORD wUnitType);

// 0x446FF0 (107 B).
void __fastcall gamesnd_update(void);

// 0x446C30 (593 B), taking the unit.
void __fastcall gamesnd_select(CUnit *pUnit);

// 0x446800 (301 B). The argument is 8 bits wide; void, since it tail-jumps into
// sub_49E3F0.
void __fastcall gamesnd_reset(BYTE bArg0);

// 0x447130 (91 B) / 0x447190 (134 B), the two random-cue pickers. The first
// argument is a unit type; pick_rnd_snd_select's second is a count.
int __fastcall pick_rnd_snd_ack(int nUnitType);
int __fastcall pick_rnd_snd_select(int nUnitType, int nCount);

// Callees SAI_Main.cpp reaches that no other header declares.

// gamesnd_rescue -- 0x446B30, 50 B, called by aiscript_switch_rescue with the
// unit.
void gamesnd_rescue(CUnit *pUnit);

// 0x446740 (24 B). `snd_get_duration(id) / 1000 * 1000`, the expression every
// other cue in this TU writes inline; gamesnd_select is its only real caller.
int gamesnd_duration(int nSoundId);

// 0x447120 (10 B), the callback gamesnd_update_attacknotify_timers hands to
// sub_442B60, of CUnitTerran.h's CUnitTerranFindCb type.
int gamesnd_set_attacknotify(CUnit *pUnit, CUnit *pContext);

