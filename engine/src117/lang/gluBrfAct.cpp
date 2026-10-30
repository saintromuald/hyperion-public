// gluBrfAct.cpp -- the mission briefing's trigger actions: wait, play a WAV,
// show a message, set the objectives, show, hide and animate a portrait, a
// transmission, the tutorial skip, and the runner that dispatches them. Bodies
// are in oracle VA order. brfact_transmission calls brfact_wav, defined above
// it, and 1.17 keeps that a call.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluBrfAct.h"   // + gluRdy.h (glurdy_*, brfact_RunAction,
                                    //   brfact_clear_waiting), TrigAct.h, dialog.h
#include "../h/victory.h"     // TRIGGERNODE / TrigCondition, psnprintf
#include "../h/snd.h"         // play_digital, snd_update_streams
#include "../h/Storm.h"       // SStrCopy
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifndef per name: the
// briefing's wait latch (0x667BAC, a BYTE), the briefing action table indexed
// by bAction (0x503908), the campaign maps' directories (0x51ABB8), and the
// campaign map index at 0x526524, 0 outside a campaign: a WORD with no name in
// any source, spelled as maphdr.cpp spells it.

#ifndef OC_SGBBRFWAITING_G
#define OC_SGBBRFWAITING_G
#ifdef OC_DLL_MODE
#define sgbBrfWaiting_g (*reinterpret_cast<BYTE *>(OC_VA(0x667BACu)))
#else
extern BYTE sgbBrfWaiting_g;
#endif
#endif

#ifndef OC_WORD_526524_G
#define OC_WORD_526524_G
#ifdef OC_DLL_MODE
#define word_526524_g (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD word_526524_g;
#endif
#endif

#ifndef OC_GSZMAPFILETBL
#define OC_GSZMAPFILETBL
#ifdef OC_DLL_MODE
#define gszMapFileTbl (reinterpret_cast<char **>(OC_VA(0x51ABB8u)))
#else
extern char *gszMapFileTbl[65];  // Hyperion: mapdata.dat's 65 rows
#endif
#endif

// The briefing action handler, and the table of them.
typedef BOOL(__fastcall *PFN_BRFACT)(TrigAction *pAct);

#ifndef OC_SGBRIEFTRIGACTIONS_G
#define OC_SGBRIEFTRIGACTIONS_G
#ifdef OC_DLL_MODE
#define sgBriefTrigActions_g (reinterpret_cast<PFN_BRFACT *>(OC_VA(0x503908u)))
#else
extern PFN_BRFACT sgBriefTrigActions_g[];
#endif
#endif

// TrigAction +0x08, which TrigAct.h leaves as `_unread_08[4]`: the WAV's map
// string, a DWORD tested whole and narrowed to a WORD for get_map_string.
#define ACT_WAV_STRING(pAct) (*reinterpret_cast<DWORD *>(&(pAct)->_unread_08[0]))

// The briefing trigger record: a TRIGGERNODE past its 8-byte link, which is
// what brfact_RunAction receives (the caller passes node + 8).
struct brf_trigger_t {
  /*0x000*/ TrigCondition conditions[16];
  /*0x140*/ TrigAction    actions[64];
  /*0x940*/ DWORD         dwFlags;
  /*0x944*/ BYTE          _unmapped_944[0x95F - 0x944];
  /*0x95F*/ BYTE          bCurrentAction;
};
static_assert(sizeof(brf_trigger_t) == 0x960, "brf_trigger_t == TRIGGERNODE(0x968) minus its 8-byte link");
static_assert(offsetof(brf_trigger_t, conditions) == 0x000, "cmp byte ptr [esi+0Fh], 0Dh");
static_assert(offsetof(brf_trigger_t, actions) == 0x140, "add ecx,0Ah / shl ecx,5h / add ecx,esi");
static_assert(offsetof(brf_trigger_t, dwFlags) == 0x940, "or dword ptr [esi+940h], 1h");
static_assert(offsetof(brf_trigger_t, bCurrentAction) == 0x95F, "mov al, byte ptr [esi+95Fh]");

// The bodies, in oracle VA order.

// 0x44FCD0 (54 B). The briefing "wait" action. The waiting latch is global, so
// the second entry into the same action, once the timer has fired and cleared
// the latch, sees bit 0 of the action's own flags still set and reports
// completion. The flags byte is changed in a local and stored back, as in 1.17.
BOOL brfact_wait(TrigAction *pAct) {
  if (sgbBrfWaiting_g == 0) {
    BYTE bFlags = pAct->bFlags;
    if (bFlags & 1) {
      pAct->bFlags = bFlags & ~1;
      return TRUE;
    }
    glurdy_start_wait_timer(pAct->dwTime);
    pAct->bFlags |= 1;
    sgbBrfWaiting_g = 1;
  }
  return FALSE;
}

