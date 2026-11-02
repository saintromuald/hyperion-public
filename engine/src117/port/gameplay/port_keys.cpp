// port_keys.cpp -- Hyperion's own hotkeys (docs/front-end.md, F3): the keys that switch
// the art between classic and HD (F2, as the camera locations moved to F5 to F8 took
// Remastered's F5; the user, 2026-11-02) and the view between 640 and the wide view (Ctrl+F2),
// and the camera's (F9 and Shift+F9 zoom out and in, Ctrl+F9 back to 1, Ctrl+Shift+F9 the
// whole map, F11 follows the selected unit, F12 replay TV; F3 Debug Tools' overlay),
// and the selection's (Tab the next subgroup, Ctrl+Tab the minimap's terrain,
// ` the next idle worker, Shift+F12 the army), and the eight camera locations' keys
// (port_camkeys.cpp). Settings' Hotkeys tab changes them; they are kept in the settings
// file's "hyperion" section as numbers, the virtual key in the low byte with
// PORT_KEY_CTRL, PORT_KEY_SHIFT and PORT_KEY_ALT or'ed in. Presentation only.
//
// Any key the window reports can be taken now (the user, 2026-11-02: every hotkey is
// rebindable), except the system's own Alt+Tab and the game menu's Alt+F4.
#include <windows.h>
#include <SDL3/SDL.h>
#include "port_keys.h"
#include "../platform/port_winmsg.h"
#include "../platform/port_settings.h"

namespace {

struct Hotkey {
  const char *pszSetting;
  unsigned nDefault;
};

// A default of 0: no key until the player gives the hotkey one (the camera locations
// 5 to 8, as SC2 leaves them).
const Hotkey kHotkeys[PORT_HOTKEYS] = {{"keyArt", 0x71},
                                       {"keyView", PORT_KEY_CTRL | 0x71},
                                       {"keyZoomOut", 0x78},
                                       {"keyZoomIn", PORT_KEY_SHIFT | 0x78},
                                       {"keyZoomReset", PORT_KEY_CTRL | 0x78},
                                       {"keyZoomFit", PORT_KEY_CTRL | PORT_KEY_SHIFT | 0x78},
                                       {"keyFollow", 0x7A},
                                       {"keyTv", 0x7B},
                                       {"keyDebugView", 0x72},
                                       {"keySubgroup", 0x09},
                                       {"keyTerrain", PORT_KEY_CTRL | 0x09},
                                       {"keyIdle", 0xC0},
                                       {"keyArmy", PORT_KEY_SHIFT | 0x7B},
                                       {"keyCamGo1", 0x74},
                                       {"keyCamGo2", 0x75},
                                       {"keyCamGo3", 0x76},
                                       {"keyCamGo4", 0x77},
                                       {"keyCamGo5", 0},
                                       {"keyCamGo6", 0},
                                       {"keyCamGo7", 0},
                                       {"keyCamGo8", 0},
                                       {"keyCamSet1", PORT_KEY_CTRL | 0x74},
                                       {"keyCamSet2", PORT_KEY_CTRL | 0x75},
                                       {"keyCamSet3", PORT_KEY_CTRL | 0x76},
                                       {"keyCamSet4", PORT_KEY_CTRL | 0x77},
                                       {"keyCamSet5", 0},
                                       {"keyCamSet6", 0},
                                       {"keyCamSet7", 0},
                                       {"keyCamSet8", 0}};

// The key the player set for one, or 0.
unsigned HotkeySet(int nWhich)
{
  double d = 0.0;
  if (PortSettingNumber("hyperion", kHotkeys[nWhich].pszSetting, &d) && d > 0.0 &&
      PortHotkeyFree(static_cast<unsigned>(d)))
    return static_cast<unsigned>(d);
  return 0;
}

}  // namespace

unsigned PortHotkey(int nWhich)
{
  if (nWhich < 0 || nWhich >= PORT_HOTKEYS)
    return 0;
  const unsigned nSet = HotkeySet(nWhich);
  if (nSet != 0)
    return nSet;
  // the default, unless the player gave that key to another
  for (int i = 0; i < PORT_HOTKEYS; ++i)
    if (i != nWhich && HotkeySet(i) == kHotkeys[nWhich].nDefault)
      return 0;
  return kHotkeys[nWhich].nDefault;
}

unsigned PortHotkeyDefault(int nWhich)
{
  return nWhich >= 0 && nWhich < PORT_HOTKEYS ? kHotkeys[nWhich].nDefault : 0;
}

void PortHotkeySet(int nWhich, unsigned nKey)
{
  if (nWhich >= 0 && nWhich < PORT_HOTKEYS && PortHotkeyFree(nKey))
    PortSettingSetNumber("hyperion", kHotkeys[nWhich].pszSetting, nKey);
}

// Whether a key may be one of Hyperion's: any the window reports except the system's
// own Alt+Tab; 0 is allowed, as a hotkey without a key (the camera locations 5 to 8).
bool PortHotkeyFree(unsigned nKey)
{
  const unsigned vk = nKey & 0xFF;
  if (nKey == 0)
    return true;
  if (vk == 0x09)  // Tab, but not Alt+Tab
    return (nKey & PORT_KEY_ALT) == 0 || (nKey & PORT_KEY_CTRL) != 0;
  return (vk >= 0x70 && vk <= 0x7B) ||   // F1 to F12
         (vk >= 0x30 && vk <= 0x39) ||   // 0 to 9
         (vk >= 0x41 && vk <= 0x5A) ||   // A to Z
         vk == 0xC0;                     // `
}

void PortHotkeyName(unsigned nKey, char *psz, size_t n)
{
  const unsigned vk = nKey & 0xFF;
  char szKey[16];
  if (vk == 0x09)
    SDL_snprintf(szKey, sizeof szKey, "Tab");
  else if (vk == 0xC0)
    SDL_snprintf(szKey, sizeof szKey, "`");
  else if (vk >= 0x70 && vk <= 0x7B)
    SDL_snprintf(szKey, sizeof szKey, "F%u", vk - 0x6F);
  else
    SDL_snprintf(szKey, sizeof szKey, "%c", static_cast<char>(vk));
  SDL_snprintf(psz, n, "%s%s%s%s", (nKey & PORT_KEY_CTRL) ? "Ctrl+" : "", (nKey & PORT_KEY_SHIFT) ? "Shift+" : "",
               (nKey & PORT_KEY_ALT) ? "Alt+" : "", szKey);
}

unsigned PortHotkeyHeld(unsigned vk)
{
  return (vk & 0xFF) | (WinMsgKeyState(VK_CONTROL) < 0 ? PORT_KEY_CTRL : 0) |
         (WinMsgKeyState(VK_SHIFT) < 0 ? PORT_KEY_SHIFT : 0) | (WinMsgKeyState(VK_MENU) < 0 ? PORT_KEY_ALT : 0);
}

int PortHotkeyPressed(unsigned vk)
{
  const unsigned nKey = PortHotkeyHeld(vk);
  if (!PortHotkeyFree(nKey & (PORT_KEY_CTRL | PORT_KEY_SHIFT | PORT_KEY_ALT | 0xFF)))
    return -1;
  for (int i = 0; i < PORT_HOTKEYS; ++i)
    if (PortHotkey(i) == nKey)
      return i;
  return -1;
}
