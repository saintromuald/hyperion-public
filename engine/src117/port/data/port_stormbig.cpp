// port_stormbig.cpp -- Storm's big numbers, the port's own. 1.17 uses them for
// one thing: signature.cpp raises a map's signature to
// Blizzard's public exponent modulo its key, to tell the maps Blizzard signed.
//
// A number is kept as Storm keeps it, in 32-bit words, least significant first.
// SBigFromBinary keeps a word for every four bytes it is given, zero words at the
// top included, and SBigToBinaryBuffer writes every word but the last whole and
// the last one's bytes up to its highest that is not zero; a result of SBigPowMod
// has no zero word at the top, so zero is no bytes at all. Found by trying
// storm.dll's; tools/porttest/big_test.cpp compares the two.
#include <windows.h>
#include <string.h>
#include <vector>

typedef std::vector<DWORD> Words;

struct BigNum {
  Words w;
};

static void Trim(Words &a)
{
  while (!a.empty() && a.back() == 0)
    a.pop_back();
}

static int Compare(const Words &a, const Words &b)
{
  if (a.size() != b.size())
    return a.size() < b.size() ? -1 : 1;
  for (size_t i = a.size(); i-- > 0;)
    if (a[i] != b[i])
      return a[i] < b[i] ? -1 : 1;
  return 0;
}

static Words Multiply(const Words &a, const Words &b)
{
  Words r(a.size() + b.size(), 0);
  for (size_t i = 0; i < a.size(); ++i) {
    unsigned __int64 carry = 0;
    for (size_t j = 0; j < b.size(); ++j) {
      unsigned __int64 t = static_cast<unsigned __int64>(a[i]) * b[j] + r[i + j] + carry;
      r[i + j] = static_cast<DWORD>(t);
      carry = t >> 32;
    }
    r[i + b.size()] = static_cast<DWORD>(carry);
  }
  Trim(r);
  return r;
}

static int LeadingZeros(DWORD x)
{
  int n = 0;
  while (!(x & 0x80000000u)) {
    x <<= 1;
    ++n;
  }
  return n;
}

// a mod m, both without zero words at the top: long division in 32-bit digits
// (Knuth's algorithm D), keeping only the remainder.
static Words Remainder(const Words &a, const Words &m)
{
  // A modulus of zero is no key the game has; the number stays as it is.
  if (m.empty() || Compare(a, m) < 0)
    return a;
  const size_t n = m.size();
  if (n == 1) {
    unsigned __int64 r = 0;
    for (size_t i = a.size(); i-- > 0;)
      r = ((r << 32) | a[i]) % m[0];
    Words out(1, static_cast<DWORD>(r));
    Trim(out);
    return out;
  }
  // Shift both so the divisor's top word has its top bit set.
  const int s = LeadingZeros(m.back());
  Words v(n), u(a.size() + 1);
  for (size_t i = n; i-- > 0;)
    v[i] = (m[i] << s) | (s && i ? m[i - 1] >> (32 - s) : 0);
  u[a.size()] = s ? a.back() >> (32 - s) : 0;
  for (size_t i = a.size(); i-- > 0;)
    u[i] = (a[i] << s) | (s && i ? a[i - 1] >> (32 - s) : 0);

  const unsigned __int64 base = 1ull << 32;
  for (size_t j = a.size() - n + 1; j-- > 0;) {
    const unsigned __int64 top = (static_cast<unsigned __int64>(u[j + n]) << 32) | u[j + n - 1];
    unsigned __int64 qhat = top / v[n - 1];
    unsigned __int64 rhat = top % v[n - 1];
    while (qhat >= base || qhat * v[n - 2] > ((rhat << 32) | u[j + n - 2])) {
      --qhat;
      rhat += v[n - 1];
      if (rhat >= base)
        break;
    }
    // u[j..j+n] -= qhat * v
    __int64 borrow = 0;
    unsigned __int64 carry = 0;
    for (size_t i = 0; i < n; ++i) {
      const unsigned __int64 p = qhat * v[i] + carry;
      carry = p >> 32;
      const __int64 t = static_cast<__int64>(u[i + j]) - borrow - static_cast<__int64>(p & 0xFFFFFFFFu);
      u[i + j] = static_cast<DWORD>(t);
      borrow = t < 0 ? 1 : 0;
    }
    const __int64 t = static_cast<__int64>(u[j + n]) - borrow - static_cast<__int64>(carry);
    u[j + n] = static_cast<DWORD>(t);
    if (t < 0) {
      // qhat was one too many: add v back.
      unsigned __int64 c = 0;
      for (size_t i = 0; i < n; ++i) {
        const unsigned __int64 sum = static_cast<unsigned __int64>(u[i + j]) + v[i] + c;
        u[i + j] = static_cast<DWORD>(sum);
        c = sum >> 32;
      }
      u[j + n] += static_cast<DWORD>(c);
    }
  }
  Words r(n);
  for (size_t i = 0; i < n; ++i)
    r[i] = (u[i] >> s) | (s ? u[i + 1] << (32 - s) : 0);
  Trim(r);
  return r;
}

extern "C" {

// Ordinal 624. A new number, zero.
void __stdcall SBigNew(void **ppBig)
{
  *ppBig = new BigNum;
}

// Ordinal 606.
void __stdcall SBigDel(void *pBig)
{
  delete static_cast<BigNum *>(pBig);
}

// Ordinal 609. Bytes, least significant first.
void __stdcall SBigFromBinary(void *pBig, const void *pvData, DWORD cbData)
{
  Words &w = static_cast<BigNum *>(pBig)->w;
  w.assign((cbData + 3) / 4, 0);
  if (cbData)
    memcpy(&w[0], pvData, cbData);
}

// Ordinal 638. As many of the number's bytes as fit, least significant first.
void __stdcall SBigToBinaryBuffer(void *pBig, void *pvBuffer, DWORD cbBuffer, DWORD *pcbWritten)
{
  const Words &w = static_cast<BigNum *>(pBig)->w;
  DWORD cb = 0;
  if (!w.empty()) {
    DWORD top = w.back();
    cb = static_cast<DWORD>(w.size() - 1) * 4;
    while (top) {
      ++cb;
      top >>= 8;
    }
  }
  if (cb > cbBuffer)
    cb = cbBuffer;
  if (cb)
    memcpy(pvBuffer, &w[0], cb);
  *pcbWritten = cb;
}

// Ordinal 628. base to the power exp, modulo mod, into the result, which may be
// the base. A power of 0 is 1, whatever the modulus, as in Storm.
void __stdcall SBigPowMod(void *pResult, void *pBase, void *pExp, void *pMod)
{
  Words base = static_cast<BigNum *>(pBase)->w, exp = static_cast<BigNum *>(pExp)->w,
        mod = static_cast<BigNum *>(pMod)->w;
  Trim(base);
  Trim(exp);
  Trim(mod);
  Words r(1, 1);
  if (!exp.empty()) {
    base = Remainder(base, mod);
    for (size_t i = exp.size(); i-- > 0;) {
      for (int bit = 31; bit >= 0; --bit) {
        r = Remainder(Multiply(r, r), mod);
        if (exp[i] >> bit & 1)
          r = Remainder(Multiply(r, base), mod);
      }
    }
  }
  static_cast<BigNum *>(pResult)->w = r;
}

}  // extern "C"
