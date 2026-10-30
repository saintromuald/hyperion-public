// port_lanfeed.cpp -- a LAN match's replay as it is played (docs/modules/lan.md,
// "The feed and watching"). Presentation and transport only: a watched match is a replay, and
// a seat's game is only read.
//
// Each seat sends the room the game's record as it grows (gpNetRecord_g's blocks: a frame, a
// count, each command with its player): first what write_replay writes but the commands, then
// every few frames the blocks since and the frame they are complete to, the open block going
// once its frame has passed with its count put in, as the recorder does when the next opens;
// last, the end frame and the header and sections as the game left them. A watcher starts the
// beginning as a replay with no commands, as Replays' Watch does, appends the blocks as they
// come (net_record_append), and runs the room's delay behind and no further.
#include "port_lanfeed.h"
#include "../platform/port_log.h"  // PortLogger
#include "../platform/port_util.h"  // PortNowMs
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../gameplay/port_launch.h"  // PortLaunchWatchReplay
#include "../platform/port_paths.h"  // PortDataFolder
#include "../render/port_present.h"  // PortPresentThumb
#include "../replays/port_tv.h"  // PortTvHottest, PortTvOn, PortTvToggle
#include "port_lanclient.h"
#include "port_lanproto.h"
#include "../front/port_text.h"
#include "../front/port_ui.h"
#include "../render/port_overlay.h"  // PORT_OVERLAY_LAYER
#include "oc_data.h"
#include "../../h/net_record.h"     // net_record_live, net_record_append
#include "../../h/replay.h"         // replay_live_parts, replay_write_parts, replay_set_total_frames
#include "../../h/port_hxreplay.h"  // REPLAY_MAGIC_HX
#include "../../h/gamemap.h"        // gamemap_center_on_mtx
#include "../../h/net_cmds.h"       // PlayerData
#include "../../h/opt_dlgs.h"       // PortQuitToMenu
#include <deque>
#include "CUnit.h"
#include "CSprite.h"
#include "../../gen/globals117.h"   // gPlayback, gwGameMode
#include "../platform/port_globals117.h"
#include "../tools/port_startup.h"  // PortStartupEnded

using namespace LanProto;

