// port_keys.h -- Hyperion's own hotkeys: the art's, the view's and the camera's (port_keys.cpp).
#ifndef PORT_KEYS_H
#define PORT_KEYS_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// Hyperion's own hotkeys (port_keys.cpp): the art's, the view's, the camera's and the
// selection's. A key is its virtual key in the low byte with PORT_KEY_CTRL, _SHIFT and _ALT
// or'ed in; only F5 to F9, F11 and F12, which 1.17 leaves free, and Tab, whose minimap toggle
// the terrain key takes over in a game of Hyperion's selection, can be taken (PortHotkeyFree). A default gives
// way to a key the player set for another. PortHotkeyPressed is which one a key down is
// with the modifiers held now, or -1.
enum {
  PORT_HOTKEY_ART,
  PORT_HOTKEY_VIEW,
  PORT_HOTKEY_ZOOM_OUT,  // the camera: F7, F8, F9, Shift+F9, F11, F12
  PORT_HOTKEY_ZOOM_IN,
  PORT_HOTKEY_ZOOM_RESET,
  PORT_HOTKEY_ZOOM_FIT,
  PORT_HOTKEY_FOLLOW,
  PORT_HOTKEY_TV,
  PORT_HOTKEY_DEBUG_VIEW,  // F6: Debug Tools' overlay (port_debugview.cpp)
  PORT_HOTKEY_SUBGROUP,    // Tab: the next subgroup, with Shift the one before
  PORT_HOTKEY_TERRAIN,     // Ctrl+Tab: the minimap's terrain, 1.17's Tab
  PORT_HOTKEY_IDLE,        // Shift+F11: the next idle worker
  PORT_HOTKEY_ARMY,        // Shift+F12: the army
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
