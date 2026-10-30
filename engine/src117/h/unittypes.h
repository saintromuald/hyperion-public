// unittypes.h -- the units.dat type ids the game's C++ uses. Add a unit type id
// here, with the function it comes from, rather than in the file that needs it.
//
// The values are 1.17's; the names, and which id of a pair is the plain unit and
// which the hero, follow the public Brood War numbering, which the code does not
// prove. The enum has no type name, so a fuller units.dat enum cannot collide
// with it; name it only when something needs the type.
#pragma once

enum {
  // CUnit::IsGoliathType (0x40FD10) accepts these two. The Goliath turret types
  // are not in its set and are not here.
  UNI_T_GOLIATH = 0x03,   // Terran Goliath (body)
  UNI_T_SCHEZAR = 0x11,   // Alan Schezar, the hero Goliath (body)

  // CUnit::IsQueenType (0x41FF30) accepts these two, and CUnit::HoldOrder
  // (0x41FAE0) gives both the same hold order, 0x6C.
  UNI_T_QUEEN     = 0x2D, // Zerg Queen
  UNI_T_MATRIARCH = 0x31, // Matriarch, the hero Queen
};
