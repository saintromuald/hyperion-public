// music.cpp -- the background music player. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/music.h"
#include "../h/Storm.h"   // SetTimer, KillTimer and <windows.h>, here and not in music.h
#include "../h/snd.h"        // music_enabled, start_song, set_music_volume, stop_song
#include "../h/gluRdy.h"     // play_music, stop_music
#include "../h/event_Win.h"  // music_update
#include "../h/net_cmds.h"   // error_get_str, wrap_map_msg
#include "../gen/globals117.h"
#include "../port/platform/port_winmsg.h"  // Hyperion: SetTimer and KillTimer, the window's

// The Storm entry points this TU reaches, extern "C" so that they link to
// Storm's own names; each trailing number is the ordinal. SFileDdaGetPos is in
// snd.h.
// Hyperion: a handle is a pointer, as Storm's own, `int` in 1.17; the x64
// build would cut it to 32 bits.
extern "C" {
int    __stdcall SFileOpenFile(const char *pszFileName, void **phFile);      // 267
int    __stdcall SFileCloseFile(void *hFile);                                 // 253
int    __stdcall SFileGetFileName(void *hFile, char *pszBuf, DWORD cchBuf);   // 276
int    __stdcall SFileGetFileArchive(void *hFile, void **phArchive);          // 264
int    __stdcall SFileGetArchiveName(void *hArchive, char *pszBuf,
                                     DWORD cchBuf);                           // 275
int    __stdcall SFileDdaEnd(void *hFile);                                    // 257
int    __stdcall SFileDdaSetVolume(void *hFile, LONG lVolume, LONG lPan);     // 261
int    __stdcall SFileDdaBeginEx(void *hFile, DWORD dwFlags, DWORD dwMask,
                                 DWORD dwOffset, LONG lVolume, LONG lPan,
                                 int nReserved);                              // 255
char * __stdcall SStrChrF(const char *pszStr, char chFind);                   // 571
}

// The globals this TU reaches, dual-spelled, one #ifdef per name. gwGameMode
// (0x6662B8) comes from ../gen/globals117.h.

// sgSndVolTbl_g, 0x4EFC00 in .rdata, spelled exactly as snd.cpp spells it;
// play_music reads [1] and stop_music [0].
#ifndef OC_SG_SND_VOL_TBL_G
#define OC_SG_SND_VOL_TBL_G
#ifdef OC_DLL_MODE
#define sgSndVolTbl_g (reinterpret_cast<const LONG *>(OC_VA(0x4EFC00u)))
#else
extern const LONG sgSndVolTbl_g[100];
#endif
#endif

// 0x508C58 sgSongs -- the 25 tracks; music_PlayRadioFreeZerg's, index 24, is
// the last.
#ifndef OC_SG_SONGS
#define OC_SG_SONGS
#ifdef OC_DLL_MODE
#define sgSongs (*reinterpret_cast<song_t (*)[25]>(OC_VA(0x508C58u)))
#else
extern song_t sgSongs[25];
#endif
#endif

// 0x66FD28 ghWnd, the window KillTimer and SetTimer are given.
#ifndef OC_GH_WND
#define OC_GH_WND
#ifdef OC_DLL_MODE
#define ghWnd (*reinterpret_cast<HWND *>(OC_VA(0x66FD28u)))
#else
extern HWND ghWnd;
#endif
#endif

// 0x67257C sgSFileSong, the open Storm file.
#ifndef OC_SG_SFILE_SONG
#define OC_SG_SFILE_SONG
#ifdef OC_DLL_MODE
#define sgSFileSong (*reinterpret_cast<void **>(OC_VA(0x67257Cu)))
#else
extern void *sgSFileSong;  // Hyperion: a pointer, `int` in 1.17
#endif
#endif

// 0x672580 sgdwSongId_g, the playing track's song-table index.
#ifndef OC_SGDW_SONG_ID_G
#define OC_SGDW_SONG_ID_G
#ifdef OC_DLL_MODE
#define sgdwSongId_g (*reinterpret_cast<DWORD *>(OC_VA(0x672580u)))
#else
extern DWORD sgdwSongId_g;
#endif
#endif

