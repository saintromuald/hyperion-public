// port_share.cpp -- the text a player copies to ask for help (port_share.h).
//
// It is made from what the start-up log or the crash report already holds, a line at a time,
// each line cleaned: every path keeps only the file it names, its folders written <program>,
// <data> or <game> when they are Hyperion's own and ... when not, and the last of what was
// logged also loses network addresses. Of the facts the log and report give, the run's folders
// and the environment's values are left out, and of the logs' lines only those of logs that
// never name a player, a profile, a room or a peer. The parts a report cannot do without come
// first, then the newest steps, or the call stack's top and the last lines logged, then the rest
// of the facts, then more of the steps or the frames, as many as fit.
#include "port_share.h"
#include <ctype.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "../platform/port_json.h"

namespace {

const size_t SHARE_LINE_MAX = 200;   // a longer line is cut
const size_t SHARE_ENDING_MAX = 600;  // but not the line that says why a start failed

// The logs whose lines a shared text may give (port_log.cpp names a line for its log): their
// lines are the engine's own, with no name of a player, a profile, a room or a peer in them.
const char *const kShareableLogs[] = {"ai",    "eud",    "render", "gpu",      "hd",     "video",
                                      "mods",  "modart", "lights", "mutators", "perf",   "handicap",
                                      "canvas", "sound", "hud",    "replaytv"};

// Hyperion's own folders, by what a shared text calls them.
typedef std::vector<std::pair<std::string, std::string>> Folders;

bool Sep(char c) { return c == '\\' || c == '/'; }
bool Alnum(char c) { return isalnum(static_cast<unsigned char>(c)) != 0; }

// Where a path ends: a quote, a list's separator, a tab or the line's end.
bool Ends(const std::string &s, size_t k)
{
  const char c = s[k];
  return c == '\r' || c == '\n' || c == '\t' || c == '"' || c == '\'' || c == ';' || c == '|' || c == '<' ||
         c == '>' || (c == ',' && k + 1 < s.size() && s[k + 1] == ' ');
}

// Whether a path begins at s[i]: a drive's (C:\), a share's (\\server), the player's own folder as
// the logs write it (%USERPROFILE%, ~), or one from the root off Windows (/usr/lib/...).
bool PathAt(const std::string &s, size_t i)
{
  const char c = s[i], before = i > 0 ? s[i - 1] : ' ';
  if (isalpha(static_cast<unsigned char>(c)) && i + 2 < s.size() && s[i + 1] == ':' && Sep(s[i + 2]) &&
      !Alnum(before))
    return true;
  if (i > 0 && strchr(" \t\"'=([<,;", before) == NULL)
    return false;
  if (c == '\\' && i + 1 < s.size() && s[i + 1] == '\\')
    return true;
  if (c == '%' && s.compare(i, 13, "%USERPROFILE%") == 0)
    return true;
  if (c == '~' && (i + 1 == s.size() || Sep(s[i + 1])))
    return true;
  if (c == '/' && i + 1 < s.size() && Alnum(s[i + 1]))
    for (size_t k = i + 1; k < s.size() && !Ends(s, k); k++)
      if (s[k] == '/')
        return true;
  return false;
}

// Off Windows a path's engine form is Z:\home\..., and /home/... is the same path.
size_t DriveZ(const std::string &s)
{
#if defined(_WIN32)
  (void)s;
  return 0;
#else
  return s.size() > 2 && (s[0] == 'Z' || s[0] == 'z') && s[1] == ':' && Sep(s[2]) ? 2 : 0;
#endif
}

std::string Lower(std::string s)
{
  s.erase(0, DriveZ(s));
  for (char &c : s)
    c = Sep(c) ? '\\' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

// A path's folders as a shared text writes them, ending in their separator.
std::string FolderAs(const std::string &dir, const Folders &known)
{
  const std::string lower = Lower(dir);
  for (const std::pair<std::string, std::string> &f : known) {
    const std::string k = Lower(f.first);
    if (!k.empty() && lower.compare(0, k.size(), k) == 0)
      return f.second + dir.substr(DriveZ(dir) + k.size() - 1);  // the known folder's own separator kept
  }
  return std::string("...") + dir[dir.size() - 1];
}

std::string Paths(const std::string &s, const Folders &known)
{
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (!PathAt(s, i)) {
      out += s[i++];
      continue;
    }
    // the path's folders end at its last separator before where it ends, or another path begins
    size_t end = i, last = std::string::npos;
    for (size_t k = i; k < s.size() && !Ends(s, k); k++) {
      if (k > i + 1 && s[k - 1] == ' ' && PathAt(s, k))
        break;
      if (Sep(s[k]))
        last = k;
      end = k + 1;
    }
    if (last == std::string::npos) {
      out.append(s, i, end - i);
      i = end;
      continue;
    }
    // one of Hyperion's folders named whole, with no separator after it, as an option names it,
    // and perhaps more of the command line after it
    size_t nNamed = 0;
    for (size_t p = end; p > i && nNamed == 0; p--) {
      if (p != end && s[p] != ' ')
        continue;
      const std::string named = Lower(s.substr(i, p - i) + '\\');
      for (const std::pair<std::string, std::string> &f : known)
        if (nNamed == 0 && Lower(f.first) == named) {
          out += f.second;
          nNamed = p - i;
        }
    }
    if (nNamed != 0) {
      i += nNamed;
      continue;
    }
    out += FolderAs(s.substr(i, last + 1 - i), known);
    i = last + 1;
  }
  return out;
}

// An IPv4 address at s[i], its port with it: where it ends, or i.
size_t Ip4At(const std::string &s, size_t i)
{
  if (i > 0 && (Alnum(s[i - 1]) || s[i - 1] == '.'))
    return i;
  size_t k = i;
  for (int nPart = 0; nPart < 4; nPart++) {
    if (nPart > 0) {
      if (k >= s.size() || s[k] != '.')
        return i;
      k++;
    }
    const size_t start = k;
    unsigned v = 0;
    while (k < s.size() && isdigit(static_cast<unsigned char>(s[k])) && k - start < 3)
      v = v * 10 + static_cast<unsigned>(s[k++] - '0');
    if (k == start || v > 255 || (k < s.size() && isdigit(static_cast<unsigned char>(s[k]))))
      return i;
  }
  if (k < s.size() && (Alnum(s[k]) || (s[k] == '.' && k + 1 < s.size() && Alnum(s[k + 1]))))
    return i;
  if (k + 1 < s.size() && s[k] == ':' && isdigit(static_cast<unsigned char>(s[k + 1])))
    for (k++; k < s.size() && isdigit(static_cast<unsigned char>(s[k]));)
      k++;
  return k;
}

// An IPv6 or a hardware address at s[i]: hex digits and colons, with "::" or five colons.
size_t HexAddressAt(const std::string &s, size_t i)
{
  if (i > 0 && (Alnum(s[i - 1]) || s[i - 1] == ':'))
    return i;
  size_t k = i;
  int nColons = 0;
  bool bDouble = false;
  while (k < s.size() && (isxdigit(static_cast<unsigned char>(s[k])) || s[k] == ':')) {
    if (s[k] == ':') {
      nColons++;
      bDouble = bDouble || (k + 1 < s.size() && s[k + 1] == ':');
    }
    k++;
  }
  if (k < s.size() && Alnum(s[k]))
    return i;
  return (bDouble && nColons >= 2) || nColons >= 5 ? k : i;
}

std::string Addresses(const std::string &s)
{
  std::string out;
  for (size_t i = 0; i < s.size();) {
    size_t k = isdigit(static_cast<unsigned char>(s[i])) ? Ip4At(s, i) : i;
    if (k == i)
      k = HexAddressAt(s, i);
    if (k > i) {
      out += "<address>";
      i = k;
    } else {
      out += s[i++];
    }
  }
  return out;
}

// A line cut to nMax, not inside a character.
std::string Cut(std::string s, size_t nMax)
{
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
    s.pop_back();
  if (s.size() <= nMax)
    return s;
  size_t n = nMax - 3;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
    n--;
  return s.substr(0, n) + "...";
}

std::string Clean(const std::string &s, const Folders &known, bool bAddresses, size_t nMax = SHARE_LINE_MAX)
{
  const std::string paths = Paths(s, known);
  return Cut(bAddresses ? Addresses(paths) : paths, nMax);
}

std::vector<std::string> Lines(const std::string &text)
{
  std::vector<std::string> lines;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find('\n', i);
    if (j == std::string::npos)
      j = text.size();
    std::string line = text.substr(i, j - i);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(line);
    i = j + 1;
  }
  return lines;
}

