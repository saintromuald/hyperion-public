// port_hudbind.cpp -- a HUD file's bindings (port_hudbind.h).
//
// The grammar is hudplayer.js's: numbers, 'strings' or "strings", the names, + - * / (+ joins
// strings), < <= > >= == !=, a ? b : c, brackets, unary minus, and min, max and round. The
// arithmetic follows JavaScript's where the two could differ on a file's values: + joins when
// either side is a string, == compares a number with a string as numbers, division by zero
// gives 0.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_hudbind.h"
#include "port_hudstate.h"

namespace {

// hudplayer.js's NAMES, in its order, then the ten groups
const char *const kNames[] = {
    "minerals", "gas", "supply.used", "supply.max", "time", "time.seconds", "apm", "selection.count", "unit.name",
    "unit.hp", "unit.hpMax", "unit.shields", "unit.shieldsMax", "unit.energy", "unit.energyMax", "unit.kills",
    "idle.count", "race", "player.colour", "selection.page", "selection.pages", "selection.subgroup",
    "selection.subgroups", "group.last", "game.multiplayer", "game.observer", "game.replay", "minimap.terrain",
    "army.count", "replay.paused", "replay.speed", "replay.step", "replay.progress", "replay.elapsed",
    "replay.length", "replay.tv", "replay.follow", "players.count", "stats.side", "stats.popup", "stats.page",
    "teams.count", "replay.seeking", "replay.live", "group.0.count", "group.1.count", "group.2.count",
    "group.3.count", "group.4.count", "group.5.count", "group.6.count", "group.7.count", "group.8.count",
    "group.9.count"};
const int kNameCount = static_cast<int>(sizeof kNames / sizeof kNames[0]);
enum {
  N_MINERALS, N_GAS, N_SUPPLY_USED, N_SUPPLY_MAX, N_TIME, N_TIME_SECONDS, N_APM, N_SELECTION_COUNT, N_UNIT_NAME,
  N_UNIT_HP, N_UNIT_HPMAX, N_UNIT_SHIELDS, N_UNIT_SHIELDSMAX, N_UNIT_ENERGY, N_UNIT_ENERGYMAX, N_UNIT_KILLS,
  N_IDLE_COUNT, N_RACE, N_PLAYER_COLOUR, N_SELECTION_PAGE, N_SELECTION_PAGES, N_SELECTION_SUBGROUP,
  N_SELECTION_SUBGROUPS, N_GROUP_LAST, N_GAME_MULTIPLAYER, N_GAME_OBSERVER, N_GAME_REPLAY, N_MINIMAP_TERRAIN,
  N_ARMY_COUNT, N_REPLAY_PAUSED, N_REPLAY_SPEED, N_REPLAY_STEP, N_REPLAY_PROGRESS, N_REPLAY_ELAPSED,
  N_REPLAY_LENGTH, N_REPLAY_TV, N_REPLAY_FOLLOW, N_PLAYERS_COUNT, N_STATS_SIDE, N_STATS_POPUP, N_STATS_PAGE,
  N_TEAMS_COUNT, N_REPLAY_SEEKING, N_REPLAY_LIVE, N_GROUP_0
};

// JavaScript's String(number), near enough for a file's values: whole numbers without a point,
// others to 15 significant digits with the trailing zeros off.
std::string NumText(double d) {
  if (d != d)
    return "NaN";
  if (d == floor(d) && fabs(d) < 1e15) {
    char sz[32];
    snprintf(sz, sizeof sz, "%.0f", d == 0.0 ? 0.0 : d);
    return sz;
  }
  char sz[40];
  snprintf(sz, sizeof sz, "%.15g", d);
  return sz;
}

struct Token {
  enum Kind { NUM, STR, NAME, OP, END };
  Kind kind = END;
  double d = 0.0;
  std::string s;
};

bool Tokenize(const std::string &src, std::vector<Token> *pOut, std::string *pszError) {
  size_t i = 0;
  const size_t n = src.size();
  while (true) {
    while (i < n && isspace(static_cast<unsigned char>(src[i])))
      ++i;
    if (i >= n)
      break;
    const char c = src[i];
    Token t;
    if (isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && isdigit(static_cast<unsigned char>(src[i + 1])))) {
      const size_t from = i;
      while (i < n && isdigit(static_cast<unsigned char>(src[i])))
        ++i;
      if (i < n && src[i] == '.') {
        ++i;
        while (i < n && isdigit(static_cast<unsigned char>(src[i])))
          ++i;
      }
      t.kind = Token::NUM;
      t.d = strtod(src.substr(from, i - from).c_str(), NULL);
    } else if (c == '\'' || c == '"') {
      const size_t close = src.find(c, i + 1);
      if (close == std::string::npos) {
        *pszError = "cannot read \"" + src.substr(i, 12) + "\"";
        return false;
      }
      t.kind = Token::STR;
      t.s = src.substr(i + 1, close - i - 1);
      i = close + 1;
    } else if (isalpha(static_cast<unsigned char>(c)) || c == '_') {
      const size_t from = i;
      while (true) {
        while (i < n && (isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_'))
          ++i;
        // a dotted part: a name's or a number's (group.3.count)
        if (i + 1 < n && src[i] == '.' &&
            (isalnum(static_cast<unsigned char>(src[i + 1])) || src[i + 1] == '_')) {
          ++i;
          continue;
        }
        break;
      }
      t.kind = Token::NAME;
      t.s = src.substr(from, i - from);
    } else {
      static const char *const kTwo[] = {"<=", ">=", "==", "!="};
      bool bTwo = false;
      for (const char *psz : kTwo)
        if (src.compare(i, 2, psz) == 0) {
          t.s = psz;
          i += 2;
          bTwo = true;
          break;
        }
      if (!bTwo) {
        if (strchr("-+*/()<>,?:", c) == NULL) {
          *pszError = "cannot read \"" + src.substr(i, 12) + "\"";
          return false;
        }
        t.s = std::string(1, c);
        ++i;
      }
      t.kind = Token::OP;
    }
    pOut->push_back(t);
  }
  return true;
}

// A recursive-descent parser over the tokens, as hudplayer.js's compile.
struct Parser {
  const std::vector<Token> &tk;
  std::vector<HudBindings::Node> &nodes;
  size_t p = 0;
  std::string err;

  Parser(const std::vector<Token> &t, std::vector<HudBindings::Node> &n) : tk(t), nodes(n) {}

  bool Peek(const char *psz) const { return p < tk.size() && tk[p].kind == Token::OP && tk[p].s == psz; }
  bool Eat(const char *psz) {
    if (!Peek(psz)) {
      Fail(std::string("expected \"") + psz + "\"");
      return false;
    }
    ++p;
    return true;
  }
  int Fail(const std::string &s) {
    if (err.empty())
      err = s;
    return -1;
  }
  int Add(HudBindings::Node::Op op, std::vector<int> args) {
    HudBindings::Node nd;
    nd.op = op;
    nd.args = std::move(args);
    nodes.push_back(nd);
    return static_cast<int>(nodes.size()) - 1;
  }

  int Ternary() {
    const int c = Compare();
    if (c < 0)
      return -1;
    if (Peek("?")) {
      ++p;
      const int a = Ternary();
      if (a < 0 || !Eat(":"))
        return -1;
      const int b = Ternary();
      if (b < 0)
        return -1;
      return Add(HudBindings::Node::IF, {c, a, b});
    }
    return c;
  }
  int Compare() {
    int l = Additive();
    while (l >= 0 && p < tk.size() && tk[p].kind == Token::OP) {
      const std::string &op = tk[p].s;
      HudBindings::Node::Op k;
      if (op == "<") k = HudBindings::Node::LT;
      else if (op == ">") k = HudBindings::Node::GT;
      else if (op == "<=") k = HudBindings::Node::LE;
      else if (op == ">=") k = HudBindings::Node::GE;
      else if (op == "==") k = HudBindings::Node::EQ;
      else if (op == "!=") k = HudBindings::Node::NE;
      else break;
      ++p;
      const int r = Additive();
      if (r < 0)
        return -1;
      l = Add(k, {l, r});
    }
    return l;
  }
  int Additive() {
    int l = Term();
    while (l >= 0 && (Peek("+") || Peek("-"))) {
      const bool bAdd = tk[p++].s == "+";
      const int r = Term();
      if (r < 0)
        return -1;
      l = Add(bAdd ? HudBindings::Node::ADD : HudBindings::Node::SUB, {l, r});
    }
    return l;
  }
  int Term() {
    int l = Unary();
    while (l >= 0 && (Peek("*") || Peek("/"))) {
      const bool bMul = tk[p++].s == "*";
      const int r = Unary();
      if (r < 0)
        return -1;
      l = Add(bMul ? HudBindings::Node::MUL : HudBindings::Node::DIV, {l, r});
    }
    return l;
  }
  int Unary() {
    if (Peek("-")) {
      ++p;
      const int u = Unary();
      return u < 0 ? -1 : Add(HudBindings::Node::NEG, {u});
    }
    return Primary();
  }
  int Primary() {
    if (p >= tk.size())
      return Fail("ends too soon");
    const Token &t = tk[p++];
    if (t.kind == Token::NUM || t.kind == Token::STR) {
      HudBindings::Node nd;
      nd.op = HudBindings::Node::LIT;
      nd.lit = t.kind == Token::NUM ? HudVal::Num(t.d) : HudVal::Str(t.s);
      nodes.push_back(nd);
      return static_cast<int>(nodes.size()) - 1;
    }
    if (t.kind == Token::OP && t.s == "(") {
      const int x = Ternary();
      if (x < 0 || !Eat(")"))
        return -1;
      return x;
    }
    if (t.kind == Token::NAME) {
      if (Peek("(")) {
        HudBindings::Node::Op op;
        if (t.s == "min") op = HudBindings::Node::MIN;
        else if (t.s == "max") op = HudBindings::Node::MAX;
        else if (t.s == "round") op = HudBindings::Node::ROUND;
        else return Fail("no function \"" + t.s + "\"; there are min, max and round");
        ++p;
        std::vector<int> args;
        if (!Peek(")")) {
          int a = Ternary();
          if (a < 0)
            return -1;
          args.push_back(a);
          while (Peek(",")) {
            ++p;
            a = Ternary();
            if (a < 0)
              return -1;
            args.push_back(a);
          }
        }
        if (!Eat(")"))
          return -1;
        return Add(op, args);
      }
      const int nName = HudNameIndex(t.s.c_str(), t.s.size());
      if (nName < 0)
        return Fail("no name \"" + t.s + "\"");
      HudBindings::Node nd;
      nd.op = HudBindings::Node::NAME;
      nd.nName = nName;
      nodes.push_back(nd);
      return static_cast<int>(nodes.size()) - 1;
    }
    return Fail("unexpected \"" + t.s + "\"");
  }
};

// JavaScript's loose ==, for the kinds a binding has
bool LooseEqual(const HudVal &a, const HudVal &b) {
  if (a.kind == HudVal::STR && b.kind == HudVal::STR)
    return a.s == b.s;
  return a.AsNum() == b.AsNum();
}

bool Less(const HudVal &a, const HudVal &b) {
  if (a.kind == HudVal::STR && b.kind == HudVal::STR)
    return a.s < b.s;
  return a.AsNum() < b.AsNum();
}

}  // namespace

HudVal HudVal::Num(double d) {
  HudVal v;
  v.kind = NUM;
  v.d = d;
  return v;
}

HudVal HudVal::Str(const std::string &s) {
  HudVal v;
  v.kind = STR;
  v.s = s;
  return v;
}

HudVal HudVal::Bool(bool b) {
  HudVal v;
  v.kind = BOOL;
  v.b = b;
  return v;
}

double HudVal::AsNum() const {
  if (kind == NUM)
    return d;
  if (kind == BOOL)
    return b ? 1.0 : 0.0;
  const char *p = s.c_str();
  while (isspace(static_cast<unsigned char>(*p)))
    ++p;
  if (*p == '\0')
    return 0.0;
  char *pEnd = NULL;
  const double r = strtod(p, &pEnd);
  while (pEnd != NULL && isspace(static_cast<unsigned char>(*pEnd)))
    ++pEnd;
  return pEnd != NULL && *pEnd == '\0' ? r : NAN;
}

bool HudVal::Truthy() const {
  if (kind == BOOL)
    return b;
  if (kind == STR)
    return !s.empty();
  return d != 0.0 && d == d;
}

int HudNameIndex(const char *pszName, size_t nLen) {
  for (int i = 0; i < kNameCount; ++i)
    if (strlen(kNames[i]) == nLen && strncmp(kNames[i], pszName, nLen) == 0)
      return i;
  return -1;
}

int HudNameCount(void) { return kNameCount; }

const char *HudNameAt(int nIndex) { return nIndex >= 0 && nIndex < kNameCount ? kNames[nIndex] : ""; }

void HudEnv::Fill(const PortHudState &s) {
  values.assign(kNameCount, HudVal());
  static const char *const kRace[3] = {"zerg", "terran", "protoss"};
  char sz[32];
  values[N_MINERALS] = HudVal::Num(s.nMinerals);
  values[N_GAS] = HudVal::Num(s.nGas);
  values[N_SUPPLY_USED] = HudVal::Num(s.nSupplyUsed);
  values[N_SUPPLY_MAX] = HudVal::Num(s.nSupplyMax);
  snprintf(sz, sizeof sz, "%d:%02d", s.nSeconds / 60, s.nSeconds % 60);
  values[N_TIME] = HudVal::Str(sz);
  values[N_TIME_SECONDS] = HudVal::Num(s.nSeconds);
  values[N_APM] = HudVal::Num(s.nApm);
  values[N_SELECTION_COUNT] = HudVal::Num(s.nSelected);
  values[N_UNIT_NAME] = HudVal::Str(s.szUnitName);
  values[N_UNIT_HP] = HudVal::Num(s.nHp);
  values[N_UNIT_HPMAX] = HudVal::Num(s.nHpMax);
  values[N_UNIT_SHIELDS] = HudVal::Num(s.nShields);
  values[N_UNIT_SHIELDSMAX] = HudVal::Num(s.nShieldsMax);
  values[N_UNIT_ENERGY] = HudVal::Num(s.nEnergy);
  values[N_UNIT_ENERGYMAX] = HudVal::Num(s.nEnergyMax);
  values[N_UNIT_KILLS] = HudVal::Num(s.nKills);
  values[N_IDLE_COUNT] = HudVal::Num(s.nIdleWorkers);
  values[N_RACE] = HudVal::Str(kRace[s.nRace >= 0 && s.nRace < 3 ? s.nRace : 1]);
  snprintf(sz, sizeof sz, "#%06x", s.nColour & 0xFFFFFFu);
  values[N_PLAYER_COLOUR] = HudVal::Str(sz);
  values[N_SELECTION_PAGE] = HudVal::Num(s.nPage + 1);
  values[N_SELECTION_PAGES] = HudVal::Num(s.nPages);
  values[N_SELECTION_SUBGROUP] = HudVal::Str(s.szUnitName);
  values[N_SELECTION_SUBGROUPS] = HudVal::Num(s.nSubgroups);
  values[N_GROUP_LAST] = HudVal::Num(s.nGroupLast);
  values[N_GAME_MULTIPLAYER] = HudVal::Bool(s.bMultiplayer);
  values[N_GAME_OBSERVER] = HudVal::Bool(s.bObserver);
  values[N_GAME_REPLAY] = HudVal::Bool(s.bReplay);
  values[N_MINIMAP_TERRAIN] = HudVal::Bool(s.bTerrain);
  values[N_ARMY_COUNT] = HudVal::Num(s.nArmy);
  values[N_REPLAY_PAUSED] = HudVal::Bool(s.bPaused);
  values[N_REPLAY_SPEED] = HudVal::Str(s.szSpeed);
  values[N_REPLAY_STEP] = HudVal::Num(s.nSpeedStep);
  values[N_REPLAY_PROGRESS] = HudVal::Num(s.fProgress);
  snprintf(sz, sizeof sz, "%d:%02d", s.nReplaySeconds / 60, s.nReplaySeconds % 60);
  values[N_REPLAY_ELAPSED] = HudVal::Str(sz);
  // a game watched as it is played has no length: empty
  if (s.nReplayLength >= 0)
    snprintf(sz, sizeof sz, "%d:%02d", s.nReplayLength / 60, s.nReplayLength % 60);
  else
    sz[0] = '\0';
  values[N_REPLAY_LENGTH] = HudVal::Str(sz);
  values[N_REPLAY_TV] = HudVal::Bool(s.bTv);
  values[N_REPLAY_FOLLOW] = HudVal::Bool(s.bFollow);
  values[N_REPLAY_LIVE] = HudVal::Bool(s.bLive);
  values[N_PLAYERS_COUNT] = HudVal::Num(s.nPlayers);
  values[N_STATS_SIDE] = HudVal::Bool(s.bStatsSide);
  values[N_STATS_POPUP] = HudVal::Bool(s.bStatsPopup);
  static const char *const s_apszPages[] = {"overview", "income", "army", "lost"};
  values[N_STATS_PAGE] = HudVal::Str(s.nStatsPage >= 0 && s.nStatsPage < 4 ? s_apszPages[s.nStatsPage] : "overview");
  values[N_TEAMS_COUNT] = HudVal::Num(s.nTeams);
  values[N_REPLAY_SEEKING] = HudVal::Bool(s.bSeeking);
  for (int g = 0; g < 10; ++g)
    values[N_GROUP_0 + g] = HudVal::Num(s.anGroups[g]);
}

const HudVal &HudEnv::Get(int nName) const {
  static const HudVal s_zero;
  return nName >= 0 && nName < static_cast<int>(values.size()) ? values[nName] : s_zero;
}

std::string HudShow(const HudVal &v) {
  if (v.kind == HudVal::NUM)
    return NumText(floor(v.d * 100.0 + 0.5) / 100.0);
  if (v.kind == HudVal::BOOL)
    return v.b ? "yes" : "no";
  return v.s;
}

int HudBindings::Compile(const std::string &s, std::string *pszError) {
  std::vector<Part> parts;
  size_t last = 0;
  size_t i = 0;
  while ((i = s.find('{', i)) != std::string::npos) {
    const size_t close = s.find_first_of("{}", i + 1);
    if (close == std::string::npos || s[close] != '}') {
      // as the player's /\{([^{}]*)\}/g: a brace with no match is text
      ++i;
      continue;
    }
    if (i > last) {
      Part text;
      text.text = s.substr(last, i - last);
      parts.push_back(text);
    }
    std::vector<Token> tk;
    std::string err;
    if (!Tokenize(s.substr(i + 1, close - i - 1), &tk, &err)) {
      *pszError = err;
      return -1;
    }
    Parser ps(tk, nodes);
    const int nRoot = ps.Ternary();
    if (nRoot < 0) {
      *pszError = ps.err.empty() ? "ends too soon" : ps.err;
      return -1;
    }
    if (ps.p < tk.size()) {
      *pszError = "unexpected \"" + (tk[ps.p].kind == Token::NUM ? NumText(tk[ps.p].d) : tk[ps.p].s) + "\"";
      return -1;
    }
    Part expr;
    expr.nNode = nRoot;
    parts.push_back(expr);
    last = i = close + 1;
  }
  if (last < s.size()) {
    Part text;
    text.text = s.substr(last);
    parts.push_back(text);
  }
  templates.push_back(std::move(parts));
  return static_cast<int>(templates.size()) - 1;
}

HudVal HudBindings::Run(int nNode, const HudEnv &env) const {
  const Node &nd = nodes[nNode];
  switch (nd.op) {
    case Node::LIT:
      return nd.lit;
    case Node::NAME:
      return env.Get(nd.nName);
    case Node::NEG:
      return HudVal::Num(-Run(nd.args[0], env).AsNum());
    case Node::ADD: {
      const HudVal a = Run(nd.args[0], env);
      const HudVal b = Run(nd.args[1], env);
      if (a.kind == HudVal::STR || b.kind == HudVal::STR)
        return HudVal::Str((a.kind == HudVal::STR ? a.s : a.kind == HudVal::BOOL ? (a.b ? "true" : "false") : NumText(a.d)) +
                           (b.kind == HudVal::STR ? b.s : b.kind == HudVal::BOOL ? (b.b ? "true" : "false") : NumText(b.d)));
      return HudVal::Num(a.AsNum() + b.AsNum());
    }
    case Node::SUB:
      return HudVal::Num(Run(nd.args[0], env).AsNum() - Run(nd.args[1], env).AsNum());
    case Node::MUL:
      return HudVal::Num(Run(nd.args[0], env).AsNum() * Run(nd.args[1], env).AsNum());
    case Node::DIV: {
      const double d = Run(nd.args[1], env).AsNum();
      return HudVal::Num(d != 0.0 && d == d ? Run(nd.args[0], env).AsNum() / d : 0.0);
    }
    case Node::LT:
      return HudVal::Bool(Less(Run(nd.args[0], env), Run(nd.args[1], env)));
    case Node::GT:
      return HudVal::Bool(Less(Run(nd.args[1], env), Run(nd.args[0], env)));
    case Node::LE: {
      const HudVal a = Run(nd.args[0], env), b = Run(nd.args[1], env);
      return HudVal::Bool(Less(a, b) || LooseEqual(a, b));
    }
    case Node::GE: {
      const HudVal a = Run(nd.args[0], env), b = Run(nd.args[1], env);
      return HudVal::Bool(Less(b, a) || LooseEqual(a, b));
    }
    case Node::EQ:
      return HudVal::Bool(LooseEqual(Run(nd.args[0], env), Run(nd.args[1], env)));
    case Node::NE:
      return HudVal::Bool(!LooseEqual(Run(nd.args[0], env), Run(nd.args[1], env)));
    case Node::IF:
      return Run(nd.args[0], env).Truthy() ? Run(nd.args[1], env) : Run(nd.args[2], env);
    case Node::MIN:
    case Node::MAX: {
      // Math.min() of nothing is Infinity, Math.max() -Infinity
      double r = nd.op == Node::MIN ? INFINITY : -INFINITY;
      for (int a : nd.args) {
        const double v = Run(a, env).AsNum();
        r = nd.op == Node::MIN ? (v < r ? v : r) : (v > r ? v : r);
      }
      return HudVal::Num(r);
    }
    case Node::ROUND:
      return HudVal::Num(nd.args.empty() ? NAN : floor(Run(nd.args[0], env).AsNum() + 0.5));
  }
  return HudVal();
}

HudVal HudBindings::Eval(int nTemplate, const HudEnv &env) const {
  if (nTemplate < 0 || nTemplate >= static_cast<int>(templates.size()))
    return HudVal();
  const std::vector<Part> &parts = templates[nTemplate];
  if (parts.size() == 1 && parts[0].nNode >= 0)
    return Run(parts[0].nNode, env);
  std::string out;
  for (const Part &part : parts)
    out += part.nNode >= 0 ? HudShow(Run(part.nNode, env)) : part.text;
  return HudVal::Str(out);
}

void HudBindings::Clear() {
  nodes.clear();
  templates.clear();
}
