// net_test.cpp -- checks the port's SNet functions (src117/port/lan/port_stormnet.cpp)
// against storm.dll's, with the loopback provider standard.snp serves, from the
// player's install (Net).
//
//   net_test.exe <install folder> [rounds]
//
// Each round starts both from nothing and makes the same random calls on each:
// starting provider 0 or one that is not there, creating and leaving games,
// sending turns and messages and reading them back, asking for the game's
// information, names, caps and counts, and the calls only the other providers
// answered. The calls are the game's own, a single player's frames most of all,
// and also its arguments' edges: null pointers, short buffers, players that are
// not there, turns read before they are sent or with the local player out of
// range. After every call the two must agree on the answer, Storm's last error
// and everything written back, the turns' and messages' bytes included.
//
// storm.dll finds its providers in the folder of the program that loads it, so
// standard.snp is copied next to this one. Its other providers (IPX, UDP, the
// modem, the cable) are gone from the port and are never started here, and
// SNetEnumProviders, which storm.dll answers with them, is compared only where
// it refuses its arguments.
// Exit code 0 means no difference was found.
#include <windows.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

extern "C" {
BOOL __stdcall SNetInitializeProvider(DWORD, void *, void *, void *, void *);
BOOL __stdcall SNetDestroy();
BOOL __stdcall SNetGetProviderCaps(DWORD *);
BOOL __stdcall SNetGetPlayerCaps(DWORD, DWORD *);
DWORD __stdcall SNetGetCurrentProviderID();
BOOL __stdcall SNetEnumProviders(const DWORD *, void *);
BOOL __stdcall SNetEnumDevices(void *);
BOOL __stdcall SNetInitializeDevice(DWORD, DWORD, DWORD_PTR, void *, void *);
BOOL __stdcall SNetRegisterEventHandler(DWORD, void *);
BOOL __stdcall SNetCreateGame(const char *, const char *, const char *, DWORD, const void *, DWORD, DWORD,
                              const char *, const char *, DWORD *);
BOOL __stdcall SNetCreateLadderGame(const char *, const char *, const char *, DWORD, DWORD, DWORD, const void *,
                                    DWORD, DWORD, const char *, const char *, DWORD *);
BOOL __stdcall SNetLeaveGame(DWORD);
BOOL __stdcall SNetDropPlayer(DWORD, DWORD);
BOOL __stdcall SNetDisconnectAll(DWORD);
BOOL __stdcall SNetGetNumPlayers(int *, int *, int *);
BOOL __stdcall SNetGetPlayerName(DWORD, char *, DWORD);
BOOL __stdcall SNetGetPlayerNames(char **);
BOOL __stdcall SNetGetGameInfo(DWORD, void *, DWORD, DWORD *);
BOOL __stdcall SNetSetGameMode(DWORD, DWORD);
BOOL __stdcall SNetGetTurnsInTransit(DWORD *);
BOOL __stdcall SNetSendTurn(const void *, DWORD);
BOOL __stdcall SNetReceiveTurns(DWORD, DWORD, const void **, DWORD *, DWORD *);
BOOL __stdcall SNetSendMessage(DWORD, const void *, DWORD);
BOOL __stdcall SNetReceiveMessage(int *, const void **, DWORD *);
BOOL __stdcall SNetReportGameResult(DWORD, DWORD, const int *, const char *, const char *);
BOOL __stdcall SNetPerformUpgrade(DWORD *);
BOOL __stdcall SNetEnumGamesEx(DWORD, DWORD, void *, DWORD *);
BOOL __stdcall SNetJoinGame(DWORD, const char *, const char *, const char *, const char *, DWORD *);
BOOL __stdcall SNetSelectGame(DWORD, void *, void *, void *, void *, DWORD *);
BOOL __stdcall SNetSendServerChatCommand(const char *);
BOOL __stdcall SNetSendLeagueCommand(const char *, void *);
BOOL __stdcall SNetSendReplayPath(const char *, DWORD, int);
BOOL __stdcall SNetGetLeagueName(int *);
BOOL __stdcall SNetLeagueLogout(const char *);
BOOL __stdcall SNetGetReplyName(char *, DWORD);
}

typedef void(__stdcall *PFN_SETERR)(DWORD);
typedef DWORD(__stdcall *PFN_GETERR)(void);
static PFN_SETERR s_pfnSetErr;
static PFN_GETERR s_pfnGetErr;

// Ours sets its errors through Storm's, as the payload does through its import.
extern "C" void __stdcall SErrSetLastError(DWORD dwErrCode) { s_pfnSetErr(dwErrCode); }

