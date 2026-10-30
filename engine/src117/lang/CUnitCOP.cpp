// CUnitCOP.cpp -- circle of power and CTF flag handling. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller here by /Ob2, while the
// oracle, built with WPO off, cannot splice across a TU boundary: a callee left in another
// TU reproduces that for free, and __declspec(noinline) is the lever when one must live
// here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/CUnitCOP.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/rect16.h"             // gUnitBoundBox's element type
#include "../h/point16.h"            // gUnitPlaceboxSize's element type
#include "../h/CUnitTerran.h"        // sub_442B60 and its callback typedef
#include "../h/placebox.h"           // set_placebox
#include "../h/TrigAct.h"            // CreateUnitAt
// collide.h declares mtx_query_place_bldg with the body's int nType; this TU
// calls it through the WORD view below, so the header's view is renamed away.
#define mtx_query_place_bldg mtx_query_place_bldg_int_view
#include "../h/collide.h"            // mtx_query_place_bldg
#undef mtx_query_place_bldg
#include "../h/gamesnd.h"            // gamesnd_placebox_error_msg
#include "../h/CUnitTSpells.h"       // gamesnd_play
#include "../h/gametype.h"           // IsCTFGame
#include "../h/Teams.h"              // IsTeamGame, GetTeamName
#include "../h/net_cmds.h"           // PlayerData, error_get_str, map_msg_chat_msg
#include "../h/victory.h"            // psnprintf
#include "../gen/globals117.h"

// The unit type goes out unconverted, as in CUnitZerg.cpp's view.
// Hyperion: the definition (collide.cpp) reads the cell and the type as
// ints, and this TU's callers pass them as 16 bits. The view takes the
// definition's types, which the x64 build needs, and the macro narrows the
// arguments as 1.17's did.
extern "C" int __fastcall mtx_query_place_bldg(CUnit *pUnit, BYTE bPlayer, int sX, int sY, int nType, int nSlot, int nArg6, int nArg7, int nArg8);
#define mtx_query_place_bldg(a0, a1, a2, a3, a4, a5, a6, a7, a8) (mtx_query_place_bldg)((a0), (a1), (short)(a2), (short)(a3), (WORD)(a4), (a5), (a6), (a7), (a8))

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one over the whole
// block, since the generated set grows over time and a single guard would go false and
// undeclare every name under it.

// The bodies, in oracle VA order.

// gUnitPlaceboxSize @0x522728, gulUnitTypeFlags @0x521660 and gPlayerData
// @0x6726C8 come from gen. gUnitBoundBox @0x522C38 is registered but gen does
// not emit it, so it is spelled here as collide.cpp and CUnitBuild.cpp spell
// it. byte_5264C2 is unregistered (banker.cpp's spelling); sgszPlaceboxStrs_g
// @0x508D90 is the sheet's name, as placebox.cpp uses it.
#ifdef OC_DLL_MODE
  #ifndef gUnitBoundBox
  #define gUnitBoundBox (reinterpret_cast<rect16_t *>(OC_VA(0x522C38u)))
  #endif
  #ifndef byte_5264C2
  #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #endif
  #ifndef sgszPlaceboxStrs_g
  #define sgszPlaceboxStrs_g (reinterpret_cast<WORD *>(OC_VA(0x508D90u)))
  #endif
#else
extern rect16_t gUnitBoundBox[228] OC_UNALIGNED;
extern BYTE byte_5264C2;
extern WORD sgszPlaceboxStrs_g[];
#endif


// The bodies, in oracle VA order.

// Runs the CTF cop over every unit in this unit's collision box, then parks it
// on a fixed countdown. The bounds are built with the same four statements as
// collide.cpp's w73a_bounds, which is what keeps the arithmetic 16-bit.
void CUnit::DispatchCTFCop()
{
  mrect_t  box;
  CSprite *pSprite = this->pSprite;
  upos_t wPosY = pSprite->wPosY;
  upos_t wPosX = pSprite->wPosX;

  box.top    = wPosY - gUnitBoundBox[wType].top;
  box.bottom = wPosY + gUnitBoundBox[wType].bottom;
  box.left   = wPosX - gUnitBoundBox[wType].left;
  box.right  = wPosX + gUnitBoundBox[wType].right;

  sub_442B60(&box, reinterpret_cast<CUnitTerranFindCb>(FlagCaptured), this);
  bUnion85 = 0x0F;
}

// Spawns the flag this cop guards once the game clock passes 0x258.
// The `je` after CreateUnitAt lands on the epilogue, so SetOrder is inside the
// null check, and the type and owner must be named locals read in this order.
void CUnit::DispatchCTFCopInit()
{
  CSprite *pSprite;
  CUnit *pFlag;
  upos_t wPosY;
  upos_t wPosX;
  int nFlagType;
  BYTE bOwner;

  if (g_dwElapsedTime < 0x258)
    return;

  SetButtonset(0xE4);

  if (g_ActiveHumanID == g_LocalHumanID && (this->pSprite->flags.b & 8) &&
      placebox_is_on())
    set_placebox(0, NULL, 1);

  nFlagType = 0xD7;
  bOwner = bPlayer;
  pSprite = this->pSprite;
  wPosY = pSprite->wPosY;
  wPosX = pSprite->wPosX;

  pFlag = CreateUnitAt(nFlagType, PosS(wPosX), PosS(wPosY), bOwner);
  if (pFlag != NULL)
  {
    pFlag->Show();
    pFlag->FinishedMaking();
    SetOrder(0x9A);
  }
}

