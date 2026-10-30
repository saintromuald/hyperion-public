// smk_test.cpp -- checks the port's Smacker player (src117/port/data/port_smacker.cpp)
// against RAD's smackw32.dll from the player's install.
//
//   smk_test.exe <install folder> [frames]
//   smk_test.exe <install folder> --sound [seconds]
//
// Every video the game plays is opened in both, from the install's archives,
// and played through the same calls: each onto a buffer of the same random
// bytes, at a random place in it. At every frame the two must agree on the frame
// number, the palette and whether the frame changed it, the whole buffer after
// the frame is painted, the rects of what changed, and each track's sound. A
// short video is played on past its end, to where it starts again. frames
// limits how many frames of each video are played (all by default).
//
// --sound plays the first seconds (3 by default) of each cinematic with its
// sound, in smackw32 and then in ours, each on a DirectSound of its own, as the
// game's player plays them: a frame whenever the wait allows. The sound paces
// the video, so the two must reach the same frame, give or take two, and drop
// the same frames. This one is heard.
// Exit code 0 means no difference was found.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

extern "C" {
BOOL __stdcall SFileOpenArchive(const char *, DWORD, DWORD, HANDLE *);
BOOL __stdcall SFileOpenFileEx(HANDLE, const char *, DWORD, HANDLE *);
BOOL __stdcall SFileLoadFile(const char *, void **, DWORD *, DWORD, void *);
BOOL __stdcall SFileUnloadFile(void *);
static DWORD s_dwError;
void __stdcall SErrSetLastError(DWORD dwErrCode)
{
  s_dwError = dwErrCode;
  SetLastError(dwErrCode);
}
DWORD __stdcall SErrGetLastError(void) { return s_dwError; }
}

// port_smacker.cpp
struct SmkFile;
struct SmkRect {
  DWORD x, y, w, h;
};
SmkFile *SmkOpen(HANDLE hFile, DWORD dwOpenFlags);
void SmkClose(SmkFile *p);
void SmkToBuffer(SmkFile *p, DWORD dwLeft, DWORD dwTop, DWORD dwPitch, void *pvBuffer);
DWORD SmkDoFrame(SmkFile *p);
void SmkNextFrame(SmkFile *p);
DWORD SmkWait(SmkFile *p);
bool SmkToBufferRect(SmkFile *p, int nMode, SmkRect *pRect);
DWORD SmkWidth(const SmkFile *p);
DWORD SmkHeight(const SmkFile *p);
DWORD SmkFrames(const SmkFile *p);
DWORD SmkFrameNum(const SmkFile *p);
bool SmkNewPalette(const SmkFile *p);
const BYTE *SmkPaletteRGB(const SmkFile *p);
const BYTE *SmkTrackData(SmkFile *p, int nTrack, DWORD *pcb);
bool SmkSoundUseDirectSound(void *pDirectSound);

// smackw32.dll's, the fields read here.
struct RadSmack {
  DWORD dwVersion, dwWidth, dwHeight, dwFrames;
  BYTE abHeader[0x58];
  DWORD dwNewPalette;
  BYTE abPalette[0x304];
  DWORD dwPalType;
  DWORD dwFrameNum;
  DWORD adwOther[2];
  DWORD dwLastRectX, dwLastRectY, dwLastRectW, dwLastRectH;
};
static RadSmack *(__stdcall *s_pfnOpen)(HANDLE, DWORD, DWORD);
static void(__stdcall *s_pfnClose)(RadSmack *);
static DWORD(__stdcall *s_pfnDoFrame)(RadSmack *);
static void(__stdcall *s_pfnNextFrame)(RadSmack *);
static void(__stdcall *s_pfnToBuffer)(RadSmack *, DWORD, DWORD, DWORD, DWORD, void *, DWORD);
static DWORD(__stdcall *s_pfnToBufferRect)(RadSmack *, DWORD);
static DWORD(__stdcall *s_pfnWait)(RadSmack *);
static DWORD(__stdcall *s_pfnGetTrackData)(RadSmack *, void *, DWORD);
static DWORD(__stdcall *s_pfnSoundUseDirectSound)(void *);

