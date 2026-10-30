// mapmsg.cpp -- the on-screen map message lines. Bodies are in oracle VA
// order.
//
// /Ob2 can splice a callee defined in this file into a caller here, where 1.17,
// built without WPO, keeps every cross-TU call; noinline on the declaration is
// the lever when such a callee has to live here.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/mapmsg.h"
#include "../h/gluMap.h"       // stat_strip_control_chars
#include "../h/Storm.h"        // SStrCopy (pulls <windows.h>)
#include "../h/dlg_draw.h"     // font_set, font_set_color_tbl
#include "../h/dlg_dlgdraw.h"  // img_area_invalid
#include "../h/CUnitTSpells.h" // gamesnd_play
#include "../h/net_cmds.h"     // PlayerData, error_get_str, wrap_map_msg
#include "../h/Teams.h"        // IsTeamGame, GetTeamName
#include "../h/victory.h"      // psnprintf
#include "../h/SystemSurvey.h" // get_printable_string
#include "../h/gds_font.h"     // check_wrap
#include "../gen/globals117.h"
#include "../port/data/port_chatfilter.h"  // Hyperion: PortChatClean
#include "../port/render/port_teamcolor.h"  // Hyperion: PortTeamMinimapShown
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../port/render/port_maptext.h"  // Hyperion: MapTextCharStart

// GetTickCount, imported from KERNEL32 at IAT slot 0x4EB2FC. The slot is read
// off the oracle's import table, the same identification credits.cpp records;
// the dllimport spelling is what emits the oracle's six-byte indirect call
// instead of a direct call through a linker thunk.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

extern "C" void *__cdecl memset(void *, int, size_t);

// Callees no header of ours declares yet.

// 0x4CDF20, in the dlg_draw.cpp range. font_adjust is spelled character for
// character as src117/lang/CtxtHelp.cpp:29 spells it; a second spelling is a
// second mangled name.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);

// 0x41D4F0. ECX is read as 8 bits and both consumers use only AL.
BYTE __fastcall get_unit_color_override(BYTE bPlayer);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x66FF00 -- the message text table, 13 rows of 0xDA. map_msg_init memsets it
// for 0xB12 = 13 * 0xDA and every slot access is `[i*0DAh + 66FF00h]`. Rows
// 0..10 are the ring, row 11 the bottom line, row 12 the target line, so the
// literals 0x67085E and 0x670938 are rows 11 and 12 and 0x670A44 / 0x670A53 /
// 0x670A54 are interior elements of the three sibling tables. Never take an
// extent here with sizeof(): over a dll-mode global that collapses to 4.
#ifndef OC_SGSZ_MSG_TBL
#define OC_SGSZ_MSG_TBL
#ifdef OC_DLL_MODE
#define sgszMsgTbl (*reinterpret_cast<char (*)[13][0xDA]>(OC_VA(0x66FF00u)))
#else
extern char sgszMsgTbl[13][0xDA];
#endif
#endif

// 0x670A12 -- the ring's write cursor, wrapped at 11. BYTE: every access is
// `mov cl, byte ptr` or `movzx eax, byte ptr`.
#ifndef OC_SGB_NEXT_MSG
#define OC_SGB_NEXT_MSG
#ifdef OC_DLL_MODE
#define sgbNextMsg (*reinterpret_cast<BYTE *>(OC_VA(0x670A12u)))
#else
extern BYTE sgbNextMsg;
#endif
#endif

// 0x670A14 -- the GetTickCount deadline per slot; map_msg_update retires a slot
// on `now - deadline >= 0`, a signed test. DWORD from the store width.
#ifndef OC_SGL_MSG_TIME_TBL
#define OC_SGL_MSG_TIME_TBL
#ifdef OC_DLL_MODE
#define sglMsgTimeTbl (reinterpret_cast<LONG *>(OC_VA(0x670A14u)))
#else
extern LONG sglMsgTimeTbl[13] OC_UNALIGNED;
#endif
#endif

// 0x670A48 -- the font colour table index per slot, handed to
// font_set_color_tbl. BYTE from `mov cl, byte ptr [esi+670A48h]`.
#ifndef OC_SGB_MSG_COLOR_TBL
#define OC_SGB_MSG_COLOR_TBL
#ifdef OC_DLL_MODE
#define sgbMsgColorTbl (reinterpret_cast<BYTE *>(OC_VA(0x670A48u)))
#else
extern BYTE sgbMsgColorTbl[13];
#endif
#endif

