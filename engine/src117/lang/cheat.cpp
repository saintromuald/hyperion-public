// cheat.cpp -- cheat code parsing and the cheat bitfield. Bodies are in oracle
// VA order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/cheat.h"

#include <ctype.h>   // tolower
#include <stdlib.h>
#include <string.h>  // memcmp, memset, strlen, strncpy
#include <time.h>    // _time64

#include "../h/CUnit.h"       // CUnit::GetPlayerUnitList, CUnit::sub_43B0B0
#include "../h/CUnitTerran.h"  // status_set_update
#include "../h/Storm.h"       // SStrLen
#include "../h/gamemap.h"     // gamemap_force_redraw
#include "../h/gametype.h"    // CheatsAllowed
#include "../h/gds_local.h"   // psnprintf
#include "../h/mask.h"        // mask_disable_fog
#include "../h/saveload.h"    // get_upgrade_level / set_upgrade_level
#include "../h/glues.h"       // glues_set_skip
#include "../h/grid.h"        // IsBroodwar
#include "../h/idea.h"        // IdeaInitialize / IdeaEncrypt / IdeaDestroy
#include "../h/music.h"       // music_PlayRadioFreeZerg
#include "../h/net_cmds.h"    // gameloop_init, error_get_str, wrap_map_msg
#include "../h/uiSingle.h"    // find_campaign
#include "../gen/globals117.h"

// Storm.dll ordinal 509, spelled exactly as uiSingle.cpp, replay.cpp and
// gluBNCreate.cpp spell it; Storm.h leaves it out for the reason given there.
// The `extern "C"` matters: the .def supplies _SStrCmpI@12, and a C++
// declaration would ask the linker for ?SStrCmpI@@YGHPBD0I@Z, an unresolved
// external.
extern "C" {
int   __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax);
}

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x5264C2, a name coined here: nothing named contains it. Read as a byte and
// only against zero, it gates the Radio Free Zerg track.
#ifndef OC_BYTE_5264C2
#define OC_BYTE_5264C2
#ifdef OC_DLL_MODE
#define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
#else
extern BYTE byte_5264C2;
#endif
#endif

// 0x524D8C upg_repeats, upgrades.dat's MaxRepeats, indexed by upgrade id 0 to
// 0x3C and compared with get_upgrade_level's BYTE return; it runs to upg_bw at
// +0x40, 61 entries plus alignment.
#ifndef OC_UPG_REPEATS
#define OC_UPG_REPEATS
#ifdef OC_DLL_MODE
#define upg_repeats (reinterpret_cast<BYTE *>(OC_VA(0x524D8Cu)))
#else
extern BYTE upg_repeats[64] OC_UNALIGNED;
#endif
#endif

// The eighteen IDEA-encrypted cheat tokens: one 8-byte-stride run at 0x4EB97C
// to 0x4EBA0B in .rdata, so one table rather than eighteen constants. Each
// index is (VA - 0x4EB97C) / 8 for the VA 1.17's `cmp eax, dword ptr [VA]`
// names.
static const DWORD s_aCheatCodes[18][2] = {
    {0xFE3BEABFu, 0x9EBDB515u}, {0x55E238E6u, 0x16488475u},
    {0x182E9E9Fu, 0xCE87028Bu}, {0x7FE014CCu, 0x6E26E2B8u},
    {0x960C7C82u, 0x902273ACu}, {0xE9664828u, 0xB0523962u},
    {0x3A932764u, 0x7D9E4677u}, {0xB2460378u, 0xC8AADFA5u},
    {0xB8B696E6u, 0xC0A2E0ABu}, {0x782E7767u, 0xEB5345E4u},
    {0x3A439A3Eu, 0x32F56535u}, {0x78EF3975u, 0x8D389B41u},
    {0xEACE9558u, 0xAD0842E6u}, {0x3A479567u, 0x8F06C237u},
    {0xA9783171u, 0x425E8432u}, {0x3CE4870Cu, 0x8FBB3912u},
    {0x1B5B3DAEu, 0x285A1628u}, {0x66459A95u, 0x1AD08449u},
};

