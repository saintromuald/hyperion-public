// port_chatfilter.cpp -- Settings' "Mature language filter" (docs/modules/data.md): players'
// chat shown with Remastered's list of profanity starred out. The list is
// rez\badnames.json in the Remastered install, regular expressions by locale, beside the names
// Battle.net reserves; the English ones are read and compiled on a thread of their own, which
// takes a few hundred milliseconds. What a pattern matches, case aside, is starred out, so its
// own \b marks keep it to whole words. Presentation only.
#include <windows.h>
#include <string.h>
#include <atomic>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>
#include <SDL3/SDL.h>
#include "port_chatfilter.h"
#include "../front/port_ui.h"  // PortCascRead
#include "../platform/port_json.h"

namespace {

std::atomic<bool> g_bOn{true};
std::atomic<bool> g_bLoaded{false};
bool g_bStarted;
std::vector<std::regex> g_patterns;  // written by the loader, read once g_bLoaded is set
std::mutex g_cacheLock;
std::map<std::string, std::string> g_cache;  // lines already cleaned: the lobby draws its every frame
const size_t kCacheMost = 256;

void Load(void)
{
  size_t nSize = 0;
  char *pText = static_cast<char *>(PortCascRead("rez\\badnames.json", &nSize));
  if (pText == NULL)
    return;
  std::string text(pText, nSize);
  SDL_free(pText);
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
    text.erase(0, 3);
  PortJson list;
  if (!PortJsonParse(text.c_str(), &list) || list.kind != PortJson::ARRAY)
    return;
  std::vector<std::string> seen;
  for (size_t i = 0; i < list.size(); ++i) {
    const PortJson &e = list.At(i);
    const std::string pattern = e["Name"].Str();
    if (e["NameType"].Str() != "profanity" || e["Locale"].Str().compare(0, 2, "en") != 0 ||
        e["UserDeleted"].kind == PortJson::STRING || pattern.empty())
      continue;
    bool bSeen = false;
    for (const std::string &s : seen)
      bSeen = bSeen || SDL_strcasecmp(s.c_str(), pattern.c_str()) == 0;
    if (bSeen)
      continue;
    seen.push_back(pattern);
    try {
      g_patterns.emplace_back(pattern, std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
    } catch (const std::regex_error &) {
      // a pattern the standard library's grammar does not take is left out
    }
  }
  g_bLoaded.store(true);
}

std::string Clean(const std::string &text)
{
  if (!g_bOn.load() || !g_bLoaded.load() || text.empty())
    return text;
  {
    std::lock_guard<std::mutex> lock(g_cacheLock);
    std::map<std::string, std::string>::const_iterator it = g_cache.find(text);
    if (it != g_cache.end())
      return it->second;
  }
  std::string out = text;
  for (const std::regex &re : g_patterns)
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
      for (std::ptrdiff_t k = it->position(), n = it->position() + it->length(); k < n; ++k)
        out[static_cast<size_t>(k)] = '*';
  std::lock_guard<std::mutex> lock(g_cacheLock);
  if (g_cache.size() >= kCacheMost)
    g_cache.clear();
  g_cache[text] = out;
  return out;
}

}  // namespace

void PortChatFilterSet(bool bOn)
{
  g_bOn.store(bOn);
  if (bOn && !g_bStarted && PortCascAvailable()) {
    g_bStarted = true;
    std::thread(Load).detach();
  }
}

const char *PortChatClean(const char *pszText, char *pszOut, size_t nOut)
{
  if (pszText == NULL || nOut == 0 || !g_bOn.load() || !g_bLoaded.load())
    return pszText;
  const std::string text(pszText);
  const std::string out = Clean(text);
  if (out == text)
    return pszText;
  SDL_strlcpy(pszOut, out.c_str(), nOut);
  return pszOut;
}