// The functions compared, Storm's by ordinal and ours by name.
struct Api {
  BOOL(__stdcall *InitializeProvider)(DWORD, void *, void *, void *, void *);
  BOOL(__stdcall *Destroy)();
  BOOL(__stdcall *GetProviderCaps)(DWORD *);
  BOOL(__stdcall *GetPlayerCaps)(DWORD, DWORD *);
  DWORD(__stdcall *GetCurrentProviderID)();
  BOOL(__stdcall *EnumProviders)(const DWORD *, void *);
  BOOL(__stdcall *EnumDevices)(void *);
  BOOL(__stdcall *InitializeDevice)(DWORD, DWORD, DWORD_PTR, void *, void *);
  BOOL(__stdcall *RegisterEventHandler)(DWORD, void *);
  BOOL(__stdcall *CreateGame)(const char *, const char *, const char *, DWORD, const void *, DWORD, DWORD,
                              const char *, const char *, DWORD *);
  BOOL(__stdcall *CreateLadderGame)(const char *, const char *, const char *, DWORD, DWORD, DWORD, const void *,
                                    DWORD, DWORD, const char *, const char *, DWORD *);
  BOOL(__stdcall *LeaveGame)(DWORD);
  BOOL(__stdcall *DropPlayer)(DWORD, DWORD);
  BOOL(__stdcall *DisconnectAll)(DWORD);
  BOOL(__stdcall *GetNumPlayers)(int *, int *, int *);
  BOOL(__stdcall *GetPlayerName)(DWORD, char *, DWORD);
  BOOL(__stdcall *GetPlayerNames)(char **);
  BOOL(__stdcall *GetGameInfo)(DWORD, void *, DWORD, DWORD *);
  BOOL(__stdcall *SetGameMode)(DWORD, DWORD);
  BOOL(__stdcall *GetTurnsInTransit)(DWORD *);
  BOOL(__stdcall *SendTurn)(const void *, DWORD);
  BOOL(__stdcall *ReceiveTurns)(DWORD, DWORD, const void **, DWORD *, DWORD *);
  BOOL(__stdcall *SendMessage)(DWORD, const void *, DWORD);
  BOOL(__stdcall *ReceiveMessage)(int *, const void **, DWORD *);
  BOOL(__stdcall *ReportGameResult)(DWORD, DWORD, const int *, const char *, const char *);
  BOOL(__stdcall *PerformUpgrade)(DWORD *);
  BOOL(__stdcall *EnumGamesEx)(DWORD, DWORD, void *, DWORD *);
  BOOL(__stdcall *JoinGame)(DWORD, const char *, const char *, const char *, const char *, DWORD *);
  BOOL(__stdcall *SelectGame)(DWORD, void *, void *, void *, void *, DWORD *);
  BOOL(__stdcall *SendServerChatCommand)(const char *);
  BOOL(__stdcall *SendLeagueCommand)(const char *, void *);
  BOOL(__stdcall *SendReplayPath)(const char *, DWORD, int);
  BOOL(__stdcall *GetLeagueName)(int *);
  BOOL(__stdcall *LeagueLogout)(const char *);
  BOOL(__stdcall *GetReplyName)(char *, DWORD);
};

static HMODULE s_hStorm;
template <typename T> static void Ord(T &p, WORD w)
{
  p = reinterpret_cast<T>(GetProcAddress(s_hStorm, MAKEINTRESOURCEA(w)));
  if (!p) {
    printf("storm.dll has no ordinal %u\n", w);
    exit(2);
  }
}

static Api StormApi()
{
  Api a;
  Ord(a.InitializeProvider, 117);
  Ord(a.Destroy, 102);
  Ord(a.GetProviderCaps, 114);
  Ord(a.GetPlayerCaps, 112);
  Ord(a.GetCurrentProviderID, 147);
  Ord(a.EnumProviders, 105);
  Ord(a.EnumDevices, 103);
  Ord(a.InitializeDevice, 116);
  Ord(a.RegisterEventHandler, 123);
  Ord(a.CreateGame, 101);
  Ord(a.CreateLadderGame, 138);
  Ord(a.LeaveGame, 119);
  Ord(a.DropPlayer, 106);
  Ord(a.DisconnectAll, 137);
  Ord(a.GetNumPlayers, 109);
  Ord(a.GetPlayerName, 113);
  Ord(a.GetPlayerNames, 144);
  Ord(a.GetGameInfo, 107);
  Ord(a.SetGameMode, 130);
  Ord(a.GetTurnsInTransit, 115);
  Ord(a.SendTurn, 128);
  Ord(a.ReceiveTurns, 122);
  Ord(a.SendMessage, 127);
  Ord(a.ReceiveMessage, 121);
  Ord(a.ReportGameResult, 139);
  Ord(a.PerformUpgrade, 120);
  Ord(a.EnumGamesEx, 133);
  Ord(a.JoinGame, 118);
  Ord(a.SelectGame, 125);
  Ord(a.SendServerChatCommand, 134);
  Ord(a.SendLeagueCommand, 141);
  Ord(a.SendReplayPath, 142);
  Ord(a.GetLeagueName, 143);
  Ord(a.LeagueLogout, 145);
  Ord(a.GetReplyName, 146);
  return a;
}

