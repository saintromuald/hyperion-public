// port_hxm.cpp -- Hyperion's own map format, .hxm (the format is
// ../../../docs/hxm-format.md, the flow docs/modules/data.md, "The .hxm map format").
//
// The first time a .hxm is opened it is compiled into a scenario.chk and an MPQ in the data
// folder's cache\hxm\, which SFileOpenArchive opens in its place (port_stormfile.cpp). The
// compile is tools/hxmap/convert.py's to_chk written again and must give the same bytes:
// `Hyperion.exe --hxm-compile <in.hxm> <out.chk>` writes what it makes, and
// tools/hxmap/check.py compares it with the tool's for every map it converts.
//
// A map of up to 8 lobby players fits 1.17's sections: 11 player slots and the neutral
// player. In a Hyperion map, whose players past 7 take part, players from index 11 are slots
// 12 and up, units may belong to them, and HXPL carries every slot from 8 (port_players.cpp).
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "port_hxm.h"
#include "port_zip.h"
#include "../platform/port_paths.h"  // PortDataFolder
#include "../platform/port_standalone.h"  // PortGameVcod
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_json.h"
#include "port_hxm_names.h"
#include "port_moddata.h"  // ModTypeByName, a mod's units by name
#include "playerlimits.h"  // PLAYERS_MAX, PLAYER_SLOTS_MAX
#include "maplimits.h"     // MAP_SIDE_MAX
#include "poollimits.h"    // POOL_UNITS_117, POOL_SCALE_MAX
#include "../../../third_party/StormLib-9.40/src/zlib/zlib.h"
#include "port_stormlib.h"  // StormLibWriteArchiveFiles

namespace {

typedef std::vector<BYTE> Bytes;

const char *const CHK_NAME = "staredit\\scenario.chk";
const int NEUTRAL = 11;
const int PLAYERS_CHK = 12;
const int HXM_VERSION = 1;  // map.json's "version" this build reads (docs/hxm-format.md)

// ---------------------------------------------------------------------------
// The log, hyperion_hxm.log in the data folder.

const PortLogger HxmLog("hyperion_hxm.log", PortLogger::SHARED);

// ---------------------------------------------------------------------------
// The package as the compile reads it.

struct Package {
  PortJson doc;
  std::vector<std::pair<std::string, Bytes>> layers;  // by name
  std::vector<std::pair<std::string, Bytes>> files;   // by the map's path, backslashes
  const Bytes *Layer(const char *psz) const {
    for (const auto &l : layers)
      if (l.first == psz)
        return &l.second;
    return NULL;
  }
};

bool ReadPackage(const char *pszPath, Package *pOut, std::string *pError) {
  Bytes zip;
  HANDLE h = CreateFileA(pszPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    *pError = "cannot open the file";
    return false;
  }
  const DWORD cb = GetFileSize(h, NULL);
  zip.resize(cb);
  DWORD got = 0;
  const BOOL bRead = cb == 0 || ReadFile(h, &zip[0], cb, &got, NULL);
  CloseHandle(h);
  if (!bRead || got != cb) {
    *pError = "cannot read the file";
    return false;
  }
  std::vector<PortZipEntry> entries;
  if (!PortZipDirectory(zip, &entries)) {
    *pError = "not a zip";
    return false;
  }
  auto find = [&](const std::string &name, Bytes *pData) -> bool {
    for (const PortZipEntry &e : entries)
      if (e.name == name)
        return PortZipRead(zip, e, pData);
    return false;
  };
  Bytes json;
  if (!find("map.json", &json)) {
    *pError = "no map.json";
    return false;
  }
  json.push_back(0);
  std::string why;
  if (!PortJsonParse(reinterpret_cast<const char *>(&json[0]), &pOut->doc, &why)) {
    *pError = "map.json: " + why;
    return false;
  }
  const PortJson &layers = pOut->doc["layers"];
  for (const auto &m : layers.members) {
    Bytes data;
    const std::string file = m.second["file"].Str();
    if (!find(file, &data)) {
      *pError = "no " + file;
      return false;
    }
    pOut->layers.push_back(std::make_pair(m.first, data));
  }
  const PortJson &files = pOut->doc["files"];
  for (size_t i = 0; i < files.size(); ++i) {
    std::string name = files.At(i).Str(), inZip = "files/" + name;
    for (char &c : inZip)
      if (c == '\\')
        c = '/';
    Bytes data;
    if (!find(inZip, &data)) {
      *pError = "no " + inZip;
      return false;
    }
    pOut->files.push_back(std::make_pair(name, data));
  }
  return true;
}

// ---------------------------------------------------------------------------
// The compile: to_chk in tools/hxmap/convert.py, step by step.

struct Compile {
  std::string error;
  bool bHx = false;  // the map has players past 7: HXPL, and units in slots past 11
  int nSlots = NEUTRAL + 1;  // the map's slots, neutral's among them
  static const int kTrigHxCode = 0x100;
  std::vector<unsigned long long> hxte;  // TRIG's triggers' slots past 11 (HXTE)
  bool bModUnits = false;  // the map names a mod's units
  bool bNoGame = false;  // a check with no game at hand: a new map's VCOD is left out

  void Fail(const std::string &why) {
    if (error.empty())
      error = why;
  }

  static bool IsDigits(const std::string &s) {
    if (s.empty())
      return false;
    for (char c : s)
      if (c < '0' || c > '9')
        return false;
    return true;
  }

  // A size compat keeps, in bytes or strings, refused past what any map holds.
  size_t Size(const PortJson &v, long long dflt, const char *pszWhat) {
    const long long n = Int(v, dflt);
    if (n >= 0 && n <= (16 << 20))
      return static_cast<size_t>(n);
    Fail(std::string("compat's ") + pszWhat + " is past what a map holds");
    return 0;
  }

  long long Int(const PortJson &v, long long dflt = 0) {
    if (v.kind == PortJson::NUMBER)
      return static_cast<long long>(v.d);
    if (v.kind == PortJson::BOOL)
      return v.b ? 1 : 0;
    return dflt;
  }

  template <size_t N>
  int Name(const HxmName (&table)[N], const PortJson &v, const char *pszWhat, int dflt = 0) {
    if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV)
      return dflt;
    if (v.kind == PortJson::NUMBER)
      return static_cast<int>(v.d);
    if (v.kind == PortJson::STRING) {
      for (size_t i = 0; i < N; ++i)
        if (v.s == table[i].psz)
          return table[i].n;
      if (IsDigits(v.s))
        return atoi(v.s.c_str());
      // a mod's unit, when its data loaded
      const int nMod = strcmp(pszWhat, "unit") == 0 ? ModTypeByName("units", v.s.c_str()) : -1;
      if (nMod >= 0) {
        bModUnits = true;
        return nMod;
      }
    }
    Fail(std::string("unknown ") + pszWhat + " " + (v.kind == PortJson::STRING ? v.s : "value"));
    return dflt;
  }

  // The document's player or group as CHK's number (player_num).
  int Player(const PortJson &v) {
    if (v.kind == PortJson::NUMBER) {
      const int n = static_cast<int>(v.d);
      if (n >= NEUTRAL)
        Fail("a player past slot 10 does not fit a classic map");
      return n;
    }
    if (v.kind == PortJson::STRING) {
      for (const HxmName &g : kHxmGroups)
        if (v.s == g.psz)
          return g.n;
      if (v.s.compare(0, 6, "group_") == 0 && IsDigits(v.s.substr(6)))
        return atoi(v.s.c_str() + 6);
    }
    Fail("unknown player or group " + v.s);
    return 0;
  }

  // A trigger's player or group: as Player, and on a Hyperion map a slot past 11 as the code
  // 0x100 + slot (TRIG_HX_CODE, TrigEnum.h), which only a Hyperion game reads.
  int TrigPlayer(const PortJson &v) {
    if (bHx && v.kind == PortJson::NUMBER && v.d > NEUTRAL) {
      if (v.d >= nSlots || v.d != static_cast<int>(v.d))
        Fail("a trigger names a slot the map does not have");
      return kTrigHxCode + static_cast<int>(v.d);
    }
    return Player(v);
  }

