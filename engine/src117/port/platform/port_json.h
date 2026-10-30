// port_json.h -- the port's JSON: a small reader for Hyperion's own files and messages (a
// mod's mod.json, the live channel's and the LAN room's messages, settings and sessions), and
// the one way a string is written into the JSON the port writes. The reader takes UTF-8 text
// into a tree of values. Numbers are doubles, objects keep their members in order, and a
// lookup that finds nothing gives an empty value of kind NONE, so a chain of lookups needs no
// checks on the way.
#ifndef PORT_JSON_H
#define PORT_JSON_H

#include <string>
#include <utility>
#include <vector>

struct PortJsonMember;

struct PortJson {
  enum Kind { NONE, NULLV, BOOL, NUMBER, STRING, ARRAY, OBJECT };
  Kind kind = NONE;
  bool b = false;
  double d = 0.0;
  std::string s;
  std::vector<PortJson> items;                             // an array's
  std::vector<PortJsonMember> members;  // an object's, as name (first) and value (second)

  // A member of an object by name, an item of an array by place, or NONE.
  const PortJson &operator[](const char *pszName) const;
  const PortJson &At(size_t i) const;
  size_t size() const;

  // The value as a string or number, or pszDefault / dDefault when it is not one.
  std::string Str(const char *pszDefault = "") const { return kind == STRING ? s : pszDefault; }
  double Num(double dDefault = 0.0) const { return kind == NUMBER ? d : dDefault; }
};

struct PortJsonMember {
  std::string first;
  PortJson second;
};

inline size_t PortJson::size() const { return kind == ARRAY ? items.size() : kind == OBJECT ? members.size() : 0; }

// Parses pszText. False, with *pszError saying where, when it is not JSON.
bool PortJsonParse(const char *pszText, PortJson *pOut, std::string *pszError = nullptr);

// s as a JSON string, quoted: " and \ escaped, a control character as \u00XX, every other
// byte (UTF-8's included) as it is. Every string the port writes into JSON goes through it.
std::string PortJsonQuote(const std::string &s);

// Text whose bytes that begin no UTF-8 character are the game's Windows-1252 (a classic map's
// name, a name cut inside a character), each written as its character, so that it is UTF-8.
std::string PortJsonUtf8(const std::string &s);

// A value as JSON text, on one line: what PortJsonParse reads back as the same tree (a number
// to 17 digits, members in their order; NONE is written as null).
std::string PortJsonWrite(const PortJson &v);

#endif
