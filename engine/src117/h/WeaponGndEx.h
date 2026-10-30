#ifndef OC117_WEAPONGNDEX_H
#define OC117_WEAPONGNDEX_H

#include "platform.h"
#include "CUnit.h"

// 0x41FC20. The ground weapon, with the unburrowed Lurker's special case. The
// body is inline here because CImage.cpp's RunScript compiles against it: the
// call then reads no global the caller holds, so sgpScripts stays in a register
// across it. The home TU is CUnitCombat.cpp; only it and CImage.cpp include this
// header, after globals117.h, which declares gubUnitWeaponGnd.
OC_EMIT inline weapon_id_t CUnit::GetWeaponGndEx()
{
  if (wType == 0x67 && !(dwFlags & 0x10))
    return WEAPON_NONE;  // Hyperion: -0x7E in 1.17's char
  return gubUnitWeaponGnd[wType];
}

#endif  // OC117_WEAPONGNDEX_H
