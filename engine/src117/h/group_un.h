// group_un.h -- declarations for src117/lang/group_un.cpp: unit control groups,
// their recall and modify commands and the number-key handler

#ifndef OC117_GROUP_UN_H
#define OC117_GROUP_UN_H

#include "platform.h"

// Five of this TU's rows are declared elsewhere and not repeated here:
// group_un_init in game.h, game_key_down_num in game_evt.h, and group_recall,
// sub_464360 and group_modify in net_cmds.h.

// 0x463D60. Clears one control group, compacting the surviving ids down. A zero
// id ends the walk rather than skipping that slot.
void CenterOnSelectionGroup(BYTE nGroup);

// 0x463ED0. Arms the group the digit names and then selects it. No `ret` of its
// own: it tail-jumps game_key_down_num, which pins the return type. The digit
// is a BYTE both ways -- the body saves and restores it with `mov bl, cl` /
// `mov cl, bl`, never the full register.
void sub_463ED0(BYTE nDigit);

// 0x464030. Selects the most recently stamped control group that holds the unit
// and returns whether one was found. The parameter is 32-bit; spelled WORD the
// body is 476 bytes against the oracle's 463.
int sub_464030(int nUnitId);

#endif  // OC117_GROUP_UN_H
