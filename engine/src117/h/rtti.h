// rtti.h -- the part of MSVC's type_info the game's allocation tags use, in
// place of <typeinfo>. 1.17 tags each Storm allocation with a type's raw name:
// it calls type_info::raw_name (0x4D1690) on that type's RTTI descriptor and
// passes the result to SMemAlloc or SMemFree. The descriptors are 1.17's own,
// globals at their addresses (gTypeInfo_TTEMPLATE at 0x5107BC in gametype.cpp,
// for one), not the compiler's typeid. A descriptor is the type_info vftable,
// a zero dword, then the raw name (".?AUTTEMPLATE@@"); the port defines
// raw_name in port_names.cpp.
//
// Do not include <typeinfo> in a TU that includes this header: two
// declarations of the class are a redefinition.
#ifndef OC117_RTTI_H
#define OC117_RTTI_H

// A declaration of 1.17's class, not a definition: nothing in the game's C++
// constructs a type_info, each is reached at its address in 1.17's data.
// raw_name is not const, unlike MSVC's own: the thunk generator cannot
// spell a const member.
class type_info {
 public:
  const char *raw_name();

 private:
  // Declared and never defined, so nothing copies or constructs one by accident.
  type_info(const type_info &);
  type_info &operator=(const type_info &);
};

#endif  // OC117_RTTI_H