static int g_nCases, g_nDiffs;
static unsigned long long g_cbSound, g_nRects;
static void Diff(const std::string &s)
{
  if (g_nDiffs++ < 40)
    printf("DIFF %s\n", s.c_str());
}

static DWORD g_dwSeed = 0x5EED;
static DWORD Random(DWORD n)
{
  g_dwSeed = g_dwSeed * 22695477 + 1;
  return ((g_dwSeed >> 16) & 0x7FFF) % n;
}

static const char *const s_aszVideos[] = {
    "smk\\blizzard.smk", "smk\\starintr.smk", "smk\\PreTerran_1.smk", "smk\\PreTerran_2.smk", "smk\\PreTerran_3.smk",
    "smk\\PreTerran_4.smk", "smk\\Terran1.smk", "smk\\Terran2.smk", "smk\\Terran3.smk", "smk\\Terran4.smk",
    "smk\\PreZerg_2.smk", "smk\\PreZerg_3.smk", "smk\\PreZerg_4.smk", "smk\\Zerg1.smk", "smk\\Zerg2.smk",
    "smk\\Zerg3.smk", "smk\\Zerg4.smk", "smk\\PreProtoss_1.smk", "smk\\PreProtoss_2.smk", "smk\\PreProtoss_3.smk",
    "smk\\Protoss1.smk", "smk\\Protoss2.smk", "smk\\Protoss3.smk", "smk\\Protoss4.smk", "smk\\starXintr.smk",
    "smk\\xProtoss.smk", "smk\\xTerran.smk", "smk\\xZerg.smk", "glue\\campaign\\disk.smk", "glue\\campaign\\Prot.smk",
    "glue\\campaign\\ProtOn.smk", "glue\\campaign\\Terr.smk", "glue\\campaign\\TerrOn.smk", "glue\\campaign\\Zerg.smk",
    "glue\\campaign\\ZergOn.smk", "glue\\Expcampaign\\disk.smk", "glue\\Expcampaign\\XZerg.smk",
    "glue\\Expcampaign\\XZergOn.smk", "glue\\Expcampaign\\XProt.smk", "glue\\Expcampaign\\XProtOn.smk",
    "glue\\Expcampaign\\XTerr.smk", "glue\\Expcampaign\\XTerrOn.smk", "glue\\mainmenu\\Exit.smk",
    "glue\\mainmenu\\ExitOn.smk", "glue\\mainmenu\\SingleOn.smk", "glue\\mainmenu\\Single.smk",
    "glue\\mainmenu\\Multi.smk", "glue\\mainmenu\\MultiOn.smk", "glue\\mainmenu\\Editor.smk",
    "glue\\mainmenu\\EditorOn.smk", "glue\\ReadyP\\Ring.smk", "glue\\ReadyP\\Glow.smk", "glue\\ReadyT\\proloop.smk",
    "glue\\ReadyT\\proj.smk", "glue\\ReadyT\\start.smk", "glue\\ReadyZ\\start.smk", "glue\\ReadyZ\\starton.smk",
    "glue\\ReadyZ\\Drip.smk"};

// The videos: the list above and the portraits portdata.tbl names.
static std::vector<std::string> VideoNames(void)
{
  std::vector<std::string> names(s_aszVideos, s_aszVideos + sizeof s_aszVideos / sizeof *s_aszVideos);
  void *pv = NULL;
  DWORD cb = 0;
  if (SFileLoadFile("arr\\portdata.tbl", &pv, &cb, 0, NULL) && cb >= 2) {
    const BYTE *p = static_cast<const BYTE *>(pv);
    const WORD n = static_cast<WORD>(p[0] | p[1] << 8);
    for (WORD i = 0; i < n && 2u + 2u * i + 1 < cb; ++i) {
      const WORD off = static_cast<WORD>(p[2 + 2 * i] | p[3 + 2 * i] << 8);
      if (off < cb && p[off])
        for (int d = 0; d < 10; ++d)
          names.push_back(std::string("portrait\\") + reinterpret_cast<const char *>(p + off) +
                          static_cast<char>('0' + d) + ".smk");
    }
    SFileUnloadFile(pv);
  }
  return names;
}

