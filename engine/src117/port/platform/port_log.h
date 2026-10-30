// port_log.h -- the port's log files, hyperion_<name>.log in the data folder (PortLogFile). A
// module names its log once and writes lines to it:
//
//   const PortLogger Log("hyperion_ai.log");
//   Log("slot %d plays %s", nSlot, name);  // a std::string is taken for a %s
//
// A FRESH log is emptied by the run's first line and kept open: one process's record. A SHARED
// log is opened for each line and appended to: several processes write it (the LAN room and the
// seats in one data folder), and it keeps the runs before. Every line ends with one newline, and
// lines from different threads do not interleave.
#ifndef PORT_LOG_H
#define PORT_LOG_H

#include <string>

class PortLogger {
public:
  enum Mode { FRESH, SHARED };

  explicit PortLogger(const char *pszFile, Mode mode = FRESH, const std::string &prefix = "")
      : m_pszFile(pszFile), m_mode(mode), m_prefix(prefix)
  {
  }

  // A line, formatted as printf formats it.
  template <class... Args>
  void operator()(const char *pszFormat, const Args &...args) const
  {
    Write(pszFormat, Arg(args)...);
  }

  // A line as it is.
  void Line(const char *pszLine) const;

private:
  template <class T>
  static const T &Arg(const T &v)
  {
    return v;
  }
  static const char *Arg(const std::string &s) { return s.c_str(); }
  void Write(const char *pszFormat, ...) const;

  const char *m_pszFile;
  Mode m_mode;
  std::string m_prefix;
};

#endif