// One row of the map-jump table at 0x4EBA18, six entries of 0x10 bytes walked
// to 0x4EBA78. The last three fields go straight to find_campaign, whose seated
// declaration in src117/h/uiSingle.h names them.
struct mapcmd_t {
  /*0x00*/ const char *pszName;
  /*0x04*/ int nRace;       // `mov edx, dword ptr [esi+4h]` into find_campaign
  /*0x08*/ int nFirst;      // `push dword ptr [esi+8h]`
  /*0x0C*/ int bExpansion;  // `push dword ptr [esi+0Ch]`, also to glues_set_skip
};
OC_X86_LAYOUT(offsetof(mapcmd_t, nRace) == 0x04, "mapcmd_t::nRace");
OC_X86_LAYOUT(offsetof(mapcmd_t, nFirst) == 0x08, "mapcmd_t::nFirst");
OC_X86_LAYOUT(offsetof(mapcmd_t, bExpansion) == 0x0C, "mapcmd_t::bExpansion");
OC_X86_LAYOUT(sizeof(mapcmd_t) == 0x10, "mapcmd_t: `add esi, 10h` per entry");

static const mapcmd_t s_aMapCmds[6] = {
    {"terran", 1, 0, 0},  {"zerg", 0, 1, 0},  {"protoss", 2, 1, 0},
    {"xterran", 1, 1, 1}, {"xzerg", 0, 1, 1}, {"xprotoss", 2, 1, 1},
};

// 0x40D920. Lower-case the token into a 0x100-byte scratch, then run it through
// IDEA in 8-byte blocks, chaining each block against the running output. The
// clamp is `cmovae`, so the length is compared unsigned; the block loop's bound
// is `jle`/`jl`, so its counter is signed. adwBlk has to be declared inside the
// block loop: at function scope MSVC gives it a slot of its own, and the oracle
// spills pdwDst into adwBlk[1] instead.
void __fastcall cheat_encrypt(const char *pszSrc, DWORD *pdwDst)
{
  char szLower[0x100];

  size_t cch = strlen(pszSrc);
  if (cch >= 0x100)
    cch = 0xFF;

  const char *pSrc = pszSrc;
  char *pDst = szLower;
  size_t n = cch;
  while (n-- != 0)
    *pDst++ = static_cast<char>(tolower(*pSrc++));
  *pDst = 0;

  memset(pdwDst, 0, 8);
  for (int i = 0; i < static_cast<int>(cch); i += 8) {
    DWORD adwBlk[2];
    strncpy(reinterpret_cast<char *>(adwBlk), &szLower[i], 8);
    adwBlk[0] ^= pdwDst[0];
    adwBlk[1] ^= pdwDst[1];
    IdeaEncrypt(0, reinterpret_cast<intptr_t>(adwBlk), reinterpret_cast<intptr_t>(pdwDst));
  }
}

