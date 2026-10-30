// remastered_stub.cpp -- a StarCraft: Remastered install (port_remastered.cpp) for the Storm tests:
// none, so every file comes from the MPQs, as the tests compare them with the install's storm.dll.
#include <stddef.h>
extern const char PORT_REMASTERED_ARCHIVE[] = "<StarCraft Remastered>";
int PortGameSource(void) { return 0; }  // PORT_SOURCE_MPQ
void *PortRemasteredLoad(const char *, unsigned long *) { return NULL; }
bool PortRemasteredLocalDll(char *, size_t) { return false; }
