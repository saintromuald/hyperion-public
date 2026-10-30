// port_veclen.h -- veclen and veclen_precise, 1.17's distances, in the original's convention
// (port_veclen.cpp), which the game reaches by those names (port_names.cpp).
#ifndef PORT_VECLEN_H
#define PORT_VECLEN_H

// veclen (0x405CF0) and veclen_precise (0x405C90), which the game reaches by those
// names (port_names.cpp). The original's convention, spelled out as the original's
// code called them: ax in ECX, bx in EDX, ay and by on the stack, callee pops 8.
int __fastcall PortVeclen(int ax, int bx, int ay, int by);
int __fastcall PortVeclenPrecise(int ax, int bx, int ay, int by);

#endif