static Api OurApi()
{
  Api a = {SNetInitializeProvider, SNetDestroy, SNetGetProviderCaps, SNetGetPlayerCaps,
           SNetGetCurrentProviderID, SNetEnumProviders, SNetEnumDevices, SNetInitializeDevice,
           SNetRegisterEventHandler, SNetCreateGame, SNetCreateLadderGame, SNetLeaveGame,
           SNetDropPlayer, SNetDisconnectAll, SNetGetNumPlayers, SNetGetPlayerName,
           SNetGetPlayerNames, SNetGetGameInfo, SNetSetGameMode, SNetGetTurnsInTransit,
           SNetSendTurn, SNetReceiveTurns, SNetSendMessage, SNetReceiveMessage,
           SNetReportGameResult, SNetPerformUpgrade, SNetEnumGamesEx, SNetJoinGame,
           SNetSelectGame, SNetSendServerChatCommand, SNetSendLeagueCommand, SNetSendReplayPath,
           SNetGetLeagueName, SNetLeagueLogout, SNetGetReplyName};
  return a;
}

static uint32_t g_rng = 0x5EED1E57u;
static uint32_t Rand32()
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static int RandBelow(int n) { return static_cast<int>(Rand32() % static_cast<uint32_t>(n)); }

static bool s_bTrace;
static int g_nCases = 0;
static int g_nDiffs = 0;
static int g_nTurns = 0;
static int g_nMessages = 0;

// A call's record: what it answered and wrote, as text, one side at a time.
struct Record {
  std::string s;
  void Add(const char *pszFmt, ...)
  {
    char sz[512];
    va_list va;
    va_start(va, pszFmt);
    _vsnprintf(sz, sizeof sz - 1, pszFmt, va);
    sz[sizeof sz - 1] = 0;
    va_end(va);
    s += sz;
  }
  void Bytes(const void *pv, size_t cb)
  {
    if (!pv) {
      s += " <null>";
      return;
    }
    char sz[4];
    s += " [";
    for (size_t i = 0; i < cb; ++i) {
      sprintf(sz, "%02X", static_cast<const BYTE *>(pv)[i]);
      s += sz;
    }
    s += "]";
  }
};

// The events the handlers would see; none is ever raised in a game of one player.
static int s_nEvents;
static void __stdcall OnEvent(void *) { ++s_nEvents; }
static int s_nCallbacks;
static BOOL __stdcall OnEnum(DWORD, void *, void *, void *) { ++s_nCallbacks; return TRUE; }

// The game's descriptors, as InitSNetProgramData and the rest fill them.
static DWORD s_adwProgram[15], s_adwPlayer[4], s_adwUi[23], s_adwVersion[5];
static void FillDescriptors()
{
  s_adwProgram[0] = 0x3C;
  s_adwProgram[1] = reinterpret_cast<DWORD>("Brood War");
  s_adwProgram[2] = reinterpret_cast<DWORD>("internal version unknown");
  s_adwProgram[3] = 'SEXP';
  s_adwProgram[4] = 0xD5;
  s_adwProgram[6] = 8;
  s_adwProgram[10] = 0xFF;
  s_adwProgram[11] = reinterpret_cast<DWORD>("owner");
  s_adwProgram[12] = reinterpret_cast<DWORD>("key");
  s_adwProgram[14] = 0x409;
  s_adwPlayer[0] = 0x10;
  s_adwPlayer[1] = reinterpret_cast<DWORD>("Player");
  s_adwPlayer[2] = reinterpret_cast<DWORD>("");
  s_adwUi[0] = 0x5C;
  s_adwUi[1] = 1;
  s_adwVersion[0] = 0x14;
  s_adwVersion[1] = reinterpret_cast<DWORD>("1.17");
  s_adwVersion[2] = reinterpret_cast<DWORD>("StarCraft.exe");
  s_adwVersion[3] = reinterpret_cast<DWORD>("StarDat.mpq");
  s_adwVersion[4] = reinterpret_cast<DWORD>("patch_rt.mpq");
}

static std::string RandName(bool bMayBeEmpty)
{
  static const char s_szChars[] = "abcXYZ019 _-";
  const int n = bMayBeEmpty && RandBelow(6) == 0 ? 0 : 1 + (RandBelow(8) ? RandBelow(24) : RandBelow(200));
  std::string s;
  for (int i = 0; i < n; ++i)
    s += s_szChars[RandBelow(sizeof s_szChars - 1)];
  return s;
}

static DWORD RandPlayer()
{
  static const DWORD s_ad[] = {0, 0, 0, 1, 7, 8, 0xFF, 0x100, 0x1FF, 0xFFFFFFFF, 0xFFFFFFFE};
  return s_ad[RandBelow(sizeof s_ad / sizeof *s_ad)];
}

// One call, the same on both; the arguments are drawn before and shared.
struct Op {
  int nKind;
  DWORD a, b, c, d;
  std::string s1, s2, s3, s4;
  std::vector<BYTE> data;
  bool bNull1, bNull2, bNull3, bNullId;
};

