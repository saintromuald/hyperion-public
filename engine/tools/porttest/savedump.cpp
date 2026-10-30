// savedump.cpp -- splits a saved game into its pieces, for comparing saves
//.
//
//   savedump <save file> <out dir>
//
// A save is a text banner ending in a Ctrl-Z, then what the subsystems write, in
// order: raw fields and compressed chunks (compress.cpp: a CRC, a block count, then
// each block's length and bytes, imploded or stored). The dump finds each chunk
// without knowing the order, by reading one at every position and keeping it when
// its CRC holds; what lies between two chunks is a raw item. It writes
// <out dir>/items.txt, one line per item, and each chunk's contents as NNN.bin, so
// two saves from two builds compare item by item, whatever their compressors wrote.
// A Hyperion save's marker and section headers are items of their own, which the
// comparer passes over. Explode is StormLib's pklib, which the engine uses too
// (port_pklib.cpp).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "pklib.h"

namespace {

// Both callbacks get the one parameter explode passes on, as comp_info_t in
// compress.cpp.
struct Io {
  const unsigned char *p;
  unsigned int n, pos;
  std::vector<unsigned char> *v;
};

unsigned int ReadBuf(char *buf, unsigned int *size, void *param) {
  Io *s = static_cast<Io *>(param);
  unsigned int k = *size < s->n - s->pos ? *size : s->n - s->pos;
  memcpy(buf, s->p + s->pos, k);
  s->pos += k;
  return k;
}

void WriteBuf(char *buf, unsigned int *size, void *param) {
  Io *d = static_cast<Io *>(param);
  if (d->v->size() + *size > 0x2000) return;
  d->v->insert(d->v->end(), buf, buf + *size);
}

// One block imploded, as comp_explode reads it; empty when it does not explode.
std::vector<unsigned char> Explode(const unsigned char *p, unsigned int n) {
  std::vector<unsigned char> out;
  static std::vector<char> work(EXP_BUFFER_SIZE);
  Io s = {p, n, 0, &out};
  if (explode(ReadBuf, WriteBuf, work.data(), &s) != CMP_NO_ERROR) out.clear();
  return out;
}

unsigned int Crc(const std::vector<unsigned char> &v) {
  unsigned int n = static_cast<unsigned int>(v.size());
  unsigned long c = 0xFFFFFFFFul;
  return static_cast<unsigned int>(crc32_pklib(reinterpret_cast<char *>(const_cast<unsigned char *>(v.data())), &n, &c));
}

// A chunk at pos: its contents and its length in the file, or false. Every block
// but the last is 0x2000 bytes of output, stored when its length is 0x2000; the
// last one's size is not in the file, so it is read both ways and the CRC decides.
bool ReadChunk(const std::vector<unsigned char> &f, size_t pos, std::vector<unsigned char> *pOut, size_t *pLen) {
  if (pos + 8 > f.size()) return false;
  unsigned int crc, blocks;
  memcpy(&crc, &f[pos], 4);
  memcpy(&blocks, &f[pos + 4], 4);
  if (blocks == 0 || blocks > 0x1000) return false;
  size_t at = pos + 8;
  std::vector<unsigned char> out;
  for (unsigned int i = 0; i < blocks; ++i) {
    if (at + 4 > f.size()) return false;
    unsigned int len;
    memcpy(&len, &f[at], 4);
    at += 4;
    if (len == 0 || len > 0x2000 || at + len > f.size()) return false;
    const unsigned char *b = &f[at];
    at += len;
    if (i + 1 < blocks) {
      if (len == 0x2000) {
        out.insert(out.end(), b, b + len);
      } else {
        std::vector<unsigned char> e = Explode(b, len);
        if (e.size() != 0x2000) return false;
        out.insert(out.end(), e.begin(), e.end());
      }
    } else {
      std::vector<unsigned char> e = Explode(b, len);
      std::vector<unsigned char> a = out;
      a.insert(a.end(), e.begin(), e.end());
      if (!e.empty() && Crc(a) == crc) {
        out.swap(a);
      } else {
        out.insert(out.end(), b, b + len);
        if (Crc(out) != crc) return false;
      }
    }
  }
  pOut->swap(out);
  *pLen = at - pos;
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: savedump <save> <out dir>\n");
    return 2;
  }
  FILE *fp = fopen(argv[1], "rb");
  if (!fp) {
    fprintf(stderr, "savedump: cannot open %s\n", argv[1]);
    return 1;
  }
  std::vector<unsigned char> f;
  unsigned char buf[65536];
  size_t k;
  while ((k = fread(buf, 1, sizeof buf, fp)) > 0) f.insert(f.end(), buf, buf + k);
  fclose(fp);

  std::string dir = argv[2];
  std::string items = dir + "\\items.txt";
  FILE *out = fopen(items.c_str(), "w");
  if (!out) {
    fprintf(stderr, "savedump: cannot write %s\n", items.c_str());
    return 1;
  }
  size_t pos = 0;
  while (pos < f.size() && f[pos] != 0x1A) ++pos;
  fprintf(out, "banner %zu\n", pos);
  ++pos;
  int nChunk = 0;
  size_t rawStart = pos;
  auto flushRaw = [&](size_t end) {
    if (end <= rawStart) return;
    fprintf(out, "raw %zu ", end - rawStart);
    for (size_t i = rawStart; i < end; ++i) fprintf(out, "%02X", f[i]);
    fprintf(out, "\n");
  };
  // Hyperion's container (port_formats.cpp): after the tag and the header, a
  // marker and then sections, each a 20-byte header before what 1.17 writes for
  // that part. Their items are dumped as a 1.17 save's are, so the two compare.
  size_t sectionEnd = f.size();
  bool hyperion = false;
  while (pos < f.size()) {
    if (hyperion && pos == sectionEnd && pos + 20 <= f.size()) {
      flushRaw(pos);
      unsigned int h[5];
      memcpy(h, &f[pos], 20);
      fprintf(out, "section %.4s %u %u %u %u\n", reinterpret_cast<const char *>(&h[0]), h[1] & 0xFFFF, h[2],
              h[3], h[4]);
      pos += 20;
      sectionEnd = pos + h[4];
      rawStart = pos;
      continue;
    }
    if (nChunk == 1 && !hyperion && pos + 12 <= f.size() && memcmp(&f[pos], "HYPERION", 8) == 0) {
      flushRaw(pos);
      unsigned int v;
      memcpy(&v, &f[pos + 8], 4);
      fprintf(out, "hyperion %u\n", v);
      hyperion = true;
      pos += 12;
      sectionEnd = pos;
      rawStart = pos;
      continue;
    }
    std::vector<unsigned char> c;
    size_t len;
    if (ReadChunk(f, pos, &c, &len) && pos + len <= sectionEnd) {
      flushRaw(pos);
      char name[32];
      snprintf(name, sizeof name, "%03d.bin", nChunk);
      FILE *cf = fopen((dir + "\\" + name).c_str(), "wb");
      if (cf) {
        fwrite(c.data(), 1, c.size(), cf);
        fclose(cf);
      }
      fprintf(out, "chunk %d %zu %08X\n", nChunk, c.size(), Crc(c));
      ++nChunk;
      pos += len;
      rawStart = pos;
    } else {
      ++pos;
    }
    if (hyperion && pos == sectionEnd) {
      flushRaw(pos);
      rawStart = pos;
    }
  }
  flushRaw(f.size());
  fclose(out);
  printf("savedump: %d chunks -> %s\n", nChunk, items.c_str());
  return 0;
}