namespace {

const uint32_t kHeader = kReplayHeader;
const uint32_t kSendEvery = 8;    // frames between a seat's sends, a third of a second at Fastest
const uint32_t kLive = 0x7FFFFFFF;  // a watched replay's length while its match goes on

const PortLogger Log("hyperion_lan.log", PortLogger::SHARED, "feed: ");

std::string Folder(void)
{
  std::string folder = std::string(PortDataFolder()) + "lan";
  CreateDirectoryA(folder.c_str(), NULL);
  return folder;
}

std::vector<uint8_t> FileBytes(const std::string &path)
{
  std::vector<uint8_t> out;
  FILE *f = fopen(path.c_str(), "rb");
  if (f == NULL)
    return out;
  uint8_t ab[4096];
  for (size_t n; (n = fread(ab, 1, sizeof ab, f)) > 0;)
    out.insert(out.end(), ab, ab + n);
  fclose(f);
  return out;
}

// ---------------------------------------------------------------------------
// A seat's side.

struct SeatFeed {
  int nMatch = 0;
  bool bStarted = false, bEnded = false;
  int nSent = 0;          // the record's bytes the room has
  uint32_t dwSentAt = 0;  // the frame of the last send
} g_seat;

// The replay's parts but the commands: its magic, header, map (bMap) and Hyperion's sections.
bool Parts(Writer *w, bool bMap)
{
  uint8_t abHeader[kHeader];
  void *pChk = NULL;
  DWORD dwChk = 0, dwMagic = 0;
  const std::string hx = Folder() + "\\feed.hx";
  const BOOL bOk = replay_live_parts(abHeader, &pChk, &dwChk, &dwMagic, hx.c_str());
  if (!bOk) {
    if (pChk != NULL)
      replay_live_free(pChk);
    return false;
  }
  w->D(dwMagic);
  w->Bytes(abHeader, kHeader);
  if (bMap)
    w->Bytes(pChk, dwChk);
  replay_live_free(pChk);
  const std::vector<uint8_t> sections = dwMagic == REPLAY_MAGIC_HX ? FileBytes(hx) : std::vector<uint8_t>();
  w->Bytes(sections.data(), sections.size());
  return true;
}

// The blocks the room does not have, complete to dwFed: the one still open goes when its frame
// is before dwFed (or at the end), its count put in.
void SendRecord(uint32_t dwFed, bool bEnd)
{
  const BYTE *pBuf;
  int nOpenAt;
  DWORD dwOpen;
  const int nLen = net_record_live(&pBuf, &nOpenAt, &dwOpen);
  int nEnd = nLen;
  bool bCount = false;
  if (nOpenAt >= 0) {
    if (nLen == nOpenAt + 5 || (!bEnd && dwOpen >= dwFed))
      nEnd = nOpenAt;  // empty, or its frame still to come
    else
      bCount = true;
  }
  if (nEnd < g_seat.nSent) {
    Log("the record is shorter than was sent (%d of %d)", nEnd, g_seat.nSent);
    g_seat.bEnded = true;
    return;
  }
  std::vector<uint8_t> bytes(pBuf + g_seat.nSent, pBuf + nEnd);
  if (bCount && nOpenAt + 4 >= g_seat.nSent)
    bytes[nOpenAt + 4 - g_seat.nSent] = static_cast<uint8_t>(nLen - nOpenAt - 5);
  Writer w;
  w.B(FEED_RECORD);
  w.D(static_cast<uint32_t>(g_seat.nMatch));
  w.D(static_cast<uint32_t>(g_seat.nSent));
  w.D(dwFed);
  w.Bytes(bytes.data(), bytes.size());
  LanClientSend(ROOM_FEED, w.v);
  g_seat.nSent = nEnd;
  g_seat.dwSentAt = dwFed;
}

// ---------------------------------------------------------------------------
// Watching.

enum { WATCH_NONE, WATCH_ASKED, WATCH_READY, WATCH_ON };

struct Watch {
  int nState = WATCH_NONE;
  int nMatch = 0;
  std::string name;
  uint32_t dwDelay = 240;   // frames behind the players
  uint32_t dwDelayS = 10;   // and in seconds
  uint32_t dwFrameMs = 42;  // a frame at the match's speed
  uint32_t dwFed = 0;       // the commands are here to this frame
  uint64_t qwFedAt = 0;
  bool bEnded = false, bLost = false;
  uint32_t dwEnd = 0;
  uint32_t dwMagic = 0;
  std::vector<uint8_t> header, chk, sections;  // the replay's parts, from its beginning
  std::vector<uint8_t> record;   // every block come, as the room counts them
  size_t nInGame = 0;            // the blocks the game's record has (the file's, then those added)
  bool bInGame = false;          // the game runs: blocks go into its record as they come
  std::string path;
  bool bCatching = false;
  bool bWasOn = false;
  bool bSpeedSet = false;  // the replay set to the match's speed, once it runs
  bool bCopy = false;      // the room's copy: pictures of the armies, and gone at the end
  uint64_t qwPicture = 0;  // when the last picture was sent
  int nLooking = -1;       // the player the camera was moved to, for the next picture
  uint32_t dwHeatSent = 0; // the copy: the frame its last heat was sent at
  bool bAheadSaid = false; // watching: the director's first look ahead logged
  struct Heat {
    uint32_t dwFrame;
    int nX, nY;
    float fHeat;
  };
  std::deque<Heat> heat;   // watching: where the copy saw the fighting, ahead of the picture
} g_watch;

// 1.17's game speeds, Slowest to Fastest: a frame in milliseconds.

uint32_t Limit(void)
{
  if (g_watch.bEnded)
    return g_watch.dwEnd;
  return g_watch.dwFed > g_watch.dwDelay ? g_watch.dwFed - g_watch.dwDelay : 0;
}

// Where the match is now: the last frame fed, and the time since.
uint32_t Edge(void)
{
  if (g_watch.bEnded)
    return g_watch.dwEnd;
  const uint64_t qwSince = PortNowMs() - g_watch.qwFedAt;
  const uint64_t qwOn = qwSince / (g_watch.dwFrameMs ? g_watch.dwFrameMs : 42);
  return g_watch.dwFed + static_cast<uint32_t>(qwOn < 16 ? qwOn : 16);
}

void Ended(uint32_t dwEnd)
{
  g_watch.bEnded = true;
  g_watch.dwEnd = dwEnd;
  if (g_watch.nState == WATCH_ON)
    replay_set_total_frames(static_cast<int>(dwEnd));
}

void Begin(Reader &r)
{
  const uint32_t dwDelayS = r.D();
  const uint32_t dwFrameMs = r.D();
  const uint32_t dwMagic = r.D();
  std::vector<uint8_t> header = r.Bytes();
  std::vector<uint8_t> chk = r.Bytes();
  std::vector<uint8_t> sections = r.Bytes();
  if (r.bBad || header.size() != kHeader || chk.empty()) {
    Log("match %d: its beginning came short", g_watch.nMatch);
    return;
  }
  g_watch.dwFrameMs = dwFrameMs ? dwFrameMs : 42;
  g_watch.dwDelayS = dwDelayS;
  g_watch.dwDelay = (dwDelayS * 1000 + g_watch.dwFrameMs / 2) / g_watch.dwFrameMs;
  // its length, at the header's second byte, as long as can be until the match ends
  memcpy(&header[1], &kLive, 4);
  g_watch.dwMagic = dwMagic;
  g_watch.header.swap(header);
  g_watch.chk.swap(chk);
  g_watch.sections.swap(sections);
  g_watch.path = Folder() + "\\watching.rep";
  g_watch.nState = WATCH_READY;
  Log("match %d: watching, %u s behind", g_watch.nMatch, dwDelayS);
}

void Blocks(Reader &r)
{
  const uint32_t dwAt = r.D();
  const uint32_t dwFed = r.D();
  const std::vector<uint8_t> bytes = r.Bytes();
  if (r.bBad)
    return;
  if (dwAt != g_watch.record.size()) {
    Log("match %d: blocks at %u, where %u were expected", g_watch.nMatch, dwAt,
        static_cast<unsigned>(g_watch.record.size()));
    return;
  }
  // as a replay's record, held to 256 MB (net_record_append)
  if (bytes.size() > (256u << 20) - g_watch.record.size()) {
    Log("match %d: the feed passed a record's most; its blocks are dropped", g_watch.nMatch);
    return;
  }
  g_watch.record.insert(g_watch.record.end(), bytes.begin(), bytes.end());
  if (g_watch.bInGame && !bytes.empty()) {
    net_record_append(bytes.data(), static_cast<int>(bytes.size()));
    g_watch.nInGame = g_watch.record.size();
  }
  if (dwFed > g_watch.dwFed) {
    g_watch.dwFed = dwFed;
    g_watch.qwFedAt = PortNowMs();
  }
}

// The room's copy: a player's army, where the camera goes for its picture. Its fighting units'
// middle, or, with none, all its units', at the unit nearest it.
bool ArmyOf(int nPlayer, int *pnX, int *pnY)
{
  long long sx = 0, sy = 0;
  int n = 0;
  for (int pass = 0; pass < 2 && n == 0; ++pass)
    for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
      if (pUnit->bPlayer != nPlayer || pUnit->pSprite == NULL || pUnit->lHitPoints <= 0)
        continue;
      const WORD t = pUnit->wType;
      // the first pass: units that fight (not workers, larvae, eggs, overlords or buildings)
      if (pass == 0 && (t >= 106 || t == 7 || t == 41 || t == 64 || t == 35 || t == 36 || t == 42))
        continue;
      sx += pUnit->wPositionX;
      sy += pUnit->wPositionY;
      ++n;
    }
  if (n == 0)
    return false;
  const int cx = static_cast<int>(sx / n), cy = static_cast<int>(sy / n);
  long long best = -1;
  for (CUnit *pUnit = CUnit::GetUsedList(); pUnit != NULL; pUnit = pUnit->pNext) {
    if (pUnit->bPlayer != nPlayer || pUnit->pSprite == NULL || pUnit->lHitPoints <= 0)
      continue;
    const long long dx = pUnit->wPositionX - cx, dy = pUnit->wPositionY - cy;
    if (best < 0 || dx * dx + dy * dy < best) {
      best = dx * dx + dy * dy;
      *pnX = pUnit->wPositionX;
      *pnY = pUnit->wPositionY;
    }
  }
  return true;
}