// 0x672584 sgnMusicVolume -- signed: every user clamps it against -10000, and
// set_music_volume takes a long.
#ifndef OC_SGN_MUSIC_VOLUME
#define OC_SGN_MUSIC_VOLUME
#ifdef OC_DLL_MODE
#define sgnMusicVolume (*reinterpret_cast<LONG *>(OC_VA(0x672584u)))
#else
extern LONG sgnMusicVolume;
#endif
#endif

// 0x672588 sgnMusicVolumeStored_g -- the volume the two fade loops walk, signed
// for the same reason.
#ifndef OC_SGN_MUSIC_VOLUME_STORED_G
#define OC_SGN_MUSIC_VOLUME_STORED_G
#ifdef OC_DLL_MODE
#define sgnMusicVolumeStored_g (*reinterpret_cast<LONG *>(OC_VA(0x672588u)))
#else
extern LONG sgnMusicVolumeStored_g;
#endif
#endif

// 0x67258C gbMusicOn, a BYTE.
#ifndef OC_GB_MUSIC_ON
#define OC_GB_MUSIC_ON
#ifdef OC_DLL_MODE
#define gbMusicOn (*reinterpret_cast<BYTE *>(OC_VA(0x67258Cu)))
#else
extern BYTE gbMusicOn;
#endif
#endif

// 0x67258D byte_67258D -- a BYTE, nonzero while sub_46F120 has the music
// ducked; the other volume setters then subtract 750.
#ifndef OC_BYTE_67258D
#define OC_BYTE_67258D
#ifdef OC_DLL_MODE
#define byte_67258D (*reinterpret_cast<BYTE *>(OC_VA(0x67258Du)))
#else
extern BYTE byte_67258D;
#endif
#endif

// 0x6D313C is gOptions[3], not a global of its own, spelled as gamesnd.cpp
// spells elements 4 and 6. Zero means the music device is off, which
// suppresses both the ramp-up timer and SFileDdaBeginEx.
#ifndef gOptions_music_on
#ifdef OC_DLL_MODE
#define gOptions_music_on (*reinterpret_cast<int *>(OC_VA(0x6D313Cu)))
#else
extern int gOptions_music_on;
#endif
#endif

// The bodies, in oracle VA order.

// Is the song file being streamed off a network drive? Everything after the
// first backslash is cut so GetDriveTypeA sees a root.
//
// The SFileGetFileName failure returns its own result rather than a literal 0,
// as in 1.17.
int IsRemoteFile(void *hFile)
{
  void *hArchive;
  char szPath[0x104];

  if (!SFileGetFileArchive(hFile, &hArchive))
    return 0;
  if (hArchive != 0) {
    if (!SFileGetArchiveName(hArchive, szPath, 0x104))
      return 0;
  } else {
    int nOk = SFileGetFileName(hFile, szPath, 0x104);
    if (nOk == 0)
      return nOk;
  }
  char *pSlash = SStrChrF(szPath, '\\');
  if (pSlash != 0)
    pSlash[1] = '\0';
  return GetDriveTypeA(szPath) == DRIVE_REMOTE;
}

// Called each frame: a mode-2 track that has run out starts its follow-on.
void music_update()
{
  DWORD dwPos;
  DWORD dwLen;

  if (gbMusicOn != 0 && sgSongs[sgdwSongId_g].bMode == 2) {
    SFileDdaGetPos(sgSFileSong, &dwPos, &dwLen);
    if (dwPos >= dwLen)
      play_music_track(sgSongs[sgdwSongId_g].bNext);
  }
}

// Toggle between the Radio Free Zerg easter egg and the ordinary zerg track,
// announcing the switch on the message line.
//
// The string and the track are picked per arm and used after the join, as in
// 1.17.
void music_PlayRadioFreeZerg()
{
  int nTrack;
  const char *psz;

  if (sgdwSongId_g != 24) {
    psz = error_get_str(0x41);
    nTrack = 24;
  } else {
    psz = error_get_str(0x42);
    nTrack = 1;
  }
  wrap_map_msg((void *)psz, -1, 0);
  play_music_track(nTrack);
}

// Start a track. A track that asks to fade in is started silent and handed to
// the ramp-up timer; anything already playing with the music on is left alone.
void play_music(int nTrack)
{
  if (sgSFileSong != 0 && gbMusicOn != 0)
    return;
  if (sgSongs[nTrack].bFadeIn == 0) {
    play_music_track(nTrack);
    return;
  }

  LONG nSaved = sgnMusicVolume;
  sgnMusicVolume = -10000;
  play_music_track(nTrack);
  sgnMusicVolume = nSaved;
  if (gOptions_music_on != 0) {
    sgnMusicVolumeStored_g = sgSndVolTbl_g[1];
    WinMsgKillTimer(ghWnd, 3);
    WinMsgSetTimer(ghWnd, 3, 20, reinterpret_cast<WinMsgTimerProc>(music_rampup_timerproc));
  }
}

