// port_standalone.h -- 1.17's data as the engine's own (port_standalone.cpp): the
// globals' starting values (gen/data117.cpp), the
// start-up, the resources, and the code map (gen/code_map.cpp).
#ifndef PORT_STANDALONE_H
#define PORT_STANDALONE_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// 1.17's data as the engine's own block, with none of 1.17's code in the process
// (port_standalone.cpp): the block filled from 1.17's starting
// values, which Hyperion.exe calls as it starts, with the count of pointers made the
// engine's functions and of those with none; what the original's start-up did for the
// game, which WinMain calls first; and the game itself, the game's WinMain with the
// command line after the program's name.
bool __cdecl PortGameDataInit(ULONG *pOurs, ULONG *pTrapped);
void __cdecl PortStartUp(void);
int __cdecl PortRunGame(void);

// Where the engine keeps the global 1.17 kept at va: in the block, or in the storage
// of its own a global with a pointer in it has; and the
// reverse, the 1.17 address an engine address stands for, 0 for none.
UINT_PTR __cdecl PortDataAddress(ULONG va);
ULONG __cdecl PortOriginalAddress(UINT_PTR p);
// The field of 1.17's layout that holds the byte at va, for a reader of 1.17's memory by its
// addresses (EUD maps, port_eud.cpp): its first address in 1.17, where the engine keeps
// it, its kind (gen/wide117.h's WIDE_*) and its bytes in 1.17. A global in the block is a byte
// copied; false for padding.
struct PortField {
  ULONG va;
  UINT_PTR p;
  int kind;
  ULONG len;
};
bool __cdecl PortDataField(ULONG va, PortField *pf);
// Whether va to va + size is in the block, no global of storage of its own in it.
bool __cdecl PortDataPlain(ULONG va, ULONG size);
// The two spans PortOriginalAddress names an address in, each its first address and one past
// its last: the state hash's quick test of whether a value can be a pointer it names.
void __cdecl PortOriginalSpans(UINT_PTR aSpans[2][2]);
// Stores bytes of data in 1.17's layout, as a file holds it, at p: into a global with
// storage of its own element by element in its layout, a pointer field taking the id
// the file has there, zero-extended; anywhere else as it is (load_array_data's DAT
// columns).
void __cdecl PortDataStore(void *p, const void *from, ULONG bytes);
// The bytes the engine keeps at p for bytes86 of 1.17's layout: more for a global whose
// element is wider on x64 (a column of ids h/typelimits.h widens), else bytes86.
ULONG __cdecl PortDataBytes(const void *p, ULONG bytes86);
// Row row of the column whose first row is at p, rowBytes a row in 1.17's layout: its
// address in the engine's layout, for PortDataStore; NULL past the storage's rows. A mod's
// rows past 1.17's go in this way (port_moddata.cpp).
void *__cdecl PortDataRow(void *p, ULONG rowBytes, ULONG row);
// Whether p is in storage of the engine's own, where a grown column's rows past 1.17's are
// (tools/wide_globals.py, GROWN); a column left in 1.17's block has 1.17's rows only.
bool __cdecl PortDataGrown(const void *p);
// The state of the C runtime's rand, which the engine defines itself with the runtime's
// numbers (port_standalone.cpp), so a save of a Hyperion game can keep it.
unsigned int *__cdecl PortCrtRandState(void);
// Local.dll, for its resources (gds_local.cpp): in the standalone mode as a data
// file, so none of its code runs and the x64 build reads the 32-bit DLL.
struct HINSTANCE__ *__cdecl PortLoadResources(const char *pszPath);
// A resource of a module PortLoadResources gave, by its number and its type's
// (RT_ACCELERATOR is 9), and its size; NULL if it has none.
const void *PortModuleResource(void *hModule, int nResId, int nType, DWORD *pcbSize);
// 1.17's code addresses that its data holds, and the engine's function for each
// (src117/gen/code_map.cpp, tools/code_map.py).
struct PortCodeRow {
  ULONG va;
  void *fn;
};
const PortCodeRow *__cdecl PortCodeMap(ULONG *pCount);

// 1.17's starting values, as Hyperion's source (src117/gen/data117.cpp,
// generated from the original exe): the pieces at their 1.17 addresses,
// the rest of the block zero; the dwords among them that hold an address; the places kept and
// the part of each section 1.17's file held, for the poison check; the extent of 1.17's code.
struct PortDataPiece {
  ULONG va, len;
  const unsigned char *bytes;
};
struct PortDataSpan {
  ULONG va, end;
};
const PortDataPiece *__cdecl PortData117(ULONG *pCount);
const ULONG *__cdecl PortData117Pointers(ULONG *pCount);
const PortDataSpan *__cdecl PortData117Kept(ULONG *pCount);
const PortDataSpan *__cdecl PortData117File(ULONG *pCount);
PortDataSpan __cdecl PortData117Code(void);
// The two resources the game took from 1.17's exe, from the same file: VCOD #202, which
// every map's VCOD section is checked against (maphdr_VCOD), and the accelerator table
// 0x71, the multiplayer chat's keys, which Local.dll lacks (RT_ACCELERATOR's bytes); NULL
// for another table.
const void *__cdecl PortGameVcod(ULONG *pcbSize);
const void *__cdecl PortGameAccelerators(int nResId, ULONG *pcbSize);

#endif