// About once a second of the game, where the fighting is hottest, if there is a fight: the room
// passes it to those watching, whose director reads ahead by it.
void CopyHeat(void)
{
  int x, y;
  float fHeat;
  if (!PortTvHottest(&x, &y, &fHeat))
    return;
  if (oc_dwReplayFrame - g_watch.dwHeatSent < 24)
    return;
  g_watch.dwHeatSent = oc_dwReplayFrame;
  Writer w;
  w.B(FEED_HEAT);
  w.D(static_cast<uint32_t>(g_watch.nMatch));
  w.D(oc_dwReplayFrame);
  w.D(static_cast<uint32_t>(x));
  w.D(static_cast<uint32_t>(y));
  w.D(static_cast<uint32_t>(fHeat * 100.0f));
  LanClientSend(ROOM_FEED, w.v);
}

// Five times a second: the picture of the army the camera went to last time, then the camera to
// the next player's, so each seat's picture on the console's Wall is new every second or so.
void CopyFrame(void)
{
  const uint64_t qwNow = PortNowMs();
  if (qwNow - g_watch.qwPicture < 200 || PortLanWatchCatching())
    return;
  g_watch.qwPicture = qwNow;
  if (g_watch.nLooking >= 0) {
    std::vector<uint8_t> rgb(static_cast<size_t>(kPictureW) * kPictureH * 3);
    if (PortPresentThumb(rgb.data(), kPictureW, kPictureH)) {
      Writer w;
      w.B(FEED_PICTURE);
      w.D(static_cast<uint32_t>(g_watch.nMatch));
      w.B(static_cast<uint8_t>(g_watch.nLooking));
      w.S(gPlayerData[g_watch.nLooking].szName);
      w.W(kPictureW);
      w.W(kPictureH);
      w.Bytes(rgb.data(), rgb.size());
      LanClientSend(ROOM_FEED, w.v);
    }
  }
  // the next player in the game with units
  for (int k = 1; k <= 8; ++k) {
    const int p = (g_watch.nLooking + k + 8) % 8;
    int x, y;
    if (gPlayerData[p].nType != 0 && ArmyOf(p, &x, &y)) {
      gamemap_center_on_mtx(x / 32, y / 32);
      g_watch.nLooking = p;
      return;
    }
  }
  g_watch.nLooking = -1;
}

}  // namespace

