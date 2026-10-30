// port_zip.cpp -- a zip read in memory (port_zip.h).
#include <string.h>
#include "port_zip.h"
#include "../../../third_party/StormLib-9.40/src/zlib/zlib.h"

namespace {

uint32_t Le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t Le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

}  // namespace

bool PortZipDirectory(const std::vector<uint8_t> &zip, std::vector<PortZipEntry> *pOut) {
  if (zip.size() < 22)
    return false;
  // the end record, searched from the end past any comment
  size_t at = zip.size() - 22;
  for (;;) {
    if (Le32(&zip[at]) == 0x06054B50)
      break;
    if (at == 0 || zip.size() - at > 22 + 0xFFFF)
      return false;
    --at;
  }
  const uint32_t nEntries = Le16(&zip[at + 10]);
  size_t p = Le32(&zip[at + 16]);
  for (uint32_t i = 0; i < nEntries; ++i) {
    // sizes are compared by subtraction: size_t is 32 bits in the browser build, where a sum wraps
    if (p > zip.size() || zip.size() - p < 46 || Le32(&zip[p]) != 0x02014B50)
      return false;
    PortZipEntry e;
    e.wMethod = Le16(&zip[p + 10]);
    e.cbPacked = Le32(&zip[p + 20]);
    e.cbSize = Le32(&zip[p + 24]);
    const uint16_t cbName = Le16(&zip[p + 28]), cbExtra = Le16(&zip[p + 30]), cbComment = Le16(&zip[p + 32]);
    e.dwLocal = Le32(&zip[p + 42]);
    if (zip.size() - p - 46 < cbName)
      return false;
    e.name.assign(reinterpret_cast<const char *>(&zip[p + 46]), cbName);
    pOut->push_back(e);
    p += 46 + cbName + cbExtra + cbComment;
  }
  return true;
}

bool PortZipRead(const std::vector<uint8_t> &zip, const PortZipEntry &e, std::vector<uint8_t> *pOut) {
  // compared by subtraction, as in PortZipDirectory: an offset or size near 4 GB wraps a sum
  const size_t local = e.dwLocal;
  if (local > zip.size() || zip.size() - local < 30 || Le32(&zip[local]) != 0x04034B50)
    return false;
  const size_t data = local + 30 + Le16(&zip[local + 26]) + Le16(&zip[local + 28]);
  if (data > zip.size() || e.cbPacked > zip.size() - data)
    return false;
  if (e.cbSize > PORT_ZIP_ENTRY_MAX)  // the size is the file's word, read before a byte is inflated
    return false;
  if (e.wMethod == 0) {
    pOut->assign(zip.begin() + data, zip.begin() + data + e.cbPacked);
    return e.cbPacked == e.cbSize;
  }
  if (e.wMethod != 8)
    return false;
  pOut->resize(e.cbSize);
  z_stream z;
  memset(&z, 0, sizeof z);
  if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
    return false;
  Bytef bEmpty;  // zlib refuses a null buffer, even for an empty entry
  z.next_in = const_cast<Bytef *>(e.cbPacked ? &zip[data] : &bEmpty);
  z.avail_in = e.cbPacked;
  z.next_out = pOut->empty() ? &bEmpty : &(*pOut)[0];
  z.avail_out = e.cbSize;
  const int r = inflate(&z, Z_FINISH);
  inflateEnd(&z);
  return r == Z_STREAM_END && z.total_out == e.cbSize;
}
