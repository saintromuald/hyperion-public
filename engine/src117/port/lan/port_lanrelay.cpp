// port_lanrelay.cpp -- one match's relay (docs/modules/lan.md; port_lanrelay.h says
// where it lives). Transport only: what the game computes comes from its turns.
//
// Every player sends one turn a step, keeping the game's turns in transit ahead; the relay puts
// the next step together once it holds a turn of every player in it and sends it to everyone.
// A step names its players and those who left before it, so every machine reads the same turns
// in the same order and sees a player arrive and leave at the same step. A joiner's turns start
// at the step being put together when it was let in. The creator comes first, with CREATE, and
// is player 0: 1.17's lobby takes slots, forces and kicks only from player 0. A join's protocol,
// Hyperion version and sim revision (h/hxsim.h) must be the relay's. Every 120 frames the
// players' state hashes (port_synchash.cpp), once all in the steps have sent one, are compared
// with the lowest player's, and hyperion_lan.log says whether they agree, or the frame and parts.
#include "port_lanrelay.h"
#include "../platform/port_log.h"  // PortLogger
#include "../data/port_stormerr.h"  // NET_CANNOT_JOIN, NET_GAME_FULL
#include "../platform/port_util.h"  // PortNowMs
#include <stdio.h>
#include "../../../hyperion/version.h"  // HYPERION_VERSION_TEXT
#include "../../h/hxsim.h"              // HX_SIM_REVISION
#include "../../h/port_synchash.h"      // SYNC_DOMAINS
#include "port_lannet.h"

using namespace LanProto;

namespace {

const uint32_t LEAVE_LOST = 0x40000006;       // a connection lost, as 1.17 drops a player that stopped answering
const uint32_t MODE_UNDER_WAY = 0x8;

}  // namespace

LanRelay::LanRelay(uint32_t dwGameId, const std::string &password, const std::string &logName)
    : m_dwGameId(dwGameId), m_password(password), m_logName(logName),
      m_log("hyperion_lan.log", PortLogger::SHARED, logName + ": ")
{
  m_qwLastStep = PortNowMs();
}

LanRelay::~LanRelay()
{
  if (m_dwAgreed || m_dwDiffered)
    m_log("the state hashes agreed at %u frames and differed at %u", m_dwAgreed, m_dwDiffered);
  for (Player &p : m_players)
    if (p.nConn >= 0)
      LanNet::Drop(p.nConn);
}

// hyperion_lan.log: who joined, left and was refused, and how a game went.

void LanRelay::Refuse(int nConn, uint32_t dwErr, const char *pszWhy)
{
  m_log("a join refused: %s", pszWhy);
  Writer w;
  w.D(dwErr);
  w.S(pszWhy);
  LanNet::Send(nConn, REFUSE, w.v.data(), w.v.size());
  LanNet::Drop(nConn);
}