// 0x670A58 -- what raised the message. map_msg_trigger_abort clears a slot only
// when bit 0 is set. BYTE from `test byte ptr [esi+670A58h], 1h`.
#ifndef OC_SGB_MSG_SOURCE_G
#define OC_SGB_MSG_SOURCE_G
#ifdef OC_DLL_MODE
#define sgbMsgSource_g (reinterpret_cast<BYTE *>(OC_VA(0x670A58u)))
#else
extern BYTE sgbMsgSource_g[13];
#endif
#endif

// 0x670A68 -- the per-line vertical step. WORD: the store is
// `mov word ptr [0x670A68], si`. map_msg_draw's `add ebx, dword ptr [0x670A68]`
// is a dead-upper-half load, not a wider field. Only this TU reaches it, and
// the proof arm is static: MSVC widens the load only for an object it placed.
#ifndef OC_SGW_Y_DELTA
#define OC_SGW_Y_DELTA
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgwYDelta (*reinterpret_cast<WORD *>(OC_VA(0x670A68u)))
#else
static WORD sgwYDelta;
#endif
#endif

// 0x670A6C -- the height of one line, added to every rectangle's top edge to
// give its bottom. WORD, stored by map_msg_init_font as `mov word ptr`. Static
// in the proof arm for the same reason as sgwYDelta.
#ifndef OC_SGW_Y_OFFSET_G
#define OC_SGW_Y_OFFSET_G
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgwYOffset_g (*reinterpret_cast<WORD *>(OC_VA(0x670A6Cu)))
#else
static WORD sgwYOffset_g;
#endif
#endif

// 0x5290B6 -- the per-player colour map, indexed by the override colour.
// initPlayerColorMap pushes the base with a length of 0x0C, which fixes the
// extent; nine bodies read it with `movzx eax, byte ptr [<idx>+5290B6h]`, which
// fixes the element width.
#ifndef OC_GB_MINIMAP_COLOR_G
#define OC_GB_MINIMAP_COLOR_G
#ifdef OC_DLL_MODE
#define gbMinimapColor_g (reinterpret_cast<BYTE *>(OC_VA(0x5290B6u)))
#else
extern PlayerMinimapColorTable gbMinimapColor_g;
#endif
#endif

// 0x6D3148 -- read once, as `test dword ptr [0x6D3148], 1000h`. Unnamed in the
// sheets, so the address is the name.
#ifndef OC_DWORD_6D3148
#define OC_DWORD_6D3148
#ifdef OC_DLL_MODE
#define dword_6D3148 (*reinterpret_cast<DWORD *>(OC_VA(0x6D3148u)))
#else
extern DWORD dword_6D3148;
#endif
#endif

// 0x50F160 gbFontAllowWrap -- gds/font.cpp's, cleared around the retry pass.
#ifndef OC_GB_FONT_ALLOW_WRAP
#define OC_GB_FONT_ALLOW_WRAP
#ifdef OC_DLL_MODE
#define gbFontAllowWrap (*reinterpret_cast<BYTE *>(OC_VA(0x50F160u)))
#else
extern BYTE gbFontAllowWrap;
#endif
#endif

// 0x6D3034 / 0x6D3037 gbFontLineExtra / gbFontMaxHgt -- gds/font.cpp's metrics.
#ifndef OC_GB_FONT_LINE_EXTRA
#define OC_GB_FONT_LINE_EXTRA
#ifdef OC_DLL_MODE
#define gbFontLineExtra (*reinterpret_cast<BYTE *>(OC_VA(0x6D3034u)))
#else
extern BYTE gbFontLineExtra;
#endif
#endif

#ifndef OC_GB_FONT_MAX_HGT
#define OC_GB_FONT_MAX_HGT
#ifdef OC_DLL_MODE
#define gbFontMaxHgt (*reinterpret_cast<BYTE *>(OC_VA(0x6D3037u)))
#else
extern BYTE gbFontMaxHgt;
#endif
#endif

