// forces.cpp -- the forces model: the four-force team assignment, the per-force
// flag byte array and force names, the emptiest-force slot chooser, and the
// force-wide shared-vision setter. Bodies are in oracle VA order.
//
// The TU is inline-dense in both directions. GetPlayerForce is spliced into
// GetEmptiestForce's counter helper and into GetPlayerForceName, GetForceName
// into GetPlayerForceName, SetForceName into LoadForceSettings, and
// GetForceOpenSlot into GetFreeSlotFromEmptiestForce; all four are extern and
// declared in a header, so each keeps its own body too. GetEmptiestForce must
// not be spliced into GetFreeSlotFromEmptiestForce, which the oracle calls, and
// at 118 B it is far over the /Ob2 budget, so it needs no lever.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <string.h>                  // memcpy -- LoadForceSettings's 8-byte blob copy

#include "../h/forces.h"
#include "../h/net_slot.h"     // IsHumanPlayerType, IsLegalChatroomSlotVal
#include "../h/net_cmds.h"     // PlayerData (globals117.h needs it), error_get_str
#include "../h/TrigAct.h"      // get_map_string
#include "../h/TrigEnum.h"     // GetPlayerForce -- declared THERE, not here
#include "../gen/globals117.h"

// SStrCopy -- Storm ordinal 501, restated rather than included, spelled to
// match src117/h/Storm.h exactly (char * / const char * / size_t, inside
// extern "C", STORMAPI == __stdcall): Storm.h needs <windows.h> and this TU is
// include-path clean. A differently typed restatement would be a second mangled
// name for one import.
extern "C" char *__stdcall SStrCopy(char *pszDest, const char *pszSrc,
                                    size_t cchDest);

// The globals this TU reaches, dual-spelled, one #ifndef per name. The three
// unregistered ones are contiguous and end where the registered sgbAlliances_g
// @0x534914 begins, which is what fixes their extents:
//
//   0x534890  BYTE[8]       per-player force
//   0x534898  BYTE[4]       per-force flag bytes
//   0x53489C  char[4][30]   per-force names     0x53489C + 4*30 == 0x534914
//
// Those three spellings are TU-local.

// 0x534890 -- the per-player force byte, one per game slot. LoadForceSettings
// fills it with an 8-byte copy and then walks 0x534897 down to 0x534890 with
// `inc byte ptr [eax+53488Fh]`; ApplyPlayerForces indexes it `[edi+534890h]`
// for edi 0..7. The stored value is 1-based: the `inc` pass turns the blob's
// 0-based force into the 1..4 the accessors range-check.
#ifndef OC_FORCES_sgbPlayerForce_s
#define OC_FORCES_sgbPlayerForce_s
#ifdef OC_DLL_MODE
#define sgbPlayerForce_s (reinterpret_cast<BYTE *>(OC_VA(0x534890u)))
#else
extern BYTE sgbPlayerForce_s[8];
#endif
#endif

// 0x534897 -- the force flag bytes, spelled at the biased base. The true base
// is 0x534898, which LoadForceSettings's last loop states literally
// (`mov byte ptr [ecx+534898h], al` for ecx = 3,2,1,0); every accessor instead
// reaches them as `[force + 534897h]` with force in 1..4, the same array biased
// by -1 because the force index is 1-based. Bit 0 is random start locations,
// bits 1 and 2 are GetForceAllianceMode's two tests, bit 3 is shared vision.
#ifndef OC_FORCES_sgbForceFlags_s
#define OC_FORCES_sgbForceFlags_s
#ifdef OC_DLL_MODE
#define sgbForceFlags_s (reinterpret_cast<BYTE *>(OC_VA(0x534897u)))
#else
// [5], not [4]: the accessors index this biased base with bForce in 1..4, so
// [4] would be one past the end.
extern BYTE sgbForceFlags_s[5];
#endif
#endif