struct Fact {
  std::string part, name, value;
};

const Fact *Find(const std::vector<Fact> &facts, const char *pszPart, const char *pszName)
{
  for (const Fact &f : facts)
    if (f.part == pszPart && f.name == pszName)
      return &f;
  return NULL;
}

// The run's folders, from the facts, longest first so a folder inside another is found first.
Folders Known(const std::vector<Fact> &facts)
{
  Folders known;
  if (const Fact *f = Find(facts, "run", "program folder"))
    known.push_back(std::make_pair(f->value, std::string("<program>")));
  if (const Fact *f = Find(facts, "run", "data folder"))
    known.push_back(std::make_pair(f->value, std::string("<data>")));
  if (const Fact *f = Find(facts, "run", "environment")) {
    const size_t at = f->value.find("HYPERION_GAME=");
    if (at != std::string::npos) {
      const size_t from = at + 14, to = f->value.find(';', from);
      known.push_back(std::make_pair(f->value.substr(from, to == std::string::npos ? to : to - from),
                                     std::string("<game>")));
    }
  }
  for (std::pair<std::string, std::string> &k : known)
    if (!k.first.empty() && !Sep(k.first.back()))
      k.first += '\\';
  std::sort(known.begin(), known.end(),
            [](const std::pair<std::string, std::string> &a, const std::pair<std::string, std::string> &b) {
              return a.first.size() > b.first.size();
            });
  return known;
}