  int Owner(const PortJson &v) {
    if (v.kind == PortJson::NONE)
      return 0;
    if (bHx && v.kind == PortJson::NUMBER && v.d > NEUTRAL && v.d < PLAYER_SLOTS_MAX)
      return static_cast<int>(v.d);
    return Player(v);
  }

  // HXPL: the slots from 8 on, document index i as slot i up to 10 and slot i + 1 after,
  // slot 11's entry neutral's; the colour 0xFF where the document names none.
  Bytes Hxpl(const PortJson &players) {
    const size_t nSlots = players.size() > static_cast<size_t>(NEUTRAL) ? players.size() + 1
                                                                       : players.size();
    Bytes body;
    PutN(&body, 1, 4);
    PutN(&body, static_cast<long long>(nSlots), 4);
    for (size_t s = PLAYER_LOBBY_117; s < nSlots; ++s) {
      if (s == static_cast<size_t>(NEUTRAL)) {
        PutN(&body, 0, 4);
        continue;
      }
      const PortJson &p = players.At(s < static_cast<size_t>(NEUTRAL) ? s : s - 1);
      PortJson inactive, zerg;
      inactive.kind = zerg.kind = PortJson::STRING;
      inactive.s = "inactive";
      zerg.s = "zerg";
      body.push_back(static_cast<BYTE>(Name(kHxmSlots, p["slot"].kind != PortJson::NONE ? p["slot"] : inactive, "slot")));
      body.push_back(static_cast<BYTE>(Name(kHxmRaces, p["race"].kind != PortJson::NONE ? p["race"] : zerg, "race")));
      body.push_back(static_cast<BYTE>(Int(p["force"])));
      body.push_back(static_cast<BYTE>(p["color"].kind == PortJson::NONE ? 0xFF : Name(kHxmColors, p["color"], "colour")));
    }
    return body;
  }

  // HXRG (stage G): the RGB colours of the slots from 8 on, as HXPL lays them out, from each
  // player's "rgb"; empty when none has one.
  Bytes Hxrg(const PortJson &players) {
    const size_t nSlots = players.size() > static_cast<size_t>(NEUTRAL) ? players.size() + 1
                                                                       : players.size();
    bool bAny = false;
    Bytes body;
    PutN(&body, 1, 4);
    PutN(&body, static_cast<long long>(nSlots), 4);
    for (size_t s = PLAYER_LOBBY_117; s < nSlots; ++s) {
      const PortJson *pRgb = s == static_cast<size_t>(NEUTRAL)
                                 ? NULL
                                 : &players.At(s < static_cast<size_t>(NEUTRAL) ? s : s - 1)["rgb"];
      const bool bSet = pRgb != NULL && pRgb->kind == PortJson::ARRAY && pRgb->size() == 3;
      body.push_back(bSet ? 1 : 0);
      for (size_t k = 0; k < 3; ++k)
        body.push_back(static_cast<BYTE>(bSet ? Int(pRgb->At(k)) : 0));
      bAny = bAny || bSet;
    }
    return bAny ? body : Bytes();
  }

  // A player key of an availability table: a number, or a name.
  int PlayerKey(const std::string &k) {
    if (IsDigits(k))
      return atoi(k.c_str());
    PortJson v;
    v.kind = PortJson::STRING;
    v.s = k;
    return Player(v);
  }

  // ---- text

  std::string encoding;
  std::vector<Bytes> strings;    // id - 1; an empty vector with bNowhere marks a reserved id
  std::vector<bool> nowhere;
  std::vector<std::pair<Bytes, int>> ids;
  std::vector<long long> reserved;  // sorted

  static bool Utf8Decode(const std::string &s, std::vector<unsigned> *pOut) {
    for (size_t i = 0; i < s.size();) {
      const BYTE c = s[i];
      unsigned cp;
      int n;
      if (c < 0x80) { cp = c; n = 1; }
      else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
      else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
      else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
      else return false;
      if (i + n > s.size())
        return false;
      for (int k = 1; k < n; ++k) {
        const BYTE d = s[i + k];
        if ((d & 0xC0) != 0x80)
          return false;
        cp = (cp << 6) | (d & 0x3F);
      }
      // Python's decoder refuses overlong forms and surrogates
      if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
          cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return false;
      pOut->push_back(cp);
      i += n;
    }
    return true;
  }

  // cp1252's bytes 0x80..0x9F; 0 where it has none (Python's decoder refuses those, and
  // convert.py keeps them as the code point of the same number).
  static unsigned Cp1252High(BYTE b) {
    static const unsigned k[32] = {
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013,
        0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    return k[b - 0x80];
  }

  static unsigned LegacyChar(BYTE b) {
    if (b < 0x80 || b >= 0xA0)
      return b;
    const unsigned u = Cp1252High(b);
    return u ? u : b;
  }

  // decode_text: UTF-8 when the bytes are UTF-8, the legacy code page otherwise.
  static std::vector<unsigned> DecodeText(const Bytes &raw) {
    std::vector<unsigned> out;
    if (Utf8Decode(std::string(raw.begin(), raw.end()), &out))
      return out;
    out.clear();
    for (BYTE b : raw)
      out.push_back(LegacyChar(b));
    return out;
  }

  // encode_text
  Bytes EncodeText(const std::string &text) {
    Bytes utf8(text.begin(), text.end());
    if (encoding == "utf-8")
      return utf8;
    std::vector<unsigned> cps;
    if (!Utf8Decode(text, &cps))
      return utf8;
    Bytes out;
    for (unsigned cp : cps) {
      int b = -1;
      if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) {
        b = static_cast<int>(cp);
      } else {
        for (int k = 0x80; k < 0xA0; ++k)
          if (Cp1252High(static_cast<BYTE>(k)) == cp)
            b = k;
        if (b < 0 && cp < 0x100)
          b = static_cast<int>(cp);
      }
      if (b < 0)
        return utf8;
      out.push_back(static_cast<BYTE>(b));
    }
    if (DecodeText(out) == cps)
      return out;
    return utf8;
  }

  bool IsReserved(long long n) const {
    for (long long r : reserved)
      if (r == n)
        return true;
    return false;
  }

  // StringsOut.id
  DWORD Text(const PortJson &v) {
    if (v.kind == PortJson::NONE || v.kind == PortJson::NULLV)
      return 0;
    if (v.kind == PortJson::OBJECT)
      return static_cast<DWORD>(Int(v["string_id"]));
    if (v.kind != PortJson::STRING) {
      Fail("a text field holds something other than text");
      return 0;
    }
    const Bytes raw = EncodeText(v.s);
    for (const auto &e : ids)
      if (e.first == raw)
        return e.second;
    while (IsReserved(static_cast<long long>(strings.size()) + 1)) {
      strings.push_back(Bytes());
      nowhere.push_back(true);
    }
    strings.push_back(raw);
    nowhere.push_back(false);
    ids.push_back(std::make_pair(raw, static_cast<int>(strings.size())));
    return static_cast<DWORD>(strings.size());
  }

  void RawStringIds(const PortJson &v) {
    if (v.kind == PortJson::OBJECT) {
      if (v.members.size() == 1 && v.members[0].first == "string_id")
        reserved.push_back(Int(v.members[0].second));
      for (const auto &m : v.members)
        RawStringIds(m.second);
    } else if (v.kind == PortJson::ARRAY) {
      for (const PortJson &i : v.items)
        RawStringIds(i);
    }
  }