// 0x40D9D0. Install a new cheat bitfield and apply the one-shot cheats in it.
// The loops walk twelve players, not the eight gPlayerData declares. Minerals
// are reached through the gas base because the two arrays are one object:
// `[eax-30h]` carries no relocation. The message id comes from two calls MSVC
// tail-merges; selecting the id for one call gives a cmov instead.
void __fastcall cheat_set_bits(DWORD dwFlags, const char *pszWho)
{
  char szMsg[0x100];

  if (gbMultiPlayerMode != 0)
    return;

  DWORD dwOld = glCheatBits;
  BOOL bChanged = (dwFlags != dwOld);
  glCheatBits = dwFlags;

  if (((glCheatBits ^ dwOld) & 0x4000) != 0 && gwGameMode == 3)
    gamemap_force_redraw();

  if (((glCheatBits ^ dwOld) & 1) != 0 && gwGameMode == 3) {
    mask_disable_fog();
    gamemap_force_redraw();
  }

  if ((glCheatBits & 0x10) != 0) {
    glCheatBits &= ~0x10u;
    for (int i = 0; i < 12; i++)
      if (gPlayerData[i].nType == 2) {
        gPlayerGas[i - 12] += 10000;
        gPlayerGas[i] += 10000;
      }
  }

  if ((glCheatBits & 0x40000) != 0) {
    glCheatBits &= ~0x40000u;
    for (int i = 0; i < 12; i++)
      if (gPlayerData[i].nType == 2)
        gPlayerMinerals[i] += 500;
  }

  if ((glCheatBits & 0x80000) != 0) {
    glCheatBits &= ~0x80000u;
    for (int i = 0; i < 12; i++)
      if (gPlayerData[i].nType == 2)
        gPlayerGas[i] += 500;
  }

  if ((glCheatBits & 8) != 0) {
    glCheatBits &= ~8u;
    for (int i = 0; i < 12; i++)
      if (gPlayerData[i].nType == 2)
        for (int j = 0; j < 61; j++) {
          if (j == 60)
            continue;
          if (get_upgrade_level(i, static_cast<WORD>(j)) >= upg_repeats[j])
            continue;
          set_upgrade_level(
              i, static_cast<WORD>(j),
              static_cast<BYTE>(get_upgrade_level(i, static_cast<WORD>(j)) + 1));
          CUnit *pUnit = CUnit::GetPlayerUnitList(i);
          if (pUnit != NULL)
            pUnit->sub_43B0B0(j);
        }
  }

  if (gwGameMode == 3 && bChanged) {
    char *pszOut;
    if (glCheatBits < dwOld)
      pszOut = const_cast<char *>(error_get_str(0x42));
    else
      pszOut = const_cast<char *>(error_get_str(0x41));
    if (pszWho != NULL && gbMultiPlayerMode != 0) {
      psnprintf(szMsg, 0x100, "%s %s", pszWho, pszOut);
      pszOut = szMsg;
    }
    wrap_map_msg(pszOut, -1, 0);
  }
  status_set_update();
}

// 0x40DC80. Match the encrypted token against the sixteen in-game codes. The
// multiplayer flag is read twice and CheatsAllowed can change it, so only the
// path that called it reloads. Each arm is a memcmp, not a pair of dword
// compares: the oracle reloads the token in every arm, which CSE would not do.
// One else-if chain and a single `return 1` at the end: the entry guard's
// return then shares the last arm's epilogue, as in the oracle.
int __fastcall parse_cheat_command(const DWORD *pdwEnc, DWORD *pdwCheatBits)
{
  if (gbMultiPlayerMode != 0)
    return 1;

  if (gwGameMode == 3 && CheatsAllowed() == 0)
    return 0;

  if (memcmp(pdwEnc, s_aCheatCodes[16], 8) == 0)      *pdwCheatBits ^= 0x200;
  else if (memcmp(pdwEnc, s_aCheatCodes[3],  8) == 0) *pdwCheatBits ^= 0x10;
  else if (memcmp(pdwEnc, s_aCheatCodes[4],  8) == 0) *pdwCheatBits ^= 0x40000;
  else if (memcmp(pdwEnc, s_aCheatCodes[5],  8) == 0) *pdwCheatBits ^= 0x80000;
  else if (memcmp(pdwEnc, s_aCheatCodes[6],  8) == 0) *pdwCheatBits ^= 0x8;
  else if (memcmp(pdwEnc, s_aCheatCodes[10], 8) == 0) *pdwCheatBits ^= 0x1000;
  else if (memcmp(pdwEnc, s_aCheatCodes[11], 8) == 0) *pdwCheatBits ^= 0x2000;
  else if (memcmp(pdwEnc, s_aCheatCodes[13], 8) == 0) *pdwCheatBits ^= 0x20000;
  else if (memcmp(pdwEnc, s_aCheatCodes[14], 8) == 0) *pdwCheatBits ^= 0x800;
  else if (memcmp(pdwEnc, s_aCheatCodes[7],  8) == 0) *pdwCheatBits ^= 0x1;
  else if (memcmp(pdwEnc, s_aCheatCodes[8],  8) == 0) *pdwCheatBits ^= 0x2;
  else if (memcmp(pdwEnc, s_aCheatCodes[2],  8) == 0) *pdwCheatBits ^= 0x4;
  else if (memcmp(pdwEnc, s_aCheatCodes[12], 8) == 0) *pdwCheatBits ^= 0x4000;
  else {
    if (gbMultiPlayerMode != 0)
      return 0;

    if (memcmp(pdwEnc, s_aCheatCodes[0], 8) == 0)      *pdwCheatBits ^= 0x80;
    else if (memcmp(pdwEnc, s_aCheatCodes[1], 8) == 0) *pdwCheatBits ^= 0x40;
    else if (memcmp(pdwEnc, s_aCheatCodes[9], 8) == 0) *pdwCheatBits ^= 0x100;
    else
      return 0;
  }
  return 1;
}