enum {
  OP_INIT, OP_DESTROY, OP_CAPS, OP_PLAYERCAPS, OP_PROVIDERID, OP_ENUMPROVIDERS, OP_ENUMDEVICES,
  OP_INITDEVICE, OP_REGISTER, OP_CREATE, OP_CREATELADDER, OP_LEAVE, OP_DROP, OP_DISCONNECT,
  OP_NUMPLAYERS, OP_PLAYERNAME, OP_PLAYERNAMES, OP_GAMEINFO, OP_SETMODE, OP_TRANSIT, OP_SENDTURN,
  OP_RECEIVETURNS, OP_SENDMESSAGE, OP_RECEIVEMESSAGE, OP_REPORT, OP_UPGRADE, OP_ENUMGAMES, OP_JOIN,
  OP_SELECT, OP_CHAT, OP_LEAGUE, OP_REPLAYPATH, OP_LEAGUENAME, OP_LOGOUT, OP_REPLYNAME, OP_KINDS
};

// Mostly a game's frames, with everything else now and then.
static const int s_anWeight[OP_KINDS] = {
    3, 1, 2, 2, 1, 1, 1, 1, 1, 3, 1, 1, 2, 1, 2, 3, 2, 4, 2, 6, 12, 14, 4, 5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static Op RandOp()
{
  static int s_nTotal;
  if (!s_nTotal)
    for (int i = 0; i < OP_KINDS; ++i)
      s_nTotal += s_anWeight[i];
  int r = RandBelow(s_nTotal);
  Op op = {};
  while (r >= s_anWeight[op.nKind])
    r -= s_anWeight[op.nKind++];
  op.bNull1 = RandBelow(12) == 0;
  op.bNull2 = RandBelow(12) == 0;
  op.bNull3 = RandBelow(12) == 0;
  op.bNullId = RandBelow(40) == 0;
  switch (op.nKind) {
  case OP_INIT:
    // Provider 0 again, while a game may be under way. One that is not there is
    // asked for only at a round's start (main): during a game, storm.dll would be
    // left with the game and no provider, which SNetDestroy then ends only in part.
    op.a = 0;
    break;
  case OP_CAPS:
  case OP_PLAYERCAPS:
    op.a = RandPlayer();
    op.b = RandBelow(8) ? 0x24 : RandBelow(2) ? 0x20 : 0x28;  // 0x28 still fits the buffer
    break;
  case OP_ENUMPROVIDERS:
    op.b = RandBelow(2) ? 0x24 : 0x10;  // with a filter; storm.dll lists what ours does not, so it must refuse
    break;
  case OP_REGISTER:
    op.a = RandBelow(6);
    break;
  case OP_CREATE:
  case OP_CREATELADDER: {
    op.s1 = RandName(false);
    op.s2 = RandBelow(4) ? "" : RandName(true);
    op.s3 = RandName(true);
    // Never without a player name: storm.dll refuses it with its lock still held,
    // and its next SNetDestroy waits for ever on its worker thread.
    op.s4 = RandName(false);
    op.a = RandBelow(4) ? 0 : Rand32();
    op.b = RandBelow(3) ? (RandBelow(2) ? 4 : 8) : RandBelow(10);
    op.c = RandBelow(4) ? 0 : Rand32() & 0xFF;
    op.d = RandBelow(4) ? 0 : Rand32();
    const int n = RandBelow(3) ? 0 : RandBelow(64);
    for (int i = 0; i < n; ++i)
      op.data.push_back(static_cast<BYTE>(Rand32()));
    break;
  }
  case OP_LEAVE:
    op.a = 1 + RandBelow(3);
    break;
  case OP_DROP:
    op.a = RandPlayer();
    op.b = RandBelow(4);
    break;
  case OP_PLAYERNAME: {
    op.a = RandPlayer();
    static const DWORD s_ad[] = {0, 1, 2, 5, 25, 0x80, 0x100};
    op.b = s_ad[RandBelow(sizeof s_ad / sizeof *s_ad)];
    break;
  }
  case OP_GAMEINFO: {
    op.a = RandBelow(9);
    static const DWORD s_ad[] = {0, 1, 3, 4, 8, 0x18, 0x80, 0x100};
    op.b = s_ad[RandBelow(sizeof s_ad / sizeof *s_ad)];
    break;
  }
  case OP_SETMODE:
    op.a = RandBelow(3) ? RandBelow(16) : Rand32();
    op.b = RandBelow(3) == 0;
    break;
  case OP_SENDTURN:
  case OP_SENDMESSAGE: {
    op.a = RandPlayer();
    // Now and then one past 64 KB, whose length Storm keeps in 16 bits with its
    // 12-byte header and so gives back short. None in the 12 bytes below 64 KB,
    // whose length would come back near 4 GB.
    const int n = RandBelow(24) == 0    ? 0
                  : RandBelow(100) == 0 ? 0x10000 + 1 + RandBelow(0x100)
                  : RandBelow(4)        ? 1 + RandBelow(12)
                                        : 1 + RandBelow(0x200);
    for (int i = 0; i < n; ++i)
      op.data.push_back(static_cast<BYTE>(Rand32()));
    break;
  }
  case OP_RECEIVETURNS: {
    static const DWORD s_adFirst[] = {0, 0, 0, 0, 1, 3};
    static const DWORD s_adCount[] = {8, 8, 8, 1, 2, 12, 0};
    op.a = s_adFirst[RandBelow(sizeof s_adFirst / sizeof *s_adFirst)];
    op.b = s_adCount[RandBelow(sizeof s_adCount / sizeof *s_adCount)];
    break;
  }
  case OP_REPORT:
    op.a = RandBelow(2);
    op.b = RandBelow(4) ? 8 : RandBelow(3);
    break;
  case OP_JOIN:
  case OP_CHAT:
  case OP_LEAGUE:
  case OP_REPLAYPATH:
  case OP_LOGOUT:
    op.s1 = RandBelow(3) ? RandName(true) : "/" + RandName(true);
    op.a = Rand32();
    break;
  case OP_REPLYNAME:
    op.b = RandBelow(4) ? 0x19 : RandBelow(2);
    break;
  }
  return op;
}

static const char *Str(const std::string &s, bool bNull) { return bNull ? NULL : s.c_str(); }

static const DWORD SENTINEL = 0x5A5A0001;

static std::string Run(const Api &api, const Op &op)
{
  Record r;
  s_pfnSetErr(SENTINEL);
  const int nEventsBefore = s_nEvents, nCallbacksBefore = s_nCallbacks;
  BOOL bRet = FALSE;
  switch (op.nKind) {
  case OP_INIT:
    r.Add("init %08X", op.a);
    bRet = api.InitializeProvider(op.a, s_adwProgram, s_adwPlayer, s_adwUi, s_adwVersion);
    break;
  case OP_DESTROY:
    r.Add("destroy");
    bRet = api.Destroy();
    break;
  case OP_CAPS:
  case OP_PLAYERCAPS: {
    DWORD adw[10];
    memset(adw, 0xCC, sizeof adw);
    adw[0] = op.b;
    r.Add(op.nKind == OP_CAPS ? "caps size %u" : "playercaps %u size %u", op.nKind == OP_CAPS ? op.b : op.a, op.b);
    bRet = op.nKind == OP_CAPS ? api.GetProviderCaps(adw) : api.GetPlayerCaps(op.a, adw);
    r.Bytes(adw, sizeof adw);
    break;
  }
  case OP_PROVIDERID:
    r.Add("providerid");
    bRet = static_cast<BOOL>(api.GetCurrentProviderID());
    break;
  case OP_ENUMPROVIDERS: {
    // Always refused, for a null callback or a filter of the wrong size.
    DWORD adw[9] = {op.b};
    r.Add("enumproviders filter %u", op.b);
    bRet = api.EnumProviders(adw, op.b == 0x24 ? NULL : reinterpret_cast<void *>(OnEnum));
    break;
  }
  case OP_ENUMDEVICES:
    r.Add("enumdevices");
    bRet = api.EnumDevices(op.bNull1 ? NULL : reinterpret_cast<void *>(OnEnum));
    break;
  case OP_INITDEVICE:
    r.Add("initdevice");
    bRet = api.InitializeDevice(0, 0, 0, NULL, NULL);
    break;
  case OP_REGISTER:
    r.Add("register %u", op.a);
    bRet = api.RegisterEventHandler(op.a, op.bNull1 ? NULL : reinterpret_cast<void *>(OnEvent));
    break;
  case OP_CREATE:
  case OP_CREATELADDER: {
    DWORD dwId = 0xCCCCCCCC;
    r.Add("create%s \"%s\" \"%s\" \"%s\" \"%s\" max %u data %u mode %08X", op.nKind == OP_CREATE ? "" : "ladder",
          op.s1.c_str(), op.s2.c_str(), op.s3.c_str(), op.s4.c_str(), op.b, static_cast<DWORD>(op.data.size()), op.d);
    const void *pvData = op.data.empty() ? NULL : op.data.data();
    if (op.nKind == OP_CREATE)
      bRet = api.CreateGame(op.s1.c_str(), Str(op.s2, op.bNull1), Str(op.s3, op.bNull2), op.a, pvData,
                            static_cast<DWORD>(op.data.size()), op.b, op.s4.c_str(), "desc",
                            op.bNullId ? NULL : &dwId);
    else
      bRet = api.CreateLadderGame(op.s1.c_str(), Str(op.s2, op.bNull1), Str(op.s3, op.bNull2), op.a, op.c, op.d,
                                  pvData, static_cast<DWORD>(op.data.size()), op.b, op.s4.c_str(), NULL,
                                  &dwId);
    r.Add(" id %08X", dwId);
    break;
  }
  case OP_LEAVE:
    r.Add("leave %u", op.a);
    bRet = api.LeaveGame(op.a);
    break;
  case OP_DROP:
    r.Add("drop %08X %u", op.a, op.b);
    bRet = api.DropPlayer(op.a, op.b);
    break;
  case OP_DISCONNECT:
    r.Add("disconnect");
    bRet = api.DisconnectAll(0x40000006);
    break;
  case OP_NUMPLAYERS: {
    int an[3] = {0x11, 0x22, 0x33};
    r.Add("numplayers");
    bRet = api.GetNumPlayers(op.bNull1 ? NULL : &an[0], op.bNull2 ? NULL : &an[1], op.bNull3 ? NULL : &an[2]);
    r.Add(" %d %d %d", an[0], an[1], an[2]);
    break;
  }
  case OP_PLAYERNAME: {
    char sz[0x110];
    memset(sz, 0xCC, sizeof sz);
    r.Add("playername %08X cap %u", op.a, op.b);
    bRet = api.GetPlayerName(op.a, op.bNull1 ? NULL : sz, op.b);
    r.Bytes(sz, op.b + 2 < sizeof sz ? op.b + 2 : sizeof sz);
    break;
  }
  case OP_PLAYERNAMES: {
    char *apsz[9];
    for (int i = 0; i < 9; ++i)
      apsz[i] = NULL;
    r.Add("playernames");
    bRet = api.GetPlayerNames(apsz);
    for (int i = 0; i < 9; ++i)
      r.Add(" %s", apsz[i] ? (std::string("\"") + apsz[i] + "\"").c_str() : "null");
    break;
  }
  case OP_GAMEINFO: {
    BYTE ab[0x110];
    memset(ab, 0xCC, sizeof ab);
    DWORD cbRead = 0xCCCCCCCC;
    r.Add("gameinfo %u size %u", op.a, op.b);
    bRet = api.GetGameInfo(op.a, op.bNull1 ? NULL : ab, op.b, op.bNull2 ? NULL : &cbRead);
    r.Add(" read %08X", cbRead);
    r.Bytes(ab, op.b + 4 < sizeof ab ? op.b + 4 : sizeof ab);
    break;
  }
  case OP_SETMODE:
    r.Add("setmode %08X %u", op.a, op.b);
    bRet = api.SetGameMode(op.a, op.b);
    break;
  case OP_TRANSIT: {
    DWORD dw = 0xCCCCCCCC;
    r.Add("transit");
    bRet = api.GetTurnsInTransit(op.bNull1 ? NULL : &dw);
    r.Add(" %08X", dw);
    break;
  }
  case OP_SENDTURN:
    r.Add("sendturn %u", static_cast<DWORD>(op.data.size()));
    bRet = api.SendTurn(op.bNull1 ? NULL : (op.data.empty() ? "" : static_cast<const void *>(op.data.data())),
                        static_cast<DWORD>(op.data.size()));
    break;
  case OP_RECEIVETURNS: {
    const void *apv[16];
    DWORD acb[16], adw[16];
    memset(apv, 0xCC, sizeof apv);
    memset(acb, 0xCC, sizeof acb);
    memset(adw, 0xCC, sizeof adw);
    r.Add("receiveturns %u %u", op.a, op.b);
    bRet = api.ReceiveTurns(op.a, op.b, op.bNull1 ? NULL : apv, op.bNull2 ? NULL : acb, op.bNull3 ? NULL : adw);
    for (int i = 0; i < 14; ++i) {
      r.Add(" %08X/%08X", acb[i], adw[i]);
      if (apv[i] == reinterpret_cast<const void *>(0xCCCCCCCC))
        r.Add(" untouched");
      else if (!apv[i])
        r.Add(" null");
      else {
        r.Bytes(apv[i], acb[i]);
        ++g_nTurns;
      }
    }
    break;
  }
  case OP_SENDMESSAGE:
    r.Add("sendmessage %08X %u", op.a, static_cast<DWORD>(op.data.size()));
    bRet = api.SendMessage(op.a, op.bNull1 ? NULL : (op.data.empty() ? "" : static_cast<const void *>(op.data.data())),
                           static_cast<DWORD>(op.data.size()));
    break;
  case OP_RECEIVEMESSAGE: {
    int nFrom = 0x11;
    const void *pv = reinterpret_cast<const void *>(0xCCCCCCCC);
    DWORD cb = 0xCCCCCCCC;
    r.Add("receivemessage");
    bRet = api.ReceiveMessage(op.bNull1 ? NULL : &nFrom, op.bNull2 ? NULL : &pv, op.bNull3 ? NULL : &cb);
    r.Add(" from %d size %08X", nFrom, cb);
    if (pv == reinterpret_cast<const void *>(0xCCCCCCCC))
      r.Add(" untouched");
    else if (!pv)
      r.Add(" null");
    else {
      r.Bytes(pv, cb);
      ++g_nMessages;
    }
    break;
  }
  case OP_REPORT: {
    int an[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    r.Add("report %u %u", op.a, op.b);
    bRet = api.ReportGameResult(op.a, op.b, op.bNull1 ? NULL : an, "header", "detail");
    break;
  }
  case OP_UPGRADE: {
    DWORD dw = 0xCCCCCCCC;
    r.Add("upgrade");
    bRet = api.PerformUpgrade(op.bNull1 ? NULL : &dw);
    r.Add(" %08X", dw);
    break;
  }
  case OP_ENUMGAMES: {
    DWORD dw = 0xCCCCCCCC;
    r.Add("enumgames");
    bRet = api.EnumGamesEx(0, 0, op.bNull1 ? NULL : reinterpret_cast<void *>(OnEvent), op.bNull2 ? NULL : &dw);
    r.Add(" %08X", dw);
    break;
  }
  case OP_JOIN: {
    // With a player name, as for SNetCreateGame.
    DWORD dw = 0xCCCCCCCC;
    r.Add("join");
    bRet = api.JoinGame(op.a, "game", "", "Player", "", op.bNull1 ? NULL : &dw);
    r.Add(" %08X", dw);
    break;
  }
  case OP_SELECT: {
    DWORD dw = 0xCCCCCCCC;
    r.Add("select");
    bRet = api.SelectGame(1, s_adwProgram, s_adwPlayer, s_adwUi, s_adwVersion, op.bNull1 ? NULL : &dw);
    r.Add(" %08X", dw);
    break;
  }
  case OP_CHAT:
    r.Add("chat \"%s\"", op.s1.c_str());
    bRet = api.SendServerChatCommand(op.s1.c_str());
    break;
  case OP_LEAGUE:
    r.Add("league \"%s\"", op.s1.c_str());
    bRet = api.SendLeagueCommand(Str(op.s1, op.bNull1), NULL);
    break;
  case OP_REPLAYPATH:
    r.Add("replaypath");
    bRet = api.SendReplayPath(Str(op.s1, op.bNull1), op.a, 1);
    break;
  case OP_LEAGUENAME: {
    int n = 0x11;
    r.Add("leaguename");
    bRet = api.GetLeagueName(op.bNull1 ? NULL : &n);
    r.Add(" %d", n);
    break;
  }
  case OP_LOGOUT:
    r.Add("logout");
    bRet = api.LeagueLogout(Str(op.s1, op.bNull1));
    break;
  case OP_REPLYNAME: {
    char sz[0x20];
    memset(sz, 0xCC, sizeof sz);
    r.Add("replyname %u", op.b);
    bRet = api.GetReplyName(op.bNull1 ? NULL : sz, op.b);
    r.Bytes(sz, sizeof sz);
    break;
  }
  }
  r.Add(" -> %d err %08X", bRet, s_pfnGetErr());
  if (s_nEvents != nEventsBefore)
    r.Add(" events %d", s_nEvents - nEventsBefore);
  if (s_nCallbacks != nCallbacksBefore)
    r.Add(" callbacks %d", s_nCallbacks - nCallbacksBefore);
  return r.s;
}

// A call that has not come back in 5 s: every thread's place, for a hang in storm.dll.
static volatile LONG s_nCall;
static DWORD s_dwMainThread;
static DWORD WINAPI Watchdog(void *)
{
  LONG nLast = -1;
  for (;;) {
    Sleep(5000);
    const LONG n = s_nCall;
    if (n != nLast) {
      nLast = n;
      continue;
    }
    printf("call %ld has not come back; storm.dll at %p\n", n, s_hStorm);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = {sizeof te};
    for (BOOL b = Thread32First(hSnap, &te); b; b = Thread32Next(hSnap, &te)) {
      if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId())
        continue;
      HANDLE h = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
      SuspendThread(h);
      CONTEXT c = {};
      c.ContextFlags = CONTEXT_FULL;
      GetThreadContext(h, &c);
      printf(" thread %u%s eip %08X esp %08X:", te.th32ThreadID, te.th32ThreadID == s_dwMainThread ? " (main)" : "",
             c.Eip, c.Esp);
      const DWORD *pdw = reinterpret_cast<const DWORD *>(c.Esp);
      for (int i = 0; i < 64 && !IsBadReadPtr(pdw + i, 4); ++i)
        if (pdw[i] >= reinterpret_cast<DWORD>(s_hStorm) && pdw[i] < reinterpret_cast<DWORD>(s_hStorm) + 0x80000)
          printf(" +%05X", pdw[i] - reinterpret_cast<DWORD>(s_hStorm));
      printf("\n");
      ResumeThread(h);
      CloseHandle(h);
    }
    CloseHandle(hSnap);
    exit(3);
  }
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: net_test.exe <install folder> [rounds]\n");
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  const std::string install = argv[1];
  const int nRounds = argc > 2 ? atoi(argv[2]) : 400;
  s_bTrace = argc > 3;
  char szSelf[MAX_PATH];
  GetModuleFileNameA(NULL, szSelf, MAX_PATH);
  std::string here = szSelf;
  here.resize(here.find_last_of('\\') + 1);
  if (!CopyFileA((install + "\\standard.snp").c_str(), (here + "standard.snp").c_str(), FALSE)) {
    printf("cannot copy standard.snp from %s\n", install.c_str());
    return 2;
  }
  s_hStorm = LoadLibraryA((install + "\\storm.dll").c_str());
  if (!s_hStorm) {
    printf("no storm.dll in %s\n", install.c_str());
    return 2;
  }
  // storm.dll's DllMain, when LoadLibrary loads it, builds the table that decrypts
  // archives from another seed than when the loader loads it with the game, and it
  // opens each provider as an archive to check its signature. Clearing its pointer
  // makes the first archive it opens build the game's table (storm_test.cpp).
  *reinterpret_cast<void **>(reinterpret_cast<BYTE *>(s_hStorm) + 0x696A4) = NULL;
  s_pfnSetErr = reinterpret_cast<PFN_SETERR>(GetProcAddress(s_hStorm, MAKEINTRESOURCEA(465)));
  s_pfnGetErr = reinterpret_cast<PFN_GETERR>(GetProcAddress(s_hStorm, MAKEINTRESOURCEA(463)));
  FillDescriptors();
  s_dwMainThread = GetCurrentThreadId();
  CreateThread(NULL, 0, Watchdog, NULL, 0, NULL);
  const Api storm = StormApi(), ours = OurApi();
  for (int nRound = 0; nRound < nRounds; ++nRound) {
    // A round starts the way the game starts a single-player game, most of the time.
    std::vector<Op> ops;
    Op destroy = {OP_DESTROY};
    ops.push_back(destroy);
    if (RandBelow(4)) {
      // Providers that are not there: the others standard.snp has are gone from the port.
      static const DWORD s_adMissing[] = {'BNET', 1, 'XXXX'};
      Op init = {OP_INIT};
      if (RandBelow(6) == 0) {
        // Storm lets go of the provider it had before it looks for the one asked
        // for, so a call after one it cannot find sees none started.
        if (RandBelow(2))
          ops.push_back(init);
        init.a = s_adMissing[RandBelow(3)];
        ops.push_back(init);
        ops.push_back(RandOp());
        init.a = 0;
      }
      ops.push_back(init);
      if (RandBelow(4)) {
        Op create = RandOp();
        while (create.nKind != OP_CREATE)
          create = RandOp();
        create.bNull1 = create.bNull2 = create.bNull3 = false;
        create.s4 = "Player";
        create.b = 8;
        ops.push_back(create);
      }
    }
    const int nOps = 20 + RandBelow(60);
    for (int i = 0; i < nOps; ++i) {
      ops.push_back(RandOp());
      // A mode set is read back at once, with the password it may drop; a read at
      // random seldom follows one.
      if (ops.back().nKind == OP_SETMODE) {
        Op mode = {OP_GAMEINFO}, password = {OP_GAMEINFO};
        mode.a = 4;
        mode.b = 4;
        password.a = 2;
        password.b = 0x80;
        ops.push_back(mode);
        ops.push_back(password);
      }
      // Now and then a run of a game's frames: the turns in transit, a turn sent
      // while none is, all eight players' turns read, and a message looked for;
      // with a message sent to everyone now and then.
      if (RandBelow(10) == 0) {
        const int nFrames = 1 + RandBelow(40);
        for (int f = 0; f < nFrames; ++f) {
          Op transit = {OP_TRANSIT}, receive = {OP_RECEIVETURNS}, message = {OP_RECEIVEMESSAGE};
          receive.b = 8;
          Op send = RandOp();
          while (send.nKind != OP_SENDTURN || send.data.empty())
            send = RandOp();
          send.bNull1 = false;
          ops.push_back(transit);
          if (RandBelow(8))
            ops.push_back(send);
          if (RandBelow(20) == 0) {
            Op say = RandOp();
            while (say.nKind != OP_SENDMESSAGE || say.data.empty())
              say = RandOp();
            say.a = static_cast<DWORD>(-1);
            say.bNull1 = false;
            ops.push_back(say);
          }
          ops.push_back(receive);
          ops.push_back(message);
        }
      }
    }
    std::string history;
    for (size_t i = 0; i < ops.size(); ++i) {
      if (s_bTrace)
        printf("round %d call %u kind %d\n", nRound, static_cast<unsigned>(i), ops[i].nKind);
      InterlockedIncrement(&s_nCall);
      const std::string a = Run(storm, ops[i]);
      if (s_bTrace)
        printf("  %s\n", a.substr(0, 300).c_str());
      const std::string b = Run(ours, ops[i]);
      ++g_nCases;
      if (a != b) {
        if (g_nDiffs++ < 20)
          printf("DIFF round %d call %u:\n  storm %s\n  ours  %s\n  after:%s\n", nRound, static_cast<unsigned>(i),
                 a.substr(0, 600).c_str(), b.substr(0, 600).c_str(), history.substr(history.size() > 600 ? history.size() - 600 : 0).c_str());
        break;
      }
      history += " | " + a.substr(0, 60);
    }
  }
  Op destroy = {OP_DESTROY};
  Run(storm, destroy);
  Run(ours, destroy);
  printf("%d rounds, %d calls, %d turns and %d messages read, %d differences\n", nRounds, g_nCases, g_nTurns,
         g_nMessages, g_nDiffs);
  return g_nDiffs ? 1 : 0;
}