void LanRelay::Admit(int nConn, uint8_t bKind, const std::vector<uint8_t> &data)
{
  Reader r(data);
  const uint16_t wProtocol = r.W();
  const uint16_t wSimRevision = r.W();
  const std::string version = r.S(), name = r.S(), desc = r.S(), password = r.S();
  r.D();  // the game id, by which the room found this relay
  if (r.bBad || wProtocol != kProtocol || (bKind != HELLO && bKind != CREATE)) {
    Refuse(nConn, NET_CANNOT_JOIN, "another kind of Hyperion");
    return;
  }
  if (version != HYPERION_VERSION_TEXT || wSimRevision != HX_SIM_REVISION) {
    m_mismatch = name + "'s game is " + version + ", and the room plays " + HYPERION_VERSION_TEXT;
    Refuse(nConn, NET_CANNOT_JOIN, "another version of Hyperion");
    return;
  }
  if (!m_password.empty() && password != m_password) {
    Refuse(nConn, NET_CANNOT_JOIN, "the password");
    return;
  }
  int nId = -1;
  if (bKind == CREATE) {
    if (m_bCreated) {
      Refuse(nConn, NET_CANNOT_JOIN, "the game is made already");
      return;
    }
    m_name = r.S();
    m_stats = r.S();
    m_gamePassword = r.S();
    m_dwMode = r.D();
    m_dwMaxPlayers = r.D();
    m_gameData = r.Bytes();
    if (r.bBad) {
      Refuse(nConn, NET_CANNOT_JOIN, "another kind of Hyperion");
      return;
    }
    if (m_dwMaxPlayers > static_cast<uint32_t>(kPlayers))
      m_dwMaxPlayers = kPlayers;
    m_bCreated = true;
    nId = 0;
    m_log("game \"%s\" for %u players, made by %s; mode 0x%X", m_name.c_str(), m_dwMaxPlayers, name.c_str(), m_dwMode);
  } else {
    if (!m_bCreated) {
      Refuse(nConn, NET_CANNOT_JOIN, "the game is not made yet");
      return;
    }
    if (!(m_dwMode & MODE_UNDER_WAY))
      for (int i = 1; i < kPlayers && i < static_cast<int>(m_dwMaxPlayers); ++i)
        if (!m_players[i].bIn) {
          nId = i;
          break;
        }
    if (nId < 0) {
      Refuse(nConn, NET_GAME_FULL, "the game is full or under way");
      return;
    }
  }
  Player &p = m_players[nId];
  p = Player();
  p.bIn = true;
  p.name = name;
  LanNet::Trust(nConn);  // let in: its messages may be larger than a greeting
  p.desc = desc;
  p.nConn = nConn;
  m_bAnyone = true;
  Writer w;
  w.B(static_cast<uint8_t>(nId));
  w.D(m_dwMaxPlayers);
  w.D(m_dwMode);
  w.S(m_name);
  w.S(m_stats);
  w.S(m_gamePassword);
  w.Bytes(m_gameData.data(), m_gameData.size());
  w.D(m_dwNextStep);
  w.B(static_cast<uint8_t>(Count()));
  for (int i = 0; i < kPlayers; ++i)
    if (m_players[i].bIn) {
      w.B(static_cast<uint8_t>(i));
      w.S(m_players[i].name);
      w.S(m_players[i].desc);
    }
  LanNet::Send(nConn, WELCOME, w.v.data(), w.v.size());
  if (nId != 0) {
    Writer j;
    j.B(static_cast<uint8_t>(nId));
    j.S(p.name);
    j.S(p.desc);
    ToAll(JOINED, j.v, nId);
    m_log("%s joins as player %d from step %u", p.name.c_str(), nId, m_dwNextStep);
  }
  if (Count() == 1)
    m_qwLastStep = PortNowMs();
}

void LanRelay::ToAll(uint8_t bKind, const std::vector<uint8_t> &data, int nExcept)
{
  for (int i = 0; i < kPlayers; ++i)
    if (i != nExcept && m_players[i].bIn && m_players[i].nConn >= 0)
      LanNet::Send(m_players[i].nConn, bKind, data.data(), data.size());
}

void LanRelay::Remove(int nId, uint32_t dwReason)
{
  if (nId < 0 || nId >= kPlayers)
    return;
  Player &p = m_players[nId];
  if (!p.bIn)
    return;
  m_log("player %d (%s) out from step %u, reason 0x%X", nId, p.name.c_str(), m_dwNextStep, dwReason);
  p.bIn = false;
  p.turns.clear();
  if (dwReason == LEAVE_LOST)
    m_lost.push_back(p.name);
  m_leavesNext.push_back(std::make_pair(static_cast<uint8_t>(nId), dwReason));
  if (p.nConn >= 0)
    LanNet::Drop(p.nConn);
  p.nConn = -1;
  // the others read the leave with the next step; with nobody left there is none
  if (Count() > 0)
    Steps();
}

void LanRelay::Route(int nFrom, int nTo, const std::vector<uint8_t> &data)
{
  Writer w;
  w.B(static_cast<uint8_t>(nFrom));
  w.v.insert(w.v.end(), data.begin(), data.end());
  for (int i = 0; i < kPlayers; ++i) {
    if (!m_players[i].bIn || m_players[i].nConn < 0)
      continue;
    const bool bFor = nTo == -1 || (nTo == -2 && i != nFrom) || nTo == i;
    if (bFor)
      LanNet::Send(m_players[i].nConn, MESSAGE, w.v.data(), w.v.size());
  }
}

