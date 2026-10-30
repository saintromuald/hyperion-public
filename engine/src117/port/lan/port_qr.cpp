// port_qr.cpp -- a QR code for a LAN room's address; port_qr.h says what it makes. The
// design center's qr.js in C++, which follows Nayuki's QR code generator cut down to one level
// and one block.
#include "port_qr.h"
#include <stdint.h>
#include <stdlib.h>

namespace {

// codewords in all, and of them error correction, one block, level L, versions 1 to 5
const int kTotal[] = {0, 26, 44, 70, 100, 134};
const int kEcc[] = {0, 7, 10, 15, 20, 26};

int Mul(int x, int y)
{
  int z = 0;
  for (int i = 7; i >= 0; --i) {
    z = (z << 1) ^ ((z >> 7) * 0x11d);
    z ^= ((y >> i) & 1) * x;
  }
  return z & 0xff;
}

std::vector<int> Divisor(int n)
{
  std::vector<int> r(n, 0);
  r[n - 1] = 1;
  int root = 1;
  for (int k = 0; k < n; ++k) {
    for (int j = 0; j < n; ++j) {
      r[j] = Mul(r[j], root);
      if (j + 1 < n)
        r[j] ^= r[j + 1];
    }
    root = Mul(root, 2);
  }
  return r;
}

std::vector<int> Remainder(const std::vector<int> &data, const std::vector<int> &div)
{
  std::vector<int> r(div.size(), 0);
  for (int b : data) {
    const int f = b ^ r.front();
    r.erase(r.begin());
    r.push_back(0);
    for (size_t i = 0; i < div.size(); ++i)
      r[i] ^= Mul(div[i], f);
  }
  return r;
}

bool Masked(int k, int x, int y)
{
  switch (k) {
  case 0: return (x + y) % 2 == 0;
  case 1: return y % 2 == 0;
  case 2: return x % 3 == 0;
  case 3: return (x + y) % 3 == 0;
  case 4: return (x / 3 + y / 2) % 2 == 0;
  case 5: return x * y % 2 + x * y % 3 == 0;
  case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
  default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}

struct Grid {
  int size;
  std::vector<std::vector<bool>> m, fn;
  void Set(int x, int y, bool bDark)
  {
    m[y][x] = bDark;
    fn[y][x] = true;
  }
  void Finder(int cx, int cy)
  {
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        const int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy), x = cx + dx, y = cy + dy;
        if (x >= 0 && x < size && y >= 0 && y < size)
          Set(x, y, d != 2 && d != 4);
      }
  }
  void Format(int nMask)
  {
    const int d = (1 << 3) | nMask;  // level L is 01
    int r = d;
    for (int i = 0; i < 10; ++i)
      r = (r << 1) ^ ((r >> 9) * 0x537);
    const int f = ((d << 10) | r) ^ 0x5412;
    auto bit = [f](int i) { return ((f >> i) & 1) != 0; };
    for (int k = 0; k <= 5; ++k)
      Set(8, k, bit(k));
    Set(8, 7, bit(6));
    Set(8, 8, bit(7));
    Set(7, 8, bit(8));
    for (int k = 9; k < 15; ++k)
      Set(14 - k, 8, bit(k));
    for (int k = 0; k < 8; ++k)
      Set(size - 1 - k, 8, bit(k));
    for (int k = 8; k < 15; ++k)
      Set(8, size - 15 + k, bit(k));
    Set(8, size - 8, true);
  }
  void Apply(int k)
  {
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x)
        if (!fn[y][x] && Masked(k, x, y))
          m[y][x] = !m[y][x];
  }
  // the four penalty rules: runs of five or more, 2 x 2 blocks, finder-like patterns, balance
  int Penalty(void) const
  {
    int p = 0, nDark = 0;
    static const char *const kPatterns[] = {"10111010000", "00001011101"};
    for (int nPass = 0; nPass < 2; ++nPass)
      for (int a = 0; a < size; ++a) {
        std::string s;
        int nRun = 1;
        for (int i = 0; i < size; ++i) {
          const bool b = nPass == 0 ? m[a][i] : m[i][a];
          s += b ? '1' : '0';
          if (i > 0 && s[i] == s[i - 1]) {
            ++nRun;
            p += nRun == 5 ? 3 : nRun > 5 ? 1 : 0;
          } else {
            nRun = 1;
          }
        }
        for (const char *pat : kPatterns)
          for (size_t at = s.find(pat); at != std::string::npos; at = s.find(pat, at + 1))
            p += 40;
      }
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        nDark += m[y][x];
        if (y < size - 1 && x < size - 1 && m[y][x] == m[y][x + 1] && m[y][x] == m[y + 1][x] && m[y][x] == m[y + 1][x + 1])
          p += 3;
      }
    p += abs(nDark * 20 - size * size * 10) / (size * size) * 10;
    return p;
  }
};

}  // namespace

