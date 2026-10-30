// port_lanroom_replay.cpp -- a match's feeds in the room: each seat's beginning,
// record and end as they come (port_lanfeed.cpp), what the watchers are sent a delay behind,
// and the replay the room writes when every feed has ended. Presentation and transport only.
#include "port_lanroom_internal.h"
#include <string.h>
#include <time.h>
#include <algorithm>
#include "../../h/replay.h"  // replay_write_parts
#include "port_lannet.h"
#include "../platform/port_paths.h"
#include "../platform/port_util.h"  // PortNowMs

namespace LanRoom {

// ---------------------------------------------------------------------------
// The feeds: each seat's record of its match (port_lanfeed.cpp), passed on to who watches, the
// room's delay behind, and the replay the room keeps of each match.

int DelaySeconds(void)
{
  const std::string d = Setting("delay", "10 s");
  return d == "5 s" ? 5 : d == "30 s" ? 30 : d == "1 min" ? 60 : 10;
}

bool WatchingOn(void)
{
  return Setting("watch", "Anyone in the room") != "Nobody";
}

// A frame at the match's speed, in milliseconds (1.17's Slowest to Fastest).
uint32_t FrameMs(const Match &m)
{
  return PortSpeedFrameMs(m.nSpeed);
}

// Whether a seat's feed may still grow: not ended, its device here, and in the match or, the
// match ended under it, leaving it (its end comes as its game ends).
bool Growing(const Match &m, int nDev, const Feed &f)
{
  if (f.bEnded)
    return false;
  const Device *d = Dev(nDev);
  if (d == NULL || d->bGone)
    return false;
  return d->nMatch == m.nId || PortNowMs() - f.qwHeard < 15000;
}

// A FEED_START's fields after the match: the replay's magic, its header, the map and Hyperion's
// sections, the header the size the replay's writer reads (WriteReplay).
bool FeedStartWellFormed(const std::vector<uint8_t> &start)
{
  Reader r(start);
  r.D();
  const size_t nHeader = r.Bytes().size();
  const bool bChk = !r.Bytes().empty();
  r.Bytes();
  return !r.bBad && nHeader == kReplayHeader && bChk;
}

void FeedHeard(Device &d, const std::vector<uint8_t> &data)
{
  Reader r(data);
  const uint8_t bKind = r.B();
  Match *m = MatchOf(static_cast<int>(r.D()));
  if (m == NULL || r.bBad || m->bFeedDone)
    return;
  Feed &f = m->feeds[d.nId];
  f.qwHeard = PortNowMs();
  if (bKind == FEED_START) {
    // once a match from each seat (LanFeedSeatFrame): a second would cut the record short
    // under those watching it
    if (!f.start.empty())
      return;
    std::vector<uint8_t> start(data.begin() + static_cast<std::ptrdiff_t>(r.at), data.end());
    if (!FeedStartWellFormed(start)) {
      Log("match %d: %s's feed began malformed, and is not kept", m->nId, d.name.c_str());
      return;
    }
    f.start.swap(start);
    if (!m->nFeed)
      m->nFeed = d.nId;
    Log("match %d: %s's feed began", m->nId, d.name.c_str());
  } else if (bKind == FEED_RECORD) {
    const uint32_t dwAt = r.D();
    const uint32_t dwFed = r.D();
    const std::vector<uint8_t> bytes = r.Bytes();
    if (r.bBad || f.start.empty() || dwAt != f.record.size()) {
      Log("match %d: %s's feed at %u, where %u were expected", m->nId, d.name.c_str(), dwAt,
          static_cast<unsigned>(f.record.size()));
      return;
    }
    f.record.insert(f.record.end(), bytes.begin(), bytes.end());
    if (dwFed > f.dwFed)
      f.dwFed = dwFed;
    // every seat records the same game: a difference is said once
    for (const auto &kv : m->feeds) {
      const Feed &o = kv.second;
      const size_t nTo = (std::min)(f.record.size(), o.record.size());
      if (kv.first == d.nId || m->bFeedsDiffer || dwAt >= nTo)
        continue;
      if (memcmp(f.record.data() + dwAt, o.record.data() + dwAt, nTo - dwAt) != 0) {
        m->bFeedsDiffer = true;
        Log("match %d: the seats' records differ after byte %u", m->nId, dwAt);
      }
    }
  } else if (bKind == FEED_PICTURE) {
    m->feeds.erase(d.nId);
    for (auto &kv : g_devices)
      if (kv.second.bAdmin && !kv.second.bGone && !kv.second.bWeb)
        LanNet::Send(kv.second.nConn, ROOM_FEED, data.data(), data.size());
    return;
  } else if (bKind == FEED_HEAT) {
    // the copy's, at the live edge: on to those watching the match, ahead of their picture
    m->feeds.erase(d.nId);
    m->qwHeat = PortNowMs();
    Reader h(data);
    h.B();
    h.D();
    h.D();
    h.D();
    h.D();
    m->fHeat = h.D() / 100.0f;
    for (int nDev : m->watchers) {
      const Device *pw = Dev(nDev);
      if (pw != NULL && !pw->bGone && !pw->bCopy && m->sent.count(nDev))
        LanNet::Send(pw->nConn, ROOM_FEED, data.data(), data.size());
    }
    return;
  } else if (bKind == FEED_HASH) {
    const std::vector<uint8_t> record(data.begin() + static_cast<std::ptrdiff_t>(r.at), data.end());
    if (record.size() >= 4) {
      uint32_t dwFrame;
      memcpy(&dwFrame, record.data(), 4);
      m->watchHashes[dwFrame][d.nId] = record;
    }
    m->feeds.erase(d.nId);  // a watcher has no feed of its own
    return;
  } else if (bKind == FEED_END) {
    f.dwEnd = r.D();
    r.D();
    f.end.assign(data.begin() + static_cast<std::ptrdiff_t>(r.at), data.end());
    f.bEnded = true;
    Log("match %d: %s's feed ended at frame %u, %u bytes", m->nId, d.name.c_str(), f.dwEnd,
        static_cast<unsigned>(f.record.size()));
  }
}

// The feed the watchers get: the one they have while it grows, or the longest growing one, or,
// once none grows, the longest.
Feed *FeedOf(Match &m)
{
  auto cur = m.feeds.find(m.nFeed);
  if (cur != m.feeds.end() && Growing(m, cur->first, cur->second))
    return &cur->second;
  int nBest = 0;
  size_t nBestLen = 0;
  bool bBestGrowing = false;
  for (const auto &kv : m.feeds) {
    if (kv.second.start.empty())
      continue;
    const bool bGrowing = Growing(m, kv.first, kv.second);
    const size_t nLen = kv.second.record.size();
    if (nBest == 0 || (bGrowing && !bBestGrowing) || (bGrowing == bBestGrowing && nLen > nBestLen)) {
      nBest = kv.first;
      nBestLen = nLen;
      bBestGrowing = bGrowing;
    }
  }
  if (nBest == 0)
    return NULL;
  if (nBest != m.nFeed) {
    if (m.nFeed)
      Log("match %d: the watchers' feed goes on from another seat's", m.nId);
    m.nFeed = nBest;
  }
  return &m.feeds[nBest];
}

std::string FileName(std::string s)
{
  for (char &c : s)
    if (strchr("\\/:*?\"<>|", c) != NULL || static_cast<unsigned char>(c) < 0x20)
      c = '-';
  return s;
}

// The match's replay, as the room keeps it: in the host's replays, in a folder of the room's name.
void WriteReplay(Match &m, const Feed &f)
{
  Reader rs(f.start);
  const uint32_t dwMagic = rs.D();
  std::vector<uint8_t> header = rs.Bytes();
  const std::vector<uint8_t> chk = rs.Bytes();
  std::vector<uint8_t> sections = rs.Bytes();
  if (!f.end.empty()) {
    Reader re(f.end);
    re.D();
    const std::vector<uint8_t> endHeader = re.Bytes();
    const std::vector<uint8_t> endSections = re.Bytes();
    if (!re.bBad && endHeader.size() == header.size()) {
      header = endHeader;
      sections = endSections;
    }
  }
  if (rs.bBad || header.size() != kReplayHeader || chk.empty())
    return;
  const uint32_t dwEnd = f.bEnded ? f.dwEnd : f.dwFed;
  memcpy(&header[1], &dwEnd, 4);  // its length (replay_set_total_frames's)
  std::string folder = std::string(PortDataFolder()) + "maps";
  CreateDirectoryA(folder.c_str(), NULL);
  folder += "\\replays";
  CreateDirectoryA(folder.c_str(), NULL);
  folder += "\\" + FileName(g_name.empty() ? "Room" : g_name);
  CreateDirectoryA(folder.c_str(), NULL);
  time_t t = time(NULL);
  char szWhen[32];
  strftime(szWhen, sizeof szWhen, "%Y-%m-%d %H.%M", localtime(&t));
  const std::string path = folder + "\\" + FileName(m.name) + " " + szWhen + ".rep";
  if (replay_write_parts(path.c_str(), dwMagic, header.data(), f.record.data(), static_cast<int>(f.record.size()),
                         chk.data(), static_cast<DWORD>(chk.size()), sections.empty() ? NULL : sections.data(),
                         static_cast<DWORD>(sections.size()))) {
    m.replay = path;
    Log("match %d: its replay is %s (%u frames)", m.nId, path.c_str(), dwEnd);
  } else {
    Log("match %d: its replay could not be written to %s", m.nId, path.c_str());
  }
}

// Those watching, held against the players: a frame's hash once the players' is here.
void CheckWatchers(Match &m)
{
  for (auto it = m.watchHashes.begin(); it != m.watchHashes.end();) {
    auto ref = m.hashes.find(it->first);
    if (ref == m.hashes.end()) {
      // the players never sent this one (gone past it): let it go once well behind theirs
      if (!m.hashes.empty() && it->first + 2400 < m.hashes.rbegin()->first)
        it = m.watchHashes.erase(it);
      else
        ++it;
      continue;
    }
    for (const auto &w : it->second) {
      const Device *pw = Dev(w.first);
      const bool bCopy = pw != NULL && pw->bCopy;
      if (w.second == ref->second) {
        ++(bCopy ? m.dwCopyAgreed : m.dwWatchAgreed);
      } else {
        ++(bCopy ? m.dwCopyDiffered : m.dwWatchDiffered);
        if (std::find(m.outOfStep.begin(), m.outOfStep.end(), w.first) == m.outOfStep.end()) {
          m.outOfStep.push_back(w.first);
          const std::string parts = HashPartsDiffer(ref->second, w.second);
          const Device *d = Dev(w.first);
          Log("match %d: %s, watching, is out of step with the players at frame %u: %s", m.nId,
              d ? d->name.c_str() : "?", it->first, parts.c_str());
        }
      }
    }
    it = m.watchHashes.erase(it);
  }
}

void PumpFeed(Match &m)
{
  // a copy gone, by its replay's end or otherwise
  if (m.hCopy != NULL && WaitForSingleObject(m.hCopy, 0) == WAIT_OBJECT_0)
    StopCopy(m);
  CheckWatchers(m);
  if (m.feeds.empty() || m.bFeedDone)
    return;
  Feed *f = FeedOf(m);
  if (f == NULL)
    return;
  bool bDone = true;
  for (const auto &kv : m.feeds)
    bDone = bDone && !Growing(m, kv.first, kv.second);
  for (int nDev : m.watchers) {
    Device *d = Dev(nDev);
    if (d == NULL || d->bGone || d->bWeb)
      continue;
    auto it = m.sent.find(nDev);
    if (it == m.sent.end()) {
      Writer w;
      w.B(FEED_START);
      w.D(static_cast<uint32_t>(m.nId));
      w.D(static_cast<uint32_t>(d->bCopy ? 0 : DelaySeconds()));
      w.D(FrameMs(m));
      w.v.insert(w.v.end(), f->start.begin(), f->start.end());
      LanNet::Send(d->nConn, ROOM_FEED, w.v.data(), w.v.size());
      it = m.sent.insert(std::make_pair(nDev, std::make_pair(0u, 0u))).first;
    }
    std::pair<uint32_t, uint32_t> &s = it->second;
    // A feed shorter than what this watcher has (FeedOf went on from another seat's) is the same
    // game not yet as far: nothing goes until it is past it.
    if (f->record.size() >= s.first && (f->record.size() > s.first || f->dwFed > s.second)) {
      Writer w;
      w.B(FEED_RECORD);
      w.D(static_cast<uint32_t>(m.nId));
      w.D(s.first);
      w.D(f->dwFed);
      w.Bytes(f->record.data() + s.first, f->record.size() - s.first);
      LanNet::Send(d->nConn, ROOM_FEED, w.v.data(), w.v.size());
      s.first = static_cast<uint32_t>(f->record.size());
      s.second = f->dwFed;
    }
    if (bDone) {
      Writer w;
      w.B(FEED_END);
      w.D(static_cast<uint32_t>(m.nId));
      w.D(f->bEnded ? f->dwEnd : f->dwFed);
      w.D(static_cast<uint32_t>(f->record.size()));
      LanNet::Send(d->nConn, ROOM_FEED, w.v.data(), w.v.size());
    }
  }
  if (bDone) {
    m.bFeedDone = true;
    WriteReplay(m, *f);
  }
}

}  // namespace LanRoom
