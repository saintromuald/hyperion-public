// port_lanstream.h -- a browser's player's seat on the host: a Hyperion the room
// starts with no window for a player in a browser, which plays the seat as an installed copy plays
// its own and streams it to the browser over WebRTC, taking the browser's mouse and keys back.
// Presentation and transport only: the seat's game is any seat's.
#ifndef PORT_LANSTREAM_H
#define PORT_LANSTREAM_H

struct PortJson;

// The room's "rtc" for the seat: the browser's answer to its offer, or the browser asking for a
// new offer (its page opened again).
void LanStreamSignal(const PortJson &j);
// Each pass of the room's client (port_lanclient.cpp), bIn once the room has let the seat in: the
// peer's events, the offer, and the browser's input into the window's messages.
void LanStreamPump(bool bIn);
// Whether a frame is wanted now (a browser watching, and its time come), and the frame as shown:
// 8 bits a pixel, a row nPitch bytes, and the palette's 256 colours of 4 bytes, r g b a
// (port_present.cpp).
bool LanStreamWants(void);
// The seat's game has ended (port_lanlobby.cpp): 1.17's score screen comes, which the browser's
// player continues, or the seat does when they have left the stream; and it has been continued.
void LanStreamGameEnded(void);
void LanStreamScoreDone(void);
void LanStreamFrame(const unsigned char *pPixels, int nWidth, int nHeight, int nPitch, const unsigned char *pPalette);

#endif