// 0x534898 -- the same four bytes at their true base, which LoadForceSettings
// needs rather than a second array: written through the biased name the store
// is `mov [eax+534897h], cl` with the index still 4..1, while the oracle has
// the decremented index in hand first (`mov al, [edx+ecx*1+0Fh] / dec ecx /
// mov byte ptr [ecx+534898h], al`). The two spellings are not interchangeable
// in the emitted code. The name is the registered one, as net_chrm.cpp spells
// it.
#ifndef OC_FORCES_byte_534898
#define OC_FORCES_byte_534898
#ifdef OC_DLL_MODE
#define byte_534898 (reinterpret_cast<BYTE *>(OC_VA(0x534898u)))
#else
extern BYTE byte_534898[4];
#endif
#endif

// 0x53489C -- the four force names, 0x1E bytes each. GetForceName computes
// force*30 + 0x53487E == 0x53489C + (force-1)*30, and LoadForceSettings walks
// the table down with `sub esi, 1Eh` until `cmp esi, 53489Ch`. 0x1E is also the
// capacity every SStrCopy in the family passes.
#ifndef OC_FORCES_sgForceNames_s
#define OC_FORCES_sgForceNames_s
#ifdef OC_DLL_MODE
#define sgForceNames_s (reinterpret_cast<char (*)[30]>(OC_VA(0x53489Cu)))
#else
extern char sgForceNames_s[4][30] OC_UNALIGNED;
#endif
#endif

// 0x673D48 -- the chat-room slot records, as distinct from the game slots
// (gPlayerData @0x6726C8). Registered as gNetPlayerData_g with type BYTE[0x1B0],
// which globals117.h does not emit, so it is spelled here as
// src117/lang/net_slot.cpp spells it: PlayerData[12], since 0x1B0 / 36 == 12.
// ApplyPlayerForces reads `[esi+673D50h]` (nType) and writes `[esi+673D52h]`
// (nTeam) at a 36-byte stride.
#ifndef OC_FORCES_gNetPlayerData_g
#define OC_FORCES_gNetPlayerData_g
#ifdef OC_DLL_MODE
#define gNetPlayerData_g (reinterpret_cast<PlayerData *>(OC_VA(0x673D48u)))
#else
extern PlayerTable<PlayerData, PlayerTagNetData> gNetPlayerData_g;
#endif
#endif

// force_settings_t -- LoadForceSettings's argument, TU-local because nothing in
// src117/h takes one. 0x14 bytes, every field from the use that body makes of
// it.
struct force_settings_t {
  // `mov eax,[ecx] / mov [534890h],eax / mov eax,[ecx+4h] / mov [534894h],eax`
  // -- an 8-byte copy into the per-player force array, then an `inc` on each.
  /*0x00*/ BYTE bPlayerForce[8];
  // `mov cx, word ptr [ebx-2h]` with ebx walking down from +0x10, four times,
  // each feeding get_map_string (whose parameter is a WORD).
  /*0x08*/ WORD wNameStrId[4];
  // `mov al, byte ptr [edx+ecx+0Fh]` for ecx = 4,3,2,1 -- i.e. +0x13..+0x10 --
  // stored to `[ecx+534898h]` after the `dec`, so bFlags[k] -> flags[k].
  /*0x10*/ BYTE bFlags[4];
};
static_assert(offsetof(force_settings_t, bPlayerForce) == 0x00,
              "bPlayerForce: mov eax, dword ptr [ecx]");
static_assert(offsetof(force_settings_t, wNameStrId) == 0x08,
              "wNameStrId: lea ebx,[ecx+10h] then mov cx, word ptr [ebx-2h] x4");
static_assert(offsetof(force_settings_t, bFlags) == 0x10,
              "bFlags: mov al, byte ptr [edx+ecx+0Fh] for ecx = 4..1");
static_assert(sizeof(force_settings_t) == 0x14,
              "force_settings_t: +0x10 plus four flag bytes, no trailing field");

