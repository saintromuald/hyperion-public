// crc32.cpp -- the table-driven CRC-32, zlib's eight-way unrolled loop. Bodies are
// in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/crc32.h"
#include "../h/SystemSurvey.h"    // crc32
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The 256-entry CRC table in .rdata.
#ifdef OC_DLL_MODE
#define CRC32_m_tab (reinterpret_cast<const unsigned int *>(OC_VA(0x4F0BC0u)))
#else
extern const unsigned int CRC32_m_tab[256];
#endif

#define CRC_DO1(p) nCrc = CRC32_m_tab[(nCrc ^ *p++) & 0xFF] ^ (nCrc >> 8)
#define CRC_DO2(p) CRC_DO1(p); CRC_DO1(p)
#define CRC_DO4(p) CRC_DO2(p); CRC_DO2(p)
#define CRC_DO8(p) CRC_DO4(p); CRC_DO4(p)

// The bodies, in oracle VA order.

// Folds nLen bytes into a running CRC-32; a null buffer returns 0.
unsigned int crc32(unsigned int nCrc, const void *pBuf, unsigned int nLen) {
  const unsigned char *p = static_cast<const unsigned char *>(pBuf);
  if (p == NULL)
    return 0;
  nCrc = ~nCrc;
  while (nLen >= 8) {
    CRC_DO8(p);
    nLen -= 8;
  }
  if (nLen) {
    do {
      CRC_DO1(p);
    } while (--nLen);
  }
  return ~nCrc;
}