// The options a run was started with: its command line less the program.
std::string Options(const std::string &line)
{
  size_t i = 0;
  if (!line.empty() && line[0] == '"') {
    i = line.find('"', 1);
    i = i == std::string::npos ? line.size() : i + 1;
  } else {
    i = line.find(' ');
    i = i == std::string::npos ? line.size() : i;
  }
  while (i < line.size() && line[i] == ' ')
    i++;
  return line.substr(i);
}

// The facts as lines: those a report needs (the build, the system, the presenter) in pMust, the
// rest it gives when there is room in pMay. The run's folders are left out, and of the
// environment only its names.
void FactLines(const std::vector<Fact> &facts, const Folders &known, std::vector<std::string> *pMust,
               std::vector<std::string> *pMay)
{
  const Fact *pVersion = Find(facts, "build", "version");
  const Fact *pCommit = Find(facts, "build", "commit");
  if (pVersion != NULL)
    pMust->push_back("Hyperion " + pVersion->value + (pCommit != NULL ? " (" + pCommit->value + ")" : ""));
  const char *const kSystem[] = {"os", "cpu", "memory"};
  for (const char *pszName : kSystem)
    if (const Fact *f = Find(facts, "system", pszName))
      pMust->push_back(f->name + ": " + f->value);
  int nDisplays = 0;
  for (const Fact &f : facts) {
    if (f.part == "presenter")
      pMust->push_back("presenter " + f.name + ": " + Clean(f.value, known, false));
    nDisplays += f.part.compare(0, 8, "display ") == 0 && f.name == "mode";
  }
  if (const Fact *f = Find(facts, "display 1", "mode"))
    pMay->push_back("display: " + f->value + (nDisplays > 1 ? " (" + std::to_string(nDisplays) + " displays)" : ""));
  const char *const kMore[] = {"sound", "sdl", "locale"};
  for (const char *pszName : kMore)
    if (const Fact *f = Find(facts, "system", pszName))
      pMay->push_back(f->name + ": " + f->value);
  for (const Fact &f : facts)
    if (f.part == "install")
      pMay->push_back("install " + f.name + ": " + Clean(f.value, known, false));
  if (const Fact *f = Find(facts, "run", "command line")) {
    const std::string options = Options(f->value);
    if (!options.empty())
      pMay->push_back("options: " + Clean(options, known, false));
  }
  if (const Fact *f = Find(facts, "run", "settings"))
    pMay->push_back("settings: " + Clean(f->value, known, false));
  if (const Fact *f = Find(facts, "run", "environment")) {
    std::string names;
    for (size_t i = 0; i < f->value.size();) {
      size_t j = f->value.find("; ", i);
      if (j == std::string::npos)
        j = f->value.size();
      const std::string each = f->value.substr(i, j - i);
      names += (names.empty() ? "" : ", ") + each.substr(0, each.find('='));
      i = j + 2;
    }
    if (!names.empty())
      pMay->push_back("environment: " + names);
  }
  if (const Fact *f = Find(facts, "build", "symbols"))
    pMay->push_back("symbols: " + f->value);
}

// What fits of a shared text, counted with the newline each line ends in.
struct Room {
  size_t left = PORT_SHARE_MAX - 8;  // the code block's marks
  bool Take(const std::string &line)
  {
    if (line.size() + 1 > left)
      return false;
    left -= line.size() + 1;
    return true;
  }
};

