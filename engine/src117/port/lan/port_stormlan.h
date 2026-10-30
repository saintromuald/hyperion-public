// port_stormlan.h -- Storm's LAN provider, the SNet calls of a game played
// between Hyperions on one network. port_stormnet.cpp hands each SNet call here once the
// provider is started; port_stormlan.cpp says how it works.
#ifndef PORT_STORMLAN_H
#define PORT_STORMLAN_H

#include <windows.h>

// The provider's id, 1.17's for its LAN provider ('UDPN'), so 1.17's screens name it
// "Local Area Network" from their own strings.
const DWORD LAN_PROVIDER = 0x5544504E;

// SNetEnumProviders' callback, given the provider.
void LanListProvider(void *pfnCallback);

// The end of a game frame, in a game over the network: the players' state hashes compared.
void LanGameFrame(unsigned long dwFrame);

// The room's launch of a match (port_lanlobby.cpp): the next game made or joined is the one at
// the relay pszAddr:nPort names, entered with the token; NULL forgets it, so 1.17's own screens
// make and find games themselves.
void LanSetLaunch(const char *pszAddr, int nPort, const char *pszToken, unsigned long dwGameId, const char *pszName,
                  const char *pszStats);
bool LanLaunchOn(void);
// In a game over the network, and whether this player made it.
bool LanInMatch(void);
bool LanCreator(void);

BOOL LanInitialize(void);
void LanDestroy(void);
BOOL LanGetProviderCaps(DWORD *pdwCaps);
BOOL LanGetPlayerCaps(DWORD dwPlayer, DWORD *pdwCaps);
BOOL LanRegisterEventHandler(DWORD dwEvent, void *pfnHandler);
BOOL LanCreateGame(const char *pszGameName, const char *pszPassword, const char *pszStats, DWORD dwMode,
                   const void *pvGameData, DWORD cbGameData, DWORD dwMaxPlayers, const char *pszPlayerName,
                   const char *pszPlayerDesc, DWORD *pdwPlayerId);
BOOL LanEnumGames(void *pfnCallback, DWORD *pdwHintNext);
BOOL LanJoinGame(DWORD dwGameId, const char *pszPassword, const char *pszPlayerName, const char *pszPlayerDesc,
                 DWORD *pdwPlayerId);
BOOL LanLeaveGame(DWORD dwReason);
BOOL LanDropPlayer(DWORD dwPlayer, DWORD dwFlags);
BOOL LanDisconnectAll(DWORD dwFlags);
BOOL LanGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive);
BOOL LanGetPlayerName(DWORD dwPlayer, char *pszBuffer, DWORD cchBuffer);
BOOL LanGetPlayerNames(char **ppszNames);
BOOL LanGetGameInfo(DWORD dwInfo, void *pvBuffer, DWORD cbBuffer, DWORD *pcbRead);
BOOL LanSetGameMode(DWORD dwMode, DWORD dwMakePublic);
BOOL LanGetTurnsInTransit(DWORD *pdwTurns);
BOOL LanSendTurn(const void *pvData, DWORD cbData);
BOOL LanReceiveTurns(DWORD nFirst, DWORD nPlayers, const void **ppvData, DWORD *pcbData, DWORD *pdwFlags);
BOOL LanSendMessage(DWORD dwPlayer, const void *pvData, DWORD cbData);
BOOL LanReceiveMessage(int *pnPlayer, const void **ppvData, DWORD *pcbData);

#endif
