// tv_stub.cpp -- Replay TV and following a unit (port_tv.cpp), both off in draw_test: no aim.
void PortTvInput(void) {}
int PortTvTarget(double *, double *, double *) { return 0; }
void PortFollowStop(void) {}
int PortFollowAt(double *, double *) { return 0; }
