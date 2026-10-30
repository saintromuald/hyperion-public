// port_teamcolor.cpp -- each player slot's colour in full RGB, its ramp and its eight
// full-colour shades (docs/modules/render.md, "Full colour"). Presentation only: the game never reads any of it.
//
// 1.17 colours a frame's team pixels, palette entries 8 to 15, with a ramp of eight palette
// entries a slot (gdwTeamColorTbl_g). A slot may also have an RGB colour, from a Remastered
// map's CRGB section (port_map.cpp), --colour or the live channel's colour. Its eight shades are
// fitted in OKLab to the tunit.pcx ramp whose first entry is nearest: each shade's lightness and
// chroma the same fraction of the colour's as the ramp's are of its first entry's, its hue turned
// as the ramp's turns. A slot without one takes its ramp's palette colours. For the 8-bit drawing
// a colour from --colour or the channel also sets the slot's ramp to the nearest entries among
// those tunit.pcx's ramps use, the same in every tileset; a CRGB colour keeps port_map.cpp's.
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../h/playertables.h"
#include "../../h/net_cmds.h"   // PlayerData
#include "../platform/port_paths.h"  // PortCommandOptions
#include "port_teamcolor.h"
#include "../platform/port_settings.h"
#include "../../h/glues.h"  // GameLoadImage
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

enum { RAMP = 8, TUNIT_RAMPS = 16 };

struct TeamRgb {
  bool bSet;
  BYTE abRgb[3];
  bool bFitted;
  float afShade[RAMP][3];  // sRGB, 0 to 1, against the tileset's palette as loaded
};
static TeamRgb g_team[PLAYER_SLOTS_MAX];
// --colour, kept from the command line to each game's start
static bool g_bAsked[PLAYER_SLOTS_MAX];
static BYTE g_abAsked[PLAYER_SLOTS_MAX][3];
static bool g_bAskedRead;

static BYTE g_abTunit[TUNIT_RAMPS][RAMP];
static bool g_bTunit;

static BYTE *SlotRamp(int nSlot)
{
  return reinterpret_cast<BYTE *>(gdwTeamColorTbl_g[nSlot]);
}

static void TunitRead(void)
{
  if (g_bTunit)
    return;
  GameLoadImage((intptr_t) "game\\tunit.pcx", 0, (intptr_t)g_abTunit, sizeof g_abTunit, 0, 0, 0);
  g_bTunit = true;
}