// ---------------------------------------------------------------------------

void LanFeedSeatFrame(int nMatch, uint32_t dwFrame)
{
  if (nMatch <= 0)
    return;
  if (g_seat.nMatch != nMatch) {
    g_seat = SeatFeed();
    g_seat.nMatch = nMatch;
  }
  if (g_seat.bEnded)
    return;
  if (!g_seat.bStarted) {
    g_seat.bStarted = true;
    Writer w;
    w.B(FEED_START);
    w.D(static_cast<uint32_t>(nMatch));
    if (!Parts(&w, true)) {
      Log("match %d: its beginning could not be made", nMatch);
      g_seat.bEnded = true;
      return;
    }
    LanClientSend(ROOM_FEED, w.v);
    Log("match %d: sending its record from frame %u", nMatch, dwFrame);
  }
  if (dwFrame - g_seat.dwSentAt >= kSendEvery)
    SendRecord(dwFrame, false);
}

void LanFeedSeatEnd(void)
{
  if (!g_seat.bStarted || g_seat.bEnded)
    return;
  int nEnd = replay_get_total_frames();
  if (nEnd <= 0 || static_cast<uint32_t>(nEnd) < g_seat.dwSentAt)
    nEnd = static_cast<int>(oc_dwReplayFrame);
  SendRecord(static_cast<uint32_t>(nEnd), true);
  Writer w;
  w.B(FEED_END);
  w.D(static_cast<uint32_t>(g_seat.nMatch));
  w.D(static_cast<uint32_t>(nEnd));
  w.D(static_cast<uint32_t>(g_seat.nSent));
  if (!Parts(&w, false))
    Log("match %d: its end's header could not be made", g_seat.nMatch);
  LanClientSend(ROOM_FEED, w.v);
  g_seat.bEnded = true;
  Log("match %d: its record sent to frame %d, %d bytes", g_seat.nMatch, nEnd, g_seat.nSent);
}

void LanWatchAsk(int nMatch, const std::string &name)
{
  g_watch = Watch();
  g_watch.nState = WATCH_ASKED;
  g_watch.nMatch = nMatch;
  g_watch.name = name;
  LanClientSay("{\"t\":\"watch\",\"match\":" + std::to_string(nMatch) + "}");
  Log("match %d: asked to watch", nMatch);
}

void LanWatchAsCopy(int nMatch)
{
  g_watch = Watch();
  g_watch.nState = WATCH_ASKED;
  g_watch.nMatch = nMatch;
  g_watch.bCopy = true;
  Log("match %d: the room's copy", nMatch);
}