void LanRelay::Steps(void)
{
  for (;;) {
    int nIn = 0;
    for (int i = 0; i < kPlayers; ++i)
      if (m_players[i].bIn) {
        if (m_players[i].turns.empty())
          return;
        ++nIn;
      }
    if (nIn == 0)
      return;
    Writer w;
    w.D(m_dwNextStep);
    w.B(static_cast<uint8_t>(nIn));
    for (int i = 0; i < kPlayers; ++i)
      if (m_players[i].bIn) {
        std::vector<uint8_t> &t = m_players[i].turns.front();
        w.B(static_cast<uint8_t>(i));
        w.Bytes(t.data(), t.size());
        m_players[i].turns.pop_front();
      }
    w.B(static_cast<uint8_t>(m_leavesNext.size()));
    for (const auto &l : m_leavesNext) {
      w.B(l.first);
      w.D(l.second);
    }
    m_leavesNext.clear();
    ToAll(STEP, w.v, -1);
    ++m_dwNextStep;
    m_qwLastStep = PortNowMs();
    m_bWaiting = 0;
  }
}

// A frame's hashes compared once every player in the steps has sent one, with the lowest
// player's. The first difference of each player is written down with the parts that differ.
void LanRelay::Compare(uint32_t dwFrame)
{
  auto it = m_hashes.find(dwFrame);
  if (it == m_hashes.end())
    return;
  for (int i = 0; i < kPlayers; ++i)
    if (m_players[i].bIn && it->second.find(i) == it->second.end())
      return;
  if (it->second.size() < 2) {
    if (!it->second.empty())
      m_taken.push_back(std::make_pair(dwFrame, it->second.begin()->second));
    m_hashes.erase(m_hashes.begin(), ++it);
    return;
  }
  const auto ref = it->second.begin();
  bool bAgreed = true;
  for (const auto &other : it->second) {
    if (other.first == ref->first || other.second == ref->second)
      continue;
    bAgreed = false;
    if (m_bOutOfStep & (1 << other.first))
      continue;
    m_bOutOfStep |= static_cast<uint8_t>(1 << other.first);
    const std::string parts = HashPartsDiffer(ref->second, other.second);
    m_log("player %d (%s) is out of step with player %d from frame %u or before: %s", other.first,
        m_players[other.first].name.c_str(), ref->first, dwFrame, parts.c_str());
  }
  if (bAgreed) {
    ++m_dwAgreed;
    m_taken.push_back(std::make_pair(dwFrame, ref->second));
  } else {
    ++m_dwDiffered;
  }
  m_dwLastFrame = dwFrame;
  if (bAgreed && m_dwAgreed % 25 == 1)
    m_log("the players agree at frame %u", dwFrame);
  m_hashes.erase(m_hashes.begin(), ++it);
}

std::vector<std::string> LanRelay::TakeLost(void)
{
  std::vector<std::string> out;
  out.swap(m_lost);
  return out;
}

std::vector<std::pair<uint32_t, std::vector<uint8_t>>> LanRelay::TakeHashes(void)
{
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> out;
  out.swap(m_taken);
  return out;
}