// 0x6D3048 gnFontCurrX -- gds/font.cpp's pen position, reset before each wrap.
#ifndef OC_GN_FONT_CURR_X
#define OC_GN_FONT_CURR_X
#ifdef OC_DLL_MODE
#define gnFontCurrX (*reinterpret_cast<int *>(OC_VA(0x6D3048u)))
#else
extern int gnFontCurrX;
#endif
#endif

// Hyperion: the text's places are in 1.17's 640-wide layout, which a
// wider view puts in its middle, nConsoleX from the left (port_view.cpp).
static inline int MsgX(int nX)
{
  return nX + g_portView.nConsoleX;
}

// Slot 12 is the target line, slot 11 the bottom line, and anything below 11 a
// ring row whose Y is its distance behind the write cursor.
void invalidate_slot(int nSlot)
{
  if (nSlot == 0x0C) {
    img_area_invalid(MsgX(0x0A), 0x127, MsgX(0x276), sgwYOffset_g + 0x127);
  } else if (nSlot >= 0x0B) {
    img_area_invalid(MsgX(0x1A4), 0x18, MsgX(0x26C), sgwYOffset_g + 0x18);
  } else {
    int nRow = (nSlot - sgbNextMsg + 0x0B) % 0x0B;
    img_area_invalid(MsgX(0x0A), sgwYDelta * nRow + 0x70, MsgX(0x276),
                     sgwYOffset_g + 0x70 + sgwYDelta * nRow);
  }
}

// Set the per-line step to the font's line height, grown so that eleven lines
// fill 0xB7 pixels when they would otherwise fall short. The global is the
// accumulator; a WORD local widens once into ESI where the oracle keeps SI.
// The operand order picks which load feeds SI, and only the DIR32 names show it.
void map_msg_init_font()
{
  font_set(sgpFonts_g[1]);

  sgwYDelta = gbFontMaxHgt + gbFontLineExtra;
  sgwYOffset_g = sgwYDelta;
  if (sgwYDelta * 0x0B < 0xB7)
    sgwYDelta += (0xB7 - sgwYDelta * 0x0B) / 0x0B;

  font_set(NULL);
  sgbNextMsg = 0x0A;
}

// The chat wrapper: prefixes the speaker's name, picks a colour from the
// player's own or the unit-colour override, and hands the line to map_msg.
// The `%c` name colour is captured before the switch, so it and map_msg's colour
// diverge on the override path, and the sound plays on every path.
// bColor takes the name colour after stat_strip_control_chars, which keeps the
// int in EBX across the call. Case labels are read off the index table.
void wrap_map_msg(void *pArg0, int nPlayer, int nFlag)
{
  const char *pszText = static_cast<const char *>(pArg0);
  char szBuf[0x100];
  char szName[0x50];

  if (pszText == NULL) {
    return;
  }
  if (nPlayer == -1) {
    map_msg_target(pszText);
    return;
  }
  // Hyperion: Settings' Mature language filter, on what is shown only (port_chatfilter.cpp)
  char szClean[0x100];
  if (static_cast<unsigned>(nPlayer) < 8)
    pszText = PortChatClean(pszText, szClean, sizeof szClean);

  BYTE bColor = 2;
  if (static_cast<unsigned>(nPlayer) >= 8) {
    SStrCopy(szBuf, pszText, 0x100);
  } else {
    if (gPlayback != 0 && nFlag == 0) {
      get_printable_string(szName, gPlayerSlots_g[nPlayer].szName, 0x50);
      psnprintf(szBuf, 0x100, "%s: %s", szName, pszText);
    } else {
      int nNameColor = (nPlayer == g_LocalNationID) ? 7 : 2;
      stat_strip_control_chars(szName, gPlayerData[nPlayer].szName, 0x50);
      bColor = static_cast<BYTE>(nNameColor);
      if ((dword_6D3148 & 0x1000) != 0) {
        // Hyperion: the colour Settings show the player in
        switch (PortTeamMinimapShown(get_unit_color_override(
            static_cast<BYTE>(nPlayer)))) {
        case 0x6F: bColor = 0x08; break;
        case 0xA5: bColor = 0x0E; break;
        case 0x9F: bColor = 0x0F; break;
        case 0xA4: bColor = 0x10; break;
        case 0x9C: bColor = 0x11; break;
        case 0x13: bColor = 0x15; break;
        case 0x54: bColor = 0x16; break;
        case 0x87: bColor = 0x17; break;
        case 0xB9: bColor = 0x18; break;
        case 0x88: bColor = 0x19; break;
        case 0x86: bColor = 0x1B; break;
        case 0x33: bColor = 0x1C; break;
        case 0x4D: bColor = 0x1D; break;
        case 0x9A: bColor = 0x1E; break;
        case 0x80: bColor = 0x1F; break;
        default:   bColor = 0x02; break;
        }
      }
      if (gPlayerData[nPlayer].nType == 9) {
        psnprintf(szBuf, 0x100, "[%s]%s:%c %s", error_get_str(0x1B), szName,
                  static_cast<char>(nNameColor), pszText);
      } else if (IsTeamGame()) {
        psnprintf(szBuf, 0x100, "[%s]%s:%c %s",
                  GetTeamName(gPlayerData[nPlayer].nTeam), szName,
                  static_cast<char>(nNameColor), pszText);
      } else {
        psnprintf(szBuf, 0x100, "%s:%c %s", szName, static_cast<char>(nNameColor), pszText);
      }
    }
  }
  gamesnd_play(0x17, NULL, 1, 0);

  map_msg(szBuf, bColor, GetTickCount() + 0x1B58, 0);
}

