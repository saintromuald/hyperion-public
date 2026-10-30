// pos_test.cpp -- the position types of src117/h/pos.h against 1.17's 16-bit fields
// (stage E).
//
// While g_posShift is 16, an spos_t must behave as a short and a upos_t as a WORD: every
// store, compound operator, increment and conversion, and a value stored through one
// read back through the other, as CFlingy's and CUnit's views of one field do. At 0 they
// keep the whole value. Prints nothing but the summary when they agree.
//
//   src117\build\x64\porttest\pos_test.exe
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pos.h"

int g_posShift = 16;
int g_posMaskU = 0xFFFF;

static int g_failures;

static void Expect(bool ok, const char *what, int n, int m, int got, int want) {
  if (ok)
    return;
  if (++g_failures <= 20)
    printf("  %s: n=%d m=%d: %d, 1.17 has %d\n", what, n, m, got, want);
}

// Every operator once, on an spos_t and a short, and on a upos_t and a WORD.
static void Wrapped(int n, int m) {
  {
    spos_t a(n);
    short b = static_cast<short>(n);
    Expect(a == b, "spos_t store", n, m, a, b);
    a += m; b += static_cast<short>(m);
    Expect(a == b, "spos_t +=", n, m, a, b);
    a -= m; b -= static_cast<short>(m);
    Expect(a == b, "spos_t -=", n, m, a, b);
    a *= 3; b *= 3;
    Expect(a == b, "spos_t *=", n, m, a, b);
    a <<= 2; b <<= 2;
    Expect(a == b, "spos_t <<=", n, m, a, b);
    a >>= 1; b >>= 1;
    Expect(a == b, "spos_t >>=", n, m, a, b);
    a &= m; b &= m;
    Expect(a == b, "spos_t &=", n, m, a, b);
    a |= m; b |= m;
    Expect(a == b, "spos_t |=", n, m, a, b);
    a ^= n; b ^= n;
    Expect(a == b, "spos_t ^=", n, m, a, b);
    if (m != 0) {
      a /= m; b /= m;
      Expect(a == b, "spos_t /=", n, m, a, b);
      a = n; b = static_cast<short>(n);
      a %= m; b %= m;
      Expect(a == b, "spos_t %=", n, m, a, b);
    }
    a = n; b = static_cast<short>(n);
    int x = a++, y = b++;
    Expect(x == y && a == b, "spos_t ++", n, m, a, b);
    x = a--; y = b--;
    Expect(x == y && a == b, "spos_t --", n, m, a, b);
    ++a; ++b;
    --a; --b;
    Expect(a == b, "spos_t ++ --", n, m, a, b);
    // the same bytes read as the other view, CUnit's WORD of CFlingy's short
    upos_t u;
    memcpy(&u, &a, sizeof u);
    Expect(u == static_cast<WORD>(b), "spos_t read as upos_t", n, m, u, static_cast<WORD>(b));
  }
  {
    upos_t a(n);
    WORD b = static_cast<WORD>(n);
    Expect(a == b, "upos_t store", n, m, a, b);
    a += m; b += static_cast<WORD>(m);
    Expect(a == b, "upos_t +=", n, m, a, b);
    a -= m; b -= static_cast<WORD>(m);
    Expect(a == b, "upos_t -=", n, m, a, b);
    a *= 3; b *= 3;
    Expect(a == b, "upos_t *=", n, m, a, b);
    a >>= 1; b >>= 1;
    Expect(a == b, "upos_t >>=", n, m, a, b);
    a = n; b = static_cast<WORD>(n);
    int x = a--, y = b--;
    Expect(x == y && a == b, "upos_t --", n, m, a, b);
    spos_t s;
    memcpy(&s, &a, sizeof s);
    Expect(s == static_cast<short>(b), "upos_t read as spos_t", n, m, s, static_cast<short>(b));
  }
}

int main() {
  int nChecked = 0;
  // every 16-bit value against a spread of second operands, then values past 16 bits
  const int aOther[] = {0, 1, -1, 2, 7, -9, 31, 32, 255, 256, 4095, -4096, 32767, -32768, 65535, 70000};
  for (int n = -32768; n <= 65535; n++)
    for (int m : aOther) {
      Wrapped(n, m);
      nChecked++;
    }
  srand(1117);
  for (int i = 0; i < 200000; i++) {
    const int n = (rand() << 16 ^ rand()) - 0x40000000;
    const int m = (rand() & 0xFFFF) - 0x8000;
    Wrapped(n, m);
    nChecked++;
  }

  // points: from and to 1.17's, and packed into a DWORD and back
  for (int n = -40000; n <= 70000; n += 37) {
    point16_t p;
    p.x = static_cast<short>(n);
    p.y = static_cast<short>(-n);
    const mpoint_t q = p;
    const point16_t r = q;
    Expect(r.x == p.x && r.y == p.y, "mpoint_t round trip", n, 0, r.x, p.x);
    const unsigned long d = PosPack(q.x, q.y);
    Expect(PosPackedX(d) == p.x && PosPackedY(d) == p.y, "PosPack round trip", n, 0, PosPackedX(d), p.x);
    const mpoint_t s = PosPoint(n, -n);
    Expect(s.x == p.x && s.y == p.y, "PosPoint", n, 0, s.x, p.x);
    nChecked++;
  }

  // positions of 31 bits, as a Hyperion game keeps them: an spos_t reads the
  // value, a upos_t its low 31 bits, so a negative one reads as a large one, as a WORD's does
  g_posShift = 1;
  g_posMaskU = 0x7FFFFFFF;
  for (int n = -200000; n <= 200000; n += 13) {
    spos_t a(n);
    upos_t u(n);
    a += 40000;
    u -= 1;
    Expect(a == n + 40000, "spos_t whole", n, 0, a, n + 40000);
    Expect(u == ((n - 1) & 0x7FFFFFFF), "upos_t whole", n, 0, u, (n - 1) & 0x7FFFFFFF);
    const upos_t w = spos_t(n);
    Expect((n < 0) == (w >= 200001), "upos_t rejects a negative", n, 0, w, n);
    Expect(PosU(n) == (n & 0x7FFFFFFF) && PosS(n) == n, "PosU and PosS whole", n, 0, PosU(n), n);
    // a WORD below zero and back, as 1.17 computes (WORD)(y - 30) + 30
    const upos_t down = spos_t(n - 30);
    const spos_t back = down + 30;
    Expect(back == n, "a sum out of range and back", n, 0, back, n);
    nChecked++;
  }
  // the edges of the 31 bits wrap as a short's 16 do
  const spos_t top = 0x3FFFFFFF + 1;
  Expect(top == -0x40000000, "spos_t wraps at 31 bits", 0x40000000, 0, top, -0x40000000);

  printf("pos_test: %d checked, %d failures\n", nChecked, g_failures);
  return g_failures ? 1 : 0;
}