// One step of the fade-in: raise the stored volume to what the setting asks
// for, then stop the timer. The target is computed twice because the last step
// overshoots.
//
// Music that is already off kills the timer: that edge falls into the KillTimer
// call rather than skipping it, and only the done flag inside the block skips.
void __stdcall music_rampup_timerproc(HWND hWnd, UINT uMsg, UINT_PTR idEvent,
                                     DWORD dwTime)
{
  int bDone = 0;

  if (gbMusicOn != 0) {
    LONG nVolume = sgnMusicVolume;
    LONG nStep = sgnMusicVolumeStored_g + 400;
    BYTE bDucked = byte_67258D;
    LONG nTarget;

    sgnMusicVolumeStored_g = nStep;
    if (bDucked == 0) {
      nTarget = nVolume;
    } else {
      LONG nDucked = nVolume - 750;
      nTarget = nDucked < -10000 ? -10000 : nDucked;
    }
    if (nStep >= nTarget) {
      if (bDucked == 0) {
        nStep = nVolume;
      } else {
        LONG nDucked = nVolume - 750;
        nStep = nDucked < -10000 ? -10000 : nDucked;
      }
      sgnMusicVolumeStored_g = nStep;
      bDone = 1;
    }
    SFileDdaSetVolume(sgSFileSong, nStep, 0);
    if (bDone == 0)
      return;
  }
  WinMsgKillTimer(ghWnd, idEvent);
}

// Stop the music, fading it out first if it is playing. The fade busy-waits on
// GetTickCount in 20 ms steps and blocks the caller until it ends.
// The wait is a `continue` in the fade loop itself, as in 1.17.
void stop_music()
{
  if (sgSFileSong != 0) {
    if (gbMusicOn != 0) {
      LONG nVolume = sgnMusicVolume;
      gbMusicOn = 0;
      if (byte_67258D != 0) {
        LONG nDucked = nVolume - 750;
        nVolume = nDucked < -10000 ? -10000 : nDucked;
      }
      sgnMusicVolumeStored_g = nVolume;

      DWORD dwDeadline = GetTickCount();
      LONG nStep = 5;
      while (1) {
        DWORD dwNow = GetTickCount();
        if ((LONG)(dwNow - dwDeadline) < 0)
          continue;
        dwDeadline = dwNow + 20;
        nVolume = sgnMusicVolumeStored_g - nStep;
        sgnMusicVolumeStored_g = nVolume;
        if (nVolume < sgSndVolTbl_g[0])
          break;
        nStep += 5;
        SFileDdaSetVolume(sgSFileSong, nVolume, 0);
      }
      sgnMusicVolumeStored_g = -10000;
      SFileDdaSetVolume(sgSFileSong, -10000, 0);
    }
    if (sgSFileSong != 0) {
      SFileDdaEnd(sgSFileSong);
      SFileCloseFile(sgSFileSong);
      sgSFileSong = 0;
    }
    gbMusicOn = 0;
  }
}

// Hyperion: stop_music's fade, the same steps 20 ms apart, on the window's timer as the
// fade-in's (music_rampup_timerproc), then nTrack, which fades in as play_music starts it.
// stop_music blocks for its fade, about a second, which held Hyperion's screen still after the
// score screen's OK (the user, 2026-10-26); this fades while the screen draws. stop_music or
// another track meanwhile ends it, and a new track's fade-in takes the timer.
static void *s_pMusicFading;
static int s_nMusicNext;
static LONG s_nMusicFadeStep;

static void __stdcall music_rampdown_timerproc(HWND hWnd, UINT, UINT_PTR idEvent, DWORD)
{
  if (sgSFileSong == 0 || sgSFileSong != s_pMusicFading || gbMusicOn == 0) {
    WinMsgKillTimer(hWnd, idEvent);
    return;
  }
  const LONG nVolume = sgnMusicVolumeStored_g - s_nMusicFadeStep;
  sgnMusicVolumeStored_g = nVolume;
  s_nMusicFadeStep += 5;
  if (nVolume >= sgSndVolTbl_g[0]) {
    SFileDdaSetVolume(sgSFileSong, nVolume, 0);
    return;
  }
  WinMsgKillTimer(hWnd, idEvent);
  sgnMusicVolumeStored_g = -10000;
  stop_song();
  play_music(s_nMusicNext);
}

