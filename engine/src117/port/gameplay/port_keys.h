// port_keys.h -- Hyperion's own hotkeys: the art's, the view's, the camera's and the camera
// locations' (port_keys.cpp, port_camkeys.cpp).
#ifndef PORT_KEYS_H
#define PORT_KEYS_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// Hyperion's own hotkeys (port_keys.cpp): the art's, the view's, the camera's, the camera
// locations' and the selection's. A key is its virtual key in the low byte with PORT_KEY_CTRL,
// _SHIFT and _ALT or'ed in; any key a game can read may be taken (PortHotkeyFree), and a
// default gives way to a key the player set for another. PortHotkeyPressed is which one a key
// down is with the modifiers held now, or -1.
enum {
  PORT_HOTKEY_ART,
  PORT_HOTKEY_VIEW,
  PORT_HOTKEY_ZOOM_OUT,  // the camera: F11, F12, Ctrl+F11, Ctrl+F12, Shift+F11, Shift+F12
  PORT_HOTKEY_ZOOM_IN,
  PORT_HOTKEY_ZOOM_RESET,
  PORT_HOTKEY_ZOOM_FIT,
  PORT_HOTKEY_FOLLOW,
  PORT_HOTKEY_TV,
  PORT_HOTKEY_DEBUG_VIEW,  // Shift+F9: Debug Tools' overlay (port_debugview.cpp)
  PORT_HOTKEY_SUBGROUP,    // Tab: the next subgroup, with Shift the one before
  PORT_HOTKEY_TERRAIN,     // Ctrl+Tab: the minimap's terrain, 1.17's Tab
  PORT_HOTKEY_IDLE,        // `: the next idle worker (SC2's, the user, 2026-11-02)
  PORT_HOTKEY_ARMY,        // Ctrl+Shift+F12: the army
  // the eight camera locations, SC2's (port_camkeys.cpp): F1 to F8 jump to them, and
  // Ctrl with the same key saves the location, so there are eight keys in all (the user,
  // 2026-11-02)
  PORT_HOTKEY_CAM1, PORT_HOTKEY_CAM2, PORT_HOTKEY_CAM3, PORT_HOTKEY_CAM4,
  PORT_HOTKEY_CAM5, PORT_HOTKEY_CAM6, PORT_HOTKEY_CAM7, PORT_HOTKEY_CAM8,
  PORT_HOTKEYS
};
enum { PORT_KEY_CTRL = 0x100, PORT_KEY_SHIFT = 0x200, PORT_KEY_ALT = 0x400 };
unsigned PortHotkey(int nWhich);
unsigned PortHotkeyDefault(int nWhich);
void PortHotkeySet(int nWhich, unsigned nKey);
bool PortHotkeyFree(unsigned nKey);
void PortHotkeyName(unsigned nKey, char *psz, size_t n);
unsigned PortHotkeyHeld(unsigned vk);
int PortHotkeyPressed(unsigned vk);

#endif
