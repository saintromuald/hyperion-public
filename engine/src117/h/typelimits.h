// typelimits.h -- the type tables' sizes: 1.17's, and the room a mod's new types take.
//
// 1.17's tables hold its types and no more: 228 units, 130 weapons, 209 flingy, 517
// sprites, 999 images, 189 orders, 44 techs, 61 upgrades. A mod's new types go past them,
// never over a 1.17 row, so a game without the mod cannot reach one. The 64-bit build
// sizes the tables' storage for the most below (tools/wide_globals.py, GROWN), and 1.17's
// file and the data files still fill 1.17's rows; the 32-bit build keeps 1.17's sizes.
// A unit type travels in a WORD, and 228 to 232 mean "none", "any unit" and the other
// groups, so a mod's units start at 256. The others travel in a BYTE in 1.17 (units.dat's
// weapon and flingy columns, CUnit::bOrderId and bTechType), which the 64-bit build widens
// to 16 bits (below), so a mod's follow 1.17's rows past the row that means none: weapon
// 130, order 189, tech 44, upgrade 61.
#ifndef TYPELIMITS_H
#define TYPELIMITS_H
#include "platform.h"  // BYTE and WORD

// Hyperion: the ids 1.17 keeps in a byte, which a mod's types pass:
// 16 bits on 64, 1.17's BYTE on 32. A 1.17 game's ids fit in a byte on either, and the
// save's and EUD's view of 1.17's records keep the byte (tools/wide_globals.py and
// tools/save_layouts.py, unsigned8).
#if defined(_M_IX86)
typedef BYTE flingy_id_t;
typedef BYTE weapon_id_t;
typedef BYTE order_id_t;
typedef BYTE tech_id_t;
typedef BYTE upgrade_id_t;
#else
typedef WORD flingy_id_t;
typedef WORD weapon_id_t;
typedef WORD order_id_t;
typedef WORD tech_id_t;
typedef WORD upgrade_id_t;
#endif

// Hyperion: an offset into the image scripts (lang/CImage.cpp, sgpScripts), a WORD in
// 1.17's records; 32 bits on 64, as a mod's scripts go on past 64 KiB.
#if defined(_M_IX86)
typedef WORD iscript_off_t;
#else
typedef DWORD iscript_off_t;
#endif

#define UNIT_TYPES_117 228
#define WEAPON_TYPES_117 130
#define FLINGY_TYPES_117 209
#define SPRITE_TYPES_117 517
#define IMAGE_TYPES_117 999
#define ORDER_TYPES_117 189
#define TECH_TYPES_117 44
#define UPGRADE_TYPES_117 61

// The sprites from 130 on have a health bar and a selection circle (sprites.dat's columns
// of 387 rows).
#define SPRITE_SELECTABLE_FIRST 130

// "No weapon" in units.dat's and orders.dat's weapon columns, which 1.17 tests as a signed
// byte (-0x7E); a mod's weapons start past it.
#define WEAPON_NONE WEAPON_TYPES_117
// "No order", in orders.dat's obscured column and where 1.17 tests 0xBD; a mod's orders
// start past it.
#define ORDER_NONE ORDER_TYPES_117
// "No tech" and "no upgrade", a building's in-progress slots when it researches nothing
// (CUnit::bTechType, bUpgradeType); a mod's start past them.
#define TECH_NONE TECH_TYPES_117
#define UPGRADE_NONE UPGRADE_TYPES_117

#define UNIT_TYPE_MOD_FIRST 256
#define WEAPON_TYPE_MOD_FIRST 131
#define FLINGY_TYPE_MOD_FIRST FLINGY_TYPES_117
#define SPRITE_TYPE_MOD_FIRST SPRITE_TYPES_117
#define IMAGE_TYPE_MOD_FIRST IMAGE_TYPES_117
#define ORDER_TYPE_MOD_FIRST 190
#define TECH_TYPE_MOD_FIRST 45
#define UPGRADE_TYPE_MOD_FIRST 62

// The most of each a game holds, 1.17's rows included.
#define UNIT_TYPES_MAX 4096
#define WEAPON_TYPES_MAX 2048
#define FLINGY_TYPES_MAX 2048
#define SPRITE_TYPES_MAX 8192
#define IMAGE_TYPES_MAX 16384
#define ORDER_TYPES_MAX 1024
#define TECH_TYPES_MAX 1024
#define UPGRADE_TYPES_MAX 1024
#define SPRITE_SELECTABLE_MAX (SPRITE_TYPES_MAX - SPRITE_SELECTABLE_FIRST)

