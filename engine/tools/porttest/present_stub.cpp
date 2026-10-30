// present_stub.cpp -- the presenter (port_present.cpp) for draw_test: no GPU, and 1.17's frame rate.
int PortPresentGpu(void) { return 0; }
int PortFrameRate(void) { return 0; }
void PortScreenCoverCopied(const unsigned char *, int, int) {}  // no GPU scene to cover