bool PortLanWatchAhead(int *pnX, int *pnY, float *pfHeat)
{
  if (g_watch.nState != WATCH_ON || g_watch.bCopy || !PortLanWatchShown())
    return false;
  // what has been passed is gone; the hottest of the next two seconds or so leads
  const uint32_t dwNow = oc_dwReplayFrame;
  while (!g_watch.heat.empty() && g_watch.heat.front().dwFrame < dwNow)
    g_watch.heat.pop_front();
  const uint32_t dwLead = 2000 / (g_watch.dwFrameMs ? g_watch.dwFrameMs : 42);
  const Watch::Heat *pBest = NULL;
  for (const Watch::Heat &h : g_watch.heat) {
    if (h.dwFrame > dwNow + dwLead)
      break;
    if (pBest == NULL || h.fHeat > pBest->fHeat)
      pBest = &h;
  }
  if (pBest == NULL)
    return false;
  if (!g_watch.bAheadSaid) {
    g_watch.bAheadSaid = true;
    Log("match %d: at frame %u the director looks ahead to the fight at frame %u", g_watch.nMatch, dwNow, pBest->dwFrame);
  }
  *pnX = pBest->nX;
  *pnY = pBest->nY;
  *pfHeat = pBest->fHeat;
  return true;
}

void LanWatchCancel(void)
{
  if (g_watch.nState == WATCH_NONE)
    return;
  LanClientSay("{\"t\":\"unwatch\"}");
  g_watch = Watch();
}

bool LanWatchWaiting(void)
{
  return g_watch.nState == WATCH_ASKED;
}

bool LanWatchReady(void)
{
  return g_watch.nState == WATCH_READY;
}

int LanWatchLaunch(void)
{
  if (g_watch.nState != WATCH_READY)
    return FALSE;
  // the replay as it is so far: the game may read it again as it starts (new_game)
  if (!replay_write_parts(g_watch.path.c_str(), g_watch.dwMagic, g_watch.header.data(), g_watch.record.data(),
                          static_cast<int>(g_watch.record.size()), g_watch.chk.data(),
                          static_cast<DWORD>(g_watch.chk.size()),
                          g_watch.sections.empty() ? NULL : g_watch.sections.data(),
                          static_cast<DWORD>(g_watch.sections.size()))) {
    Log("match %d: its replay could not be written to %s", g_watch.nMatch, g_watch.path.c_str());
    LanWatchCancel();
    return FALSE;
  }
  g_watch.nInGame = g_watch.record.size();
  const int nStarted = PortLaunchWatchReplay(g_watch.path.c_str());
  if (!nStarted) {
    Log("match %d: its replay did not start", g_watch.nMatch);
    LanWatchCancel();
    return FALSE;
  }
  g_watch.nState = WATCH_ON;
  g_watch.bWasOn = true;
  g_watch.bCatching = true;
  if (g_watch.bEnded)
    replay_set_total_frames(static_cast<int>(g_watch.dwEnd));
  return nStarted;
}

void LanWatchFollowPump(void)
{
  static bool s_bLeaving;
  static uint64_t s_qwOver;
  if (!PortLanWatchShown()) {
    s_bLeaving = false;
    s_qwOver = 0;
    return;
  }
  if (!PortTvOn())
    PortTvToggle();
  if (s_bLeaving)
    return;
  // Match Over shown for five seconds, the game is left: the room ends the page's watching, or the
  // Big Screen goes on to another match
  if (g_watch.bEnded && oc_dwReplayFrame >= g_watch.dwEnd) {
    if (s_qwOver == 0)
      s_qwOver = PortNowMs();
    if (PortNowMs() - s_qwOver > 5000) {
      s_bLeaving = true;
      Log("match %d: shown to its end; left", g_watch.nMatch);
      PortQuitToMenu();
    }
    return;
  }
  const int nFollow = LanClientFollow();
  if (nFollow > 0 && nFollow != g_watch.nMatch) {
    s_bLeaving = true;
    Log("match %d: left for match %d, which the room says to show", g_watch.nMatch, nFollow);
    PortQuitToMenu();
  } else if (LanClientTakeEnd()) {
    // the page stopped watching (a seat in the browser's tab)
    s_bLeaving = true;
    Log("match %d: left, as the room says", g_watch.nMatch);
    PortQuitToMenu();
  }
}

