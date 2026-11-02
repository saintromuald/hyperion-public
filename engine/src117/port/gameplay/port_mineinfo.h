// port_mineinfo.h -- the mining efficiency a town hall shows over it (port_mineinfo.cpp).
#ifndef PORT_MINEINFO_H
#define PORT_MINEINFO_H

class CUnit;

// The overlay layer is self-registering (port_mineinfo.cpp).
// A start worker's first order, SC2's split (the user, 2026-11-02): each worker of the
// start goes to its own mineral field, the base's nearest ones in the round; a replayed,
// loaded or classic game keeps its own. placebox.cpp calls it as the workers are placed.
void PortStartWorkerMine(CUnit *pWorker, int nWorker);

#endif