// Wrap the text into ring rows, one per pass, and repaint every non-empty row.
// The retry with wrapping off is what stops a word too long for the line.
void map_msg(const char *pszText, BYTE bColor, DWORD dwDeadline, BYTE bSource)
{
  BYTE bSaved = bColor;

  font_set(sgpFonts_g[1]);
  gsFontMargins.left = 0;
  gsFontMargins.right = 0x26C;

  while (*pszText != '\0') {
    const char *pszStart = pszText;

    gnFontCurrX = 0;
    pszText = check_wrap(pszStart);
    if (pszText == pszStart) {
      gnFontCurrX = 0;
      gbFontAllowWrap = 0;
      pszText = check_wrap(pszStart);
      gbFontAllowWrap = 1;
    }

    BYTE bNext = sgbNextMsg;
    BYTE bSlot = bNext++;
    sgbNextMsg = (bNext < 0x0B) ? bNext : 0;

    sglMsgTimeTbl[bSlot] = dwDeadline;
    sgbMsgSource_g[bSlot] = bSource;
    sgbMsgColorTbl[bSlot] = bSaved;

    // Hyperion: the slot's cut falls between characters
    const size_t nCopy = strlen(pszStart) - strlen(pszText) + 1 > 0xDA ? 0xDA : strlen(pszStart) - strlen(pszText) + 1;
    SStrCopy(sgszMsgTbl[bSlot], pszStart,
             static_cast<unsigned>(MapTextCharStart(pszStart, pszStart + nCopy - 1) - pszStart + 1));
    bColor = bSaved;
  }

  for (int i = 0; i < 0x0B; ++i) {
    if (sgszMsgTbl[i][0] != '\0') {
      invalidate_slot(i);
    }
  }

  font_set(NULL);
}

// The chat line's own default lifetime is 0x1B58 ms.
void map_msg_chat_msg(const char *pszText, int nTime)
{
  if (pszText != NULL) {
    if (nTime == 0) {
      nTime = 0x1B58;
    }
    map_msg(pszText, 3, GetTickCount() + nTime, 0);
    gamesnd_play(0x17, NULL, 1, 0);
  }
}