// SMK_DUMP=<folder>: both buffers of the first picture that differs, as PPM files.
static void Dump(const std::vector<BYTE> *aBuf, DWORD w, DWORD h, const BYTE *pbPalette)
{
  static int s_nDumps = 0;
  const char *pszDump = getenv("SMK_DUMP");
  if (!pszDump || s_nDumps++)
    return;
  for (int k = 0; k < 2; ++k) {
    FILE *pf = fopen((std::string(pszDump) + (k ? "\\ours.ppm" : "\\rad.ppm")).c_str(), "wb");
    fprintf(pf, "P6 %u %u 255\n", w, h);
    for (DWORD i = 0; i < w * h; ++i)
      fwrite(pbPalette + 3 * aBuf[k][i], 1, 3, pf);
    fclose(pf);
  }
}

static void CompareVideo(const std::string &name, DWORD nMaxFrames)
{
  HANDLE ah[2] = {NULL, NULL};
  if (!SFileOpenFileEx(NULL, name.c_str(), 5, &ah[0]))
    return;
  SFileOpenFileEx(NULL, name.c_str(), 5, &ah[1]);
  // storm.dll's flags, a handle and sometimes painting a late frame, but no
  // tracks: the sound is --sound's
  const DWORD dwOpenFlags = 0x1000 | (Random(2) ? 0x400 : 0);
  RadSmack *pRad = s_pfnOpen(ah[0], dwOpenFlags, 0xFFFFFFFF);
  SmkFile *pOurs = SmkOpen(ah[1], dwOpenFlags);
  ++g_nCases;
  char sz[300];
  if (!pRad || !pOurs) {
    if (pRad || pOurs)
      Diff(name + ": opens in one only");
  } else if (pRad->dwWidth != SmkWidth(pOurs) || pRad->dwHeight != SmkHeight(pOurs) ||
             pRad->dwFrames != SmkFrames(pOurs)) {
    sprintf(sz, "%s: %ux%u, %u frames, ours %ux%u, %u", name.c_str(), pRad->dwWidth, pRad->dwHeight, pRad->dwFrames,
            SmkWidth(pOurs), SmkHeight(pOurs), SmkFrames(pOurs));
    Diff(sz);
  } else {
    const DWORD w = pRad->dwWidth, h = pRad->dwHeight;
    // the destination, somewhere in a larger buffer of noise
    const DWORD dwLeft = Random(40), dwTop = Random(20), dwPitch = dwLeft + w + Random(3) * 4;
    const DWORD dwLines = dwTop + h + 2;
    std::vector<BYTE> aBuf[2];
    aBuf[0].resize(dwPitch * dwLines);
    for (size_t i = 0; i < aBuf[0].size(); ++i)
      aBuf[0][i] = static_cast<BYTE>(Random(256));
    aBuf[1] = aBuf[0];
    s_pfnToBuffer(pRad, dwLeft, dwTop, dwPitch, dwLines, &aBuf[0][0], 0);
    SmkToBuffer(pOurs, dwLeft, dwTop, dwPitch, &aBuf[1][0]);
    // the first wait is a whole frame's time off in both
    const DWORD dwWaitRad = s_pfnWait(pRad), dwWaitOurs = SmkWait(pOurs);
    if (dwWaitRad != dwWaitOurs) {
      sprintf(sz, "%s: first wait %u, ours %u", name.c_str(), dwWaitRad, dwWaitOurs);
      Diff(sz);
    }
    // a short video plays on past its end
    DWORD nFrames = pRad->dwFrames <= 64 ? pRad->dwFrames + pRad->dwFrames / 2 + 2 : pRad->dwFrames;
    if (nFrames > nMaxFrames)
      nFrames = nMaxFrames;
    for (DWORD f = 0; f < nFrames; ++f) {
      ++g_nCases;
      const std::string at = name + " step " + std::to_string(static_cast<unsigned long long>(f));
      if (pRad->dwFrameNum != SmkFrameNum(pOurs)) {
        sprintf(sz, "%s: frame %u, ours %u", at.c_str(), pRad->dwFrameNum, SmkFrameNum(pOurs));
        Diff(sz);
        break;
      }
      const BYTE *pbPalette = SmkPaletteRGB(pOurs);
      if ((pRad->dwNewPalette != 0) != SmkNewPalette(pOurs) || memcmp(pbPalette, pRad->abPalette, 768) != 0) {
        int i = 0;
        while (i < 768 && pbPalette[i] == pRad->abPalette[i])
          ++i;
        sprintf(sz, "%s: palette new %u/%d, differs at byte %d", at.c_str(), pRad->dwNewPalette, SmkNewPalette(pOurs),
                i);
        Diff(sz);
      }
      // each track's sound for the frame
      for (int nTrack = 0; nTrack < 7; ++nTrack) {
        static BYTE s_abRad[0x100000];
        const DWORD cbRad = s_pfnGetTrackData(pRad, s_abRad, 0x2000u << nTrack);
        DWORD cbOurs = 0;
        const BYTE *pbOurs = SmkTrackData(pOurs, nTrack, &cbOurs);
        g_cbSound += cbRad;
        if (cbRad != cbOurs || (cbRad && memcmp(s_abRad, pbOurs, cbRad) != 0)) {
          DWORD i = 0;
          while (i < cbRad && i < cbOurs && s_abRad[i] == pbOurs[i])
            ++i;
          sprintf(sz, "%s track %d: %u bytes of sound, ours %u, differ at %u", at.c_str(), nTrack, cbRad, cbOurs, i);
          Diff(sz);
        }
      }
      const DWORD dwDropRad = s_pfnDoFrame(pRad), dwDropOurs = SmkDoFrame(pOurs);
      if (dwDropRad != dwDropOurs) {
        sprintf(sz, "%s: DoFrame %u, ours %u", at.c_str(), dwDropRad, dwDropOurs);
        Diff(sz);
      }
      DWORD nDiff = 0, i0 = 0;
      for (DWORD i = 0; i < aBuf[0].size(); ++i)
        if (aBuf[0][i] != aBuf[1][i] && nDiff++ == 0)
          i0 = i;
      if (nDiff) {
        sprintf(sz, "%s %ux%u at (%u,%u) pitch %u: %u bytes differ, first at (%u,%u): %02X, ours %02X", at.c_str(), w,
                h, dwLeft, dwTop, dwPitch, nDiff, i0 % dwPitch, i0 / dwPitch, aBuf[0][i0], aBuf[1][i0]);
        Diff(sz);
        Dump(aBuf, dwPitch, dwLines, pRad->abPalette);
        break;
      }
      // the rects, all of them most frames, now and then only some, the rest
      // left for the next frame to hand on first
      const DWORD nTake = Random(8) ? 0xFFFFFFFF : Random(3);
      const DWORD dwMode = Random(10) ? 0 : Random(3);
      for (DWORD n = 0; n < nTake; ++n) {
        SmkRect rc = {};
        const DWORD dwRad = s_pfnToBufferRect(pRad, dwMode);
        const bool bOurs = SmkToBufferRect(pOurs, static_cast<int>(dwMode), &rc);
        if ((dwRad != 0) != bOurs ||
            (bOurs && (rc.x != pRad->dwLastRectX || rc.y != pRad->dwLastRectY || rc.w != pRad->dwLastRectW ||
                       rc.h != pRad->dwLastRectH))) {
          sprintf(sz, "%s mode %u rect %u: %u {%u,%u,%u,%u}, ours %d {%u,%u,%u,%u}", at.c_str(), dwMode, n, dwRad,
                  pRad->dwLastRectX, pRad->dwLastRectY, pRad->dwLastRectW, pRad->dwLastRectH, bOurs, rc.x, rc.y, rc.w,
                  rc.h);
          Diff(sz);
          break;
        }
        if (!bOurs)
          break;
        ++g_nRects;
      }
      s_pfnNextFrame(pRad);
      SmkNextFrame(pOurs);
    }
  }
  if (pRad)
    s_pfnClose(pRad);
  SmkClose(pOurs);
  CloseHandle(ah[0]);
  if (ah[1])
    CloseHandle(ah[1]);
}