// Moves the cop onto its ordered tile if the placement query allows it.
// The type is read twice: once for the pushed argument and once as the size
// table index, which gives the oracle's index in EAX and the pushed copy in ESI.
void CUnit::DispatchCTFCop2()
{
  WORD wUnitType = this->wType;
  BYTE bOwner = bPlayer;
  WORD wIdx = this->wType;
  WORD wSizeY = (WORD)gUnitPlaceboxSize[wIdx].y;
  WORD wSizeX = (WORD)gUnitPlaceboxSize[wIdx].x;
  int nResult;

  nResult = mtx_query_place_bldg(this, bOwner,
                                 PosS((wOrderTargetX - (short)wSizeX / 2) / 32),
                                 PosS((wOrderTargetY - (short)wSizeY / 2) / 32),
                                 wUnitType, 0, 0, 1, 0);
  if (nResult == 0)
  {
    UntrackCollision();
    SetUnitPosition(wOrderTargetX, wOrderTargetY);
    TrackCollision();
    SetButtonset(0xE4);
    SetOrder(0x99);
    return;
  }

  gamesnd_placebox_error_msg(this, sgszPlaceboxStrs_g[nResult]);
  SetOrder(0x99);
}

// sub_442B60's per-unit callback: hands the cop's own player the flag it is
// carrying, or eats a flag that walked onto the circle itself.
BOOL FlagCaptured(intptr_t nArg0, void *pArg1)
{
  CUnit *pUnit = reinterpret_cast<CUnit *>(nArg0);
  CUnit *pCop = static_cast<CUnit *>(pArg1);

  if (pUnit->bPlayer == pCop->bPlayer)
  {
    if (gulUnitTypeFlags[pUnit->wType] & 8)
    {
      CUnit *pHeld = pUnit->pPowerup;

      if (pHeld != NULL && pHeld->wType == 0xD7)
      {
        ctf_flag_captured(pHeld->bPlayer);
        pUnit->DestroyInventory();
        return FALSE;
      }
    }
  }
  else if (pUnit->wType == 0xD7)
  {
    ctf_flag_captured(pUnit->bPlayer);
    pUnit->SelfDestruct();
  }

  return FALSE;
}

intptr_t CUnit::GetHeldPowerup()
{
  if (gulUnitTypeFlags[wType] & 8)
    return reinterpret_cast<intptr_t>(pPowerup);

  return 0;
}

BOOL CUnit::IsCircleOfPower()
{
  return wType >= 0xC2 && wType <= 0xC7;
}

// The three CTF chat announcements. They differ only in the two string ids and
// in whether a sound plays; the team arm falls through, so it is written first.
void ctf_flag_captured(int nArg0)
{
  char szBuf[0x100];
  const char *pszName;
  int nStrId;

  if (!IsCTFGame())
    return;

  gamesnd_play(byte_5264C2 + 0x20, NULL, 1, 0);

  nStrId = 0x81;
  if (IsTeamGame())
  {
    pszName = GetTeamName(gPlayerData[nArg0].nTeam);
    nStrId = 0x82;
  }
  else
  {
    pszName = gPlayerData[nArg0].szName;
  }

  psnprintf(szBuf, sizeof(szBuf), error_get_str(nStrId), pszName);
  map_msg_chat_msg(szBuf, 0);
}

void ctf_flag_returned(int nArg0)
{
  char szBuf[0x100];
  const char *pszName;
  int nStrId;

  if (!IsCTFGame())
    return;

  nStrId = 0x7F;
  if (IsTeamGame())
  {
    pszName = GetTeamName(gPlayerData[nArg0].nTeam);
    nStrId = 0x80;
  }
  else
  {
    pszName = gPlayerData[nArg0].szName;
  }

  psnprintf(szBuf, sizeof(szBuf), error_get_str(nStrId), pszName);
  map_msg_chat_msg(szBuf, 0);
}

void ctf_flag_taken(int nArg0)
{
  char szBuf[0x100];
  const char *pszName;
  int nStrId;

  if (!IsCTFGame())
    return;

  gamesnd_play(byte_5264C2 + 0x20, NULL, 1, 0);

  nStrId = 0x7D;
  if (IsTeamGame())
  {
    pszName = GetTeamName(gPlayerData[nArg0].nTeam);
    nStrId = 0x7E;
  }
  else
  {
    pszName = gPlayerData[nArg0].szName;
  }

  psnprintf(szBuf, sizeof(szBuf), error_get_str(nStrId), pszName);
  map_msg_chat_msg(szBuf, 0);
}