  // chk.write_strings
  bool WriteStrings(bool bWide, size_t count, Bytes *pOut) {
    const size_t width = bWide ? 4 : 2;
    const size_t n = strings.size() > count ? strings.size() : count;
    const size_t head = width + n * width;
    Bytes data(1, 0);
    std::vector<std::pair<Bytes, size_t>> at;
    at.push_back(std::make_pair(Bytes(), 0));
    std::vector<DWORD> offsets;
    const DWORD dwNowhere = bWide ? 0xFFFFFFFF : 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
      if (i < strings.size() && nowhere[i]) {
        offsets.push_back(dwNowhere);
        continue;
      }
      const Bytes s = i < strings.size() ? strings[i] : Bytes();
      size_t off = SIZE_MAX;
      for (const auto &a : at)
        if (a.first == s)
          off = a.second;
      if (off == SIZE_MAX) {
        off = data.size();
        at.push_back(std::make_pair(s, off));
        data.insert(data.end(), s.begin(), s.end());
        data.push_back(0);
      }
      offsets.push_back(static_cast<DWORD>(head + off));
    }
    if (!bWide && head + data.size() > 0x10000)
      return false;
    pOut->clear();
    PutN(pOut, n, width);
    for (DWORD o : offsets)
      PutN(pOut, o, width);
    pOut->insert(pOut->end(), data.begin(), data.end());
    return true;
  }

  // ---- bytes

  static void PutN(Bytes *p, unsigned long long v, size_t n) {
    for (size_t i = 0; i < n; ++i)
      p->push_back(static_cast<BYTE>(v >> (8 * i)));
  }
  static void SetN(Bytes &b, size_t at, unsigned long long v, size_t n) {
    for (size_t i = 0; i < n; ++i)
      b[at + i] = static_cast<BYTE>(v >> (8 * i));
  }
  static void Section(Bytes *pChk, const std::string &tag, const Bytes &body) {
    pChk->insert(pChk->end(), tag.begin(), tag.begin() + 4);
    PutN(pChk, body.size(), 4);
    pChk->insert(pChk->end(), body.begin(), body.end());
  }
  static Bytes Base64(const std::string &s) {
    Bytes out;
    unsigned acc = 0;
    int bits = 0;
    for (char c : s) {
      int v;
      if (c >= 'A' && c <= 'Z') v = c - 'A';
      else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
      else if (c >= '0' && c <= '9') v = c - '0' + 52;
      else if (c == '+') v = 62;
      else if (c == '/') v = 63;
      else continue;
      acc = (acc << 6) | v;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        out.push_back(static_cast<BYTE>(acc >> bits));
      }
    }
    return out;
  }

  // ---- sections

  // The unit fields, by name, offset and width (UNIT_FLAGS), and their defaults.
  struct UnitField {
    const char *psz;
    size_t at, n;
    long long dflt;
  };
  static const UnitField *UnitFields(size_t *pN) {
    static const UnitField k[] = {
        {"valid_state", 0x0C, 2, 0x1F}, {"valid_props", 0x0E, 2, 0x3F}, {"hp", 0x11, 1, 100},
        {"shields", 0x12, 1, 100}, {"energy", 0x13, 1, 100}, {"resources", 0x14, 4, 0},
        {"hangar", 0x18, 2, 0}, {"state", 0x1A, 2, 0}, {"unused", 0x1C, 4, 0}};
    *pN = sizeof k / sizeof k[0];
    return k;
  }

  Bytes Units(const PortJson &units) {
    Bytes out;
    size_t nFields;
    const UnitField *fields = UnitFields(&nFields);
    for (size_t i = 0; i < units.size(); ++i) {
      const PortJson &u = units.At(i);
      Bytes e(36, 0);
      SetN(e, 0, Int(u["id"], static_cast<long long>(i)), 4);
      SetN(e, 4, Int(u["x"]), 2);
      SetN(e, 6, Int(u["y"]), 2);
      if (u["type"].kind == PortJson::NONE)
        Fail("a unit has no type");
      SetN(e, 8, Name(kHxmUnits, u["type"], "unit"), 2);
      SetN(e, 10, Int(u["relation"]), 2);
      e[0x10] = static_cast<BYTE>(Owner(u["owner"]));
      for (size_t f = 0; f < nFields; ++f)
        SetN(e, fields[f].at, Int(u[fields[f].psz], fields[f].dflt), fields[f].n);
      SetN(e, 0x20, Int(u["link"]), 4);
      out.insert(out.end(), e.begin(), e.end());
    }
    return out;
  }

  Bytes Doodads(const PortJson &doodads) {
    Bytes out;
    for (size_t i = 0; i < doodads.size(); ++i) {
      const PortJson &d = doodads.At(i);
      long long kind, flags = Int(d["flags"]);
      if (d["sprite"].kind != PortJson::NONE) {
        kind = Int(d["sprite"]);
        flags |= 0x1000;
      } else {
        kind = Name(kHxmUnits, d["unit"], "unit");
      }
      PutN(&out, kind, 2);
      PutN(&out, Int(d["x"]), 2);
      PutN(&out, Int(d["y"]), 2);
      PutN(&out, Owner(d["owner"]), 1);
      PutN(&out, Int(d["pad"]), 1);
      PutN(&out, flags, 2);
    }
    return out;
  }

  Bytes Locations(const PortJson &locs, int version, const PortJson &compat) {
    const size_t count = version == 59 ? 64 : 255;
    Bytes body(count * 20, 0);
    for (size_t i = 0; i < locs.size(); ++i) {
      const PortJson &l = locs.At(i);
      const long long id = Int(l["id"]);
      if (id < 1 || static_cast<size_t>(id) > count) {
        Fail("a location past the map's count");
        continue;
      }
      const size_t at = (id - 1) * 20;
      SetN(body, at, Int(l["left"]), 4);
      SetN(body, at + 4, Int(l["top"]), 4);
      SetN(body, at + 8, Int(l["right"]), 4);
      SetN(body, at + 12, Int(l["bottom"]), 4);
      SetN(body, at + 16, Text(l["name"]), 2);
      SetN(body, at + 18, Int(l["elevation"]), 2);
    }
    if (compat["mrgn_bytes"].kind == PortJson::NUMBER)
      body.resize(Size(compat["mrgn_bytes"], 0, "mrgn_bytes"), 0);
    return body;
  }

  struct Field {
    const char *psz;
    size_t at, n;
  };

  Bytes Condition(const PortJson &c) {
    static const Field k[] = {{"location", 0x00, 4}, {"player", 0x04, 4}, {"amount", 0x08, 4},
                              {"unit", 0x0C, 2}, {"comparison", 0x0E, 1}, {"type", 0x0F, 1},
                              {"resource", 0x10, 1}, {"flags", 0x11, 1}, {"mask", 0x12, 2}};
    Bytes e(20, 0);
    for (const Field &f : k) {
      const PortJson &v = c[f.psz];
      long long n;
      if (!strcmp(f.psz, "type")) n = Name(kHxmConditions, v, "condition");
      else if (!strcmp(f.psz, "player")) n = (v.kind == PortJson::NONE || (v.kind == PortJson::NUMBER && v.d == 0)) ? 0 : TrigPlayer(v);
      else if (!strcmp(f.psz, "unit")) n = Name(kHxmUnits, v, "unit");
      else if (!strcmp(f.psz, "comparison")) n = Name(kHxmComparisons, v, "comparison");
      else n = Int(v);
      SetN(e, f.at, n, f.n);
    }
    return e;
  }

  Bytes Action(const PortJson &a, bool bBriefing) {
    static const Field k[] = {{"location", 0x00, 4}, {"text", 0x04, 4}, {"wav", 0x08, 4},
                              {"time", 0x0C, 4}, {"player", 0x10, 4}, {"number", 0x14, 4},
                              {"unit", 0x18, 2}, {"type", 0x1A, 1}, {"modifier", 0x1B, 1},
                              {"flags", 0x1C, 1}, {"pad", 0x1D, 1}, {"mask", 0x1E, 2}};
    const int t = bBriefing ? Name(kHxmBriefingActions, a["type"], "briefing action")
                            : Name(kHxmActions, a["type"], "action");
    Bytes e(32, 0);
    for (const Field &f : k) {
      const PortJson &v = a[f.psz];
      const bool bZero = v.kind == PortJson::NONE || (v.kind == PortJson::NUMBER && v.d == 0);
      long long n;
      if (!strcmp(f.psz, "type")) n = t;
      else if (!strcmp(f.psz, "text") || !strcmp(f.psz, "wav")) n = Text(v);
      else if (!strcmp(f.psz, "player")) n = bZero ? 0 : (bBriefing ? Player(v) : TrigPlayer(v));
      else if (!strcmp(f.psz, "number") && !bBriefing && t == 48) n = bZero ? 0 : TrigPlayer(v);
      else if (!strcmp(f.psz, "unit")) n = Name(kHxmUnits, v, "unit");
      else n = Int(v);
      SetN(e, f.at, n, f.n);
    }
    return e;
  }

  Bytes Triggers(const PortJson &trigs, bool bBriefing) {
    Bytes out;
    for (size_t i = 0; i < trigs.size(); ++i) {
      const PortJson &t = trigs.At(i);
      Bytes body(2400, 0);
      const PortJson &conds = t["conditions"], &acts = t["actions"];
      if (conds.size() > 16 || acts.size() > 64)
        Fail("a trigger holds 16 conditions and 64 actions");
      for (size_t j = 0; j < conds.size() && j < 16; ++j) {
        const Bytes c = Condition(conds.At(j));
        memcpy(&body[j * 20], &c[0], 20);
      }
      for (size_t j = 0; j < acts.size() && j < 64; ++j) {
        const Bytes a = Action(acts.At(j), bBriefing);
        memcpy(&body[320 + j * 32], &a[0], 32);
      }
      SetN(body, 2368, Int(t["flags"]), 4);
      const PortJson &players = t["players"];
      unsigned long long qwHx = 0;
      for (size_t j = 0; j < players.size(); ++j) {
        const PortJson &v = players.At(j);
        if (!bBriefing && bHx && v.kind == PortJson::NUMBER && v.d > NEUTRAL) {
          if (v.d >= nSlots || v.d >= 64 || v.d != static_cast<int>(v.d))
            Fail("a trigger runs for a slot the map does not have, or one past 63");
          else
            qwHx |= 1ull << static_cast<int>(v.d);
          continue;
        }
        const int p = Player(v);
        if (p >= 0 && p < 28)
          body[2372 + p] = 1;
      }
      if (!bBriefing)
        hxte.push_back(qwHx);
      for (const auto &m : t["player_bytes"].members) {
        const int p = atoi(m.first.c_str());
        if (p >= 0 && p < 28)
          body[2372 + p] = static_cast<BYTE>(Int(m.second));
      }
      body[2399] = static_cast<BYTE>(Int(t["current_action"]));
      out.insert(out.end(), body.begin(), body.end());
    }
    return out;
  }

  Bytes Forces(const PortJson &forces, const PortJson &players) {
    Bytes body(20, 0);
    for (size_t i = 0; i < players.size() && i < 8; ++i)
      body[i] = static_cast<BYTE>(Int(players.At(i)["force"]));
    for (size_t f = 0; f < forces.size() && f < 4; ++f) {
      const PortJson &force = forces.At(f);
      SetN(body, 8 + f * 2, Text(force["name"]), 2);
      body[16 + f] = static_cast<BYTE>(Flags(force["flags"]));
    }
    return body;
  }

  int Flags(const PortJson &items) {
    int v = 0;
    for (size_t i = 0; i < items.size(); ++i)
      v |= Name(kHxmForceFlags, items.At(i), "force flag");
    return v;
  }

  // unit_properties: the sets and their used flags
  Bytes Uprp(const PortJson &props) {
    Bytes body(0x500, 0);
    for (size_t i = 0; i < props.size(); ++i) {
      const PortJson &p = props.At(i);
      if (p["meta"].kind != PortJson::NONE)
        continue;
      const long long slot = Int(p["slot"]);
      if (slot < 1 || slot > 64)
        continue;
      const size_t at = (slot - 1) * 20;
      SetN(body, at, Int(p["valid_state"]), 2);
      SetN(body, at + 2, Int(p["valid_props"]), 2);
      body[at + 4] = static_cast<BYTE>(Int(p["owner"]));
      body[at + 5] = static_cast<BYTE>(Int(p["hp"]));
      body[at + 6] = static_cast<BYTE>(Int(p["shields"]));
      body[at + 7] = static_cast<BYTE>(Int(p["energy"]));
      SetN(body, at + 8, Int(p["resources"]), 4);
      SetN(body, at + 12, Int(p["hangar"]), 2);
      SetN(body, at + 14, Int(p["state"]), 2);
      SetN(body, at + 16, Int(p["unused"]), 4);
    }
    return body;
  }

  Bytes Upus(const PortJson &props, size_t size) {
    Bytes body(size > 64 ? size : 64, 0);
    for (size_t i = 0; i < props.size(); ++i) {
      const PortJson &p = props.At(i);
      const PortJson &used = p["used"];
      const long long slot = Int(p["slot"]);
      if (slot < 1 || slot > 64 || used.kind == PortJson::NONE)
        continue;
      if (used.kind == PortJson::BOOL ? used.b : Int(used) != 0)
        body[slot - 1] = static_cast<BYTE>(used.kind == PortJson::BOOL ? 1 : Int(used));
    }
    body.resize(size);
    return body;
  }

  Bytes Unis(const PortJson &s, bool bBw) {
    const size_t nWeap = bBw ? 130 : 100;
    Bytes body(0xFD0 + (nWeap - 100) * 4, 0);
    memset(&body[0], 1, 228);
    for (const auto &m : s["units"].members) {
      PortJson key;
      key.kind = PortJson::STRING;
      key.s = m.first;
      const int i = Name(kHxmUnits, key, "unit");
      if (i < 0 || i >= 228)
        continue;
      const PortJson &u = m.second;
      body[i] = 0;
      SetN(body, 0xE4 + i * 4, Int(u["hp"]), 4);
      SetN(body, 0x474 + i * 2, Int(u["shields"]), 2);
      body[0x63C + i] = static_cast<BYTE>(Int(u["armor"]));
      SetN(body, 0x720 + i * 2, Int(u["build_time"]), 2);
      SetN(body, 0x8E8 + i * 2, Int(u["minerals"]), 2);
      SetN(body, 0xAB0 + i * 2, Int(u["gas"]), 2);
      SetN(body, 0xC78 + i * 2, Text(u["name"]), 2);
    }
    for (const auto &m : s["weapons"].members) {
      const int w = atoi(m.first.c_str());
      if (w < 0 || static_cast<size_t>(w) >= nWeap)
        continue;
      SetN(body, 0xE40 + w * 2, Int(m.second.At(0)), 2);
      SetN(body, 0xE40 + nWeap * 2 + w * 2, Int(m.second.At(1)), 2);
    }
    return body;
  }

  Bytes Upgs(const PortJson &s, bool bBw) {
    static const char *const kFields[] = {"minerals", "minerals_factor", "gas", "gas_factor",
                                          "time", "time_factor"};
    const size_t n = bBw ? 61 : 46, base = n + (bBw ? 1 : 0);
    Bytes body(base + n * 12, 0);
    memset(&body[0], 1, n);
    for (const auto &m : s.members) {
      PortJson key;
      key.kind = PortJson::STRING;
      key.s = m.first;
      const int i = Name(kHxmUpgrades, key, "upgrade");
      if (i < 0 || static_cast<size_t>(i) >= n)
        continue;
      body[i] = 0;
      for (size_t k = 0; k < 6; ++k)
        SetN(body, base + k * n * 2 + i * 2, Int(m.second[kFields[k]]), 2);
    }
    return body;
  }

  Bytes Tecs(const PortJson &s, bool bBw) {
    static const char *const kFields[] = {"minerals", "gas", "time", "energy"};
    const size_t n = bBw ? 44 : 24;
    Bytes body(n + n * 8, 0);
    memset(&body[0], 1, n);
    for (const auto &m : s.members) {
      PortJson key;
      key.kind = PortJson::STRING;
      key.s = m.first;
      const int i = Name(kHxmTechs, key, "tech");
      if (i < 0 || static_cast<size_t>(i) >= n)
        continue;
      body[i] = 0;
      for (size_t k = 0; k < 4; ++k)
        SetN(body, n + k * n * 2 + i * 2, Int(m.second[kFields[k]]), 2);
    }
    return body;
  }

  // fill(): a run of n bytes at off set to dflt, then the listed values over it.
  template <size_t N>
  void Fill(Bytes &body, size_t off, size_t n, int dflt, const PortJson &values,
            const HxmName (&table)[N], const char *pszWhat) {
    memset(&body[off], dflt, n);
    for (const auto &m : values.members) {
      PortJson key;
      key.kind = PortJson::STRING;
      key.s = m.first;
      const int i = Name(table, key, pszWhat);
      if (i >= 0 && static_cast<size_t>(i) < n)
        body[off + i] = static_cast<BYTE>(Int(m.second));
    }
  }

  Bytes Puni(const PortJson &a) {
    Bytes body(5700, 0);
    const PortJson none;
    for (int p = 0; p < 12; ++p) {
      Fill(body, p * 228, 228, 1, none, kHxmUnits, "unit");
      Fill(body, 12 * 228 + 228 + p * 228, 228, 1, none, kHxmUnits, "unit");
    }
    Fill(body, 12 * 228, 228, 1, a["global"], kHxmUnits, "unit");
    for (const auto &m : a["players"].members) {
      const int p = PlayerKey(m.first);
      if (p < 0 || p >= 12)
        continue;
      Fill(body, p * 228, 228, 1, m.second["available"], kHxmUnits, "unit");
      Fill(body, 12 * 228 + 228 + p * 228, 228, 1, m.second["use_global"], kHxmUnits, "unit");
    }
    return body;
  }

  Bytes Pupx(const PortJson &a, size_t n) {
    Bytes body(38 * n, 0);
    const PortJson none;
    for (int p = 0; p < 12; ++p)
      Fill(body, 26 * n + p * n, n, 1, none, kHxmUpgrades, "upgrade");
    Fill(body, 24 * n, n, 0, a["global_max"], kHxmUpgrades, "upgrade");
    Fill(body, 25 * n, n, 0, a["global_start"], kHxmUpgrades, "upgrade");
    for (const auto &m : a["players"].members) {
      const int p = PlayerKey(m.first);
      if (p < 0 || p >= 12)
        continue;
      Fill(body, p * n, n, 0, m.second["max"], kHxmUpgrades, "upgrade");
      Fill(body, 12 * n + p * n, n, 0, m.second["start"], kHxmUpgrades, "upgrade");
      Fill(body, 26 * n + p * n, n, 1, m.second["use_global"], kHxmUpgrades, "upgrade");
    }
    return body;
  }

  Bytes Ptex(const PortJson &a, size_t n) {
    Bytes body(38 * n, 0);
    const PortJson none;
    for (int p = 0; p < 12; ++p)
      Fill(body, 26 * n + p * n, n, 1, none, kHxmTechs, "tech");
    Fill(body, 24 * n, n, 0, a["global_available"], kHxmTechs, "tech");
    Fill(body, 25 * n, n, 0, a["global_researched"], kHxmTechs, "tech");
    for (const auto &m : a["players"].members) {
      const int p = PlayerKey(m.first);
      if (p < 0 || p >= 12)
        continue;
      Fill(body, p * n, n, 0, m.second["available"], kHxmTechs, "tech");
      Fill(body, 12 * n + p * n, n, 0, m.second["researched"], kHxmTechs, "tech");
      Fill(body, 26 * n + p * n, n, 1, m.second["use_global"], kHxmTechs, "tech");
    }
    return body;
  }

  Bytes Crgb(const PortJson &players) {
    Bytes body(32, 0);
    for (size_t i = 0; i < 8; ++i) {
      const PortJson &p = players.At(i);
      const PortJson &rgb = p["rgb"];
      for (size_t k = 0; k < 3; ++k)
        body[i * 3 + k] = static_cast<BYTE>(Int(rgb.At(k)));
      const PortJson &mode = p["color_mode"];
      int m = 1;
      if (mode.kind == PortJson::STRING)
        m = mode.s == "random" ? 0 : mode.s == "rgb" ? 2 : 1;
      else if (mode.kind == PortJson::NUMBER)
        m = static_cast<int>(mode.d);
      body[24 + i] = static_cast<BYTE>(m);
    }
    return body;
  }

  bool Run(const Package &pkg, Bytes *pChk) {
    const PortJson &doc = pkg.doc;
    if (doc["format"].Str() != "hyperion-map") {
      Fail("not a Hyperion map");
      return false;
    }
    // a newer format may mean fields this build would drop without a word
    if (Int(doc["version"], HXM_VERSION) > HXM_VERSION) {
      char sz[96];
      sprintf_s(sz, "format version %lld, where this Hyperion reads version %d", Int(doc["version"]),
                HXM_VERSION);
      Fail(sz);
      return false;
    }
    const PortJson &compat = doc["compat"];
    const PortJson &size = doc["size"];
    const long long width = Int(size.At(0)), height = Int(size.At(1));
    const bool bExpansion = doc["expansion"].kind == PortJson::BOOL ? doc["expansion"].b : true;
    encoding = doc["text_encoding"].Str("utf-8");
    RawStringIds(doc);
    bModUnits = false;
    const bool bConverted = compat["chk_version"].kind == PortJson::NUMBER;
    int version = bConverted ? static_cast<int>(Int(compat["chk_version"])) : (bExpansion ? 205 : 63);

    struct Part {
      std::string tag;
      Bytes body;
    };
    std::vector<Part> parts, later;
    auto add = [](std::vector<Part> &v, const std::string &tag, const Bytes &b) { v.push_back(Part{tag, b}); };
    auto word = [](long long w) { Bytes b; PutN(&b, w, 2); return b; };

    if (!bConverted || compat["type"].kind == PortJson::STRING) {
      const std::string type = compat["type"].Str(bExpansion ? "RAWB" : "RAWS");
      add(parts, "TYPE", Bytes(type.begin(), type.end()));
    }
    const size_t verAt = parts.size();
    add(parts, "VER ", Bytes());
    if (compat["iver"].kind == PortJson::NUMBER)
      add(parts, "IVER", word(Int(compat["iver"])));
    if (compat["ive2"].kind == PortJson::NUMBER || !bConverted)
      add(parts, "IVE2", word(Int(compat["ive2"], 11)));
    const std::string vcod = compat["vcod"].Str(bConverted ? "" : "standard");
    if (vcod == "standard" && bNoGame) {
      // a check's compile is not played, and VCOD is the same for every new map
    } else if (vcod == "standard") {
      ULONG cb = 0;
      const BYTE *p = static_cast<const BYTE *>(PortGameVcod(&cb));
      add(parts, "VCOD", Bytes(p, p + cb));
    } else if (!vcod.empty()) {
      add(parts, "VCOD", Base64(vcod));
    }

    // the slots: the document's players, inactive ones up to 10, then neutral; on a
    // Hyperion map HXPL has the slots from 8 on
    const PortJson &docPlayers = doc["players"];
    bHx = false;
    for (size_t i = PLAYER_LOBBY_117; i < docPlayers.size(); ++i) {
      const PortJson &slot = docPlayers.At(i)["slot"];
      const int n = slot.kind == PortJson::NONE ? 0 : Name(kHxmSlots, slot, "slot");
      bHx = bHx || n == 5 || n == 6;
    }
    if (docPlayers.size() > static_cast<size_t>(bHx ? PLAYERS_MAX : NEUTRAL)) {
      Fail(bHx ? "more players than Hyperion holds" : "more player slots than a classic map holds");
      return false;
    }
    nSlots = docPlayers.size() > static_cast<size_t>(NEUTRAL) ? static_cast<int>(docPlayers.size()) + 1 : NEUTRAL + 1;
    if (bHx) {
      add(parts, "HXPL", Hxpl(docPlayers));
      const Bytes hxrg = Hxrg(docPlayers);
      if (!hxrg.empty())
        add(parts, "HXRG", hxrg);
    }
    std::vector<const PortJson *> players;
    PortJson blank;
    blank.kind = PortJson::OBJECT;
    for (size_t i = 0; i < docPlayers.size() && i < static_cast<size_t>(NEUTRAL); ++i)
      players.push_back(&docPlayers.At(i));
    while (players.size() < static_cast<size_t>(NEUTRAL))
      players.push_back(&blank);
    players.push_back(doc["neutral"].kind == PortJson::OBJECT ? &doc["neutral"] : &blank);
    Bytes ownr, side, iown;
    for (const PortJson *p : players) {
      PortJson inactive, zerg;
      inactive.kind = zerg.kind = PortJson::STRING;
      inactive.s = "inactive";
      zerg.s = "zerg";
      const PortJson &slot = (*p)["slot"].kind != PortJson::NONE ? (*p)["slot"] : inactive;
      const PortJson &race = (*p)["race"].kind != PortJson::NONE ? (*p)["race"] : zerg;
      const PortJson &eslot = (*p)["editor_slot"].kind != PortJson::NONE ? (*p)["editor_slot"] : slot;
      ownr.push_back(static_cast<BYTE>(Name(kHxmSlots, slot, "slot")));
      side.push_back(static_cast<BYTE>(Name(kHxmRaces, race, "race")));
      iown.push_back(static_cast<BYTE>(Name(kHxmSlots, eslot, "slot")));
    }
    add(parts, "IOWN", iown);
    add(parts, "OWNR", ownr);
    add(parts, "ERA ", word(Name(kHxmTilesets, doc["tileset"], "tileset")));
    Bytes dim;
    PutN(&dim, width, 2);
    PutN(&dim, height, 2);
    add(parts, "DIM ", dim);
    add(parts, "SIDE", side);

    const Bytes *tiles = pkg.Layer("tiles");
    Bytes mtxm = tiles ? *tiles : Bytes();
    if (compat["mtxm_bytes"].kind == PortJson::NUMBER) {
      const Bytes *extra = pkg.Layer("tiles_extra");
      if (extra)
        mtxm.insert(mtxm.end(), extra->begin(), extra->end());
      mtxm.resize(Size(compat["mtxm_bytes"], 0, "mtxm_bytes"), 0);
    }
    add(parts, "MTXM", mtxm);

    const PortJson &avail = doc["availability"];
    if (avail["units"].kind != PortJson::NONE)
      add(parts, "PUNI", Puni(avail["units"]));
    if (avail["upgrades_classic"].kind != PortJson::NONE)
      add(parts, "UPGR", Pupx(avail["upgrades_classic"], 46));
    if (avail["techs_classic"].kind != PortJson::NONE)
      add(parts, "PTEC", Ptex(avail["techs_classic"], 24));
    add(parts, "UNIT", Units(doc["units"]));
    if (const Bytes *isom = pkg.Layer("isom"))
      add(parts, "ISOM", *isom);
    if (doc["editor"]["tiles_same_as_game"].kind == PortJson::BOOL && doc["editor"]["tiles_same_as_game"].b)
      add(parts, "TILE", mtxm);
    else if (const Bytes *et = pkg.Layer("editor_tiles"))
      add(parts, "TILE", *et);
    if (const Bytes *dd2 = pkg.Layer("editor_doodads"))
      add(parts, "DD2 ", *dd2);
    add(parts, "THG2", Doodads(doc["doodads"]));
    if (const Bytes *fog = pkg.Layer("fog"))
      add(parts, "MASK", *fog);

    const PortJson &props = doc["unit_properties"];
    const PortJson *meta = NULL;
    for (size_t i = 0; i < props.size(); ++i)
      if (props.At(i)["meta"].kind == PortJson::OBJECT)
        meta = &props.At(i)["meta"];
    const bool bNoUprp = meta && (*meta)["no_uprp"].kind == PortJson::BOOL && (*meta)["no_uprp"].b;
    const bool bNoUpus = meta && (*meta)["no_upus"].kind == PortJson::BOOL && (*meta)["no_upus"].b;
    if (!bNoUprp)
      add(later, "UPRP", Uprp(props));
    if (!bNoUpus)
      add(later, "UPUS", Upus(props, meta ? Size((*meta)["upus_bytes"], 64, "upus_bytes") : 64));
    add(later, "MRGN", Locations(doc["locations"], version, compat));
    hxte.clear();
    add(later, "TRIG", Triggers(doc["triggers"], false));
    bool bHxte = false;
    for (unsigned long long q : hxte)
      bHxte = bHxte || q != 0;
    if (bHxte) {
      Bytes body;
      PutN(&body, 1, 4);
      PutN(&body, static_cast<long long>(hxte.size()), 4);
      for (unsigned long long q : hxte)
        PutN(&body, static_cast<long long>(q), 8);
      add(later, "HXTE", body);
    }
    if (doc["briefing"].kind != PortJson::NONE)
      add(later, "MBRF", Triggers(doc["briefing"], true));
    Bytes sprp;
    PutN(&sprp, Text(doc["name"]), 2);
    PutN(&sprp, Text(doc["description"]), 2);
    add(later, "SPRP", sprp);
    add(later, "FORC", Forces(doc["forces"], docPlayers));
    if (doc["sounds"].kind == PortJson::OBJECT) {
      Bytes wav(2048, 0);
      for (const auto &m : doc["sounds"].members) {
        const int i = atoi(m.first.c_str());
        if (i >= 0 && i < 512)
          SetN(wav, i * 4, Text(m.second), 4);
      }
      wav.resize(Size(compat["wav_bytes"], 2048, "wav_bytes"), 0);
      add(later, "WAV ", wav);
    }
    const PortJson &settings = doc["settings"];
    if (settings["units_classic"].kind != PortJson::NONE)
      add(later, "UNIS", Unis(settings["units_classic"], false));
    if (settings["upgrades_classic"].kind != PortJson::NONE)
      add(later, "UPGS", Upgs(settings["upgrades_classic"], false));
    if (settings["techs_classic"].kind != PortJson::NONE)
      add(later, "TECS", Tecs(settings["techs_classic"], false));
    if (doc["switches"].kind == PortJson::OBJECT) {
      Bytes swnm(1024, 0);
      for (const auto &m : doc["switches"].members) {
        const int i = atoi(m.first.c_str()) - 1;
        if (i >= 0 && i < 256)
          SetN(swnm, i * 4, Text(m.second), 4);
      }
      swnm.resize(Size(compat["swnm_bytes"], 1024, "swnm_bytes"), 0);
      add(later, "SWNM", swnm);
    }
    bool bColors = false, bCrgb = false;
    for (size_t i = 0; i < 8; ++i) {
      bColors = bColors || (*players[i])["color"].kind != PortJson::NONE;
      bCrgb = bCrgb || (*players[i])["color_mode"].kind != PortJson::NONE;
    }
    if (bColors) {
      Bytes colr;
      for (size_t i = 0; i < 8; ++i) {
        const PortJson &c = (*players[i])["color"];
        colr.push_back(static_cast<BYTE>(c.kind == PortJson::NONE ? static_cast<int>(i)
                                                                  : Name(kHxmColors, c, "colour")));
      }
      add(later, "COLR", colr);
    }
    if (bCrgb) {
      PortJson list;
      list.kind = PortJson::ARRAY;
      for (size_t i = 0; i < 8; ++i)
        list.items.push_back(*players[i]);
      add(later, "CRGB", Crgb(list));
    }
    if (avail["upgrades"].kind != PortJson::NONE)
      add(later, "PUPx", Pupx(avail["upgrades"], 61));
    if (avail["techs"].kind != PortJson::NONE)
      add(later, "PTEx", Ptex(avail["techs"], 44));
    if (settings["units"].kind != PortJson::NONE)
      add(later, "UNIx", Unis(settings["units"], true));
    if (settings["upgrades"].kind != PortJson::NONE)
      add(later, "UPGx", Upgs(settings["upgrades"], true));
    if (settings["techs"].kind != PortJson::NONE)
      add(later, "TECx", Tecs(settings["techs"], true));

    // ids kept as numbers that fall inside the table point nowhere, as they did
    const size_t stringCount = Size(compat["string_count"], 0, "string_count");
    std::vector<long long> sorted = reserved;
    std::sort(sorted.begin(), sorted.end());
    for (long long n : sorted) {
      if (n < 1 || static_cast<size_t>(n) > stringCount)
        continue;
      while (strings.size() < static_cast<size_t>(n)) {
        strings.push_back(Bytes());
        nowhere.push_back(false);
      }
      if (!nowhere[n - 1] && strings[n - 1].empty())
        nowhere[n - 1] = true;
    }
    const size_t count = strings.size() > stringCount ? strings.size() : stringCount;
    bool bWide = compat["strx"].kind == PortJson::BOOL && compat["strx"].b;
    Bytes table;
    if (!WriteStrings(bWide, count, &table)) {
      bWide = true;
      WriteStrings(bWide, count, &table);
    }
    if (bWide && (version == 59 || version == 63))
      version = 64;
    else if (bWide && version == 205)
      version = 206;
    // a map past what a classic map holds says so in its VER (maplimits.h)
    if (bHx || bHxte || bModUnits || width > MAP_SIDE_117 || height > MAP_SIDE_117)
      version = bExpansion ? MAP_VERSION_HX : MAP_VERSION_HX_ORIGINAL;
    else if (version == MAP_VERSION_HX || version == MAP_VERSION_HX_ORIGINAL)
      version = bExpansion ? (bWide ? 206 : 205) : (bWide ? 64 : 63);
    parts[verAt].body = word(version);
    add(parts, bWide ? "STRx" : "STR ", table);
    const PortJson &others = doc["sections"];
    for (size_t i = 0; i < others.size(); ++i) {
      // the tag's four bytes, which the tool writes as Latin-1 text
      std::vector<unsigned> cps;
      std::string tag;
      if (Utf8Decode(others.At(i)["tag"].Str(), &cps))
        for (unsigned cp : cps)
          tag.push_back(cp < 0x100 ? static_cast<char>(cp) : '?');
      if (tag.size() != 4) {
        Fail("a section's tag is not 4 characters");
        continue;
      }
      add(later, tag, Base64(others.At(i)["data"].Str()));
    }

    if (!error.empty())
      return false;
    pChk->clear();
    for (const Part &p : parts)
      Section(pChk, p.tag, p.body);
    for (const Part &p : later)
      Section(pChk, p.tag, p.body);
    return true;
  }
};

