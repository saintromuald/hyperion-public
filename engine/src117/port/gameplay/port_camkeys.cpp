// port_camkeys.cpp -- the eight camera locations, StarCraft II's (port_camkeys.h). The user,
// 2026-11-02: the locations remade to be exactly SC2's, eight keys in all: F1 to F8 jump to
// them and Ctrl with the same key saves, as SC2's F-keys and Ctrl do. 1.17's F2 to F4
// locations are gone: their keys are taken off the accelerator tables (event_Win.cpp), and
// the keys F1 to F8 are the locations' alone - 1.17's Ctrl+F1 help moved to Ctrl+F10 there
// too, so every install plays the one camera system.
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

// The game's own view centre, the seed at a game's start: the camera is not up yet, and
// asking the seen rectangle there would run the camera's game-start again (the camera
// asks back), so the seed reads the game's view directly, as it was at zoom 1.
void Seed(int an[2])
{
  an[0] = static_cast<int>(glScrollPixelX) + PortViewShownWidth() / 2;
  an[1] = static_cast<int>(glScrollPixelY) + g_portView.nViewRows / 2;
}

// The centre of the view as it shows now: the seen rectangle's middle, so the point saved
// is exactly the screen's own middle at whatever zoom, side columns and raise it had (the
// user, 2026-11-02).
void Centre(int an[2])
{
  int anRect[4];
  PortViewSeenRect(&anRect[0], &anRect[1], &anRect[2], &anRect[3]);
  an[0] = (anRect[0] + anRect[2]) / 2;
  an[1] = (anRect[1] + anRect[3]) / 2;
}

// The view over the point: the camera centred on it at once, as 1.17's locations jump
// (no easing), at the zoom it has.
void GoTo(const int an[2])
{
  PortViewCameraCentre(static_cast<double>(an[0]), static_cast<double>(an[1]));
}

}  // namespace

// A game begins: every location the start view, which 1.17's three were (gamemap.cpp).
// port_camera.cpp calls it for a new game, a replay's and a loaded one's alike.
void PortCamKeysInit(void)
{
  for (int n = 0; n < 8; ++n)
    Seed(s_anLoc[n]);
}

// Which camera action a key down is with the modifiers held now: 0 to 7 a jump to the
// location, 8 to 15 a save of it (the action's location is n % 8), or -1 for none. The
// eight keys are PORT_HOTKEY_CAM1 to 8 (port_keys.h), F1 to F8, which Settings' Hotkeys
// tab changes; Ctrl with a location's key saves it, as SC2's Ctrl does.
int PortCameraPressed(unsigned vk)
{
  const unsigned nKey = PortHotkeyHeld(vk);
  for (int n = 0; n < 8; ++n) {
    const unsigned nGo = PortHotkey(PORT_HOTKEY_CAM1 + n);
    if (nGo == 0)
      continue;
    if (nGo == nKey)
      return n;
    if (nKey == (nGo | PORT_KEY_CTRL) && (nGo & PORT_KEY_CTRL) == 0)
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