std::string Join(const std::vector<std::string> &lines)
{
  std::string s;
  for (const std::string &line : lines)
    s += line + "\n";
  if (!s.empty())
    s.pop_back();
  return s;
}

// The newest lines of a list that fit, nMost of them at most, from its end; returns how many.
size_t TakeNewest(Room &room, const std::vector<std::string> &list, size_t nTaken, size_t nMost)
{
  while (nTaken < list.size() && nTaken < nMost && room.Take(list[list.size() - 1 - nTaken]))
    nTaken++;
  return nTaken;
}

std::vector<Fact> FactsOfText(const std::vector<std::string> &lines)
{
  // "part      name: value", the part in ten columns, before the first step
  std::vector<Fact> facts;
  for (const std::string &line : lines) {
    if (!line.empty() && line[0] == '[')
      break;
    if (line.size() < 12 || line[0] == ' ')
      continue;
    Fact f;
    f.part = line.substr(0, 10);
    while (!f.part.empty() && f.part.back() == ' ')
      f.part.pop_back();
    const size_t colon = line.find(": ", 10);
    if (f.part.empty() || colon == std::string::npos)
      continue;
    f.name = line.substr(10, colon - 10);
    while (!f.name.empty() && f.name[0] == ' ')
      f.name.erase(0, 1);
    f.value = line.substr(colon + 2);
    facts.push_back(f);
  }
  return facts;
}

}  // namespace

std::string PortShareClean(const std::string &text, bool bAddresses)
{
  std::vector<std::string> out;
  for (const std::string &line : Lines(text))
    out.push_back(Clean(line, Folders(), bAddresses));
  return Join(out);
}

std::string PortShareBlock(const std::string &text) { return "```\n" + text + "\n```"; }

std::string PortShareStartup(const char *pszTitle, const std::string &log)
{
  const std::vector<std::string> lines = Lines(log);
  const std::vector<Fact> facts = FactsOfText(lines);
  const Folders known = Known(facts);
  // the steps, "[  0.042] window: created" as "0.042 window: created", and how the start ended
  std::vector<std::string> steps;
  std::string ending;
  bool bStep = false;
  for (const std::string &line : lines) {
    if (line.empty() || (line[0] != '[' && !bStep))
      continue;
    bStep = true;
    std::string step = line;
    if (line[0] == '[') {
      const size_t close = line.find("] ");
      size_t from = 1;
      while (from < line.size() && line[from] == ' ')
        from++;
      if (close != std::string::npos)
        step = line.substr(from, close - from) + " " + line.substr(close + 2);
      const std::string what = close != std::string::npos ? line.substr(close + 2) : line;
      if (what.compare(0, 8, "failed: ") == 0 || what.compare(0, 9, "stalled: ") == 0 || ending.empty() ||
          ending.compare(0, 7, "failed:") != 0)
        ending = what;
      // the failure is the text's second line, so its step only says when
      if (close != std::string::npos && what.compare(0, 8, "failed: ") == 0)
        step = line.substr(from, close - from) + " failed, as above";
    }
    steps.push_back(Clean(step, known, true));
  }
  std::vector<std::string> must, may;
  must.push_back(pszTitle);
  if (!ending.empty())
    must.push_back(
        Clean(ending.compare(0, 8, "failed: ") == 0 ? ending : "last step, " + ending, known, true, SHARE_ENDING_MAX));
  FactLines(facts, known, &must, &may);
  const std::string stepsHead = "steps (seconds since the start):";
  Room room;
  for (const std::string &line : must)
    room.Take(line);
  room.Take(stepsHead);
  size_t nSteps = TakeNewest(room, steps, 0, 8);
  std::vector<std::string> mayTaken;
  for (const std::string &line : may)
    if (room.Take(line))
      mayTaken.push_back(line);
  nSteps = TakeNewest(room, steps, nSteps, steps.size());
  std::vector<std::string> out = must;
  out.insert(out.end(), mayTaken.begin(), mayTaken.end());
  if (nSteps > 0) {
    out.push_back(stepsHead);
    if (nSteps < steps.size())
      out.back() = "steps (seconds since the start; the last " + std::to_string(nSteps) + " of " +
                   std::to_string(steps.size()) + "):";
    out.insert(out.end(), steps.end() - static_cast<std::ptrdiff_t>(nSteps), steps.end());
  }
  return Join(out);
}