bool CompileFile(const char *pszHxm, Bytes *pChk, Package *pPkg, std::string *pError) {
  if (!ReadPackage(pszHxm, pPkg, pError))
    return false;
  Compile c;
  if (!c.Run(*pPkg, pChk)) {
    *pError = c.error;
    return false;
  }
  return true;
}

bool FileTime(const char *pszPath, ULONGLONG *pTime) {
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!GetFileAttributesExA(pszPath, GetFileExInfoStandard, &a))
    return false;
  *pTime = (static_cast<ULONGLONG>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
  return true;
}

// ---------------------------------------------------------------------------
// The check an editor runs on a map it wrote (--hxm-check, docs/hxm-for-editors.md):
// what the compile lets through but a game would not play as the map means. The compile
// has passed, so every name and player in the document reads.

const int HXM_TARGET_SIDE = 512;     // the sizes and players Hyperion aims at for now (PRD,
const int HXM_TARGET_PLAYERS = 32;   // 2026-10-12); past them a map plays, with a warning
const int UNIT_START_LOCATION = 214;
const int SLOT_COMPUTER = 5;
const int SLOT_HUMAN = 6;

struct MapCheck {
  std::vector<std::string> errors, warnings;
  Compile n;  // its names and player rules

  void Error(const char *pszFormat, ...) {
    char sz[400];
    va_list ap;
    va_start(ap, pszFormat);
    vsprintf_s(sz, pszFormat, ap);
    va_end(ap);
    errors.push_back(sz);
  }
  void Warn(const char *pszFormat, ...) {
    char sz[400];
    va_list ap;
    va_start(ap, pszFormat);
    vsprintf_s(sz, pszFormat, ap);
    va_end(ap);
    warnings.push_back(sz);
  }

