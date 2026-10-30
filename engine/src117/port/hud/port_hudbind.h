// port_hudbind.h -- a HUD file's bindings (docs/hud-format.md, "Bindings"): the small
// expression language in {...}, compiled once when a design loads and evaluated each frame
// against the values the data layer reads (port_hudstate.h). The same language and the same
// names as the design center's player (hudplayer.js, compile and template), so a file checked
// there means the same here.
#pragma once

#include <string>
#include <vector>

struct PortHudState;

// A value: a number, a string, or a true or false.
struct HudVal {
  enum Kind { NUM, STR, BOOL };
  Kind kind = NUM;
  double d = 0.0;
  bool b = false;
  std::string s;

  static HudVal Num(double d);
  static HudVal Str(const std::string &s);
  static HudVal Bool(bool b);
  double AsNum() const;  // as JavaScript's Number(): a string read as a number, true 1
  bool Truthy() const;
};

// The names a binding may read, each with an index into HudEnv::values. -1 for a name that
// is not one.
int HudNameIndex(const char *pszName, size_t nLen);
int HudNameCount(void);
const char *HudNameAt(int nIndex);

// The names' values for one frame.
struct HudEnv {
  std::vector<HudVal> values;
  void Fill(const PortHudState &s);
  const HudVal &Get(int nName) const;
};

// A value as a binding shows it in text: a number to at most two decimals, true and false as
// yes and no.
std::string HudShow(const HudVal &v);

// A compiled template: a string with {...} in it. One that is a single binding and nothing
// else gives that binding's value as it is; one mixed with text gives a string.
struct HudBindings {
  // Compiles s, or returns -1 with *pszError saying why.
  int Compile(const std::string &s, std::string *pszError);
  HudVal Eval(int nTemplate, const HudEnv &env) const;
  void Clear();

  // An expression's node: a literal, a name, an operator over the nodes it names, or a call.
  struct Node {
    enum Op { LIT, NAME, NEG, ADD, SUB, MUL, DIV, LT, GT, LE, GE, EQ, NE, IF, MIN, MAX, ROUND };
    Op op = LIT;
    HudVal lit;
    int nName = -1;
    std::vector<int> args;  // the nodes it reads
  };
  // A template's part: text as it is, or an expression's root node.
  struct Part {
    std::string text;
    int nNode = -1;
  };
  std::vector<Node> nodes;
  std::vector<std::vector<Part>> templates;

 private:
  HudVal Run(int nNode, const HudEnv &env) const;
};

// Whether a value holds a binding at all.
inline bool HudBound(const std::string &s) { return s.find('{') != std::string::npos; }