void PortMusicChange(int nTrack)
{
  if (sgSFileSong == 0 || gbMusicOn == 0) {
    stop_music();
    play_music(nTrack);
    return;
  }
  LONG nVolume = sgnMusicVolume;
  if (byte_67258D != 0)
    nVolume = nVolume - 750 < -10000 ? -10000 : nVolume - 750;
  sgnMusicVolumeStored_g = nVolume;
  s_pMusicFading = sgSFileSong;
  s_nMusicNext = nTrack;
  s_nMusicFadeStep = 5;
  WinMsgKillTimer(ghWnd, 3);
  WinMsgSetTimer(ghWnd, 3, 20, reinterpret_cast<WinMsgTimerProc>(music_rampdown_timerproc));
}

int PortMusicPlaying(void)
{
  return sgSFileSong != 0 && gbMusicOn != 0 ? static_cast<int>(sgdwSongId_g) : -1;
}

// Is a track playing? 1.17 returns AL alone, so this returns a BYTE.
BYTE music_enabled()
{
  return gbMusicOn;
}

// Duck the music one step and remember it, so the other volume setters keep it
// ducked until music_volume_default lifts it.
void sub_46F120()
{
  if (byte_67258D == 0) {
    void *hFile = sgSFileSong;
    byte_67258D = 1;
    if (hFile != 0) {
      LONG nVolume = sgnMusicVolume - 750;
      if (nVolume < -10000)
        nVolume = -10000;
      SFileDdaSetVolume(hFile, nVolume, 0);
    }
  }
}

// Restart whatever track the song id names.
void start_song()
{
  play_music(sgdwSongId_g);
}

// Publish a new music volume and apply it to the stream, keeping the duck.
// The clamp spells the subtraction twice, as a max macro would, and as 1.17
// has it.
void set_music_volume(LONG nVolume)
{
  void *hFile = sgSFileSong;
  sgnMusicVolume = nVolume;
  if (hFile != 0) {
    if (byte_67258D != 0)
      nVolume = nVolume - 750 < -10000 ? -10000 : nVolume - 750;
    SFileDdaSetVolume(hFile, nVolume, 0);
  }
}

// Switch tracks: close whatever is open, open the new one and start the stream.
// A track served off a network drive is refused rather than streamed, with a
// message only while a game is running.
void play_music_track(int nTrack)
{
  void *hFile = sgSFileSong;

  if (hFile == 0 || nTrack != (int)sgdwSongId_g) {
    sgdwSongId_g = nTrack;
    if (hFile != 0) {
      SFileDdaEnd(hFile);
      SFileCloseFile(sgSFileSong);
      sgSFileSong = 0;
      gbMusicOn = 0;
    }
    if (nTrack == 0)
      return;
    SFileOpenFile(sgSongs[nTrack].pszFile, &sgSFileSong);
    hFile = sgSFileSong;
  }
  if (hFile == 0)
    return;
  if (IsRemoteFile(hFile)) {
    if (gwGameMode == 3)
      wrap_map_msg((void *)error_get_str(0x8C), 8, 0);
    return;
  }

  byte_67258D = 0;
  if (gOptions_music_on == 0)
    return;
  if (SFileDdaBeginEx(sgSFileSong, 0x40000,
                      sgSongs[nTrack].bMode == 1 ? 0x40000 : 0, 0,
                      sgnMusicVolume, 0, 0) == 0) {
    stop_song();
    return;
  }
  gbMusicOn = 1;
}

// Close the stream without fading.
void stop_song()
{
  if (sgSFileSong != 0) {
    SFileDdaEnd(sgSFileSong);
    SFileCloseFile(sgSFileSong);
    sgSFileSong = 0;
  }
  gbMusicOn = 0;
}

// Lift the duck sub_46F120 applied.
void music_volume_default()
{
  if (byte_67258D != 0) {
    void *hFile = sgSFileSong;
    byte_67258D = 0;
    if (hFile != 0)
      SFileDdaSetVolume(hFile, sgnMusicVolume, 0);
  }
}
