// port_iscript.cpp -- a mod's image scripts, compiled from text (port_iscript.h). The text
// is the form iscript editors write, a script named rather than numbered in a .headerstart /
// .headerend block, then labelled code (docs/mod-data.md, "iscript.txt", has an example). An
// animation a header leaves out, or names [NONE], has none. An opcode's image, sprite or
// flingy argument is a number, or the name of one of the mod's own; a weapon's, a number or a
// weapon's name. The opcodes and their arguments are those CImage::RunScript reads. A jump
// and useweapon are written in forms of Hyperion's own past 1.17's last opcode, their
// target a DWORD and their weapon a WORD, and a header's animations are DWORDs, so a mod's
// scripts go on past 64 KiB and name weapons past 255.
#include "port_iscript.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>

namespace {

// An argument: a byte, a word, a label (a DWORD offset), or a word that may name an image, a
// sprite, a flingy, a weapon or a sound.
enum Arg { B, W, L, IMG, SPR, FLI, WPN, SND };

struct Opcode {
  const char *psz;
  int n;
  int nArgs;
  Arg args[3];
  bool bList;  // a byte count, then that many words (playsndrand, attackmelee)
};

const Opcode kOpcodes[] = {
    {"playfram", 0x00, 1, {W}, false},           {"playframtile", 0x01, 1, {W}, false},
    {"sethorpos", 0x02, 1, {B}, false},          {"setvertpos", 0x03, 1, {B}, false},
    {"setpos", 0x04, 2, {B, B}, false},          {"wait", 0x05, 1, {B}, false},
    {"waitrand", 0x06, 2, {B, B}, false},        {"goto", 0x07, 1, {L}, false},
    {"imgol", 0x08, 3, {IMG, B, B}, false},      {"imgul", 0x09, 3, {IMG, B, B}, false},
    {"imgolorig", 0x0A, 1, {IMG}, false},        {"switchul", 0x0B, 1, {IMG}, false},
    {"imgoluselo", 0x0D, 3, {IMG, B, B}, false}, {"imguluselo", 0x0E, 3, {IMG, B, B}, false},
    {"sprol", 0x0F, 3, {SPR, B, B}, false},      {"highsprol", 0x10, 3, {SPR, B, B}, false},
    {"lowsprul", 0x11, 3, {SPR, B, B}, false},   {"uflunstable", 0x12, 1, {FLI}, false},
    {"spruluselo", 0x13, 3, {SPR, B, B}, false}, {"sprul", 0x14, 3, {SPR, B, B}, false},
    {"sproluselo", 0x15, 2, {SPR, B}, false},    {"end", 0x16, 0, {}, false},
    {"setflipstate", 0x17, 1, {B}, false},       {"playsnd", 0x18, 1, {SND}, false},
    {"playsndrand", 0x19, 0, {}, true},          {"playsndbtwn", 0x1A, 2, {SND, SND}, false},
    {"domissiledmg", 0x1B, 0, {}, false},        {"attackmelee", 0x1C, 0, {}, true},
    {"followmaingraphic", 0x1D, 0, {}, false},   {"randcondjmp", 0x1E, 2, {B, L}, false},
    {"turnccwise", 0x1F, 1, {B}, false},         {"turncwise", 0x20, 1, {B}, false},
    {"turn1cwise", 0x21, 0, {}, false},          {"turnrand", 0x22, 1, {B}, false},
    {"setspawnframe", 0x23, 1, {B}, false},      {"sigorder", 0x24, 1, {B}, false},
    {"attackwith", 0x25, 1, {B}, false},         {"attack", 0x26, 0, {}, false},
    {"castspell", 0x27, 0, {}, false},           {"useweapon", 0x28, 1, {WPN}, false},
    {"move", 0x29, 1, {B}, false},               {"gotorepeatattk", 0x2A, 0, {}, false},
    {"engframe", 0x2B, 1, {B}, false},           {"engset", 0x2C, 1, {B}, false},
    {"__2d", 0x2D, 0, {}, false},                {"nobrkcodestart", 0x2E, 0, {}, false},
    {"nobrkcodeend", 0x2F, 0, {}, false},        {"ignorerest", 0x30, 0, {}, false},
    {"attkshiftproj", 0x31, 1, {B}, false},      {"tmprmgraphicstart", 0x32, 0, {}, false},
    {"tmprmgraphicend", 0x33, 0, {}, false},     {"setfldirect", 0x34, 1, {B}, false},
    {"call", 0x35, 1, {L}, false},               {"return", 0x36, 0, {}, false},
    {"setflspeed", 0x37, 1, {W}, false},         {"creategasoverlays", 0x38, 1, {B}, false},
    {"pwrupcondjmp", 0x39, 1, {L}, false},       {"trgtrangecondjmp", 0x3A, 2, {W, L}, false},
    {"trgtarccondjmp", 0x3B, 3, {W, W, L}, false}, {"curdirectcondjmp", 0x3C, 3, {W, W, L}, false},
    {"imgulnextid", 0x3D, 2, {B, B}, false},     {"liftoffcondjmp", 0x3F, 1, {L}, false},
    {"warpoverlay", 0x40, 1, {W}, false},        {"orderdone", 0x41, 1, {B}, false},
    {"grdsprol", 0x42, 3, {SPR, B, B}, false},   {"dogrddamage", 0x44, 0, {}, false},
};

// The animations in header order (1.17's names).
const char *const kAnims[] = {
    "Init",          "Death",         "GndAttkInit",   "AirAttkInit", "Unused1",    "GndAttkRpt",
    "AirAttkRpt",    "CastSpell",     "GndAttkToIdle", "AirAttkToIdle", "Unused2",  "Walking",
    "WalkingToIdle", "SpecialState1", "SpecialState2", "AlmostBuilt", "Built",      "Landing",
    "LiftOff",       "IsWorking",     "WorkingToIdle", "WarpIn",      "Unused3",    "StarEditInit",
    "Disable",       "Burrow",        "UnBurrow",      "Enable",
};
const int kAnimCount = sizeof kAnims / sizeof kAnims[0];

// The opcode a mod's script is written with: Hyperion's wide form of a jump or useweapon
// (CImage::RunScript's 0x45 to 0x4D), else 1.17's.
int WideOpcode(int n) {
  switch (n) {
  case 0x07: return 0x45;  // goto
  case 0x1E: return 0x46;  // randcondjmp
  case 0x39: return 0x47;  // pwrupcondjmp
  case 0x3A: return 0x48;  // trgtrangecondjmp
  case 0x3B: return 0x49;  // trgtarccondjmp
  case 0x3C: return 0x4A;  // curdirectcondjmp
  case 0x3F: return 0x4B;  // liftoffcondjmp
  case 0x35: return 0x4C;  // call
  case 0x28: return 0x4D;  // useweapon
  default: return n;
  }
}

std::vector<std::string> Words(const std::string &line) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && isspace(static_cast<unsigned char>(line[i])))
      ++i;
    size_t j = i;
    while (j < line.size() && !isspace(static_cast<unsigned char>(line[j])))
      ++j;
    if (j > i)
      out.push_back(line.substr(i, j - i));
    i = j;
  }
  return out;
}

