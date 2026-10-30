// port_hudlive.h -- the HUD's data layer as the live channel answers it (port_control.cpp):
// the hud command's values, under the format's names, and hud_events' list. Apart from
// port_hudstate.h, which game files include, since these are std::string.
#pragma once

#include <string>

// The answer's fields ("name":value,... without the braces).
std::string PortHudJson(void);
std::string PortHudEventsJson(unsigned int nSerial);
