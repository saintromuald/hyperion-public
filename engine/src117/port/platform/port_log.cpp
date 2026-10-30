// port_log.cpp -- the log files port_log.h describes: a FRESH log's file kept open by its name,
// a SHARED log's opened for each line; one lock for every log's lines.
#include "port_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <mutex>
#include "port_paths.h"  // PortLogFile
#include "../tools/port_crash.h"  // PortCrashNote

namespace {

std::mutex g_lock;
std::map<std::string, FILE *> g_open;  // the FRESH logs written so far, by file name

void Put(FILE *f, const std::string &prefix, const char *pszText)
{
  fputs(prefix.c_str(), f);
  fputs(pszText, f);
  const size_t n = strlen(pszText);
  if (n == 0 || pszText[n - 1] != '\n')
    fputc('\n', f);
}

}  // namespace

void PortLogger::Line(const char *pszLine) const
{
  // the crash report's last notes, named for their log: "front: ..."
  const char *pszName = strncmp(m_pszFile, "hyperion_", 9) == 0 ? m_pszFile + 9 : m_pszFile;
  char szNote[320];
  snprintf(szNote, sizeof szNote, "%.*s: %s%s", static_cast<int>(strcspn(pszName, ".")), pszName, m_prefix.c_str(),
           pszLine);
  PortCrashNote(szNote);
  std::lock_guard<std::mutex> hold(g_lock);
  if (m_mode == SHARED) {
    FILE *f = fopen(PortLogFile(m_pszFile), "a");
    if (f == NULL)
      return;
    Put(f, m_prefix, pszLine);
    fclose(f);
    return;
  }
  auto it = g_open.find(m_pszFile);
  if (it == g_open.end())
    it = g_open.insert(std::make_pair(std::string(m_pszFile), fopen(PortLogFile(m_pszFile), "w"))).first;
  if (it->second == NULL)
    return;
  Put(it->second, m_prefix, pszLine);
  fflush(it->second);
}

void PortLogger::Write(const char *pszFormat, ...) const
{
  char sz[2048];
  va_list args;
  va_start(args, pszFormat);
  const int n = vsnprintf(sz, sizeof sz, pszFormat, args);
  va_end(args);
  if (n < 0)
    return;
  if (static_cast<size_t>(n) < sizeof sz) {
    Line(sz);
    return;
  }
  // a longer line, formatted again at its length
  std::string s(static_cast<size_t>(n) + 1, '\0');
  va_start(args, pszFormat);
  vsnprintf(&s[0], s.size(), pszFormat, args);
  va_end(args);
  s.resize(static_cast<size_t>(n));
  Line(s.c_str());
}