std::string PortShareCrash(const PortJson &report)
{
  std::vector<Fact> facts;
  const PortJson &system = report["system"];
  for (const PortJsonMember &part : system.members)
    for (const PortJsonMember &fact : part.second.members)
      facts.push_back({part.first, fact.first, fact.second.Str()});
  const Folders known = Known(facts);
  std::vector<std::string> must, may;
  must.push_back("Hyperion stopped on an error");
  must.push_back("what: " + Clean(report["what"].Str(), known, true, SHARE_ENDING_MAX));
  must.push_back("where: " + Clean(report["where"].Str(), known, false));
  if (!report["access"].Str().empty())
    must.push_back("access: " + report["access"].Str());
  must.push_back("thread: " + report["thread"].Str());
  FactLines(facts, known, &must, &may);
  const PortJson &session = report["session"];
  must.push_back(std::string("session: the start ") +
                 (session["startComplete"].kind == PortJson::BOOL && session["startComplete"].b ? "completed"
                                                                                               : "had not completed") +
                 "; now " + Clean(session["now"].Str(), known, true));
  const std::string game = session["game"].Str();
  if (!game.empty())
    must.push_back("game: " + Clean(game, known, true));
  const std::string played = session["games played"].Str();
  if (!played.empty())
    may.insert(may.begin(), "games before this one: " + std::to_string(std::count(played.begin(), played.end(), '\n')));
  // the stack, its top first, and the last lines of the logs a shared text may give
  std::vector<std::string> frames;
  const PortJson &stack = report["stack"];
  // the reporter's and the C library's frames above the one the report names as where, left out
  const std::string where = report["where"].Str();
  size_t first = 0;
  for (size_t i = 1; i < stack.size() && first == 0; i++) {
    const std::string at = stack.At(i)["module"].Str() + "+" + stack.At(i)["offset"].Str();
    if (where.compare(0, at.size(), at) == 0 && (where.size() == at.size() || where[at.size()] == ' '))
      first = i;
  }
  for (size_t i = first; i < stack.size(); i++) {
    const PortJson &f = stack.At(i);
    std::string line = "#" + std::to_string(i) + " " + f["module"].Str() + "+" + f["offset"].Str();
    if (!f["function"].Str().empty())
      line += " " + f["function"].Str();
    if (!f["file"].Str().empty())
      line += " (" + f["file"].Str() + ":" + std::to_string(static_cast<unsigned>(f["line"].Num())) + ")";
    frames.push_back(Clean(line, known, false));
  }
  std::vector<std::string> notes;
  const PortJson &all = report["notes"];
  for (size_t i = 0; i < all.size(); i++) {
    const std::string note = all.At(i).Str();
    // "[     3.196] ai: ...": the log is the word after the time
    const size_t close = note.find("] ");
    if (close == std::string::npos)
      continue;
    const std::string rest = note.substr(close + 2);
    const std::string log = rest.substr(0, rest.find(':'));
    bool bShareable = false;
    for (const char *pszLog : kShareableLogs)
      bShareable = bShareable || log == pszLog;
    if (!bShareable)
      continue;
    size_t from = 1;
    while (from < close && note[from] == ' ')
      from++;
    notes.push_back(Clean(note.substr(from, close - from) + " " + rest, known, true));
  }
  const std::string stackHead = "stack:", notesHead = "last lines (seconds since the start):";
  Room room;
  for (const std::string &line : must)
    room.Take(line);
  room.Take(stackHead);
  size_t nFrames = 0;
  while (nFrames < frames.size() && nFrames < 12 && room.Take(frames[nFrames]))
    nFrames++;
  const size_t nNotes = !notes.empty() && room.Take(notesHead) ? TakeNewest(room, notes, 0, 8) : 0;
  std::vector<std::string> mayTaken;
  for (const std::string &line : may)
    if (room.Take(line))
      mayTaken.push_back(line);
  while (nFrames < frames.size() && nFrames < 24 && room.Take(frames[nFrames]))
    nFrames++;
  std::vector<std::string> out = must;
  out.insert(out.end(), mayTaken.begin(), mayTaken.end());
  out.push_back(stackHead);
  out.insert(out.end(), frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(nFrames));
  if (nNotes > 0) {
    out.push_back(notesHead);
    out.insert(out.end(), notes.end() - static_cast<std::ptrdiff_t>(nNotes), notes.end());
  }
  return Join(out);
}
