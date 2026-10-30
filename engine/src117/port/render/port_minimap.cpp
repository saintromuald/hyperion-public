// port_minimap.cpp -- the minimap's look (port_minimap.h): the looks over 1.17's minimap, kept in
// the order they were added, and the one the active mod's hud.json gives, its dots in a colour
// of the mod's by the owner's relation to the player. Presentation only: a look changes no dot's
// place and no step of the dots' walk, whose counts the multiplayer game's hash takes.
#include <windows.h>
#include <string>
#include "port_minimap.h"
#include "../platform/port_json.h"
#include "../platform/port_log.h"
#include "../../h/playerlimits.h"  // PlayerSlots, PLAYER_NEUTRAL
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

const int kLooksMax = 16;
const PortMinimapLook *g_apLook[kLooksMax];  // in the order added; zero before any is
int g_nLooks;

const PortLogger Log("hyperion_mods.log");

// hud.json's dots: a palette number for each relation to the player, -1 to leave it
enum { OWN, ALLIED, ENEMY, NEUTRAL, RELATIONS };
const char *const kapszRelation[RELATIONS] = {"own", "allied", "enemy", "neutral"};
int g_anDot[RELATIONS] = {-1, -1, -1, -1};

// The owner's relation to the player, in a game (a replay has no player of its own): -1 in a
// replay.
int Relation(int nOwner)
{
  const int nLocal = static_cast<int>(g_LocalNationID);
  const int nSlots = PlayerSlots();
  if (gPlayback != 0 || nLocal < 0 || nLocal >= nSlots || nOwner < 0 || nOwner >= nSlots)
    return -1;
  if (nOwner == PLAYER_NEUTRAL)
    return NEUTRAL;
  if (nOwner == nLocal)
    return OWN;
  return sgbAlliances_g[nLocal][nOwner] != 0 ? ALLIED : ENEMY;
}

unsigned char HudDotColour(int nOwner, int nType, unsigned char bColour)
{
  (void)nType;
  const int r = Relation(nOwner);
  return r >= 0 && g_anDot[r] >= 0 ? static_cast<unsigned char>(g_anDot[r]) : bColour;
}

}  // namespace

bool PortMinimapAdd(const PortMinimapLook *pLook)
{
  if (g_nLooks == kLooksMax)
    return false;
  g_apLook[g_nLooks++] = pLook;
  return true;
}

unsigned char PortMinimapDotColour(int nOwner, int nType, unsigned char bColour)
{
  for (int i = 0; i < g_nLooks; ++i)
    if (g_apLook[i]->DotColour != NULL)
      bColour = g_apLook[i]->DotColour(nOwner, nType, bColour);
  return bColour;
}

void PortMinimapHud(const PortJson &minimap)
{
  for (int &n : g_anDot)
    n = -1;
  const PortJson &dots = minimap["dots"];
  for (int r = 0; r < RELATIONS; ++r) {
    const PortJson &v = dots[kapszRelation[r]];
    if (v.kind == PortJson::NONE)
      continue;
    if (v.kind == PortJson::NUMBER && v.d >= 0.0 && v.d <= 255.0 && v.d == static_cast<int>(v.d))
      g_anDot[r] = static_cast<int>(v.d);
    else
      Log("minimap: dots' %s is a palette number, 0 to 255", kapszRelation[r]);
  }
}

// hud.json's look.
static const PortMinimapLook kHudLook = {"hud.json", HudDotColour};
PORT_MINIMAP_LOOK(kHudLook);
