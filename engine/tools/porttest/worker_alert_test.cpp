// worker_alert_test.cpp -- race-specific worker-death voices and simultaneous base pings.
// Build as C++17 with -ffunction-sections -fdata-sections and link with --gc-sections.
#include <assert.h>
#include <math.h>
#include <string.h>
#include <iostream>
#include <vector>

#include "../../src117/lang/gamesnd.cpp"

DWORD gPlayback = 0;
DWORD g_LocalNationID = 0;
DWORD gulUnitTypeFlags[228] = {};
DWORD gbSpeechMuted_g = 0;
BYTE byte_5264C2 = 0;
int gnSfxVolume = 100;
const LONG sgSndVolTbl_g[100] = {};
unsigned oc_dwReplayFrame = 1;
int gOptions_sound_on = 1;
DWORD gdwReplayVisionMask = 0;
DWORD gdwLocalMask = 0;
DWORD gOptions_lift_variant = 0;
BYTE gbSoundMinVolume[1144] = {};
int g_posMaskU = 0x7FFFFFFF;
int g_posShift = 1;

bool ViewHxSpriteSeen(const CSprite *) { return false; }
BOOL __fastcall IsSelfOwned(CUnit *) { return true; }
int CUnit::GetPan() { return 0; }
void PortViewSeenRect(int *pnLeft, int *pnTop, int *pnRight, int *pnBottom) {
  *pnLeft = *pnTop = *pnRight = *pnBottom = 0;
}
int __fastcall sub_49E780(int, int, int, CUnit *) { return 1; }

struct Ping { ULONG x, y; unsigned char colour; };
static std::vector<Ping> g_pings;
static std::vector<int> g_voices;
static CUnit *g_units = NULL;

CUnit *__fastcall CUnit::GetUsedList() { return g_units; }
BOOL CUnit::IsEnemyUnit(CUnit *pOther) { return pOther != NULL && pOther->bPlayer != bPlayer; }
BOOL CUnit::IsDeadOrDying() { return false; }
BOOL CUnit::IsResourceDest() { return wType == 0x83 || wType == 0x6A || wType == 0x9A; }
void __fastcall do_minimap_ping(ULONG x, ULONG y, unsigned char colour) {
  g_pings.push_back({x, y, colour});
}
bool PortSoundOneShot(std::shared_ptr<const std::vector<float>>, float) { return true; }
int PortSoundRate(void) { return 44100; }
bool PortSoundWorkerAlert(int nRace, float fGain) {
  assert(nRace >= 0 && nRace < 3);
  assert(fGain > 0.0f);
  g_voices.push_back(nRace);
  return true;
}

static CUnit MakeUnit(int nType, int nPlayer, int nX, int nY) {
  CUnit u;
  memset(&u, 0, sizeof u);
  CSprite sprite;
  memset(&sprite, 0, sizeof sprite);
  sprite.wPosX = static_cast<upos_t>(nX);
  sprite.wPosY = static_cast<upos_t>(nY);
  u.wType = static_cast<WORD>(nType);
  u.bPlayer = static_cast<BYTE>(nPlayer);
  // The sprite storage must outlive the unit. Tests use this only immediately after copying.
  u.pSprite = reinterpret_cast<CSprite *>(malloc(sizeof(CSprite)));
  *u.pSprite = sprite;
  return u;
}

static void Link(CUnit *pUnit) {
  pUnit->pNext = g_units;
  g_units = pUnit;
}

static void ResetAlert(unsigned nFrame) {
  oc_dwReplayFrame = nFrame;
  sgWorkerKillAlerts.clear();
  sgnWorkerKillSndTimer = 0;
  g_pings.clear();
  g_voices.clear();
  g_units = NULL;
  gPlayback = 0;
  g_LocalNationID = 0;
  gbSpeechMuted_g = 0;
  gnSfxVolume = 100;
  memset(gulUnitTypeFlags, 0, sizeof gulUnitTypeFlags);
  gulUnitTypeFlags[0x29] = 8;  // Drone's worker flag
}

int main() {
  ResetAlert(10);
  CUnit hatchery = MakeUnit(0x83, 0, 400, 500);
  CUnit drone = MakeUnit(0x29, 0, 420, 520);
  CUnit enemy = MakeUnit(0, 1, 425, 520);
  Link(&hatchery);
  Link(&drone);
  Link(&enemy);
  gamesnd_worker_killed(&drone, &enemy);
  assert(g_pings.size() == 1 && g_pings[0].x == 400 && g_pings[0].y == 500);
  assert(g_pings[0].colour == 0x10);
  assert(g_voices.size() == 1 && g_voices[0] == 0);  // Zerg's own recording

  // A second base in the same frame gets its own ping, but the advisor batch has one voice.
  CUnit secondBase = MakeUnit(0x83, 0, 2000, 2100);
  CUnit secondDrone = MakeUnit(0x29, 0, 2020, 2120);
  Link(&secondBase);
  Link(&secondDrone);
  gamesnd_worker_killed(&secondDrone, &enemy);
  assert(g_pings.size() == 2 && g_pings[1].x == 2000 && g_pings[1].y == 2100);
  assert(g_voices.size() == 1);

  // A second death at an already-reported base adds neither another ping nor another voice.
  gamesnd_worker_killed(&drone, &enemy);
  assert(g_pings.size() == 2 && g_voices.size() == 1);

  // Terran and Protoss select their own recordings, never IDs 121/122's attack cue.
  byte_5264C2 = 1;
  sgnWorkerKillSndTimer = 0;
  sgWorkerKillAlerts.clear();
  gamesnd_worker_killed(&secondDrone, &enemy);
  assert(g_voices.size() == 2 && g_voices[1] == 1);
  byte_5264C2 = 2;
  sgnWorkerKillSndTimer = 0;
  sgWorkerKillAlerts.clear();
  gamesnd_worker_killed(&secondDrone, &enemy);
  assert(g_voices.size() == 3 && g_voices[2] == 2);

  // A non-worker and a replay death do not produce the dedicated alert.
  secondDrone.wType = 0;
  gamesnd_worker_killed(&secondDrone, &enemy);
  assert(g_voices.size() == 3);
  secondDrone.wType = 0x29;
  gPlayback = 1;
  gamesnd_worker_killed(&secondDrone, &enemy);
  assert(g_voices.size() == 3);

  std::cout << "worker alert race selection and simultaneous ping checks passed\n";
  return 0;
}