// 0x443180 (153 B) -- set the alliance byte between every pair of human players
// that share force bForce. sgbAlliances_g is registered BYTE[12][12] and the
// 12-byte row stride is the oracle's own `add edi, 0Ch`. The short-circuit
// order is bValue first: the oracle emits `test bl,bl / jnz` before the i == j
// test, and the two tests the other way round transpose.
void SetForceAlliance(BYTE bForce, BYTE bValue) {
  if (static_cast<BYTE>(bForce - 1) <= 3) {
    for (int i = 0; i < 8; i++) {
      if (gPlayerData[i].nTeam == bForce && IsHumanPlayerType(gPlayerData[i].nType)) {
        for (int j = 0; j < 8; j++) {
          if (bValue == 0 && i == j)
            continue;
          if (gPlayerData[j].nTeam == bForce && IsHumanPlayerType(gPlayerData[j].nType))
            sgbAlliances_g[i][j] = bValue;
        }
      }
    }
  }
}

// 0x443220 (167 B) -- share or unshare vision across force bForce: for every
// human player i in the force, set or clear bit j of SharedVision[i] for every
// other human member j. The bit mask is a strength-reduced 1 << j, not a
// rotate: the oracle seeds `lea edi,[eax+1h]` off the zeroed index and updates
// it with `rol edi, 1h` once per iteration, which is what MSVC does to 1 << j
// for an ascending j.
void SetForceVision(BYTE bForce, BYTE bEnable) {
  if (static_cast<BYTE>(bForce - 1) <= 3) {
    for (int i = 0; i < 8; i++) {
      if (gPlayerData[i].nTeam == bForce && IsHumanPlayerType(gPlayerData[i].nType)) {
        for (int j = 0; j < 8; j++) {
          if (i == j)
            continue;
          if (gPlayerData[j].nTeam == bForce && IsHumanPlayerType(gPlayerData[j].nType)) {
            if (bEnable != 0)
              SharedVision[i] |= (1 << j);
            else
              SharedVision[i] &= ~(1 << j);
          }
        }
      }
    }
  }
}

// 0x4432D0 (72 B) -- push the per-player force array out to both slot arrays,
// clearing the entry of any slot whose chat-room type is not a legal value. The
// cleared path stores AL rather than an immediate: it is reached only through
// `test eax,eax / jnz`, so the zero is already in AL, and `= 0` in the source
// is what produces that.
void ApplyPlayerForces() {
  for (int i = 0; i < 8; i++) {
    if (!IsLegalChatroomSlotVal(gNetPlayerData_g[i].nType)) {
      sgbPlayerForce_s[i] = 0;
    } else {
      BYTE bForce = sgbPlayerForce_s[i];
      gNetPlayerData_g[i].nTeam = bForce;
      gPlayerData[i].nTeam = bForce;
    }
  }
}

// 0x443320 (161 B) -- install a whole force configuration from a 0x14-byte
// settings blob: the per-player force bytes (converted from 0-based to 1-based),
// the four force names (resolved through the map string table, falling back to
// the stock strings), and the four flag bytes. Every loop decrements first; the
// name loop calls SetForceName(k + 1) and exits on k, and /Ob2 splices it and
// strength-reduces the table into the walked ESI.
void LoadForceSettings(void *pArg0) {
  const force_settings_t *pSettings = static_cast<const force_settings_t *>(pArg0);

  memcpy(sgbPlayerForce_s, pSettings->bPlayerForce, 8);

  int n = 8;
  do {
    --n;
    ++sgbPlayerForce_s[n];
  } while (n != 0);

  int k = 4;
  do {
    --k;
    const char *pszName = get_map_string(pSettings->wNameStrId[k]);
    if (pszName == 0)
      pszName = error_get_str(k + 0x17);
    SetForceName(k + 1, pszName);
  } while (k != 0);

  // The flag store goes through the true base 0x534898, so the load uses the
  // pre-decrement index and the store the post-decrement one.
  int j = 4;
  do {
    --j;
    byte_534898[j] = pSettings->bFlags[j];
  } while (j != 0);
}

// 0x4433D0 (34 B) -- the index of the first open game slot (nType == 6) assigned
// to bForce, or 8 if the force has none.
BYTE GetForceOpenSlot(BYTE bForce) {
  for (int i = 0; i < 8; i++) {
    if (gPlayerData[i].nTeam == bForce && gPlayerData[i].nType == 6)
      return static_cast<BYTE>(i);
  }
  return 8;
}

