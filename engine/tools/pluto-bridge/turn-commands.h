#pragma once

// Binary-safe classic StarCraft turn-command framing shared by the host and
// native tests.  Return the complete command size, zero for a truncated tail,
// or -1 for an opcode that Hyperion's in-game dispatcher does not accept.

namespace pluto_bridge {

inline int TurnCommandSize(const unsigned char *p, unsigned left) {
  const unsigned char op = p[0];
  if (op == 0x09 || op == 0x0A || op == 0x0B) {
    if (left < 2)
      return 0;
    if (p[1] > 12)
      return -1;
    const int size = 2 + p[1] * 2;
    return left < static_cast<unsigned>(size) ? 0 : size;
  }
  int size = -1;
  switch (op) {
    case 0x08: case 0x10: case 0x11: case 0x18: case 0x19:
    case 0x1B: case 0x1C: case 0x27: case 0x2A: case 0x2E:
    case 0x31: case 0x33: case 0x34: case 0x36: case 0x5A:
      size = 1; break;
    case 0x1A: case 0x1E: case 0x21: case 0x22: case 0x25:
    case 0x26: case 0x28: case 0x2B: case 0x2C: case 0x2D:
    case 0x30: case 0x32: case 0x57:
      size = 2; break;
    case 0x0D: case 0x1F: case 0x20: case 0x23: case 0x29:
    case 0x35:
      size = 3; break;
    case 0x0E: case 0x12: case 0x2F: case 0x58:
      size = 5; break;
    case 0x0C:
      size = 8; break;
    case 0x14:
      size = 10; break;
    case 0x15:
      size = 11; break;
    default:
      // 0x3C, 0x41, 0x44, and 0x3E are lobby-only in Hyperion.  Accepting them
      // here would desynchronize the real in-game receiver's command stream.
      return -1;
  }
  return left < static_cast<unsigned>(size) ? 0 : size;
}

}  // namespace pluto_bridge
