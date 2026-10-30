// port_debugview.h -- Debug Tools' overlay (port_debugview.cpp).
#ifndef PORT_DEBUGVIEW_H
#define PORT_DEBUGVIEW_H

// Debug Tools' overlay (port_debugview.cpp): off, the game in a window with the
// readings round it, full screen with them over it, or a counter. It is a layer over the game
// (port_overlay.h), which places the game, draws the panels and takes the mouse over them.
bool PortDebugViewAvailable(void);
int PortDebugViewMode(void);
const char *PortDebugViewModeName(void);
void PortDebugViewSet(int nMode);
void PortDebugViewCycle(void);
bool PortDebugViewShown(void);

#endif