void LanWatchPump(void)
{
  if (g_watch.nState == WATCH_NONE)
    return;
  LanClientPump();
  std::vector<uint8_t> data;
  while (LanClientTakeFeed(&data)) {
    Reader r(data);
    const uint8_t bKind = r.B();
    const int nMatch = static_cast<int>(r.D());
    if (nMatch != g_watch.nMatch)
      continue;
    if (bKind == FEED_START && g_watch.nState == WATCH_ASKED)
      Begin(r);
    else if (bKind == FEED_RECORD)
      Blocks(r);
    else if (bKind == FEED_END && !g_watch.bEnded)
      Ended(r.D());
    else if (bKind == FEED_HEAT) {
      Watch::Heat h;
      h.dwFrame = r.D();
      h.nX = static_cast<int>(r.D());
      h.nY = static_cast<int>(r.D());
      h.fHeat = r.D() / 100.0f;
      if (!r.bBad) {
        if (g_watch.heat.empty())
          Log("match %d: the copy saw fighting at frame %u (%d, %d), heat %.1f", g_watch.nMatch, h.dwFrame, h.nX, h.nY,
              h.fHeat);
        g_watch.heat.push_back(h);
      }
      if (g_watch.heat.size() > 600)
        g_watch.heat.pop_front();
    }
  }
  // the room gone: the match ends where its feed did
  if (!g_watch.bEnded && !g_watch.bLost && !LanClientIn() && g_watch.nState == WATCH_ON) {
    g_watch.bLost = true;
    Log("match %d: the room was lost; the replay ends at frame %u", g_watch.nMatch, g_watch.dwFed);
    Ended(g_watch.dwFed);
  }
}

bool PortLanWatchHold(void)
{
  if (g_watch.nState != WATCH_ON || gPlayback == 0)
    return false;
  // the game runs: what came while it started goes into its record
  if (!g_watch.bInGame) {
    g_watch.bInGame = true;
    if (g_watch.record.size() > g_watch.nInGame)
      net_record_append(g_watch.record.data() + g_watch.nInGame,
                        static_cast<int>(g_watch.record.size() - g_watch.nInGame));
    g_watch.nInGame = g_watch.record.size();
    Log("match %d: playing from frame %u, %u bytes of commands", g_watch.nMatch, oc_dwReplayFrame,
        static_cast<unsigned>(g_watch.record.size()));
  }
  LanWatchPump();
  if (g_watch.bCopy && g_watch.bEnded && oc_dwReplayFrame >= g_watch.dwEnd) {
    Log("match %d: the copy has played it to its end, frame %u", g_watch.nMatch, g_watch.dwEnd);
    LanClientLeave();
    PortStartupEnded(0);
    ExitProcess(0);
  }
  // the replay plays at the match's own speed, as the players see it (replay_init_speed set the
  // player's own as the game began)
  if (!g_watch.bSpeedSet) {
    g_watch.bSpeedSet = true;
    for (int i = 0; i < kPortSpeeds; ++i)
      if (PortSpeedFrameMs(i) == g_watch.dwFrameMs)
        replay_set_speed(i, 1, 0);
  }
  if (g_watch.bCopy) {
    CopyFrame();
    CopyHeat();
  }
  if (g_watch.bEnded) {
    g_watch.bCatching = false;
    return false;
  }
  const uint32_t dwLimit = Limit();
  if (g_watch.bCatching && oc_dwReplayFrame + 12 >= dwLimit)
    g_watch.bCatching = false;
  return oc_dwReplayFrame >= dwLimit;
}

bool PortLanWatchLive(void)
{
  return g_watch.nState == WATCH_ON && g_watch.bInGame && gPlayback != 0;
}

bool PortLanWatchCatching(void)
{
  return g_watch.nState == WATCH_ON && g_watch.bCatching && gwGameMode == 3;
}

// L, Go Live: the key going down (not a held key's repeats) and the character it makes.
static bool LayerKey(unsigned vk, bool bAct, bool bRepeat)
{
  if (g_watch.nState != WATCH_ON || gPlayback == 0 || (bAct && bRepeat))
    return false;
  if (!bAct)
    return vk == 'l' || vk == 'L';
  if (vk != 'L')
    return false;
  // Go Live: to the delay behind the players, at once
  if (!g_watch.bEnded && oc_dwReplayFrame + 24 < Limit())
    g_watch.bCatching = true;
  return true;
}

bool PortLanWatchShown(void)
{
  return g_watch.nState == WATCH_ON && gwGameMode == 3 && gPlayback != 0;
}

