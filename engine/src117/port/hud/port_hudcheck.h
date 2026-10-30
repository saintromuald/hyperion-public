// port_hudcheck.h -- Hyperion.exe --hud-check (port_hudcheck.cpp): a HUD file checked from the
// command line, and its boxes at a screen's shape.
#pragma once

#include <string>
#include <vector>

// The arguments after --hud-check. The exit code: 0 plays, 1 errors, 2 a file does not read.
int PortHudCheckCommand(const std::vector<std::string> &args);