// The ring is drawn newest-first from the write cursor, then the target line
// and the bottom line, each with its own justification and margins.
// nY is a WORD: every use is 16-bit, which lets MSVC drop the 0x66 prefixes.
// The counter is decremented before the slot advance and tested on its own,
// which keeps the oracle's `test ecx, ecx` and its `cmp [..], cl` after the loop.
void map_msg_draw()
{
  font_set(sgpFonts_g[1]);

  int nSlot = sgbNextMsg;
  WORD nY = 0x70;
  int i = 0x0B;
  do {
    if (sgszMsgTbl[nSlot][0] != '\0') {
      font_set_color_tbl(sgbMsgColorTbl[nSlot]);
      gsFontMargins.right = static_cast<short>(MsgX(0x276));
      gbFontJust = 0x11;
      gsFontMargins.left = static_cast<short>(MsgX(0x0A));
      gsFontMargins.top = PosS(nY);
      gsFontMargins.bottom = PosS(sgwYOffset_g + nY);
      font_adjust(MsgX(0x0A), PosU(nY), sgszMsgTbl[nSlot]);
    }
    nY += sgwYDelta;
    --i;
    nSlot = (nSlot + 1) % 0x0B;
  } while (i != 0);

  // Field order in the two fixed lines: MSVC merges left and top into one store.
  if (sgszMsgTbl[12][0] != '\0') {
    font_set_color_tbl(sgbMsgColorTbl[12]);
    gbFontJust = 0x12;
    gsFontMargins.left = static_cast<short>(MsgX(0x0A));
    gsFontMargins.top = 0x127;
    gsFontMargins.right = static_cast<short>(MsgX(0x276));
    gsFontMargins.bottom = sgwYOffset_g + 0x127;
    font_adjust(MsgX(0x0A), 0x127, sgszMsgTbl[12]);
  }

  if (sgszMsgTbl[11][0] != '\0') {
    font_set_color_tbl(sgbMsgColorTbl[11]);
    gbFontJust = 0x14;
    gsFontMargins.left = static_cast<short>(MsgX(0x1A4));
    gsFontMargins.top = 0x18;
    gsFontMargins.right = static_cast<short>(MsgX(0x26C));
    gsFontMargins.bottom = sgwYOffset_g + 0x18;
    font_adjust(MsgX(0x1A4), 0x18, sgszMsgTbl[11]);
  }

  font_set(NULL);
}

void map_msg_init()
{
  memset(sgszMsgTbl, 0, 0xB12);
  map_msg_init_font();
}

// Only the local player's own units raise the target line.
void map_msg_target_self(char *pszMsg, DWORD dwNation)
{
  if (pszMsg != NULL && dwNation == g_LocalNationID &&
      g_ActiveHumanID == g_LocalHumanID) {
    map_msg_target(pszMsg);
  }
}

// The invalidated rectangle is four literals in the oracle, three of them bare
// and the fourth sgwYOffset_g biased by the same 0x127 as the top edge.
void map_msg_target(const char *pszText)
{
  if (pszText != NULL) {
    SStrCopy(sgszMsgTbl[12], pszText, 0xDA);
    sgbMsgColorTbl[12] = 4;
    sglMsgTimeTbl[12] = GetTickCount() + 0x1B58;
    img_area_invalid(MsgX(0xA), 0x127, MsgX(0x276), sgwYOffset_g + 0x127);
  }
}

void map_msg_default(const char *pszText, int nTime)
{
  if (pszText != NULL) {
    map_msg(pszText, 2, GetTickCount() + nTime, 1);
  }
}

// Retire every ring slot a trigger raised. The bound is 11, so the target and
// bottom lines survive even though the inlined invalidate_slot still carries
// their arms.
void map_msg_trigger_abort(void)
{
  for (int i = 0; i < 0x0B; ++i) {
    if (sgszMsgTbl[i][0] != '\0' && (sgbMsgSource_g[i] & 1) != 0) {
      sgszMsgTbl[i][0] = '\0';
      invalidate_slot(i);
    }
  }
}

// Retire every slot whose deadline has passed, this time including the target
// and bottom lines. The comparison is a signed difference, so it survives the
// tick counter wrapping.
void map_msg_update()
{
  DWORD dwNow = GetTickCount();

  for (int i = 0; i < 0x0D; ++i) {
    if (sgszMsgTbl[i][0] != '\0' &&
        static_cast<LONG>(dwNow - sglMsgTimeTbl[i]) >= 0) {
      sgszMsgTbl[i][0] = '\0';
      invalidate_slot(i);
    }
  }
}

void map_msg_error(const char *pszMsg, BYTE bType)
{
  if (pszMsg != NULL) {
    map_msg(pszMsg, bType, GetTickCount() + 0x1B58, 0);
    gamesnd_play(2, NULL, 1, 0);
  }
}

// Hyperion: the lines on screen (h/mapmsg.h)
int MapMsgLines(const char **ppszLines, int nMax)
{
  int n = 0;
  for (int i = 0; i < 0x0D && n < nMax; ++i)
    if (sgszMsgTbl[i][0] != '\0')
      ppszLines[n++] = sgszMsgTbl[i];
  return n;
}
