// port_keys.cpp -- Hyperion's own hotkeys (docs/front-end.md, F3): the keys that switch
// the art between classic and HD (F5, as Remastered's) and the view between 640 and the
// wide view (Ctrl+F5), and the camera's (F7 and F8 zoom out and in, F9 back to 1,
// Shift+F9 the whole map, F11 follows the selected unit, F12 replay TV; F6 Debug Tools' overlay),
// and the selection's (Tab the next subgroup, Ctrl+Tab the minimap's terrain,
// Shift+F11 the next idle worker, Shift+F12 the army).
// Settings' Hotkeys tab changes them; they are kept in the settings
// file's "hyperion" section as numbers, the virtual key in the low byte with
// PORT_KEY_CTRL, PORT_KEY_SHIFT and PORT_KEY_ALT or'ed in. Presentation only.
//
// Only the function keys 1.17 leaves free can be taken: F1 is its help, F2 to F4 its
// screen locations (with Shift to set them), and F10 its menu; and Tab, the minimap's
// terrain in 1.17, which a game of Hyperion's selection gives to these keys (minimap.cpp).
// Alt+Tab is Windows'.
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

const Hotkey kHotkeys[PORT_HOTKEYS] = {{"keyArt", 0x74},
                                       {"keyView", PORT_KEY_CTRL | 0x74},
                                       {"keyZoomOut", 0x76},
                                       {"keyZoomIn", 0x77},
                                       {"keyZoomReset", 0x78},
                                       {"keyZoomFit", PORT_KEY_SHIFT | 0x78},
                                       {"keyFollow", 0x7A},
                                       {"keyTv", 0x7B},
                                       {"keyDebugView", 0x75},
                                       {"keySubgroup", 0x09},
                                       {"keyTerrain", PORT_KEY_CTRL | 0x09},
                                       {"keyIdle", PORT_KEY_SHIFT | 0x7A},
                                       {"keyArmy", PORT_KEY_SHIFT | 0x7B}};

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

bool PortHotkeyFree(unsigned nKey)
{
  const unsigned vk = nKey & 0xFF;
  if (vk == 0x09)  // Tab, but not Alt+Tab
    return (nKey & PORT_KEY_ALT) == 0 || (nKey & PORT_KEY_CTRL) != 0;
  return (vk >= 0x74 && vk <= 0x78) || vk == 0x7A || vk == 0x7B;  // F5 to F9, F11, F12
}

void PortHotkeyName(unsigned nKey, char *psz, size_t n)
{
  char szKey[8];
  if ((nKey & 0xFF) == 0x09)
    SDL_snprintf(szKey, sizeof szKey, "Tab");
  else
    SDL_snprintf(szKey, sizeof szKey, "F%u", (nKey & 0xFF) - 0x6F);
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
  if (((nKey & 0xFF) < 0x74 || (nKey & 0xFF) > 0x7B) && (nKey & 0xFF) != 0x09)
    return -1;
  for (int i = 0; i < PORT_HOTKEYS; ++i)
    if (PortHotkey(i) == nKey)
      return i;
  return -1;
}
