// port_camkeys.h -- the eight camera locations, SC2's (port_camkeys.cpp).
#ifndef PORT_CAMKEYS_H
#define PORT_CAMKEYS_H

// A game begins: every location the start view (port_camera.cpp's game start calls it).
void PortCamKeysInit(void);
// Which camera action a key down is with the modifiers held now: 0 to 7 a jump to the
// location, 8 to 15 a save of it (n % 8), or -1 for none. The keys are PORT_HOTKEY_CAMGO1
// to 8 and PORT_HOTKEY_CAMSET1 to 8 (port_keys.h), which Settings' Hotkeys tab changes.
int PortCameraPressed(unsigned vk);
// The action runs (event_Win.cpp): the jump puts the view over the location, the save
// takes the view's centre and says so on the map's message.
void PortCameraRun(int nAction);

#endif
