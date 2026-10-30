// CUnitUpgrade.h -- the free functions of src117/lang/CUnitUpgrade.cpp, the
// research and upgrade block: the three upgrades.dat cost queries, the two
// refunds, and the unit-type predicates CUnit::sub_43B0B0 walks a player's
// units with. The TU's CUnit members (start, cancel and the per-tick dispatch
// of research and upgrades) are declared in CUnit.h. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_CUNITUPGRADE_H
#define OC117_CUNITUPGRADE_H

#include "platform.h"
#include "typelimits.h"  // Hyperion: upgrade_id_t

class CUnit;

// 0x43B270 / 0x43B2A0 / 0x43B350. One shape over three table pairs:
// cost = base[bUpgradeId] + factor[bUpgradeId] * get_upgrade_level(...), with
// ore at 0x5248D0 / 0x524950, gas at 0x5249D0 / 0x524A50 and time at
// 0x524AD0 / 0x524B50. The player in CL and the upgrade id in DL are bytes. The
// return is a WORD because the callers narrow it to 16 bits; the body's own
// arithmetic is 32-bit.
WORD get_upgrade_ore_cost(BYTE bPlayer, upgrade_id_t bUpgradeId);
WORD get_upgrade_gas_cost(BYTE bPlayer, upgrade_id_t bUpgradeId);
WORD get_upgrade_time(BYTE bPlayer, upgrade_id_t bUpgradeId);

// 0x43B210 and 0x43B2D0. The full refund credits the whole ore and gas cost
// back to the player; the partial one three quarters of each, as a signed
// x * 3 / 4 (rounded toward zero). The same byte arguments as the cost queries.
void refund_upgrade_full(BYTE bPlayer, upgrade_id_t bUpgradeId);
void refund_upgrade_partial(BYTE bPlayer, upgrade_id_t bUpgradeId);

// 0x43B060, 0x43B070, 0x43B090. One signature for all three: CUnit::sub_43B0B0
// calls them through a pointer with the unit in ECX and a type in EDX, and the
// vulture and ultralisk tests ignore EDX.
BOOL unit_is_type_cb(CUnit *pUnit, int nType);
BOOL unit_is_vulture_cb(CUnit *pUnit, int nType);
BOOL unit_is_ultralisk_cb(CUnit *pUnit, int nType);

#endif  // OC117_CUNITUPGRADE_H
