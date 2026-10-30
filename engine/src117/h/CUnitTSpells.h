// CUnitTSpells.h -- declarations for src117/lang/CUnitTSpells.cpp, the terran
// unit spells. Member functions are in CUnit.h; this header has the free
// callback and helper declarations.

#ifndef OC117_CUNITTSPELLS_H
#define OC117_CUNITTSPELLS_H

#include "platform.h"

class CUnit;

typedef int (__fastcall *CUnitTSpellsFindProc)(CUnit *pCandidate,
                                               CUnit *pContext);

int __fastcall IrradiateProc(CUnit *pCandidate, CUnit *pIrradiated);
int __fastcall gamesnd_play(int nSound, CUnit *pUnit, int a3, int a4);
int __fastcall net_randbetween(int nMin, int nMax, int nPlayer);
void __fastcall status_set_update();

#endif  // OC117_CUNITTSPELLS_H
