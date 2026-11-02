// port_camkeys.cpp -- the eight camera locations, StarCraft II's (port_camkeys.h). The user,
// 2026-11-02: the locations remade to be exactly SC2's, where F5 to F8 jump to the first four
// and Ctrl+F5 to Ctrl+F8 save them, and four more (5 to 8) have no key until the player gives
// them one in Settings, Hotkeys. 1.17's F2 to F4 locations are gone: their keys are taken off
// the accelerator tables (event_Win.cpp), so every install plays the one camera system.
//
// Each location holds the view's centre when it was saved, the point SC2 keeps. The game
// seeds all eight with the start view, so a jump before a save lands on home; a save says
// so on the map's message, as the rich bases' roll does. Presentation only: where the camera
// is put is not a game event, as 1.17's locations never were, and a replay does not hold
// them.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "port_camkeys.h"
#include "port_keys.h"
#include "../front/port_text.h"
#include "../render/port_view.h"
#include "../../h/TrigAct.h"     // map_msg_default
#include "../../h/minimap.h"     // cell_set_pos
#include "../../gen/globals117.h"

namespace {

int s_anLoc[8][2];  // each location's view centre, in map pixels; the start's until saved

// The view's centre now, in map pixels.
void Centre(int an[2])
{
  an[0] = static_cast<int>(glScrollPixelX) + PortViewShownWidth() / 2;
  an[1] = static_cast<int>(glScrollPixelY) + g_portView.nViewRows / 2;
}

// The view over the point: the location's centre in the screen's middle.
void GoTo(const int an[2])
{
  cell_set_pos((an[0] - PortViewShownWidth() / 2) & ~7, (an[1] - g_portView.nViewRows / 2) & ~7);
}

}  // namespace

// A game begins: every location the start view, which 1.17's three were (gamemap.cpp).
// port_camera.cpp calls it for a new game, a replay's and a loaded one's alike.
void PortCamKeysInit(void)
{
  for (int n = 0; n < 8; ++n)
    Centre(s_anLoc[n]);
}

// Which camera action a key down is with the modifiers held now: 0 to 7 a jump to the
// location, 8 to 15 a save of it (the action's location is n % 8), or -1 for none.
int PortCameraPressed(unsigned vk)
{
  const unsigned nKey = PortHotkeyHeld(vk);
  for (int n = 0; n < 8; ++n) {
    const unsigned nGo = PortHotkey(PORT_HOTKEY_CAMGO1 + n);
    if (nGo != 0 && nGo == nKey)
      return n;
    const unsigned nSet = PortHotkey(PORT_HOTKEY_CAMSET1 + n);
    if (nSet != 0 && nSet == nKey)
      return 8 + n;
  }
  return -1;
}

// The action runs: a jump puts the view over the location, a save takes the view's centre
// and says so on the map's message, as SC2 shows the save with a toast.
void PortCameraRun(int nAction)
{
  if (nAction < 0 || nAction >= 16)
    return;
  const int n = nAction % 8;
  if (nAction < 8) {
    GoTo(s_anLoc[n]);
    return;
  }
  Centre(s_anLoc[n]);
  // "Location N saved" on the map's message, for eight seconds
  const std::string msg = PortTextFill(PortText("Location {0} saved."), {std::to_string(n + 1)});
  map_msg_default(msg.c_str(), 8000);
}
