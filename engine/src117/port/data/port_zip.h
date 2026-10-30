// port_zip.h -- a zip read in memory: its central directory, and each entry stored or
// deflated. Hyperion's .hxm maps (port_hxm.cpp) and .hud packages (port_hud.cpp) are zips.
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

struct PortZipEntry {
  std::string name;
  uint16_t wMethod;
  uint32_t cbPacked, cbSize, dwLocal;
};

// The most an entry unpacks to.
const uint32_t PORT_ZIP_ENTRY_MAX = 256u << 20;

// The entries of the zip in zip, or false when it is not one.
bool PortZipDirectory(const std::vector<uint8_t> &zip, std::vector<PortZipEntry> *pOut);
// An entry's bytes: false for a method other than stored or deflated, an entry larger than
// PORT_ZIP_ENTRY_MAX, or bytes that do not unpack to its size.
bool PortZipRead(const std::vector<uint8_t> &zip, const PortZipEntry &e, std::vector<uint8_t> *pOut);