// 0x443400 (34 B) -- decode the force's two-bit victory and alliance mode out
// of the flag byte: 0 when bit 1 is clear, else 1 or 2 according to bit 2. The
// in-range arm goes in the if and the sentinel after it, because the oracle has
// `ja <tail>` with the in-range arm as the fall-through and `or al, -1h / ret`
// last. The inner test is laid out the same way, so the bit-set arm is the if.
BYTE GetForceAllianceMode(BYTE bForce) {
  if (static_cast<BYTE>(bForce - 1) <= 3) {
    BYTE bFlags = sgbForceFlags_s[bForce];
    if ((bFlags & 2) != 0) {
      // The whole computation stays eight bits. A BYTE lvalue incremented in
      // place is what forces `test al,4h / setnz al / inc al`; written as
      // `((bFlags & 4) != 0) + 1` the `!=` yields int and MSVC emits
      // `movzx / shr / and / inc` instead. Two separate returns are what it
      // if-converts into the oracle's two results one apart; five other
      // spellings were measured and this is the only 34-byte one.
      if ((bFlags & 4) != 0)
        return 2;
      return 1;
    }
    return 0;
  }
  return 0xFF;
}

// 0x443430 (40 B) -- copy force nForce's name out. The guard admits nothing but
// 1..4 on the positive side and leaves negatives to the signed `jg`, which is
// the oracle's own shape and is reproduced rather than tidied.
void GetForceName(int nForce, char *pszDest, int nMaxLen) {
  if (nForce != 0 && nForce <= 4)
    SStrCopy(pszDest, sgForceNames_s[nForce - 1], nMaxLen);
}

// 0x443460 (24 B) -- bit 3 of the force flags. Statement order is the whole of
// it: with the sentinel as an early return MSVC hoists it inline, where the
// oracle has `ja <tail>` with the normal path as the fall-through and
// `or al, -1h / ret` as the last two instructions.
BYTE IsForceVisionShared(BYTE bForce) {
  if (static_cast<BYTE>(bForce - 1) <= 3)
    return static_cast<BYTE>(sgbForceFlags_s[bForce] & 8);
  return 0xFF;
}

// GetForceOpenSlotCount -- not a ledger row: no 1.17 function has this body on
// its own, it exists only spliced into GetEmptiestForce, so it is static here
// and the name is ours. Its shape is read out of GetEmptiestForce's 118 bytes:
// the 1..4 guard folded into `inc ch / je / cmp ch,4h / ja`, the descending
// count loop over the game slots, and GetPlayerForce inlined inside it, whose
// `cmp edx,8h / jl / xor cl,cl` bounds check could not be there if the source
// read gPlayerData[j].nTeam directly.
//
// The counter is an `int` because that is what decides the caller's register
// allocation: declared BYTE it lives in BL, leaving BH for GetEmptiestForce's
// running best count, so the caller spills the running best force instead and
// comes out three bytes short. An int counter occupies the whole of EBX and the
// two roles swap to the oracle's assignment. The return stays BYTE: the
// comparison against the running best is `cmp bl, .. / jbe`, eight-bit unsigned.
static BYTE GetForceOpenSlotCount(BYTE bForce) {
  if (bForce == 0 || bForce > 4)
    return 0;
  int nCount = 0;
  int j = 8;
  while (j != 0) {
    --j;
    if (GetPlayerForce(j) == bForce && gPlayerData[j].nType == 6)
      nCount++;
  }
  return static_cast<BYTE>(nCount);
}

// 0x443480 (118 B) -- the force with the most open slots, or 0 if none has any.
BYTE GetEmptiestForce() {
  BYTE bBestForce = 0;
  BYTE bBestCount = 0;
  for (int i = 0; i < 4; i++) {
    BYTE bForce = static_cast<BYTE>(i + 1);
    BYTE bCount = GetForceOpenSlotCount(bForce);
    if (bCount > bBestCount) {
      bBestCount = bCount;
      bBestForce = bForce;
    }
  }
  return bBestForce;
}

