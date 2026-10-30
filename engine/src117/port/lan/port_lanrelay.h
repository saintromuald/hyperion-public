// port_lanrelay.h -- one match's relay: the players' turns made into steps, their
// messages passed on, their state hashes compared. It lives in the room (port_lanroom.cpp), a
// relay a match, or in the Hyperion that made a game on 1.17's own screens (port_stormlan.cpp).
// No player is in it: every player, the creator too, is a connection to it.
#ifndef PORT_LANRELAY_H
#define PORT_LANRELAY_H

#include <stdint.h>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "port_lanproto.h"
#include "../platform/port_log.h"

class LanRelay {
public:
  // A match's relay, by the game id it is joined by. A password, when given, is asked of
  // everyone: the room's token for its seats, or the game's own on 1.17's screens.
  LanRelay(uint32_t dwGameId, const std::string &password, const std::string &logName);
  ~LanRelay();

  // A new connection's first frame, HELLO or CREATE: let in, or refused and dropped.
  void Admit(int nConn, uint8_t bKind, const std::vector<uint8_t> &data);
  // Reads every player, puts the steps together, and every half second says who a step waits
  // for and every player's latency.
  void Pump(void);

  uint32_t GameId(void) const { return m_dwGameId; }
  bool Created(void) const { return m_bCreated; }
  bool UnderWay(void) const { return (m_dwMode & 0x8) != 0; }
  // Everyone has gone, after someone came.
  bool Done(void) const;
  int Count(void) const;
  int MaxPlayers(void) const { return static_cast<int>(m_dwMaxPlayers); }
  uint32_t Mode(void) const { return m_dwMode; }
  const std::string &Name(void) const { return m_name; }
  const std::string &Stats(void) const { return m_stats; }
  uint32_t Steps(void) const { return m_dwNextStep; }
  uint32_t HashesAgreed(void) const { return m_dwAgreed; }
  uint32_t HashesDiffered(void) const { return m_dwDiffered; }
  uint32_t LastHashFrame(void) const { return m_dwLastFrame; }
  // Who was refused for another version of Hyperion, and theirs; empty while nobody was.
  const std::string &Mismatch(void) const { return m_mismatch; }
  // The players whose connection the relay lost since the last call, by name: the room tells a
  // seat whose game is still in the room to leave the match (port_lanroom.cpp's PumpMatches).
  std::vector<std::string> TakeLost(void);
  // The players' hashes since the last call, each frame's that they agree on (or the one
  // player's), for the room to hold those watching against.
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> TakeHashes(void);

  struct Seat {
    int nId;
    std::string name, desc;
    uint32_t dwLatency;  // the round trip, in ms
    bool bWaitedFor;     // the step waits on it
  };
  std::vector<Seat> Seats(void) const;
  // A player out of the steps from the next one on (the room's Remove from Room, End).
  void Remove(int nId, uint32_t dwReason);

private:
  struct Player {
    bool bIn = false;
    std::string name, desc;
    int nConn = -1;
    std::deque<std::vector<uint8_t>> turns;  // not yet in a step
    uint32_t dwLatency = 0;
  };

  void Refuse(int nConn, uint32_t dwErr, const char *pszWhy);
  void ToAll(uint8_t bKind, const std::vector<uint8_t> &data, int nExcept);
  void Route(int nFrom, int nTo, const std::vector<uint8_t> &data);
  void Steps(void);
  void Compare(uint32_t dwFrame);

  uint32_t m_dwGameId;
  std::string m_password, m_logName;
  bool m_bCreated = false;
  bool m_bAnyone = false;
  std::string m_name, m_stats, m_gamePassword;
  uint32_t m_dwMode = 0, m_dwMaxPlayers = 0;
  std::vector<uint8_t> m_gameData;
  Player m_players[LanProto::kPlayers];
  std::vector<std::pair<uint8_t, uint32_t>> m_leavesNext;
  uint32_t m_dwNextStep = 0;
  uint64_t m_qwLastStep = 0, m_qwLastStatus = 0, m_qwLastPing = 0;
  uint8_t m_bWaiting = 0;
  // each frame's hashes by player, until they have been compared
  std::map<uint32_t, std::map<int, std::vector<uint8_t>>> m_hashes;
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> m_taken;
  uint32_t m_dwAgreed = 0, m_dwDiffered = 0, m_dwLastFrame = 0;
  std::string m_mismatch;
  std::vector<std::string> m_lost;
  uint8_t m_bOutOfStep = 0;
  PortLogger m_log;  // hyperion_lan.log, each line with the relay's name
};

#endif