bool Number(const std::string &s, long *pn) {
  if (s.empty())
    return false;
  char *pEnd = nullptr;
  const long n = strtol(s.c_str(), &pEnd, 0);
  if (*pEnd != '\0')
    return false;
  *pn = n;
  return true;
}

struct Fixup {
  size_t at;  // in the code
  std::string label;
  int line;
};

}  // namespace

int IscriptAnimCount(int nType) {
  static const int kCounts[31] = {2, 2, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, 14, 14, 16, 16,
                                  -1, -1, -1, -1, 22, 22, -1, 24, 26, -1, 28, 28, -1, 28, -1};
  return nType >= 0 && nType <= 30 ? kCounts[nType] : -1;
}

bool IscriptCompile(const std::string &text, unsigned nBase, const IscriptNames &names,
                    std::vector<unsigned char> *pBlock, std::vector<IscriptHeader> *pHeaders,
                    std::string *pError) {
  std::vector<unsigned char> &code = *pBlock;
  code.clear();
  pHeaders->clear();
  std::map<std::string, unsigned> labels;
  std::vector<Fixup> fixups;
  IscriptHeader *pOpen = nullptr;
  char sz[256];
  auto fail = [&](int line, const std::string &what) {
    snprintf(sz, sizeof sz, "iscript.txt, line %d: ", line);
    *pError = sz + what;
    return false;
  };
  auto word = [&](unsigned v) {
    code.push_back(static_cast<unsigned char>(v & 0xFF));
    code.push_back(static_cast<unsigned char>(v >> 8 & 0xFF));
  };
  auto dword = [&](unsigned v) {
    word(v & 0xFFFF);
    word(v >> 16);
  };

  int nLine = 0;
  size_t i = 0;
  while (i <= text.size()) {
    size_t j = text.find('\n', i);
    if (j == std::string::npos)
      j = text.size();
    std::string line = text.substr(i, j - i);
    i = j + 1;
    ++nLine;
    const size_t hash = line.find('#');
    if (hash != std::string::npos)
      line.resize(hash);
    std::vector<std::string> w = Words(line);
    if (w.empty())
      continue;

    if (w[0] == ".headerstart") {
      if (pOpen)
        return fail(nLine, "a header inside a header");
      pHeaders->push_back(IscriptHeader());
      pOpen = &pHeaders->back();
      pOpen->anims.assign(kAnimCount, std::string());
      continue;
    }
    if (w[0] == ".headerend") {
      if (!pOpen)
        return fail(nLine, ".headerend with no header open");
      if (pOpen->name.empty() || IscriptAnimCount(pOpen->nType) < 0)
        return fail(nLine, "a header needs a Name and a Type that the original has (0, 1, 2, 12 to 15, 20, 21, 23, 24, 26, 27, 29)");
      pOpen->anims.resize(IscriptAnimCount(pOpen->nType));
      pOpen = nullptr;
      continue;
    }
    if (pOpen) {
      if (w.size() != 2)
        return fail(nLine, "a header line is a field and a value");
      long n = 0;
      if (w[0] == "Name") {
        pOpen->name = w[1];
      } else if (w[0] == "Type") {
        if (!Number(w[1], &n))
          return fail(nLine, "Type is a number");
        pOpen->nType = static_cast<int>(n);
      } else {
        int k = 0;
        while (k < kAnimCount && w[0] != kAnims[k])
          ++k;
        if (k == kAnimCount)
          return fail(nLine, "no animation " + w[0]);
        if (w[1] != "[NONE]")
          pOpen->anims[k] = w[1];
      }
      continue;
    }

    // A label, then perhaps an opcode on the same line.
    size_t first = 0;
    if (w[0].back() == ':') {
      const std::string label = w[0].substr(0, w[0].size() - 1);
      if (labels.count(label))
        return fail(nLine, "the label " + label + " twice");
      labels[label] = nBase + static_cast<unsigned>(code.size());
      first = 1;
      if (w.size() == 1)
        continue;
    }
    const Opcode *pOp = nullptr;
    for (const Opcode &op : kOpcodes)
      if (w[first] == op.psz)
        pOp = &op;
    if (!pOp)
      return fail(nLine, "no opcode " + w[first]);
    code.push_back(static_cast<unsigned char>(WideOpcode(pOp->n)));
    std::vector<std::string> a(w.begin() + first + 1, w.end());
    if (pOp->bList) {
      if (a.empty() || a.size() > 255)
        return fail(nLine, std::string(pOp->psz) + " takes one sound or more");
      code.push_back(static_cast<unsigned char>(a.size()));
      for (const std::string &s : a) {
        long n = 0;
        if (!Number(s, &n))
          n = names("sound", s);
        if (n < 0 || n > 0xFFFF)
          return fail(nLine, "a sound is a number or a sound's name: " + s);
        word(static_cast<unsigned>(n));
      }
      continue;
    }
    const size_t nArgs = static_cast<size_t>(pOp->nArgs);
    if (a.size() != nArgs) {
      snprintf(sz, sizeof sz, "%s takes %u arguments", pOp->psz, static_cast<unsigned>(nArgs));
      return fail(nLine, sz);
    }
    for (size_t k = 0; k < nArgs; ++k) {
      const Arg arg = pOp->args[k];
      long n = 0;
      if (arg == L) {
        fixups.push_back({code.size(), a[k], nLine});
        dword(0);
        continue;
      }
      if (!Number(a[k], &n)) {
        const char *pszKind = arg == IMG   ? "image"
                              : arg == SPR ? "sprite"
                              : arg == FLI ? "flingy"
                              : arg == WPN ? "weapon"
                              : arg == SND ? "sound"
                                           : nullptr;
        n = pszKind ? names(pszKind, a[k]) : -1;
        if (n < 0)
          return fail(nLine, "not a number" + std::string(pszKind ? " or a " : "") +
                                 (pszKind ? pszKind : "") + " of the mod's: " + a[k]);
      }
      if (arg == WPN && (n < 0 || n > 0xFFFF))
        return fail(nLine, "a weapon from 0 to 65535: " + a[k]);
      if (arg == B) {
        if (n < -128 || n > 255)
          return fail(nLine, "a byte argument from -128 to 255: " + a[k]);
        code.push_back(static_cast<unsigned char>(n & 0xFF));
      } else {
        if (n < 0 || n > 0xFFFF)
          return fail(nLine, "a word argument from 0 to 65535: " + a[k]);
        word(static_cast<unsigned>(n));
      }
    }
  }
  if (pOpen)
    return fail(nLine, "a header with no .headerend");

  for (const Fixup &f : fixups) {
    auto it = labels.find(f.label);
    if (it == labels.end())
      return fail(f.line, "no label " + f.label);
    for (int b = 0; b < 4; ++b)
      code[f.at + b] = static_cast<unsigned char>(it->second >> (8 * b) & 0xFF);
  }

  // The headers, after the code: "SCPE", the type, three bytes of padding, the animations,
  // a DWORD each (CImage.cpp, ImageScriptAnim).
  for (IscriptHeader &h : *pHeaders) {
    while ((nBase + code.size()) % 4)
      code.push_back(0);
    h.nHeader = nBase + static_cast<unsigned>(code.size());
    code.insert(code.end(), {'S', 'C', 'P', 'E'});
    code.push_back(static_cast<unsigned char>(h.nType));
    code.insert(code.end(), {0, 0, 0});
    for (const std::string &anim : h.anims) {
      unsigned v = 0;
      if (!anim.empty()) {
        auto it = labels.find(anim);
        if (it == labels.end())
          return fail(nLine, "the header " + h.name + " names no label " + anim);
        v = it->second;
      }
      dword(v);
    }
  }
#if defined(_M_IX86)
  // 1.17's records hold a WORD offset on 32 bits (h/typelimits.h, iscript_off_t)
  if (nBase + code.size() > 0xFFFF) {
    snprintf(sz, sizeof sz, "the scripts take %u bytes past the original's, %u more than a word reaches",
             static_cast<unsigned>(code.size()), static_cast<unsigned>(nBase + code.size() - 0xFFFF));
    *pError = sz;
    return false;
  }
#endif
  return true;
}