// The Live bug: at the top of the game's frame, the dot, Live and how far behind the match the
// picture is; L's line when it has fallen behind the delay; Match Over at the end.
static bool LayerDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  if (!PortLanWatchShown())
    return false;
  const float k = fH / 480.0f;
  const float fSize = 8.0f * k;
  const float fPad = 4.0f * k;
  const UiColor ink = UiRgb(0xffffff);
  const UiColor dim = UiRgb(0x9eccf3);
  const UiColor red = UiRgb(0xe5484d);
  const UiColor shade = UiRgb(0x000000, 0.55f);
  const uint32_t dwFrame = oc_dwReplayFrame;
  const bool bOver = g_watch.bEnded && dwFrame >= g_watch.dwEnd;
  const uint32_t dwEdge = Edge();
  const uint32_t dwBehind = dwEdge > dwFrame ? (dwEdge - dwFrame) * g_watch.dwFrameMs / 1000 : 0;
  const uint32_t dwDelayS = g_watch.dwDelayS;
  const bool bLate = !g_watch.bEnded && dwBehind > dwDelayS + 2;
  std::string head = bOver ? PortText("Match Over") : g_watch.bEnded ? PortText("Ended") : PortText("Live");
  std::string tail = g_watch.name;
  if (!bOver)
    // {0} the match's name, {1} how many seconds behind the players the picture is
    tail = PortTextF("{0} \xc2\xb7 {1} s behind", {tail, std::to_string(dwBehind)});
  const float wHead = UiTextWidth(UI_FACE_BOLD, fSize, 0.0f, head.c_str());
  const float wTail = UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, tail.c_str());
  const float fDot = 3.0f * k;
  const float w = fPad + (bOver ? 0.0f : 2.0f * fDot + fPad) + wHead + fPad * 1.5f + wTail + fPad;
  float fAscent, fDescent, fGap;
  UiFontMetrics(UI_FACE_BOLD, fSize, &fAscent, &fDescent, &fGap);
  const float h = fSize * 1.15f + 2.0f * fPad;
  const float x0 = fX + (fW - w) * 0.5f;
  const float y0 = fY + 20.0f * k;  // under 1.17's own line at the top
  UiFill(x0, y0, w, h, shade, 3.0f * k);
  float x = x0 + fPad;
  const float fBase = y0 + fPad + fAscent * 0.95f;
  if (!bOver) {
    UiFill(x, y0 + h * 0.5f - fDot, 2.0f * fDot, 2.0f * fDot, g_watch.bEnded ? dim : red, fDot);
    x += 2.0f * fDot + fPad;
  }
  x += UiText(x, fBase, UI_FACE_BOLD, fSize, 0.0f, ink, head.c_str()) + fPad * 1.5f;
  UiText(x, fBase, UI_FACE_TEXT, fSize, 0.0f, dim, tail.c_str());
  if (bLate) {
    const std::string line = PortTextF("L goes back to {0} s behind", {std::to_string(dwDelayS)});
    const float wl = UiTextWidth(UI_FACE_TEXT, fSize, 0.0f, line.c_str()) + 2.0f * fPad;
    UiFill(fX + (fW - wl) * 0.5f, y0 + h + 3.0f * k, wl, h, shade, 3.0f * k);
    UiText(fX + (fW - wl) * 0.5f + fPad, fBase + h + 3.0f * k, UI_FACE_TEXT, fSize, 0.0f, ink, line.c_str());
  }
  return true;
}

void PortLanWatchHash(const std::vector<uint8_t> &record)
{
  if (!PortLanWatchShown())
    return;
  Writer w;
  w.B(FEED_HASH);
  w.D(static_cast<uint32_t>(g_watch.nMatch));
  w.v.insert(w.v.end(), record.begin(), record.end());
  LanClientSend(ROOM_FEED, w.v);
}

bool PortLanWatchAfterGame(void)
{
  if (!g_watch.bWasOn)
    return false;
  Log("match %d: no longer watching", g_watch.nMatch);
  LanWatchCancel();
  g_watch.bWasOn = false;
  return true;
}

// The Live bug as a layer over the game (port_overlay.h), with L.
const PortOverlayLayer kLayer = {
    "lanwatch", PORT_OVERLAY_LANWATCH, 0,
    PortLanWatchShown, NULL, LayerDraw, NULL, NULL, LayerKey, NULL};
PORT_OVERLAY_LAYER(kLayer);