std::vector<std::vector<bool>> QrMatrix(const std::string &text)
{
  const int nBytes = static_cast<int>(text.size());
  int ver = 1;
  while (ver <= 5 && 4 + 8 + nBytes * 8 > (kTotal[ver] - kEcc[ver]) * 8)
    ++ver;
  if (ver > 5)
    return {};
  const int nCap = (kTotal[ver] - kEcc[ver]) * 8;
  std::vector<int> bits;
  auto put = [&bits](int v, int n) {
    for (int i = n - 1; i >= 0; --i)
      bits.push_back((v >> i) & 1);
  };
  put(4, 4);
  put(nBytes, 8);
  for (unsigned char b : text)
    put(b, 8);
  put(0, nCap - static_cast<int>(bits.size()) < 4 ? nCap - static_cast<int>(bits.size()) : 4);
  while (bits.size() % 8)
    bits.push_back(0);
  for (int pad = 0xec; static_cast<int>(bits.size()) < nCap; pad ^= 0xec ^ 0x11)
    put(pad, 8);
  std::vector<int> words;
  for (size_t i = 0; i < bits.size(); i += 8) {
    int b = 0;
    for (int j = 0; j < 8; ++j)
      b = (b << 1) | bits[i + j];
    words.push_back(b);
  }
  const std::vector<int> ecc = Remainder(words, Divisor(kEcc[ver]));
  words.insert(words.end(), ecc.begin(), ecc.end());

  Grid g;
  g.size = 17 + 4 * ver;
  g.m.assign(g.size, std::vector<bool>(g.size, false));
  g.fn = g.m;
  for (int t = 0; t < g.size; ++t) {
    g.Set(6, t, t % 2 == 0);
    g.Set(t, 6, t % 2 == 0);
  }
  g.Finder(3, 3);
  g.Finder(g.size - 4, 3);
  g.Finder(3, g.size - 4);
  if (ver > 1) {
    const int a = g.size - 7;
    for (int ay = -2; ay <= 2; ++ay)
      for (int ax = -2; ax <= 2; ++ax)
        g.Set(a + ax, a + ay, (abs(ax) > abs(ay) ? abs(ax) : abs(ay)) != 1);
  }
  g.Format(0);
  // the data, in the zigzag the standard reads it in
  size_t n = 0;
  for (int right = g.size - 1; right >= 1; right -= 2) {
    if (right == 6)
      right = 5;
    for (int v = 0; v < g.size; ++v)
      for (int c = 0; c < 2; ++c) {
        const int x = right - c;
        const bool bUp = ((right + 1) & 2) == 0;
        const int y = bUp ? g.size - 1 - v : v;
        if (!g.fn[y][x] && n < words.size() * 8) {
          g.m[y][x] = ((words[n >> 3] >> (7 - (n & 7))) & 1) != 0;
          ++n;
        }
      }
  }
  int nBest = 0, nBestPenalty = 1 << 30;
  for (int k = 0; k < 8; ++k) {
    g.Apply(k);
    g.Format(k);
    const int p = g.Penalty();
    if (p < nBestPenalty) {
      nBestPenalty = p;
      nBest = k;
    }
    g.Apply(k);
  }
  g.Apply(nBest);
  g.Format(nBest);
  return g.m;
}