// 0x443500 (84 B) -- the first open slot of force bForce at or after
// bAfter + 1, searched circularly over all eight slots; 8 if there is none.
// The % 8 is signed (`and ebx, 80000007h / jns / dec / or / inc`), and the
// increment is a statement on the BYTE parameter: an int nSlot local swaps
// `movzx edx, dl` ahead of `xor eax, eax`.
BYTE GetNextForceOpenSlot(BYTE bForce, BYTE bAfter) {
  ++bAfter;
  for (int i = 0; i < 8; i++) {
    int m = (bAfter + i) % 8;
    if (gPlayerData[m].nTeam == bForce && gPlayerData[m].nType == 6)
      return static_cast<BYTE>(m);
  }
  return 8;
}

// 0x443560 (19 B) -- the force of game slot nNation. Declared in
// src117/h/TrigEnum.h, not in forces.h, with the calling convention spelled
// out; the definition repeats it so the two cannot drift into two mangled
// names. The sentinel comes first here, unlike the 1..4 range-checked
// accessors: the oracle's `jl` jumps forward over `xor al,al / ret` into the
// load, so the out-of-range arm is the fall-through.
BYTE __fastcall GetPlayerForce(int nNation) {
  if (!PlayerIsLobby(nNation))  // Hyperion: >= 8 in 1.17
    return 0;
  return gPlayerData[nNation].nTeam;
}

// 0x443580 (58 B) -- the name of the force that game slot nPlayer belongs to.
// Both callees are spliced in: GetPlayerForce supplies the `cmp ecx,8h / jge`
// and GetForceName the `test al,al / cmp ecx,4h / jg` plus the SStrCopy tail,
// and MSVC folds all three rejections onto one `pop ebp / ret 4h`. The range
// test is hoisted into this function for that: spelled as the bare nested call,
// GetPlayerForce's out-of-range arm stays live and a second exit lands four
// bytes past the oracle's end.
void GetPlayerForceName(int nPlayer, char *pszDest, int nMaxLen) {
  if (PlayerIsLobby(nPlayer))  // Hyperion: < 8 in 1.17
    GetForceName(GetPlayerForce(nPlayer), pszDest, nMaxLen);
}

// 0x4435C0 (55 B) -- an open slot in the emptiest force: 0xFF when no force has
// one, otherwise the slot index (or 8, which GetForceOpenSlot already returns
// for "none"; the clamp below is the oracle's, redundant and reproduced).
// GetForceOpenSlot is spliced in, and the duplicated tail shows it: the oracle
// ends `mov al,8h / ret` for the not-found arm, where 8 >= 8 folds at compile
// time, and `mov al,cl / cmp cl,8h / jb / mov al,8h / ret` for the found arm.
BYTE GetFreeSlotFromEmptiestForce() {
  BYTE bForce = GetEmptiestForce();
  if (bForce == 0)
    return 0xFF;
  BYTE bSlot = GetForceOpenSlot(bForce);
  if (bSlot >= 8)
    return 8;
  return bSlot;
}

// 0x443600 (37 B) -- is any computer slot (nType == 5) assigned to this force?
// The exit test is the first statement of a do/while body: a while loop is
// rotated and tests the post-decrement value at the bottom (34 B).
int IsComputerForce(BYTE bForce) {
  int i = 8;
  do {
    if (i-- == 0)
      return 0;
  } while (gPlayerData[i].nTeam != bForce || gPlayerData[i].nType != 5);
  return 1;
}

// 0x443630 (24 B) -- bit 0 of the force flags. Same shape as the spine.
int IsForceRandomStartLocations(BYTE bForce) {
  if (static_cast<BYTE>(bForce - 1) <= 3)
    return sgbForceFlags_s[bForce] & 1;
  return 0;
}

// 0x443650 (33 B) -- set force nForce's name. No frame: every argument is in a
// register, so nothing on the stack is addressed. The capacity is the literal
// 0x1E the oracle pushes, not sizeof -- under OC_DLL_MODE the table's spelling
// is a pointer and sizeof would silently collapse.
void SetForceName(int nForce, const char *pszName) {
  if (nForce != 0 && nForce <= 4)
    SStrCopy(sgForceNames_s[nForce - 1], pszName, 0x1E);
}
