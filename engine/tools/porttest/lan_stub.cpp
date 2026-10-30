// lan_stub.cpp -- the LAN provider (port_stormlan.cpp), which net_test never starts: nothing is listed or found.
#include <windows.h>
void LanListProvider(void *) {}
BOOL LanInitialize(void) { return FALSE; }
void LanDestroy(void) {}
BOOL LanGetProviderCaps(DWORD *) { return FALSE; }
BOOL LanGetPlayerCaps(DWORD, DWORD *) { return FALSE; }
BOOL LanRegisterEventHandler(DWORD, void *) { return FALSE; }
BOOL LanCreateGame(const char *, const char *, const char *, DWORD, const void *, DWORD, DWORD, const char *,
                   const char *, DWORD *)
{
  return FALSE;
}
BOOL LanEnumGames(void *, DWORD *) { return FALSE; }
BOOL LanJoinGame(DWORD, const char *, const char *, const char *, DWORD *) { return FALSE; }
BOOL LanLeaveGame(DWORD) { return FALSE; }
BOOL LanDropPlayer(DWORD, DWORD) { return FALSE; }
BOOL LanDisconnectAll(DWORD) { return FALSE; }
BOOL LanGetNumPlayers(int *, int *, int *) { return FALSE; }
BOOL LanGetPlayerName(DWORD, char *, DWORD) { return FALSE; }
BOOL LanGetPlayerNames(char **) { return FALSE; }
BOOL LanGetGameInfo(DWORD, void *, DWORD, DWORD *) { return FALSE; }
BOOL LanSetGameMode(DWORD, DWORD) { return FALSE; }
BOOL LanGetTurnsInTransit(DWORD *) { return FALSE; }
BOOL LanSendTurn(const void *, DWORD) { return FALSE; }
BOOL LanReceiveTurns(DWORD, DWORD, const void **, DWORD *, DWORD *) { return FALSE; }
BOOL LanSendMessage(DWORD, const void *, DWORD) { return FALSE; }
BOOL LanReceiveMessage(int *, const void **, DWORD *) { return FALSE; }
