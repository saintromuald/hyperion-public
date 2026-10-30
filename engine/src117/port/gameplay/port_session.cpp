// port_session.cpp -- the choices a single-player game is launched with (port_session.h).
#include <string.h>

#include "port_session.h"
#include "../../h/playertables.h"  // PlayersSeatsBegin

namespace {

PortSession g_session;

}  // namespace

PortSession::PortSession() {
  memset(abExtraRace, 0xFF, sizeof abExtraRace);
}

PortSession &Session(void) {
  return g_session;
}

void SessionClear(void) {
  g_session = PortSession();
  PlayersSeatsBegin();  // and where the last set-up's computers went
}

void SessionAfterGame(int nMode) {
  if (nMode != 5)
    SessionClear();
}