// 0x40DEE0. Seed the cipher from a fixed PRNG sequence, encrypt the token, and
// offer it to the three parsers in turn. srand is reseeded from the clock so
// the fixed seed leaks nothing into later draws.
BOOL __fastcall parse_command(const char *pszToken, DWORD *pdwCheatBits)
{
  DWORD adwEnc[2];
  DWORD adwKey[4];

  if (gbMultiPlayerMode != 0)
    return 0;

  OcSrand(0x75);
  for (unsigned int i = 0; i < 4; i++)
    adwKey[i] = OcRand();
  OcSrand(static_cast<unsigned int>(_time64(NULL)));

  IdeaInitialize(0, 1, adwKey);
  cheat_encrypt(pszToken, adwEnc);
  IdeaDestroy();

  if (parse_cheat_command(adwEnc, pdwCheatBits) != 0)
    return 1;
  if (parse_debug_command(adwEnc, pdwCheatBits) != 0)
    return 1;
  return parse_map_command(pszToken, pdwCheatBits) != 0;
}

// 0x40DFA0. The two codes that are not gameplay cheats. gwGameMode is read
// twice; MSVC holds it in SI and reloads only past the call. An else-if chain
// with one `return 1` ranks pdwEnc into EBX as the oracle does.
int __fastcall parse_debug_command(const DWORD *pdwEnc, DWORD *pdwCheatBits)
{
  if (gwGameMode == 3 && CheatsAllowed() == 0)
    return 0;

  if (memcmp(pdwEnc, s_aCheatCodes[17], 8) == 0)
    *pdwCheatBits ^= 0x20000000;
  else if (memcmp(pdwEnc, s_aCheatCodes[15], 8) == 0) {
    if (gwGameMode == 3 && IsBroodwar() && byte_5264C2 == 0)
      music_PlayRadioFreeZerg();
  }
  else
    return 0;
  return 1;
}

// 0x40E030. Jump straight to a campaign mission named on the command line. The
// walk's bound is tested twice because the match arm re-enters at the post-loop
// test while falling out of the loop returns directly. The two game-mode arms
// are an if/else joining on one `return 1`; with a `return` in the first arm
// MSVC shares the epilogue instead of duplicating it.
int __fastcall parse_map_command(const char *pszToken, DWORD *pdwCheatBits)
{
  DWORD dwMission;

  if (gbMultiPlayerMode == 0 && (glCheatBits & 0x200) != 0) {
    const mapcmd_t *p = s_aMapCmds;
    while (p < s_aMapCmds + 6) {
      if (SStrCmpI(pszToken, p->pszName, SStrLen(p->pszName)) == 0)
        break;
      p++;
    }
    if (p >= s_aMapCmds + 6)
      return 0;

    if (find_campaign(pszToken + SStrLen(p->pszName), p->nRace, p->nFirst,
                      p->bExpansion, &dwMission) == 0 ||
        dwMission == 0x3F)
      return 1;

    glues_set_skip(static_cast<int>(dwMission), p->nRace, p->bExpansion);
    if (gwGameMode == 3) {
      gameloop_init(4);
      glGluesMode = 0;
    } else {
      glGluesMode = 0;
      gwGameMode = 4;
    }
    return 1;
  }
  return 0;
}
