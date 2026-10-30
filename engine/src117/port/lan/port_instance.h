// port_instance.h -- the single-instance check, scoped to the install folder (port_instance.cpp).
#ifndef PORT_INSTANCE_H
#define PORT_INSTANCE_H

// The single-instance check scoped to the install folder. The engine's
// CheckForOtherInstances (main.cpp, 0x466F20) forwards here; ECX is the window
// class, as in the oracle.
void PortCheckForOtherInstances(const char *pszTitle);

// Starts Hyperion again with this copy's command line and --after naming this process, so the
// new copy waits at the check for this one to end; the caller then quits. pszDrop names an
// option the new copy starts without, with its value: --mod, once the Mods screen chose
// another mod. False when it could not start.
bool PortInstanceRestart(const char *pszDrop = nullptr);

#endif