// 0x44FD10 (137 B). Plays the briefing's WAV. Outside a campaign
// (word_526524_g == 0) the map string is the path; inside one it is joined
// under the campaign's own directory from gszMapFileTbl.
BOOL brfact_wav(TrigAction *pAct) {
  char szPath[0x104];

  if (ACT_WAV_STRING(pAct) != 0) {
    if (word_526524_g == 0)
      SStrCopy(szPath, get_map_string((WORD)ACT_WAV_STRING(pAct)), 0x104);
    else
      psnprintf(szPath, 0x104, "%s\\%s", gszMapFileTbl[word_526524_g],
                get_map_string((WORD)ACT_WAV_STRING(pAct)));
    play_digital(szPath);
  }
  return TRUE;
}

// 0x44FDA0 (29 B). Shows a map string for dwTime; the string id is narrowed to
// get_map_string's WORD.
BOOL brfact_message(TrigAction *pAct) {
  glurdy_text_display(get_map_string((WORD)pAct->dwStringId), pAct->dwTime);
  return TRUE;
}

// 0x44FDC0 (26 B). Sets this nation's mission-objective string id, then
// repaints the panel. glurdy_update_objectives never reads ECX, though gluRdy.h
// gives it a parameter; the string id passed there is the value 1.17 leaves in
// ECX, and the callee cannot see it.
BOOL brfact_objectives(TrigAction *pAct) {
  DWORD dwStringId = pAct->dwStringId;
  dword_5349A4[g_LocalNationID] = dwStringId;
  glurdy_update_objectives(reinterpret_cast<ctrl_t *>((DWORD_PTR)dwStringId));
  return TRUE;
}

// 0x44FDE0 (18 B). Shows the action's unit portrait in slot dwGroup.
BOOL brfact_show(TrigAction *pAct) {
  glurdy_show_portrait(pAct->wUnitType, (BYTE)pAct->dwGroup);
  return TRUE;
}

// 0x44FE00 (14 B). Hides the portrait in slot dwGroup.
BOOL brfact_hide(TrigAction *pAct) {
  glurdy_hide_portrait((BYTE)pAct->dwGroup);
  return TRUE;
}

// 0x44FE10 (17 B). Animates the portrait in slot dwGroup as talking for
// dwTime.
BOOL brfact_talking(TrigAction *pAct) {
  glurdy_talking_portrait((BYTE)pAct->dwGroup, pAct->dwTime);
  return TRUE;
}

// 0x44FE30 (135 B). brfact_wait's latch and flag handling, then the
// transmission itself: plays the WAV, posts the message, runs the talking
// portrait and arms the wait timer. bCount says how dwValue changes the wait:
// 7 sets it, 8 adds to it, 9 subtracts with a floor of zero.
BOOL brfact_transmission(TrigAction *pAct) {
  if (sgbBrfWaiting_g == 0) {
    BYTE bFlags = pAct->bFlags;
    if (bFlags & 1) {
      pAct->bFlags = bFlags & ~1;
      return TRUE;
    }
    DWORD dwTime = pAct->dwTime;
    switch (pAct->bCount) {
      case 7:
        dwTime = pAct->dwValue;
        break;
      case 8:
        dwTime += pAct->dwValue;
        break;
      case 9:
        if (pAct->dwValue > dwTime)
          dwTime = 0;
        else
          dwTime -= pAct->dwValue;
        break;
    }
    brfact_wav(pAct);
    glurdy_text_display(get_map_string((WORD)pAct->dwStringId), pAct->dwTime);
    glurdy_talking_portrait((BYTE)pAct->dwGroup, dwTime);
    glurdy_start_wait_timer(dwTime);
    pAct->bFlags |= 1;
    sgbBrfWaiting_g = 1;
    snd_update_streams();
  }
  return FALSE;
}

// 0x44FEC0 (30 B). Offers the skip-tutorial button, in a single-player
// campaign only.
BOOL brfact_tutorial() {
  if (gbMultiPlayerMode == 0 && word_526524_g != 0)
    glurdy_show_skip_tutorial();
  return TRUE;
}

// 0x44FEE0 (125 B). Runs the briefing node's pending actions. The argument is
// the node's &conditions[0] (the caller passes node + 8), so brf_trigger_t
// starts there. The bound is clamped to 0x40 and then tested for equality as
// its own statement, as 1.17 does.
int brfact_RunAction(void *pConditions) {
  brf_trigger_t *pNode = reinterpret_cast<brf_trigger_t *>(pConditions);

  if (pNode->conditions[0].bCondition == 0x0D) {
    pNode->dwFlags |= 1;
    for (;;) {
      BYTE bIndex = pNode->bCurrentAction;
      if (bIndex >= 0x40)
        bIndex = 0x40;
      if (bIndex == 0x40)
        break;
      TrigAction *pAct = &pNode->actions[bIndex];
      if (pAct->bFlags & 2) {
        pNode->bCurrentAction = bIndex + 1;
        continue;
      }
      if (pAct->bAction == 0) {
        pNode->bCurrentAction = 0x40;
        break;
      }
      if (sgBriefTrigActions_g[pAct->bAction](pAct) == 0)
        break;
      pNode->bCurrentAction++;
    }
    if (pNode->bCurrentAction < 0x40)
      return 0;
    pNode->dwFlags &= ~1;
    pNode->bCurrentAction = 0;
  }
  return 1;
}


// 0x44FF60 (8 B). Clears the wait latch.
void brfact_clear_waiting() {
  sgbBrfWaiting_g = 0;
}