// --sound: a cinematic's first dwMs milliseconds with its first track, in one
// player, a frame whenever its wait allows (the first at once), all its rects
// taken. Answers the frames played and how many of them the player dropped.
static const DWORD PACE_FLAGS = 0x1000 | 0x2000;

static bool PaceRad(const std::string &name, DWORD dwMs, DWORD &nFrames, DWORD &nDropped)
{
  HANDLE h;
  if (!SFileOpenFileEx(NULL, name.c_str(), 5, &h))
    return false;
  RadSmack *p = s_pfnOpen(h, PACE_FLAGS, 0xFFFFFFFF);
  if (!p) {
    CloseHandle(h);
    return false;
  }
  std::vector<BYTE> buf(p->dwWidth * p->dwHeight);
  s_pfnToBuffer(p, 0, 0, p->dwWidth, p->dwHeight, &buf[0], 0);
  nFrames = nDropped = 0;
  const DWORD dwStart = timeGetTime();
  while (timeGetTime() - dwStart < dwMs && nFrames < p->dwFrames) {
    if (nFrames && s_pfnWait(p)) {
      Sleep(1);
      continue;
    }
    nDropped += s_pfnDoFrame(p) != 0;
    while (s_pfnToBufferRect(p, 0)) {
    }
    s_pfnNextFrame(p);
    ++nFrames;
  }
  s_pfnClose(p);
  CloseHandle(h);
  return true;
}

