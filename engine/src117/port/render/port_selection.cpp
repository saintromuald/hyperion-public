// port_selection.cpp -- the selection's look (port_selection.h): the looks over 1.17's circles
// and health bars, kept in the order they were added, and the one the active mod's hud.json
// gives, which can leave each kind of mark undrawn. Presentation only: a mark left undrawn is
// still the game's.
#include <windows.h>
#include "port_selection.h"
#include "../platform/port_json.h"
#include "../platform/port_log.h"

namespace {

const int kLooksMax = 16;
const PortSelectionLook *g_apLook[kLooksMax];  // in the order added; zero before any is
int g_nLooks;

const PortLogger Log("hyperion_mods.log");

// hud.json's: whether each kind of mark is drawn
const char *const kapszMark[SELECTION_MARKS] = {"circles", "teamCircles", "healthBars"};
bool g_abHudShown[SELECTION_MARKS] = {true, true, true};

bool HudShown(int nMark, int nOwner, bool bShown)
{
  (void)nOwner;
  return bShown && g_abHudShown[nMark];
}

}  // namespace

bool PortSelectionAdd(const PortSelectionLook *pLook)
{
  if (g_nLooks == kLooksMax)
    return false;
  g_apLook[g_nLooks++] = pLook;
  return true;
}

bool PortSelectionImageShown(int nImageId, int nPaletteType, int nOwner)
{
  int nMark;
  if (nPaletteType == 0x0B)
    nMark = SELECTION_HEALTH_BAR;
  else if (nImageId >= 0x231 && nImageId <= 0x23A)
    nMark = SELECTION_CIRCLE;
  else if (nImageId >= 0x23B && nImageId <= 0x244)
    nMark = SELECTION_TEAM_CIRCLE;
  else
    return true;
  bool bShown = true;
  for (int i = 0; i < g_nLooks; ++i)
    if (g_apLook[i]->Shown != NULL)
      bShown = g_apLook[i]->Shown(nMark, nOwner, bShown);
  return bShown;
}

void PortSelectionHud(const PortJson &selection)
{
  for (int m = 0; m < SELECTION_MARKS; ++m) {
    g_abHudShown[m] = true;
    const PortJson &v = selection[kapszMark[m]];
    if (v.kind == PortJson::BOOL)
      g_abHudShown[m] = v.b;
    else if (v.kind != PortJson::NONE)
      Log("selection: %s is true or false", kapszMark[m]);
  }
}

// hud.json's look.
static const PortSelectionLook kHudLook = {"hud.json", HudShown};
PORT_SELECTION_LOOK(kHudLook);