void LanRelay::Pump(void)
{
  const uint64_t qwNow = PortNowMs();
  for (int i = 0; i < kPlayers; ++i) {
    Player &p = m_players[i];
    if (!p.bIn || p.nConn < 0)
      continue;
    uint8_t bKind;
    std::vector<uint8_t> data;
    while (p.bIn && LanNet::Receive(p.nConn, &bKind, &data)) {
      Reader r(data);
      switch (bKind) {
      case TURN:
        // A game sends a turn a step and only a few steps ahead of the ones it has, so a queue
        // this long is a seat that is not playing the game: out, before it holds the room's memory.
        if (p.turns.size() >= kTurnsQueuedMost) {
          m_log("player %d (%s) sent %u turns ahead of the steps", i, p.name.c_str(),
              static_cast<unsigned>(p.turns.size()));
          Remove(i, LEAVE_LOST);
        } else if (data.size() > kTurnMost) {
          m_log("player %d (%s) sent a turn of %u bytes", i, p.name.c_str(), static_cast<unsigned>(data.size()));
          Remove(i, LEAVE_LOST);
        } else {
          p.turns.push_back(data);
        }
        break;
      case MESSAGE: {
        const int nTo = static_cast<int>(r.D());
        if (!r.bBad)
          Route(i, nTo, std::vector<uint8_t>(data.begin() + 4, data.end()));
        break;
      }
      case LEAVE: Remove(i, r.D()); break;
      case PONG: p.dwLatency = static_cast<uint32_t>(qwNow) - r.D(); break;
      case HASH:
        if (data.size() >= 4) {
          const uint32_t dwFrame = r.D();
          m_hashes[dwFrame][i] = data;
          Compare(dwFrame);
        }
        break;
      case DROP: {
        // As 1.17's: player 0 kicks anyone, in the lobby or after; the others drop only a player
        // the steps have waited on for a second or more (the waiting dialog's Drop Players).
        const int nWho = r.B();
        const uint32_t dwWhy = r.D();
        if (!r.bBad && nWho != i && nWho >= 0 && nWho < kPlayers &&
            (i == 0 || (m_bWaiting & (1 << nWho)) != 0))
          Remove(nWho, dwWhy);
        break;
      }
      case MODE:
        // the creator's: 1.17 marks its game under way as its countdown starts
        if (i == 0) {
          m_dwMode = r.D();
          ToAll(MODE, data, 0);
        }
        break;
      default: break;
      }
    }
    if (p.bIn && !LanNet::Alive(p.nConn))
      Remove(i, LEAVE_LOST);
  }
  // a frame some player will never send a hash for: compared with those who did
  for (auto it = m_hashes.begin(); it != m_hashes.end() && m_hashes.size() > 4;)
    it = m_hashes.erase(it);
  // How long the steps have waited. A step made in this pass (a leave lets one go) is after
  // qwNow, so the age is taken against the later of the two, never wrapped.
  const uint64_t qwWaited = qwNow > m_qwLastStep ? qwNow - m_qwLastStep : 0;
  // a game under way whose steps have waited this long on a seat: that seat is lost (the lobby
  // and the loading, before the first step, are not counted)
  if (m_dwNextStep > 0 && qwWaited >= kStalledMs)
    for (int i = 0; i < kPlayers; ++i)
      if (m_players[i].bIn && m_players[i].turns.empty()) {
        m_log("player %d (%s) sent no turn for %u s", i, m_players[i].name.c_str(),
            static_cast<unsigned>(kStalledMs / 1000));
        Remove(i, LEAVE_LOST);
      }
  Steps();
  // a step that waits: who it waits for, and every player's latency, twice a second
  if (qwNow - m_qwLastStatus >= 500) {
    m_qwLastStatus = qwNow;
    uint8_t bWaiting = 0;
    if (qwWaited >= 1000)
      for (int i = 0; i < kPlayers; ++i)
        if (m_players[i].bIn && m_players[i].turns.empty())
          bWaiting |= static_cast<uint8_t>(1 << i);
    m_bWaiting = bWaiting;
    Writer w;
    w.B(bWaiting);
    for (int i = 0; i < kPlayers; ++i)
      w.W(static_cast<uint16_t>(m_players[i].dwLatency));
    ToAll(STATUS, w.v, -1);
  }
  if (qwNow - m_qwLastPing >= 1000) {
    m_qwLastPing = qwNow;
    Writer w;
    w.D(static_cast<uint32_t>(qwNow));
    ToAll(PING, w.v, -1);
  }
}

bool LanRelay::Done(void) const
{
  return m_bAnyone && Count() == 0;
}

int LanRelay::Count(void) const
{
  int n = 0;
  for (const Player &p : m_players)
    n += p.bIn;
  return n;
}

std::vector<LanRelay::Seat> LanRelay::Seats(void) const
{
  std::vector<Seat> out;
  for (int i = 0; i < kPlayers; ++i)
    if (m_players[i].bIn)
      out.push_back(Seat{i, m_players[i].name, m_players[i].desc, m_players[i].dwLatency,
                         (m_bWaiting & (1 << i)) != 0});
  return out;
}
