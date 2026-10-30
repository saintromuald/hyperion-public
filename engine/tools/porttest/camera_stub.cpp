// camera_stub.cpp -- the camera (port_camera.cpp) for draw_test: the game's own view, at zoom 1 and still.
double PortCameraZoom(void) { return 1.0; }
double PortCameraZoomTarget(void) { return 1.0; }
double PortCameraZoomMax(void) { return 2.0; }
double PortCameraShown(void) { return 1.0; }
void PortCameraGameStarts(int) {}
void PortCameraRemember(void) {}
int PortCameraSmooth(void) { return 1; }
void PortCameraSlow(int) {}
int PortCameraIsSlow(void) { return 0; }
void PortCameraZoomTo(double) {}
void PortCameraWheel(double) {}
int PortCameraStep(void) { return 0; }
int PortCameraForced(void) { return 0; }
