// port_json.cpp -- the JSON reader port_json.h describes: recursive descent over the
// text, as RFC 8259 has it, with \u escapes turned into UTF-8 (a surrogate pair into one
// code point); and the writers, PortJsonQuote and PortJsonWrite.
#include "port_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const PortJson kNone;

class Reader {
public:
  explicit Reader(const char *p) : m_p(p), m_start(p) {}

  bool Document(PortJson *pOut, std::string *pszError) {
    Space();
    bool bOk = Value(pOut, 0);
    Space();
    if (bOk && *m_p != '\0')
      bOk = Fail("text after the value");
    if (!bOk && pszError != nullptr)
      *pszError = m_error + " at byte " + std::to_string(m_p - m_start);
    return bOk;
  }

private:
  const char *m_p;
  const char *m_start;
  std::string m_error;

  bool Fail(const char *psz) {
    if (m_error.empty())
      m_error = psz;
    return false;
  }

  void Space() {
    while (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r')
      ++m_p;
  }

  bool Word(const char *pszWord) {
    const size_t n = strlen(pszWord);
    if (strncmp(m_p, pszWord, n) != 0)
      return Fail("an unknown word");
    m_p += n;
    return true;
  }

  bool Value(PortJson *pOut, int nDepth) {
    if (nDepth > 64)
      return Fail("too deep");
    switch (*m_p) {
      case '{': return Object(pOut, nDepth);
      case '[': return Array(pOut, nDepth);
      case '"': pOut->kind = PortJson::STRING; return String(&pOut->s);
      case 't': pOut->kind = PortJson::BOOL; pOut->b = true; return Word("true");
      case 'f': pOut->kind = PortJson::BOOL; pOut->b = false; return Word("false");
      case 'n': pOut->kind = PortJson::NULLV; return Word("null");
      default: return Number(pOut);
    }
  }

  bool Number(PortJson *pOut) {
    const char *p = m_p;
    if (*p == '-')
      ++p;
    if (*p < '0' || *p > '9')
      return Fail("not a value");
    char *pEnd = nullptr;
    pOut->kind = PortJson::NUMBER;
    pOut->d = strtod(m_p, &pEnd);
    m_p = pEnd;
    return true;
  }

  static void Utf8(unsigned c, std::string *ps) {
    if (c < 0x80) {
      *ps += static_cast<char>(c);
    } else if (c < 0x800) {
      *ps += static_cast<char>(0xC0 | (c >> 6));
      *ps += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      *ps += static_cast<char>(0xE0 | (c >> 12));
      *ps += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      *ps += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      *ps += static_cast<char>(0xF0 | (c >> 18));
      *ps += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      *ps += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      *ps += static_cast<char>(0x80 | (c & 0x3F));
    }
  }

  bool Hex4(unsigned *pc) {
    *pc = 0;
    for (int i = 0; i < 4; ++i, ++m_p) {
      const char c = *m_p;
      const unsigned v = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 16;
      if (v == 16)
        return Fail("a bad \\u escape");
      *pc = *pc * 16 + v;
    }
    return true;
  }

  bool String(std::string *ps) {
    ++m_p;  // the opening quote
    for (;;) {
      const char c = *m_p++;
      if (c == '"')
        return true;
      if (c == '\0' || static_cast<unsigned char>(c) < 0x20)
        return Fail("a string not closed");
      if (c != '\\') {
        *ps += c;
        continue;
      }
      const char e = *m_p++;
      switch (e) {
        case '"': case '\\': case '/': *ps += e; break;
        case 'b': *ps += '\b'; break;
        case 'f': *ps += '\f'; break;
        case 'n': *ps += '\n'; break;
        case 'r': *ps += '\r'; break;
        case 't': *ps += '\t'; break;
        case 'u': {
          unsigned c1;
          if (!Hex4(&c1))
            return false;
          if (c1 >= 0xD800 && c1 < 0xDC00 && m_p[0] == '\\' && m_p[1] == 'u') {
            m_p += 2;
            unsigned c2;
            if (!Hex4(&c2))
              return false;
            if (c2 >= 0xDC00 && c2 < 0xE000) {
              c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
            } else {
              // the low half of the pair is not there: the high half stands for the
              // replacement character and what followed is its own character
              Utf8(0xFFFD, ps);
              c1 = c2 >= 0xD800 && c2 < 0xE000 ? 0xFFFD : c2;
            }
          } else if (c1 >= 0xD800 && c1 < 0xE000) {
            c1 = 0xFFFD;  // a surrogate on its own, which no code point is
          }
          Utf8(c1, ps);
          break;
        }
        default: return Fail("a bad escape");
      }
    }
  }

  bool Array(PortJson *pOut, int nDepth) {
    pOut->kind = PortJson::ARRAY;
    ++m_p;
    Space();
    if (*m_p == ']') {
      ++m_p;
      return true;
    }
    for (;;) {
      pOut->items.emplace_back();
      Space();
      if (!Value(&pOut->items.back(), nDepth + 1))
        return false;
      Space();
      if (*m_p == ',') {
        ++m_p;
        continue;
      }
      if (*m_p == ']') {
        ++m_p;
        return true;
      }
      return Fail("an array not closed");
    }
  }

  bool Object(PortJson *pOut, int nDepth) {
    pOut->kind = PortJson::OBJECT;
    ++m_p;
    Space();
    if (*m_p == '}') {
      ++m_p;
      return true;
    }
    for (;;) {
      Space();
      if (*m_p != '"')
        return Fail("a member without a name");
      pOut->members.emplace_back();
      if (!String(&pOut->members.back().first))
        return false;
      Space();
      if (*m_p++ != ':')
        return Fail("a member without a colon");
      Space();
      if (!Value(&pOut->members.back().second, nDepth + 1))
        return false;
      Space();
      if (*m_p == ',') {
        ++m_p;
        continue;
      }
      if (*m_p == '}') {
        ++m_p;
        return true;
      }
      return Fail("an object not closed");
    }
  }
};

}  // namespace