// sRGB and OKLab (Bjorn Ottosson's, 2020).
static float Linear(float c)
{
  return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float Gamma(float c)
{
  c = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
  return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static void ToLab(const float *pRgb, float *pLab)
{
  const float r = Linear(pRgb[0]), g = Linear(pRgb[1]), b = Linear(pRgb[2]);
  const float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  const float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  const float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  pLab[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
  pLab[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
  pLab[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

static void FromLab(const float *pLab, float *pRgb)
{
  const float l = pLab[0] + 0.3963377774f * pLab[1] + 0.2158037573f * pLab[2];
  const float m = pLab[0] - 0.1055613458f * pLab[1] - 0.0638541728f * pLab[2];
  const float s = pLab[0] - 0.0894841775f * pLab[1] - 1.2914855480f * pLab[2];
  const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
  pRgb[0] = Gamma(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3);
  pRgb[1] = Gamma(-1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3);
  pRgb[2] = Gamma(-0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3);
}

// For the set-up's team shades (port_teams.cpp): sRGB from 0 to 1 and OKLab. PortLabToRgb says
// whether the colour is in sRGB's gamut, as the design's lchHex tests it, and clamps it.
void PortRgbToLab(const float *pRgb, float *pLab)
{
  ToLab(pRgb, pLab);
}

bool PortLabToRgb(const float *pLab, float *pRgb)
{
  const float l = pLab[0] + 0.3963377774f * pLab[1] + 0.2158037573f * pLab[2];
  const float m = pLab[0] - 0.1055613458f * pLab[1] - 0.0638541728f * pLab[2];
  const float s = pLab[0] - 0.0894841775f * pLab[1] - 1.2914855480f * pLab[2];
  const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
  const float lin[3] = {4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3,
                        -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3,
                        -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3};
  bool bIn = true;
  for (int i = 0; i < 3; ++i) {
    const float c = lin[i] <= 0.0031308f ? lin[i] * 12.92f : 1.055f * powf(lin[i], 1.0f / 2.4f) - 0.055f;
    bIn = bIn && c >= -0.5f / 255.0f && c <= 255.5f / 255.0f;
    pRgb[i] = Gamma(lin[i]);
  }
  return bIn;
}

static void EntryLab(int nEntry, float *pLab)
{
  const float rgb[3] = {sgGamePalette_g[4 * nEntry] / 255.0f, sgGamePalette_g[4 * nEntry + 1] / 255.0f,
                        sgGamePalette_g[4 * nEntry + 2] / 255.0f};
  ToLab(rgb, pLab);
}

static float LabDistance(const float *a, const float *b)
{
  const float d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2];
  return d0 * d0 + d1 * d1 + d2 * d2;
}

static void Fit(TeamRgb &t)
{
  TunitRead();
  const float rgb[3] = {t.abRgb[0] / 255.0f, t.abRgb[1] / 255.0f, t.abRgb[2] / 255.0f};
  float lab[3];
  ToLab(rgb, lab);
  int nRef = 0;
  float fBest = 1e9f;
  for (int c = 0; c < TUNIT_RAMPS; ++c) {
    float first[3];
    EntryLab(g_abTunit[c][0], first);
    const float d = LabDistance(first, lab);
    if (d < fBest) {
      fBest = d;
      nRef = c;
    }
  }
  float ref[RAMP][3];
  for (int k = 0; k < RAMP; ++k)
    EntryLab(g_abTunit[nRef][k], ref[k]);
  const float fL = lab[0], fC = sqrtf(lab[1] * lab[1] + lab[2] * lab[2]), fH = atan2f(lab[2], lab[1]);
  const float fL0 = ref[0][0], fC0 = sqrtf(ref[0][1] * ref[0][1] + ref[0][2] * ref[0][2]);
  const float fH0 = atan2f(ref[0][2], ref[0][1]);
  for (int k = 0; k < RAMP; ++k) {
    const float fCk = sqrtf(ref[k][1] * ref[k][1] + ref[k][2] * ref[k][2]);
    // a grey ramp has no hue to follow, and no chroma to take a fraction of
    const bool bGrey = fC0 < 0.02f;
    const float l = fL0 > 0.0f ? fL * ref[k][0] / fL0 : fL;
    const float c = bGrey ? fC : fC * fCk / fC0;
    const float h = bGrey ? fH : fH + atan2f(ref[k][2], ref[k][1]) - fH0;
    const float out[3] = {l, c * cosf(h), c * sinf(h)};
    FromLab(out, t.afShade[k]);
  }
  t.bFitted = true;
}

// The nearest palette entry to an sRGB colour among those tunit.pcx's ramps use.
static BYTE NearestEntry(const float *pRgb)
{
  float lab[3];
  ToLab(pRgb, lab);
  BYTE bBest = g_abTunit[0][0];
  float fBest = 1e9f;
  for (int c = 0; c < TUNIT_RAMPS; ++c)
    for (int k = 0; k < RAMP; ++k) {
      float e[3];
      EntryLab(g_abTunit[c][k], e);
      const float d = LabDistance(e, lab);
      if (d < fBest) {
        fBest = d;
        bBest = g_abTunit[c][k];
      }
    }
  return bBest;
}

static void SetRamp(int nSlot)
{
  TeamRgb &t = g_team[nSlot];
  Fit(t);
  BYTE *pRamp = SlotRamp(nSlot);
  for (int k = 0; k < RAMP; ++k)
    pRamp[k] = NearestEntry(t.afShade[k]);
  const float rgb[3] = {t.abRgb[0] / 255.0f, t.abRgb[1] / 255.0f, t.abRgb[2] / 255.0f};
  gbMinimapColor_g[nSlot] = NearestEntry(rgb);
}

void PortTeamColorsBegin(void)
{
  memset(g_team, 0, sizeof g_team);
}

void PortTeamColorSet(int nSlot, unsigned char r, unsigned char g, unsigned char b, int bRamp)
{
  if (nSlot < 0 || nSlot >= PLAYER_SLOTS_MAX)
    return;
  TeamRgb &t = g_team[nSlot];
  t.bSet = true;
  t.abRgb[0] = r;
  t.abRgb[1] = g;
  t.abRgb[2] = b;
  t.bFitted = false;
  if (bRamp)
    SetRamp(nSlot);
}

void PortTeamColorClear(int nSlot)
{
  if (nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX)
    g_team[nSlot].bSet = false;
}

int PortTeamColorGet(int nSlot, unsigned char *pRgb)
{
  if (nSlot < 0 || nSlot >= PLAYER_SLOTS_MAX || !g_team[nSlot].bSet)
    return 0;
  memcpy(pRgb, g_team[nSlot].abRgb, 3);
  return 1;
}

int PortTeamSlots(void)
{
  return PLAYER_SLOTS_MAX;
}

// The ramp the slot is drawn with, as sub_41D740 puts it in the remap: its own, or the one
// Settings show it in (PortTeamRampShown).
void PortTeamRamp(int nSlot, unsigned char *pRamp)
{
  memcpy(pRamp, PortTeamRampShown(nSlot), RAMP);
}

// "#rrggbb" or "rrggbb"; false when it is neither.
int PortTeamColorParse(const char *psz, unsigned char *pRgb)
{
  if (*psz == '#')
    ++psz;
  if (strlen(psz) != 6)
    return 0;
  char *pEnd;
  const unsigned long n = strtoul(psz, &pEnd, 16);
  if (*pEnd != '\0')
    return 0;
  pRgb[0] = static_cast<unsigned char>(n >> 16);
  pRgb[1] = static_cast<unsigned char>(n >> 8);
  pRgb[2] = static_cast<unsigned char>(n);
  return 1;
}

// --colour <slot>:<#rrggbb>[,<slot>:<#rrggbb>...], read once.
static void AskedRead(void)
{
  if (g_bAskedRead)
    return;
  g_bAskedRead = true;
  char sz[260];
  lstrcpynA(sz, PortCommandOptions()->szColour, sizeof sz);
  for (char *p = strtok(sz, ","); p != NULL; p = strtok(NULL, ",")) {
    char *pColon = strchr(p, ':');
    if (pColon == NULL)
      continue;
    *pColon = '\0';
    const int nSlot = atoi(p);
    unsigned char rgb[3];
    if (nSlot >= 0 && nSlot < PLAYER_SLOTS_MAX && PortTeamColorParse(pColon + 1, rgb)) {
      g_bAsked[nSlot] = true;
      memcpy(g_abAsked[nSlot], rgb, 3);
    }
  }
}

// At a game's start, after every other colour is set: the command line's.
void PortTeamColorsApply(void)
{
  AskedRead();
  for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
    if (g_bAsked[s])
      PortTeamColorSet(s, g_abAsked[s][0], g_abAsked[s][1], g_abAsked[s][2], 1);
}

// ---------------------------------------------------------------------------
// Settings' "Allies blue, enemies red" (allyColours) and "Colour-blind player colours"
// (colourBlind: 0 off, 1 red-green, 2 blue-yellow) change only the colours a slot is shown in,
// through PortTeamRampShown, PortTeamMinimapShown, PortTeamRamp and PortTeamShades; the tables
// the game, its replays and saves keep stay, so a replay or save shows each player's own.
// allyColours, in a game the player plays: the player in 1.17's blue, allies teal, the rest red;
// with colourBlind the three come from its palette, and colourBlind alone gives each slot in
// turn the palette's next colour, fitted as an RGB slot's is. Neutral and rescuable keep theirs.

namespace {

struct Shown {
  bool bOn;   // drawn in other colours than its own
  bool bRgb;  // a palette colour, fitted as an RGB slot's is; else a ramp of tunit.pcx
  TeamRgb rgb;
  BYTE abRamp[RAMP];
  BYTE bMinimap;
};
Shown g_shown[PLAYER_SLOTS_MAX];
bool g_bShownRead;
bool g_bAllyColours;
int g_nColourBlind;
BYTE g_abShownKey[8 + 2 * PLAYER_SLOTS_MAX];
bool g_bShownKeyed;
bool g_bShownAny;  // a slot drawn in other colours than its own
BYTE g_abTminimap[12];  // tminimap.pcx, as 1.17 reads it
bool g_bTminimap;

// Okabe and Ito's colours, which stay apart for red-green colour blindness, with white in
// place of their black, which a dark map would hide; the four most apart first, for the
// games of two to four players: blue, orange, yellow, reddish purple.
const BYTE kRedGreen[8][3] = {{0x00, 0x72, 0xB2}, {0xE6, 0x9F, 0x00}, {0xF0, 0xE4, 0x42}, {0xCC, 0x79, 0xA7},
                              {0x00, 0x9E, 0x73}, {0xD5, 0x5E, 0x00}, {0x56, 0xB4, 0xE9}, {0xF0, 0xF0, 0xF0}};
// For blue-yellow colour blindness: colours apart along red and cyan, and in lightness.
const BYTE kBlueYellow[8][3] = {{0xE4, 0x00, 0x2B}, {0x00, 0xC8, 0xD7}, {0xF2, 0xF2, 0xF2}, {0xFF, 0x6E, 0xB4},
                                {0x80, 0x00, 0x20}, {0x8C, 0x8C, 0x8C}, {0x00, 0x6E, 0x78}, {0xFF, 0xC0, 0xCB}};
// You, allies and enemies in each palette: 1.17's blue, teal and red ramps (tunit.pcx 1,
// 2, 0), or the palette's colours for them.
const int kAllyRamps[3] = {1, 2, 0};
const BYTE kAllyRedGreen[3][3] = {{0x00, 0x72, 0xB2}, {0xF0, 0xE4, 0x42}, {0xD5, 0x5E, 0x00}};
const BYTE kAllyBlueYellow[3][3] = {{0x00, 0xC8, 0xD7}, {0xF2, 0xF2, 0xF2}, {0xE4, 0x00, 0x2B}};

void ShownRead(void)
{
  if (g_bShownRead)
    return;
  g_bShownRead = true;
  double d = 0.0;
  g_bAllyColours = PortSettingNumber("hyperion", "allyColours", &d) && d != 0.0;
  d = 0.0;
  PortSettingNumber("hyperion", "colourBlind", &d);
  g_nColourBlind = d >= 1.0 && d <= 2.0 ? static_cast<int>(d) : 0;
}

bool IsPlayer(int nSlot)
{
  const BYTE nType = gPlayerData[nSlot].nType;
  return nType == 1 || nType == 2;  // a computer or a person, in a game
}

void ShowRamp(Shown &s, int nRamp)
{
  TunitRead();
  if (!g_bTminimap) {
    GameLoadImage((intptr_t) "game\\tminimap.pcx", 0, (intptr_t)g_abTminimap, sizeof g_abTminimap, 0, 0, 0);
    g_bTminimap = true;
  }
  s.bOn = true;
  s.bRgb = false;
  memcpy(s.abRamp, g_abTunit[nRamp], RAMP);
  s.bMinimap = g_abTminimap[nRamp];
}

void ShowRgb(Shown &s, const BYTE *pRgb)
{
  s.bOn = true;
  s.bRgb = true;
  s.rgb.bSet = true;
  memcpy(s.rgb.abRgb, pRgb, 3);
  Fit(s.rgb);
  for (int k = 0; k < RAMP; ++k)
    s.abRamp[k] = NearestEntry(s.rgb.afShade[k]);
  const float rgb[3] = {pRgb[0] / 255.0f, pRgb[1] / 255.0f, pRgb[2] / 255.0f};
  s.bMinimap = NearestEntry(rgb);
}

// Worked out again when what it follows changes: the settings, the game, the player's
// slot, whom the player is allied to, and which slots are players.
void ShownRefresh(void)
{
  ShownRead();
  // sub_41D740 asks for every image it draws: with both off, there is nothing to work out
  if (!g_bAllyColours && g_nColourBlind == 0) {
    if (g_bShownAny)
      for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
        g_shown[s].bOn = false;
    g_bShownAny = false;
    g_bShownKeyed = false;
    return;
  }
  BYTE abKey[sizeof g_abShownKey];
  memset(abKey, 0, sizeof abKey);
  const bool bGame = gwGameMode == 3;
  const int nSlots = bGame ? (PlayerSlots() < PLAYER_SLOTS_MAX ? PlayerSlots() : PLAYER_SLOTS_MAX) : 0;
  const int nLocal = static_cast<int>(g_LocalNationID);
  abKey[0] = g_bAllyColours;
  abKey[1] = static_cast<BYTE>(g_nColourBlind);
  abKey[2] = bGame;
  abKey[3] = gPlayback != 0;
  abKey[4] = static_cast<BYTE>(nLocal);
  abKey[5] = static_cast<BYTE>(nSlots);
  for (int s = 0; s < nSlots; ++s) {
    abKey[8 + s] = gPlayerData[s].nType;
    abKey[8 + PLAYER_SLOTS_MAX + s] = nLocal >= 0 && nLocal < nSlots ? sgbAlliances_g[nLocal][s] : 0;
  }
  if (g_bShownKeyed && memcmp(abKey, g_abShownKey, sizeof abKey) == 0)
    return;
  memcpy(g_abShownKey, abKey, sizeof abKey);
  g_bShownKeyed = true;
  for (int s = 0; s < PLAYER_SLOTS_MAX; ++s)
    g_shown[s].bOn = false;
  g_bShownAny = false;
  if (!bGame)
    return;
  const bool bAllies = g_bAllyColours && gPlayback == 0 && nLocal >= 0 && nLocal < nSlots;
  int nNext = 0;
  for (int s = 0; s < nSlots; ++s) {
    if (!IsPlayer(s) && !(bAllies && s == nLocal))
      continue;
    if (bAllies) {
      const int nRole = s == nLocal ? 0 : sgbAlliances_g[nLocal][s] != 0 ? 1 : 2;
      if (g_nColourBlind == 1)
        ShowRgb(g_shown[s], kAllyRedGreen[nRole]);
      else if (g_nColourBlind == 2)
        ShowRgb(g_shown[s], kAllyBlueYellow[nRole]);
      else
        ShowRamp(g_shown[s], kAllyRamps[nRole]);
    } else if (g_nColourBlind != 0) {
      ShowRgb(g_shown[s], (g_nColourBlind == 1 ? kRedGreen : kBlueYellow)[nNext++ % 8]);
    }
    g_bShownAny = g_bShownAny || g_shown[s].bOn;
  }
}

}  // namespace

void PortTeamShownReread(void)
{
  g_bShownRead = false;
  g_bShownKeyed = false;
}

const unsigned char *PortTeamRampShown(int nSlot)
{
  if (nSlot < 0 || nSlot >= PLAYER_SLOTS_MAX)
    return SlotRamp(0);
  ShownRefresh();
  return g_shown[nSlot].bOn ? g_shown[nSlot].abRamp : SlotRamp(nSlot);
}

unsigned char PortTeamMinimapShown(int nSlot)
{
  if (nSlot < 0 || nSlot >= PLAYER_SLOTS_MAX)
    return gbMinimapColor_g[0];
  ShownRefresh();
  return g_shown[nSlot].bOn ? g_shown[nSlot].bMinimap : gbMinimapColor_g[nSlot];
}

// The slot's eight shades as the full-colour drawing takes them, sRGB 0 to 1, against
// pLive, the palette as shown now (4 bytes an entry): the ramp's own colours, or the
// fitted shades, as dim as the palette's fade makes the ramp's entries.
void PortTeamShades(int nSlot, const unsigned char *pLive, float (*pafOut)[3])
{
  if (nSlot < 0 || nSlot >= PLAYER_SLOTS_MAX)
    nSlot = 0;
  // the colours Settings show the slot in, when they do
  const BYTE *pRamp = PortTeamRampShown(nSlot);
  TeamRgb &t = g_shown[nSlot].bOn ? g_shown[nSlot].rgb : g_team[nSlot];
  if (g_shown[nSlot].bOn && !g_shown[nSlot].bRgb)
    t.bSet = false;
  if (t.bSet && !t.bFitted)
    Fit(t);
  for (int k = 0; k < RAMP; ++k) {
    const unsigned char *pEntry = pLive + 4 * pRamp[k];
    if (!t.bSet) {
      for (int i = 0; i < 3; ++i)
        pafOut[k][i] = pEntry[i] / 255.0f;
      continue;
    }
    const BYTE *pGame = sgGamePalette_g + 4 * pRamp[k];
    const float fLive = 0.2126f * pEntry[0] + 0.7152f * pEntry[1] + 0.0722f * pEntry[2];
    const float fGame = 0.2126f * pGame[0] + 0.7152f * pGame[1] + 0.0722f * pGame[2];
    float f = fGame > 0.5f ? fLive / fGame : 1.0f;
    f = f > 2.0f ? 2.0f : f;
    for (int i = 0; i < 3; ++i) {
      const float c = t.afShade[k][i] * f;
      pafOut[k][i] = c > 1.0f ? 1.0f : c;
    }
  }
}
