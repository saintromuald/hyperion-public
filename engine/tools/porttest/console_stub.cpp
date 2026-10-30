// console_stub.cpp -- the HD console's layout (port_console.cpp), which draw_test never lays out.
struct ctrl_t;
int PortConsoleShift(const ctrl_t *) { return 0; }
void PortConsoleUnsplit(void) {}