  // A location named by a condition or an action, when the map has no such location.
  void LocationNamed(const PortJson &e, const std::vector<bool> &defined, size_t nTrigger,
                     const char *pszWhat, size_t nAt) {
    const long long id = n.Int(e["location"]);
    if (id > 0 && (static_cast<size_t>(id) >= defined.size() || !defined[static_cast<size_t>(id)]))
      Warn("trigger %u, %s %u, names location %lld, which the map does not have",
           static_cast<unsigned>(nTrigger + 1), pszWhat, static_cast<unsigned>(nAt + 1), id);
  }

  void Run(const Package &pkg, bool bHx) {
    const PortJson &doc = pkg.doc;
    n.bHx = bHx;

    const long long width = n.Int(doc["size"].At(0)), height = n.Int(doc["size"].At(1));
    if (width < 1 || height < 1 || width > MAP_SIDE_MAX || height > MAP_SIDE_MAX) {
      Error("the map is %lld by %lld tiles; Hyperion plays 1 to %d tiles a side", width, height,
            MAP_SIDE_MAX);
      return;
    }
    if (width > HXM_TARGET_SIDE || height > HXM_TARGET_SIDE)
      Warn("the map is %lld by %lld tiles; Hyperion plays it, but its target for now is %d a side",
           width, height, HXM_TARGET_SIDE);
    if (doc["compat"]["mtxm_bytes"].kind != PortJson::NUMBER) {
      size_t nTiles = 0;
      for (const auto &l : pkg.layers)
        if (l.first == "tiles")
          nTiles = l.second.size() / 2;
      if (nTiles < static_cast<size_t>(width * height))
        Warn("the tiles layer has %u tiles where the map has %lld; the rest are tile 0",
             static_cast<unsigned>(nTiles), width * height);
    }

    // the slots, by the game's numbers: document index i is slot i up to 10, slot i + 1 after
    const PortJson &players = doc["players"];
    std::vector<int> kind(PLAYER_SLOTS_MAX + 1, 0);
    int nSlots = 0, nPlaying = 0;
    for (size_t i = 0; i < players.size(); ++i) {
      const PortJson &p = players.At(i);
      const int s = static_cast<int>(i < static_cast<size_t>(NEUTRAL) ? i : i + 1);
      kind[s] = n.Name(kHxmSlots, p["slot"], "slot", 0);
      if (kind[s] != 0)
        nSlots = s + 1;
      if (kind[s] == SLOT_HUMAN || kind[s] == SLOT_COMPUTER)
        ++nPlaying;
      const long long force = n.Int(p["force"]);
      if (force < 0 || force > 3)
        Warn("slot %d is in force %lld; a map has forces 0 to 3", s, force);
    }
    if (nPlaying > HXM_TARGET_PLAYERS)
      Warn("%d slots are open to a human or a computer; Hyperion plays them, but its target for "
           "now is %d players", nPlaying, HXM_TARGET_PLAYERS);

    // the units: inside the map, owned by a slot it has, links that land, and a start
    // location for every slot a human or a computer can take
    const PortJson &units = doc["units"];
    std::vector<long long> ids;
    for (size_t i = 0; i < units.size(); ++i)
      ids.push_back(n.Int(units.At(i)["id"], static_cast<long long>(i)));
    std::vector<int> starts(PLAYER_SLOTS_MAX + 1, 0);
    long long nPlaced = 0;
    for (size_t i = 0; i < units.size(); ++i) {
      const PortJson &u = units.At(i);
      const int type = n.Name(kHxmUnits, u["type"], "unit", 0);
      const int owner = n.Owner(u["owner"]);
      const long long x = n.Int(u["x"]), y = n.Int(u["y"]);
      if (x < 0 || y < 0 || x >= width * 32 || y >= height * 32)
        Warn("unit %u (type %d) at %lld, %lld is outside the map", static_cast<unsigned>(i + 1), type, x, y);
      if (owner != NEUTRAL && owner >= nSlots)
        Warn("unit %u (type %d) belongs to slot %d, past the map's last slot", static_cast<unsigned>(i + 1),
             type, owner);
      const long long link = n.Int(u["link"]);
      if (link != 0 && std::find(ids.begin(), ids.end(), link) == ids.end())
        Warn("unit %u (type %d) links to unit id %lld, which no unit has", static_cast<unsigned>(i + 1),
             type, link);
      if (type == UNIT_START_LOCATION) {
        if (owner >= 0 && owner <= PLAYER_SLOTS_MAX)
          ++starts[owner];
      } else {
        ++nPlaced;
      }
    }
    for (int s = 0; s < nSlots; ++s) {
      if (kind[s] != SLOT_HUMAN && kind[s] != SLOT_COMPUTER)
        continue;
      if (starts[s] == 0)
        Warn("slot %d is open to a %s but has no start location", s,
             kind[s] == SLOT_HUMAN ? "human" : "computer");
      else if (starts[s] > 1)
        Warn("slot %d has %d start locations; the game uses one", s, starts[s]);
    }
    // a game's units (port_pools.cpp): a Hyperion map's table grows to hold the map's own and
    // its players', up to the most; a 1.17 map's is 1.17's 1,700 unless a mutator
    // grows it
    const long long nHolds = bHx ? POOL_UNITS_MAX : POOL_UNITS_117;
    if (nPlaced > nHolds)
      Warn("the map places %lld units; a game on it holds %lld", nPlaced, nHolds);

    // the locations, and those the triggers name
    const PortJson &locs = doc["locations"];
    std::vector<bool> defined(256, false);
    for (size_t i = 0; i < locs.size(); ++i) {
      const long long id = n.Int(locs.At(i)["id"]);
      if (id >= 1 && id < 256) {
        if (defined[static_cast<size_t>(id)])
          Warn("location %lld is defined twice; the last one counts", id);
        defined[static_cast<size_t>(id)] = true;
      }
    }
    const PortJson &triggers = doc["triggers"];
    for (size_t t = 0; t < triggers.size(); ++t) {
      const PortJson &trig = triggers.At(t);
      for (size_t c = 0; c < trig["conditions"].size(); ++c)
        LocationNamed(trig["conditions"].At(c), defined, t, "condition", c);
      for (size_t a = 0; a < trig["actions"].size(); ++a)
        LocationNamed(trig["actions"].At(a), defined, t, "action", a);
    }
  }
};

}  // namespace