// Hyperion: the sounds (sfxdata.dat) and portraits (portdata.dat), a mod's after 1.17's
//. What is heard and seen only: no game's state holds one, so a mod's are
// there from start-up, whatever the game, and in no fingerprint. Sound 0 is "none".
#define SOUND_TYPES_117 1144
#define PORTRAIT_TYPES_117 110
#define SOUND_TYPE_MOD_FIRST SOUND_TYPES_117
#define PORTRAIT_TYPE_MOD_FIRST PORTRAIT_TYPES_117
#define SOUND_TYPES_MAX 16384
#define PORTRAIT_TYPES_MAX 4096

// The sounds and portraits loaded: 1.17's, then the mod's (port_moddata.cpp).
extern int g_hxSoundTypes;
extern int g_hxPortraitTypes;
inline bool SoundTypeValid(unsigned t) { return t < static_cast<unsigned>(g_hxSoundTypes); }
inline bool PortraitTypeValid(unsigned t) { return t < static_cast<unsigned>(g_hxPortraitTypes); }

// The types the game being played has: 1.17's, and from each table's _MOD_FIRST row up to
// its count the mod's, when the game has a mod with data (port_rules.cpp). A count at
// 1.17's, or for units at UNIT_TYPE_MOD_FIRST or below, means no types of a mod's. Images,
// sprites and their scripts are a mod's from start-up; the rest from the game's start.
struct HxTypeCounts {
  int units, weapons, flingy, sprites, images, orders, techs, upgrades;
};
extern HxTypeCounts g_hxTypes;

// Whether t is one of a mod's types the game has.
inline bool UnitTypeMod(unsigned t) {
  return t >= UNIT_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.units);
}
inline bool WeaponTypeMod(unsigned t) {
  return t >= WEAPON_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.weapons);
}
inline bool OrderTypeMod(unsigned t) {
  return t >= ORDER_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.orders);
}
inline bool ImageTypeMod(unsigned t) {
  return t >= IMAGE_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.images);
}
inline bool TechTypeMod(unsigned t) {
  return t >= TECH_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.techs);
}
inline bool UpgradeTypeMod(unsigned t) {
  return t >= UPGRADE_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.upgrades);
}

// Whether t is a type the game has, in place of 1.17's tests against its counts. The
// markers keep their meaning: unit 228 "none", 229 to 232 the groups; weapon 130 "none";
// order 189, tech 44 and upgrade 61 "none".
inline bool UnitTypeValid(unsigned t) { return t < UNIT_TYPES_117 || UnitTypeMod(t); }
inline bool WeaponTypeValid(unsigned t) { return t < WEAPON_TYPES_117 || WeaponTypeMod(t); }
inline bool OrderTypeValid(unsigned t) { return t < ORDER_TYPES_117 || OrderTypeMod(t); }
inline bool ImageTypeValid(unsigned t) { return t < IMAGE_TYPES_117 || ImageTypeMod(t); }
inline bool TechTypeValid(unsigned t) { return t < TECH_TYPES_117 || TechTypeMod(t); }
inline bool UpgradeTypeValid(unsigned t) { return t < UPGRADE_TYPES_117 || UpgradeTypeMod(t); }
inline bool FlingyTypeValid(unsigned t) {
  return t < FLINGY_TYPES_117 || (t >= FLINGY_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.flingy));
}
inline bool SpriteTypeValid(unsigned t) {
  return t < SPRITE_TYPES_117 || (t >= SPRITE_TYPE_MOD_FIRST && t < static_cast<unsigned>(g_hxTypes.sprites));
}

// The mod's unit types in the game, from UNIT_TYPE_MOD_FIRST; none in any other game.
inline int UnitTypesMod() {
  return g_hxTypes.units > UNIT_TYPE_MOD_FIRST ? g_hxTypes.units - UNIT_TYPE_MOD_FIRST : 0;
}

// The 1.17 unit a mod's unit takes its looks from in 1.17's tables that are not its DAT
// files' (the status panel's functions, the wireframes, the command icons): its record's
// "looks", else its base (port_moddata.cpp). Any other type is its own.
unsigned ModUnitLooks(unsigned t);
inline unsigned UnitTypeLooks(unsigned t) { return UnitTypeMod(t) ? ModUnitLooks(t) : t; }

// A unit a build queue trains (1.17's units up to 105, the buildings past them), or a mod's
// unit that is no building (port_moddata.cpp).
bool ModUnitTrainable(unsigned t);
inline bool UnitTypeQueueable(unsigned t) { return t <= 0x69 || (UnitTypeMod(t) && ModUnitTrainable(t)); }

// The unit type after t, for a loop over the game's types in place of 1.17's to 228:
//   for (unsigned t = 0; UnitTypeValid(t); t = UnitTypeNext(t))
// 1.17's, then the mod's from UNIT_TYPE_MOD_FIRST; a game without them stops at 228.
inline unsigned UnitTypeNext(unsigned t) {
  return t + 1 == UNIT_TYPES_117 && UnitTypesMod() != 0 ? UNIT_TYPE_MOD_FIRST : t + 1;
}

#endif
