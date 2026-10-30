// port_gpu_hd.h -- the HD art in the GPU presenter: the art hotkey, HD lighting and Remastered's
// console (port_gpu_hd.cpp).
#ifndef PORT_GPU_HD_H
#define PORT_GPU_HD_H

// The art hotkey in a game (F5 unless Settings changed it): the art from classic to HD or
// back, blending over hd.fade seconds, and kept as Settings' Art (port_gpu.cpp). Nothing
// without the GPU presenter's HD. PortGpuHdCan says whether this run can draw in HD (the
// GPU presenter, its HD pipelines and the GPU scene at its last stage); PortGpuHdSet sets
// the art, blending as the hotkey does.
void PortGpuHdToggle(void);
int PortGpuHdCan(void);
// Whether the frame is in HD now, wholly or blending.
int PortGpuHdShown(void);
// The HD art's draws the last scene recorded, for the frame log (port_framelog.cpp).
int PortGpuHdDrawCount(void);
void PortGpuHdSet(int bOn);
// Settings' HD lighting: Remastered's lighting on the HD frames (hd.lighting).
void PortGpuHdLightingSet(int bOn);
// The console's art as status_load_console loaded it (game\<race>console.pcx, nWidth x
// nHeight at the console's origin, nX), and its skin: 0 zerg, 1 terran, 2 protoss, 3 the neutral
// one of a replay. The HD frame draws Remastered's console there and keeps 1.17's pixels
// over it only where they differ from this art: the panels' content (port_gpu.cpp, R4f).
void PortGpuConsoleArt(const unsigned char *pPixels, int nWidth, int nHeight, int nX, int nSkin);
// Each HD piece's console origin in game pixels, in port_hd.cpp's order (left, right,
// centre): the art's nX for all three unless the console is laid out at the screen's
// edges (port_console.cpp). Whether the frame shows the HD console now, wholly in HD.
void PortGpuConsolePieces(const int anShift[3]);
int PortGpuHdConsoleShown(void);
// Whether the GPU draws a mod's own icons and wireframe over the console at the HD scale, in
// the game drawn: the console leaves them out (PortConsoleModUi).
int PortGpuModUiShown(void);

#endif
