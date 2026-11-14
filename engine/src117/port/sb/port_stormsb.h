// port_stormsb.h -- Storm's online provider: 1.17's multiplayer and lockstep over
// ShieldBattery's rally-point2 netcode (tools/rallypoint2/), through the same SNet
// calls the LAN provider answers (port_stormlan.h is the shape). The relay is the
// authority for turns, pacing and leaves; this file's player is a client of it.
#ifndef PORT_STORMSB_H
#define PORT_STORMSB_H

#include <windows.h>

// The provider's id, Hyperion's own ('SBRP'): listed beside the LAN's 'UDPN'.
const DWORD SB_PROVIDER = 0x53425250;

// SNetEnumProviders' callback, given the provider.
void SbListProvider(void *pfnCallback);

// The room's launch of a match over the SB relay: pszSetupFile names the launch
// JSON rp_start consumes (token, pinned relay cert, roster size, buffer depth).
// NULL forgets it. Starting the session dials the relay and blocks until the
// session is running or fails (bounded by the bridge), so call it during the
// launch cover, never from the game loop.
void SbSetLaunch(const char *pszSetupFile);
// The room's launch: the setup JSON as the launch carries it, and this seat's
// local map file for a server manifest's hash check.
void SbSetLaunchJson(const char *pszSetupJson, const char *pszMapPath);
bool SbLaunchOn(void);
// The relay reports that every expected member has connected. A canonical
// manifest launch waits for this before starting its countdown.
bool SbSessionStarted(void);
// A secret-free lifecycle record for a direct sidecar's stdout, enabled only
// when that sidecar sets HYPERION_SB_EVENTS=1 in its child's environment.
void SbEmitLifecycleEvent(const char *pszType);
// The launch's rosterSize (the session's slots), 0 when none was written.
int SbRosterCount(void);
// Starts the session and waits for the creator's launch blobs (init data and
// stat string). The join path calls it before the game list, whose parser
// needs the stat string before SNetJoinGame can run.
bool SbSessionStart(const char *pszWho);
// In a game over the SB relay, and whether this player made it.
bool SbInMatch(void);
bool SbCreator(void);
// The map is the launch's: every seat holds the same file out of band, so the
// LAN's map-download session is not part of the SB flow (the game's start gate
// and the Start button read this, gluChat.cpp).
bool SbMapFromLaunch(void);
// The creator's header flags, from the launch blob (the 0x20-byte game-type
// block at the header's +0x6D): the join path's listed header never carries
// them, and without them the joiner reads the game as a scenario.
bool SbLaunchHeader(BYTE *pbSubCategory, BYTE *pbScenarioFlag, BYTE *pbLadderGame);
// The creator's whole game-type block (the header's +0x6D..+0x8C, the
// TGAMEPARAMS): 1.17's join re-derives the template from the listed game's
// key, which does not reproduce the creator's (its game type, resource mode
// and starting minerals differ), so the joiner adopts the block wholesale.
bool SbLaunchParams(void *pvParams, size_t cbParams);

BOOL SbInitialize(void);
void SbDestroy(void);
BOOL SbGetProviderCaps(DWORD *pdwCaps);
BOOL SbGetPlayerCaps(DWORD dwPlayer, DWORD *pdwCaps);
BOOL SbRegisterEventHandler(DWORD dwEvent, void *pfnHandler);
BOOL SbCreateGame(const char *pszGameName, const char *pszPassword, const char *pszStats, DWORD dwMode,
                  const void *pvGameData, DWORD cbGameData, DWORD dwMaxPlayers, const char *pszPlayerName,
                  const char *pszPlayerDesc, DWORD *pdwPlayerId);
BOOL SbEnumGames(void *pfnCallback, DWORD *pdwHintNext);
BOOL SbJoinGame(DWORD dwGameId, const char *pszPassword, const char *pszPlayerName, const char *pszPlayerDesc,
                DWORD *pdwPlayerId);
BOOL SbLeaveGame(DWORD dwReason);
BOOL SbDropPlayer(DWORD dwPlayer, DWORD dwFlags);
BOOL SbDisconnectAll(DWORD dwFlags);
BOOL SbGetNumPlayers(int *pnFirst, int *pnLast, int *pnActive);
BOOL SbGetPlayerName(DWORD dwPlayer, char *pszBuffer, DWORD cchBuffer);
BOOL SbGetPlayerNames(char **ppszNames);
BOOL SbGetGameInfo(DWORD dwInfo, void *pvBuffer, DWORD cbBuffer, DWORD *pcbRead);
BOOL SbSetGameMode(DWORD dwMode, DWORD dwMakePublic);
// The relay-selected depth the native flush loop targets, 0 outside an SB game.
DWORD SbTurnsInTransitTarget(void);
BOOL SbGetTurnsInTransit(DWORD *pdwTurns);
BOOL SbSendTurn(const void *pvData, DWORD cbData);
BOOL SbReceiveTurns(DWORD nFirst, DWORD nPlayers, const void **ppvData, DWORD *pcbData, DWORD *pdwFlags);
BOOL SbSendMessage(DWORD dwPlayer, const void *pvData, DWORD cbData);
BOOL SbReceiveMessage(int *pnPlayer, const void **ppvData, DWORD *pcbData);
// Builds and queues this seat's raw v2 evidence before its clean leave.
BOOL SbReportGameResult(void);

#endif