bool PortHxmIsMap(const char *pszPath) {
  const size_t n = pszPath ? strlen(pszPath) : 0;
  return n > 4 && _stricmp(pszPath + n - 4, ".hxm") == 0;
}

bool PortHxmCompiled(const char *pszHxm, char *pszOut, size_t cchOut) {
  char szFull[MAX_PATH];
  if (!GetFullPathNameA(pszHxm, MAX_PATH, szFull, NULL))
    return false;
  // the cache's name: the map's own name and a hash of its full path
  DWORD dwHash = 2166136261u;
  for (const char *p = szFull; *p; ++p)
    dwHash = (dwHash ^ static_cast<BYTE>(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p)) * 16777619u;
  const char *pszLeaf = strrchr(szFull, '\\');
  std::string leaf = pszLeaf ? pszLeaf + 1 : szFull;
  leaf.resize(leaf.size() - 4);
  std::string folder = std::string(PortDataFolder()) + "cache\\";
  CreateDirectoryA(folder.c_str(), NULL);
  folder += "hxm\\";
  CreateDirectoryA(folder.c_str(), NULL);
  char szHash[16];
  sprintf_s(szHash, "-%08x", dwHash);
  const std::string out = folder + leaf + szHash + ".scx";

  ULONGLONG tMap = 0, tOut = 0;
  if (!FileTime(szFull, &tMap))
    return false;
  if (!FileTime(out.c_str(), &tOut) || tOut < tMap) {
    Bytes chk;
    Package pkg;
    std::string why;
    if (!CompileFile(szFull, &chk, &pkg, &why)) {
      HxmLog("hxm: %s: %s", szFull, why.c_str());
      return false;
    }
    std::vector<const char *> names(1, CHK_NAME);
    std::vector<const void *> data(1, chk.data());
    std::vector<DWORD> sizes(1, static_cast<DWORD>(chk.size()));
    for (const auto &f : pkg.files) {
      names.push_back(f.first.c_str());
      data.push_back(f.second.data());
      sizes.push_back(static_cast<DWORD>(f.second.size()));
    }
    if (!StormLibWriteArchiveFiles(out.c_str(), static_cast<int>(names.size()), names.data(),
                                   data.data(), sizes.data())) {
      HxmLog("hxm: %s: cannot write %s", szFull, out.c_str());
      return false;
    }
    HxmLog("hxm: %s -> %s, %u bytes of map, %u other files", szFull, out.c_str(),
           static_cast<unsigned>(chk.size()), static_cast<unsigned>(pkg.files.size()));
  }
  if (out.size() + 1 > cchOut)
    return false;
  memcpy(pszOut, out.c_str(), out.size() + 1);
  return true;
}