static bool PaceOurs(const std::string &name, DWORD dwMs, DWORD &nFrames, DWORD &nDropped)
{
  HANDLE h;
  if (!SFileOpenFileEx(NULL, name.c_str(), 5, &h))
    return false;
  SmkFile *p = SmkOpen(h, PACE_FLAGS);
  if (!p) {
    CloseHandle(h);
    return false;
  }
  std::vector<BYTE> buf(SmkWidth(p) * SmkHeight(p));
  SmkToBuffer(p, 0, 0, SmkWidth(p), &buf[0]);
  nFrames = nDropped = 0;
  const DWORD dwStart = timeGetTime();
  while (timeGetTime() - dwStart < dwMs && nFrames < SmkFrames(p)) {
    if (nFrames && SmkWait(p)) {
      Sleep(1);
      continue;
    }
    nDropped += SmkDoFrame(p) != 0;
    SmkRect rc;
    while (SmkToBufferRect(p, 0, &rc)) {
    }
    SmkNextFrame(p);
    ++nFrames;
  }
  SmkClose(p);
  CloseHandle(h);
  return true;
}

static void PaceCinematics(DWORD dwSeconds)
{
  // a window of the process's own for the DirectSounds
  WNDCLASSA wc = {};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.lpszClassName = "smk_test";
  RegisterClassA(&wc);
  CreateWindowA("smk_test", "smk_test", WS_OVERLAPPED, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
  s_pfnSoundUseDirectSound(NULL);
  SmkSoundUseDirectSound(NULL);
  timeBeginPeriod(1);
  for (size_t i = 0; i < sizeof s_aszVideos / sizeof *s_aszVideos; ++i) {
    const std::string name = s_aszVideos[i];
    if (name.compare(0, 4, "smk\\") != 0)
      continue;
    DWORD nRad = 0, nDropsRad = 0, nOurs = 0, nDropsOurs = 0;
    const bool bRad = PaceRad(name, dwSeconds * 1000, nRad, nDropsRad);
    const bool bOurs = PaceOurs(name, dwSeconds * 1000, nOurs, nDropsOurs);
    ++g_nCases;
    printf("%s: %u frames, %u dropped; ours %u, %u\n", name.c_str(), nRad, nDropsRad, nOurs, nDropsOurs);
    if (bRad != bOurs || nRad > nOurs + 2 || nOurs > nRad + 2 || nDropsRad != nDropsOurs)
      Diff(name + ": a different pace");
  }
  timeEndPeriod(1);
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: smk_test.exe <install folder> [frames]\n       smk_test.exe <install folder> --sound [seconds]\n");
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  const std::string install = argv[1];
  const bool bSound = argc > 2 && strcmp(argv[2], "--sound") == 0;
  const DWORD nMaxFrames = argc > 2 && !bSound ? static_cast<DWORD>(atoi(argv[2])) : 0xFFFFFFFF;
  const DWORD dwSeconds = bSound && argc > 3 ? static_cast<DWORD>(atoi(argv[3])) : 3;
  HMODULE hSmack = LoadLibraryA((install + "\\smackw32.dll").c_str());
  if (!hSmack) {
    printf("no smackw32.dll in %s\n", install.c_str());
    return 2;
  }
  s_pfnOpen = reinterpret_cast<RadSmack *(__stdcall *)(HANDLE, DWORD, DWORD)>(GetProcAddress(hSmack, "_SmackOpen@12"));
  s_pfnClose = reinterpret_cast<void(__stdcall *)(RadSmack *)>(GetProcAddress(hSmack, "_SmackClose@4"));
  s_pfnDoFrame = reinterpret_cast<DWORD(__stdcall *)(RadSmack *)>(GetProcAddress(hSmack, "_SmackDoFrame@4"));
  s_pfnNextFrame = reinterpret_cast<void(__stdcall *)(RadSmack *)>(GetProcAddress(hSmack, "_SmackNextFrame@4"));
  s_pfnToBuffer = reinterpret_cast<void(__stdcall *)(RadSmack *, DWORD, DWORD, DWORD, DWORD, void *, DWORD)>(
      GetProcAddress(hSmack, "_SmackToBuffer@28"));
  s_pfnToBufferRect =
      reinterpret_cast<DWORD(__stdcall *)(RadSmack *, DWORD)>(GetProcAddress(hSmack, "_SmackToBufferRect@8"));
  s_pfnWait = reinterpret_cast<DWORD(__stdcall *)(RadSmack *)>(GetProcAddress(hSmack, "_SmackWait@4"));
  s_pfnGetTrackData = reinterpret_cast<DWORD(__stdcall *)(RadSmack *, void *, DWORD)>(
      GetProcAddress(hSmack, "_SmackGetTrackData@12"));
  s_pfnSoundUseDirectSound =
      reinterpret_cast<DWORD(__stdcall *)(void *)>(GetProcAddress(hSmack, "_SmackSoundUseDirectSound@4"));
  static const char *const s_aszArchives[] = {"StarDat.mpq", "BrooDat.mpq", "Starcraft.mpq", "BroodWar.mpq"};
  static const DWORD s_adwPriority[] = {0x7D0, 0x9C4, 0x3E8, 0x3E9};
  for (int i = 0; i < 4; ++i) {
    HANDLE h;
    if (!SFileOpenArchive((install + "\\" + s_aszArchives[i]).c_str(), s_adwPriority[i], 2, &h))
      printf("cannot open %s\n", s_aszArchives[i]);
  }
  if (bSound) {
    PaceCinematics(dwSeconds);
    printf("%d cinematics, %d differences\n", g_nCases, g_nDiffs);
    return g_nDiffs ? 1 : 0;
  }
  const std::vector<std::string> names = VideoNames();
  for (size_t i = 0; i < names.size(); ++i)
    CompareVideo(names[i], nMaxFrames);
  printf("%d cases, %llu rects, %llu bytes of sound, %d differences\n", g_nCases, g_nRects, g_cbSound, g_nDiffs);
  return g_nDiffs ? 1 : 0;
}
