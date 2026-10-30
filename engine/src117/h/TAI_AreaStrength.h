// TAI_AreaStrength.h -- declarations for src117/lang/TAI_AreaStrength.cpp, the
// town AI's area threat model: the per-unit-type air and ground threat base
// table, the per-unit strength and threat scoring that feeds it, and the
// greatest-threat queries the captain and the town builder ask of a region.
//
// Three of the TU's rows are declared elsewhere: tai_compute_unit_threat_scores
// in src117/h/SAI_Main.h, and the CUnit::AirStrength and CUnit::GroundStrength
// members in src117/h/CUnit.h.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.

#ifndef OC117_TAI_AREASTRENGTH_H
#define OC117_TAI_AREASTRENGTH_H

#include "platform.h"
#include "typelimits.h"  // weapon_id_t

class CUnit;

// Every `nAirGround` below is 0 for air and 1 for ground, not a player index:
// tai_get_total_air_threat_region forwards `push 0h` and its ground twin
// `push 1h`, tai_get_air_threat_base reads sgnThreatBase_g[nUnitType] while
// tai_get_ground_threat_base reads sgnThreatBase_g[228 + nUnitType], and
// tai_assign_unit_strength stores the EDX=0 result to CUnit +0x138, which
// CUnit::AirStrength reads, and the EDX=1 result to +0x13A.

// 0x4A97E0 / 0x4A97F0 (8 B each). ECX is a unit-type index; row 0 and row 1 of
// sgnThreatBase_g, 0x390 = 228*4 apart.
int tai_get_air_threat_base(int nUnitType);
int tai_get_ground_threat_base(int nUnitType);

// 0x4A9760 / 0x4A9860 (114 B each). Return the unit found, not an int.
CUnit *tai_get_greatest_air_threat_near(CUnit *pUnit);
CUnit *tai_get_greatest_ground_threat_near(CUnit *pUnit);

// 0x4A8F60 (54 B). ECX is a unit-type index biased by -7 into a jump table, not
// a pointer; EDX is a value, not a pointer -- one arm is `shr edx, 2h`.
DWORD tai_compute_splash_damage_threat(int nUnitType, DWORD dwThreat);

// 0x4A9000 (195 B). ECX a unit-type index, EDX read as 8 bits (`mov bl, dl`).
// The return is unsigned: the body converts with `_dtoui3`, the double to
// unsigned routine, where a signed result would use `_ftol`. It is the first
// floating-point body in src117 and reaches two CRT helpers the hook generator
// does not treat as stateless, so the row is not hooked (see the .cpp).
DWORD tai_compute_hit_point_threat(int nUnitType, weapon_id_t bWeapon);

// 0x4A9460 (181 B). ECX is a real CUnit *: `test dword ptr [esi+0DCh], 20000h`
// after `mov esi, ecx`, a field read at a fixed offset, not a scaled index.
int tai_compute_threat_priority(CUnit *pUnit, int nAirGround);

// 0x4A9630 (115 B). Three consuming callers use the full EAX and none narrows
// it.
int tai_get_threat_score(CUnit *pUnit, int nAirGround);

// 0x4A96B0 (170 B). DWORD, not WORD: the body ends `mov eax, ebx / div ecx /
// ret`, leaving a full 32-bit quotient, and the copy inlined into
// tai_sum_threat_scores_region compares it at full width. The two 16-bit caller
// uses are tai_assign_unit_strength's stores into the WORD fields at +0x138 and
// +0x13A, which truncate under either return type.
DWORD tai_compute_unit_strength(CUnit *pUnit, int nAirGround);

// 0x4A8F30 (38 B). `mov ecx, <unit>` and nothing pushed; EAX never read.
void tai_assign_unit_strength(CUnit *pUnit);

// 0x4A9800 / 0x4A9820 (29 B each). Return the unit, not an int.
CUnit *tai_get_greatest_air_threat_region(int nPlayer, int nRegion);
CUnit *tai_get_greatest_ground_threat_region(int nPlayer, int nRegion);

// 0x4A9840 / 0x4A9850 (8 B each). ECX and EDX are forwarded to a callee
// untouched, so the body proves no width; the single consuming caller narrows
// EAX to 16 bits, so WORD.
WORD tai_get_total_ground_threat_region(int nPlayer, int nRegion);
WORD tai_get_total_air_threat_region(int nPlayer, int nRegion);

// 0x4A9330 (293 B), `ret 4h`. DWORD, not void: its two callers tail-forward EAX
// rather than consuming it.
DWORD tai_sum_threat_scores_region(int nPlayer, int nRegion, int nAirGround);

// 0x4A9520 (147 B), `ret 4h`. ECX is a real CUnit* (`movzx eax, word ptr
// [ecx+64h]` -- wType at a fixed offset).
DWORD tai_get_special_threat_scores(CUnit *pUnit, int nAirGround, DWORD nScore);

// 0x4A90D0 (598 B), `ret Ch`. CUnit *, not void: all four wrappers tail-forward
// EAX.
CUnit *tai_get_greatest_threat(int nPlayer, CUnit *pUnit, int nRegion,
                               CUnit **ppUnits, int nAirGround);

#endif  // OC117_TAI_AREASTRENGTH_H