const PortJson &PortJson::operator[](const char *pszName) const {
  if (kind == OBJECT)
    for (const auto &m : members)
      if (m.first == pszName)
        return m.second;
  return kNone;
}

const PortJson &PortJson::At(size_t i) const {
  return kind == ARRAY && i < items.size() ? items[i] : kNone;
}

bool PortJsonParse(const char *pszText, PortJson *pOut, std::string *pszError) {
  *pOut = PortJson();
  // a byte order mark, which Windows editors write
  if (static_cast<unsigned char>(pszText[0]) == 0xEF && static_cast<unsigned char>(pszText[1]) == 0xBB &&
      static_cast<unsigned char>(pszText[2]) == 0xBF)
    pszText += 3;
  return Reader(pszText).Document(pOut, pszError);
}

// The length of the valid UTF-8 character at p, 0 when its bytes are not one.
static size_t Utf8Length(const unsigned char *p, size_t cbLeft) {
  const unsigned char b = p[0];
  const size_t cb = b >= 0xC2 && b < 0xE0 ? 2 : b >= 0xE0 && b < 0xF0 ? 3 : b >= 0xF0 && b <= 0xF4 ? 4 : 0;
  if (cb == 0 || cb > cbLeft)
    return 0;
  for (size_t i = 1; i < cb; ++i)
    if ((p[i] & 0xC0) != 0x80)
      return 0;
  return cb;
}

std::string PortJsonUtf8(const std::string &s) {
  static const unsigned short k1252High[32] = {
      0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
      0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
      0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
  };
  std::string out;
  out.reserve(s.size());
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data());
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = p[i];
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (const size_t cb = Utf8Length(p + i, s.size() - i)) {
      out.append(s, i, cb);
      i += cb - 1;
    } else {
      const unsigned cp = c < 0xA0 ? k1252High[c - 0x80] : c;
      if (cp < 0x800) {
        out += static_cast<char>(0xC0 | cp >> 6);
      } else {
        out += static_cast<char>(0xE0 | cp >> 12);
        out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
      }
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

std::string PortJsonQuote(const std::string &s) {
  std::string out = "\"";
  out.reserve(s.size() + 2);
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char sz[8];
      snprintf(sz, sizeof sz, "\\u%04x", c);
      out += sz;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

std::string PortJsonWrite(const PortJson &v) {
  switch (v.kind) {
    case PortJson::BOOL: return v.b ? "true" : "false";
    case PortJson::NUMBER: {
      char sz[40];
      snprintf(sz, sizeof sz, "%.17g", v.d);
      return sz;
    }
    case PortJson::STRING: return PortJsonQuote(v.s);
    case PortJson::ARRAY: {
      std::string s = "[";
      for (size_t i = 0; i < v.items.size(); ++i)
        s += (i ? "," : "") + PortJsonWrite(v.items[i]);
      return s + "]";
    }
    case PortJson::OBJECT: {
      std::string s = "{";
      for (size_t i = 0; i < v.members.size(); ++i)
        s += (i ? "," : "") + PortJsonQuote(v.members[i].first) + ":" + PortJsonWrite(v.members[i].second);
      return s + "}";
    }
    default: return "null";
  }
}
