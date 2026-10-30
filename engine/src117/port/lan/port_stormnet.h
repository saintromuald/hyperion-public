// port_stormnet.h -- Storm's network, for Hyperion's own players and the LAN (port_stormnet.cpp):
// what the trainer's snapshots keep of it.
#ifndef PORT_STORMNET_H
#define PORT_STORMNET_H

#include <stddef.h>
#include <vector>

// The turns and messages in transit on the loopback, kept by a snapshot and put back (port_snap.cpp);
// bKeepSyncs leaves each turn its syncs only.
void PortNetSnapSave(std::vector<unsigned char> *pOut);
void PortNetSnapRestore(const std::vector<unsigned char> &saved, bool bKeepSyncs);
// A turn's commands less all but the syncs (0x05, 0x37), which every turn must hold once: the
// new length. A command of no known size ends the walk, what is after it dropped.
size_t PortNetKeepSyncs(unsigned char *p, size_t cb);

#endif
