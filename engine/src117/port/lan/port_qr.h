// port_qr.h -- a QR code for a short text, so a phone joins a LAN room by pointing its camera at
// the host's console (the design center's qr.js). Presentation only.
#ifndef PORT_QR_H
#define PORT_QR_H

#include <string>
#include <vector>

// The code's modules, a row of booleans a line, true dark, without the quiet zone: byte mode,
// error correction L, versions 1 to 5 (up to 106 bytes), with the mask that scores best under
// the standard's four penalty rules. Empty when the text is too long.
std::vector<std::vector<bool>> QrMatrix(const std::string &text);

#endif