int PortHxmCompileCommand(const char *pszIn, const char *pszOut) {
  Bytes chk;
  Package pkg;
  std::string why;
  if (!CompileFile(pszIn, &chk, &pkg, &why)) {
    HxmLog("hxm: %s: %s", pszIn, why.c_str());
    fprintf(stderr, "%s: %s\n", pszIn, why.c_str());
    return 1;
  }
  FILE *f = fopen(pszOut, "wb");
  if (f == NULL)
    return 1;
  fwrite(chk.data(), 1, chk.size(), f);
  fclose(f);
  return 0;
}

int PortHxmCheckCommand(const char *pszIn, const char *pszReport) {
  Package pkg;
  std::string why;
  MapCheck check;
  int code = 0;
  if (!ReadPackage(pszIn, &pkg, &why)) {
    check.errors.push_back(why);
    code = 2;
  } else {
    Compile c;
    c.bNoGame = true;
    Bytes chk;
    if (!c.Run(pkg, &chk))
      check.errors.push_back(c.error);
    else
      check.Run(pkg, c.bHx);
    if (!check.errors.empty())
      code = 1;
  }
  FILE *f = pszReport != NULL ? fopen(pszReport, "w") : stdout;
  if (f == NULL)
    return 2;
  for (const std::string &s : check.errors)
    fprintf(f, "error: %s\n", s.c_str());
  for (const std::string &s : check.warnings)
    fprintf(f, "warning: %s\n", s.c_str());
  const size_t nErrors = check.errors.size(), nWarnings = check.warnings.size();
  fprintf(f, "%s: %u error%s, %u warning%s\n", code == 0 ? "plays" : "refused", static_cast<unsigned>(nErrors),
          nErrors == 1 ? "" : "s", static_cast<unsigned>(nWarnings), nWarnings == 1 ? "" : "s");
  if (f != stdout)
    fclose(f);
  else
    fflush(f);
  HxmLog("hxm: check %s: %u errors, %u warnings", pszIn, static_cast<unsigned>(check.errors.size()),
         static_cast<unsigned>(check.warnings.size()));
  return code;
}
