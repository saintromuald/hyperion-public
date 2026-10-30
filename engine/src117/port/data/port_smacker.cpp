// port_smacker.cpp -- a Smacker decoder of the port's own, in place of RAD's smackw32.dll,
// which storm.dll's video player drove (docs/modules/data.md, "Video", has the format).
// Checked frame by frame against smackw32.dll (tools/porttest/smk_test.cpp).
//
// A Smacker file is a header, each frame's size and kind, four Huffman trees, then the
// frames. Playing follows smackw32's calls, the ones storm.dll made, down to the rects of what
// a frame changed: a frame is painted straight onto the caller's buffer, which keeps what no
// block changed, and the rects come from smackw32's map of the changed blocks, walked its way.
// The sound plays, and paces the video, as smackw32's DirectSound back-end does.
//
// SmkOpenVideo plays a video in full colour (port_video.h) through the same calls: each frame
// decoded whole, put in 256 colours and painted whole, its sound decoded at the start.
#include <windows.h>
#include "port_dsound.h"
#include <stdlib.h>
#include <string.h>
#include "port_smacker.h"
#include "port_video.h"

// ---------------------------------------------------------------------------
// Bits, least significant first. Past the end they read as 0.

struct SmkBits {
  const BYTE *p;
  const BYTE *pEnd;
  DWORD nBit;  // of *p
};

static void SmkBitsInit(SmkBits &b, const BYTE *p, DWORD cb)
{
  b.p = p;
  b.pEnd = p + cb;
  b.nBit = 0;
}

static DWORD SmkBit(SmkBits &b)
{
  if (b.p >= b.pEnd)
    return 0;
  const DWORD n = (*b.p >> b.nBit) & 1;
  if (++b.nBit == 8) {
    b.nBit = 0;
    ++b.p;
  }
  return n;
}

static DWORD SmkBitsN(SmkBits &b, int n)
{
  DWORD v = 0;
  for (int i = 0; i < n; ++i)
    v |= SmkBit(b) << i;
  return v;
}

// ---------------------------------------------------------------------------
// Trees. A node is a leaf when its first child is -1.

struct SmkNode {
  int aChild[2];
  int nValue;
};

struct SmkTree {
  SmkNode *pNodes;
  int nNodes, nMax;
  int aMarker[3];  // the leaves that hold the last three values, or -1
  int aMarkerValue[3];
};

static int SmkTreeNode(SmkTree &t)
{
  if (t.nNodes == t.nMax) {
    t.nMax = t.nMax ? t.nMax * 2 : 64;
    t.pNodes = static_cast<SmkNode *>(realloc(t.pNodes, t.nMax * sizeof(SmkNode)));
  }
  SmkNode &n = t.pNodes[t.nNodes];
  n.aChild[0] = n.aChild[1] = -1;
  n.nValue = 0;
  return t.nNodes++;
}

static void SmkTreeFree(SmkTree &t)
{
  free(t.pNodes);
  memset(&t, 0, sizeof t);
}

static DWORD SmkTreeDecode(const SmkTree &t, SmkBits &b)
{
  if (!t.nNodes)
    return 0;
  int n = 0;
  while (t.pNodes[n].aChild[0] >= 0)
    n = t.pNodes[n].aChild[SmkBit(b)];
  return static_cast<DWORD>(t.pNodes[n].nValue);
}

// A packed 8-bit tree: a bit per node, 1 for a node, whose '0' side comes first,
// or 0 for a leaf and its eight bits.
static int SmkReadNode8(SmkTree &t, SmkBits &b, int nDepth)
{
  const int n = SmkTreeNode(t);
  if (SmkBit(b) && nDepth < 32) {
    const int n0 = SmkReadNode8(t, b, nDepth + 1);
    const int n1 = SmkReadNode8(t, b, nDepth + 1);
    t.pNodes[n].aChild[0] = n0;
    t.pNodes[n].aChild[1] = n1;
  } else {
    t.pNodes[n].nValue = static_cast<int>(SmkBitsN(b, 8));
  }
  return n;
}

static void SmkReadTree8(SmkTree &t, SmkBits &b)
{
  if (SmkBit(b)) {
    SmkReadNode8(t, b, 0);
    SmkBit(b);
  }
}

// A 16-bit tree's leaves: the low byte through one tree, the high through the
// other.
static int SmkReadNode16(SmkTree &t, SmkBits &b, const SmkTree &lo, const SmkTree &hi, const int *aEscape,
                         int nDepth)
{
  const int n = SmkTreeNode(t);
  if (SmkBit(b) && nDepth < 32) {
    const int n0 = SmkReadNode16(t, b, lo, hi, aEscape, nDepth + 1);
    const int n1 = SmkReadNode16(t, b, lo, hi, aEscape, nDepth + 1);
    t.pNodes[n].aChild[0] = n0;
    t.pNodes[n].aChild[1] = n1;
  } else {
    const DWORD dwLow = SmkTreeDecode(lo, b);
    const DWORD v = dwLow | SmkTreeDecode(hi, b) << 8;
    t.pNodes[n].nValue = static_cast<int>(v);
    for (int i = 0; i < 3; ++i) {
      if (static_cast<int>(v) == aEscape[i] && t.aMarker[i] < 0) {
        t.aMarker[i] = n;
        t.pNodes[n].nValue = 0;
        break;
      }
    }
  }
  return n;
}

static void SmkReadTree16(SmkTree &t, SmkBits &b)
{
  t.aMarker[0] = t.aMarker[1] = t.aMarker[2] = -1;
  if (!SmkBit(b)) {
    // no tree: every value is 0
    SmkTreeNode(t);
    return;
  }
  SmkTree lo = {}, hi = {};
  SmkReadTree8(lo, b);
  SmkReadTree8(hi, b);
  int aEscape[3];
  for (int i = 0; i < 3; ++i)
    aEscape[i] = static_cast<int>(SmkBitsN(b, 16));
  SmkReadNode16(t, b, lo, hi, aEscape, 0);
  SmkBit(b);
  SmkTreeFree(lo);
  SmkTreeFree(hi);
  for (int i = 0; i < 3; ++i) {
    if (t.aMarker[i] < 0)
      t.aMarker[i] = SmkTreeNode(t);  // a leaf no code reaches, to keep the bookkeeping plain
  }
}

// Each frame starts the last three values again at 0.
static void SmkTreeResetMarkers(SmkTree &t)
{
  if (t.aMarker[0] < 0)
    return;
  for (int i = 0; i < 3; ++i)
    t.pNodes[t.aMarker[i]].nValue = 0;
}

static DWORD SmkTreeDecode16(SmkTree &t, SmkBits &b)
{
  const DWORD v = SmkTreeDecode(t, b);
  if (t.aMarker[0] >= 0 && static_cast<int>(v) != t.pNodes[t.aMarker[0]].nValue) {
    t.pNodes[t.aMarker[2]].nValue = t.pNodes[t.aMarker[1]].nValue;
    t.pNodes[t.aMarker[1]].nValue = t.pNodes[t.aMarker[0]].nValue;
    t.pNodes[t.aMarker[0]].nValue = static_cast<int>(v);
  }
  return v;
}

// ---------------------------------------------------------------------------
// Sound on DirectSound, as smackw32's back-end plays it. Each track has a ring of decoded
// sound, filled a frame at a time, and a looping DirectSound buffer half a second long that
// a service fills from the ring ahead of the play cursor, off a 75 ms timer and from each
// call that lets time pass, at most once in 10 ms. A dry ring writes a quarter of the buffer
// of silence and stops the buffer once that plays; the next sound starts it again.

struct SmkTrack {
  SmkTrack *pNext;             // every track playing, for the service
  IDirectSoundBuffer *pBuffer;
  DWORD cbBuffer;              // half a second, in whole pages
  DWORD dwBufferFlags;
  BYTE *pbRing, *pbRingEnd;
  BYTE *pbRead, *pbWrite;
  DWORD cbRing, cbPending;     // the ring's size and what it holds
  DWORD dwHz, dwBytesPerSec;
  bool b16, bStereo;
  DWORD cbFrameCap;            // the most a frame holds, from the header
  // what has been decoded: (dwClock >> 10) + dwClockBase bytes, dwClock in
  // 1024ths of a byte, moved on a frame's worth a frame; dwCushion is dwClock
  // half a second on
  DWORD dwFrameBytes, dwClock, dwClockBase, dwCushion;
  DWORD cbDecoded;             // bytes put in the ring
  bool bDry;                   // the frame set up had no sound
  bool bUnderran, bPadded;     // the buffer ran dry; the next frame's sound was padded out for it
  bool bStopped;
  DWORD dwWriteOffset;         // in the buffer
  DWORD cbWritten;             // to the buffer
  DWORD cbFree;                // of the buffer: played and not written again
  DWORD dwLastPlay;            // the play cursor the service last saw
  DWORD dwDataEnd, dwSilenceEnd;  // the silence after the last of the sound, or dwDataEnd -1
  DWORD dwLastPosition, dwLastPositionTime, dwPositionFloor;
};

static IDirectSound *g_pSmkSound;  // what the tracks play on
static bool g_bSmkSoundChosen;     // SmkSoundUseDirectSound was called
static bool g_bSmkOwnSound;        // g_pSmkSound was made here, when none was given
static bool g_bSmkSetPrimary;      // an own one's primary buffer is yet to take the first track's format
static IDirectSoundBuffer *g_pSmkPrimary;
static HMODULE g_hSmkDsound;
static HRESULT(WINAPI *g_pfnSmkDirectSoundCreate)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);
static SmkTrack *g_pSmkTracks;
static UINT g_uSmkTimer;
static DWORD g_dwSmkNextService;
static CRITICAL_SECTION g_csSmk;   // the service's, against the timer's thread
static bool g_bSmkLockReady;

static void SmkLock(void)
{
  if (!g_bSmkLockReady) {
    InitializeCriticalSection(&g_csSmk);
    g_bSmkLockReady = true;
  }
  EnterCriticalSection(&g_csSmk);
}

static void SmkUnlock(void) { LeaveCriticalSection(&g_csSmk); }

// The tracks play on pDirectSound, or when it is NULL on a DirectSound made here
// when the first track opens. Answers whether DirectSound is there to be had.
bool SmkSoundUseDirectSound(void *pDirectSound)
{
  g_pSmkSound = static_cast<IDirectSound *>(pDirectSound);
  g_bSmkOwnSound = false;
  if (g_bSmkSoundChosen)
    return true;
  // DirectSound is port_dsound.cpp's, on SDL audio; Smacker loaded dsound.dll
  g_pfnSmkDirectSoundCreate = PortDirectSoundCreate;
  g_bSmkSoundChosen = true;
  return true;
}

// The decoder's own DirectSound, on the process's first top-level window.
static bool SmkMakeSound(void)
{
  if (g_pSmkSound)
    return true;
  if (!g_pfnSmkDirectSoundCreate || FAILED(g_pfnSmkDirectSoundCreate(NULL, &g_pSmkSound, NULL))) {
    g_pSmkSound = NULL;
    return false;
  }
  HWND hWnd = GetTopWindow(NULL);
  const DWORD dwProcess = GetCurrentProcessId();
  for (; hWnd; hWnd = GetWindow(hWnd, GW_HWNDNEXT)) {
    DWORD dwOwner = 0;
    GetWindowThreadProcessId(hWnd, &dwOwner);
    if (dwOwner == dwProcess)
      break;
  }
  if (!hWnd)
    hWnd = GetActiveWindow();
  g_bSmkOwnSound = true;
  g_pSmkSound->SetCooperativeLevel(hWnd, DSSCL_PRIORITY);
  g_bSmkSetPrimary = true;
  return true;
}

static void SmkTrackFormat(const SmkTrack *t, WAVEFORMATEX &wfx)
{
  memset(&wfx, 0, sizeof wfx);
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = static_cast<WORD>(t->bStereo ? 2 : 1);
  wfx.nSamplesPerSec = t->dwHz;
  wfx.wBitsPerSample = static_cast<WORD>(t->b16 ? 16 : 8);
  wfx.nBlockAlign = static_cast<WORD>(wfx.nChannels * wfx.wBitsPerSample / 8);
  wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;
}

// An own DirectSound's primary buffer takes the first track's format, and plays.
static void SmkSetPrimary(const SmkTrack *t)
{
  DSBUFFERDESC desc = {};
  desc.dwSize = sizeof desc;
  desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
  if (FAILED(g_pSmkSound->CreateSoundBuffer(&desc, &g_pSmkPrimary, NULL)))
    return;
  WAVEFORMATEX wfx;
  SmkTrackFormat(t, wfx);
  g_pSmkPrimary->SetFormat(&wfx);
  g_pSmkPrimary->Play(0, 0, DSBPLAY_LOOPING);
}

// Copies cb bytes out of a ring at pbRead into pbDest, and answers where the
// ring is read from next.
static BYTE *SmkRingRead(BYTE *pbDest, BYTE *pbRead, BYTE *pbBase, BYTE *pbEnd, DWORD cb)
{
  const DWORD cbFirst = static_cast<DWORD>(pbEnd - pbRead);
  if (cbFirst <= cb) {
    memcpy(pbDest, pbRead, cbFirst);
    pbDest += cbFirst;
    cb -= cbFirst;
    pbRead = pbBase;
  }
  memcpy(pbDest, pbRead, cb);
  return pbRead + cb;
}

// Silence: 0x80 for 8-bit samples, 0 for 16-bit ones.
static BYTE SmkSilence(const SmkTrack *t) { return t->b16 ? 0 : 0x80; }

// Stops the buffer and rewinds it, to start again with the next sound.
static void SmkTrackStop(SmkTrack *t)
{
  t->pBuffer->Stop();
  t->pBuffer->SetCurrentPosition(0);
  t->bStopped = true;
}

// Fills each track's buffer from its ring, as far as the play cursor allows.
static void SmkService(void)
{
  if (!g_bSmkLockReady || !TryEnterCriticalSection(&g_csSmk))
    return;
  const DWORD dwNow = timeGetTime();
  if (!g_dwSmkNextService)
    g_dwSmkNextService = dwNow;
  if (dwNow >= g_dwSmkNextService) {
    g_dwSmkNextService = dwNow + 10;
    for (SmkTrack *t = g_pSmkTracks; t; t = t->pNext) {
      if (!t->pBuffer)
        continue;
      DWORD dwOffset = 0, cbFree = t->cbBuffer;
      bool bStop = false;
      if (t->bStopped) {
        t->dwDataEnd = 0xFFFFFFFF;
      } else {
        DWORD dwPlay = 0, dwWrite = 0;
        t->pBuffer->GetCurrentPosition(&dwPlay, &dwWrite);
        // stopped once the silence after the last of the sound begins to play
        if (t->dwDataEnd != 0xFFFFFFFF && dwPlay >= t->dwDataEnd &&
            (t->dwDataEnd > t->dwSilenceEnd || dwPlay < t->dwSilenceEnd))
          bStop = true;
        if (!bStop) {
          const DWORD dwPlayed =
              dwPlay >= t->dwLastPlay ? dwPlay - t->dwLastPlay : t->cbBuffer - t->dwLastPlay + dwPlay;
          t->dwLastPlay = dwPlay;
          cbFree = dwPlayed + t->cbFree;
          dwOffset = t->dwWriteOffset;
          // or once all it had was played
          bStop = t->cbBuffer <= cbFree;
        }
        if (bStop) {
          t->bUnderran = true;
          SmkTrackStop(t);
          dwOffset = 0;
          cbFree = t->cbBuffer;
          t->dwDataEnd = 0xFFFFFFFF;
        }
      }
      t->cbFree = cbFree;
      if (cbFree > t->cbBuffer >> 2 && t->dwDataEnd == 0xFFFFFFFF) {
        void *pv1, *pv2;
        DWORD cb1, cb2;
        if (t->cbPending > 3) {
          const DWORD cb = (cbFree < t->cbPending ? cbFree : t->cbPending) & ~3u;
          if (SUCCEEDED(t->pBuffer->Lock(dwOffset, cb, &pv1, &cb1, &pv2, &cb2, 0))) {
            t->pbRead = SmkRingRead(static_cast<BYTE *>(pv1), t->pbRead, t->pbRing, t->pbRingEnd, cb1);
            t->cbPending -= cb1;
            if (cb2 && pv2) {
              t->pbRead = SmkRingRead(static_cast<BYTE *>(pv2), t->pbRead, t->pbRing, t->pbRingEnd, cb2);
              t->cbPending -= cb2;
            }
            t->pBuffer->Unlock(pv1, cb1, pv2, cb2);
            t->dwWriteOffset = dwOffset + cb;
            if (t->dwWriteOffset >= t->cbBuffer)
              t->dwWriteOffset -= t->cbBuffer;
            t->cbWritten += cb;
            t->cbFree -= cb;
          }
        } else if (t->bStopped) {
          continue;
        } else if (t->cbBuffer * 4 / 5 < cbFree) {
          // the last of the sound, then silence; smackw32's arithmetic for
          // where the silence ends is kept
          const DWORD cbSilence = (t->cbBuffer & ~0xCu) >> 2, cb = cbSilence + t->cbPending;
          if (SUCCEEDED(t->pBuffer->Lock(dwOffset, cb, &pv1, &cb1, &pv2, &cb2, 0))) {
            const DWORD cbLocked1 = cb1, cbLocked2 = cb2;
            BYTE *pb1 = static_cast<BYTE *>(pv1), *pb2 = static_cast<BYTE *>(pv2);
            DWORD cbLeft = cb;
            if (t->cbPending) {
              const DWORD n = t->cbPending < cb1 ? t->cbPending : cb1;
              t->pbRead = SmkRingRead(pb1, t->pbRead, t->pbRing, t->pbRingEnd, n);
              t->cbPending -= n;
              pb1 += n;
              cb1 -= n;
              cbLeft -= n;
              if (t->cbPending && cb2 && pb2) {
                const DWORD n2 = t->cbPending < cb2 ? t->cbPending : cb2;
                t->pbRead = SmkRingRead(pb2, t->pbRead, t->pbRing, t->pbRingEnd, n2);
                t->cbPending -= n2;
                pb2 += n2;
                cb2 -= n2;
                cbLeft -= n2;
              }
            }
            const DWORD cbSound = cb - cbLeft;
            t->dwWriteOffset = dwOffset + cbSound;
            if (t->dwWriteOffset >= t->cbBuffer)
              t->dwWriteOffset -= t->cbBuffer;
            t->cbWritten += cbSound;
            t->cbFree -= cbSound;
            t->dwDataEnd = t->dwWriteOffset;
            if (cb1)
              memset(pb1, SmkSilence(t), cb1);
            if (!pb2 || !cb2)
              cb2 = 0;
            else
              memset(pb2, SmkSilence(t), cb2);
            t->dwWriteOffset = dwOffset + cb1 + cb2;
            if (t->dwWriteOffset >= t->cbBuffer)
              t->dwWriteOffset -= t->cbBuffer;
            t->dwSilenceEnd = t->dwWriteOffset;
            t->pBuffer->Unlock(pv1, cbLocked1, pv2, cbLocked2);
            t->bUnderran = true;
          }
        }
      }
      if (t->bStopped) {
        if (g_bSmkSetPrimary) {
          g_bSmkSetPrimary = false;
          SmkSetPrimary(t);
        }
        t->bStopped = false;
        t->pBuffer->Play(0, 0, DSBPLAY_LOOPING);
        t->dwLastPlay = 0;
      }
    }
  }
  LeaveCriticalSection(&g_csSmk);
}

static void CALLBACK SmkTimer(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) { SmkService(); }

// A track of nHz samples a second, stereo or not, 16-bit or 8, one frame of
// dwInterval hundredths of a millisecond holding at most cbFrameCap bytes.
// dwOpenFlags 0x20 and 0x40 give its buffer pan and volume controls. NULL when
// it cannot play.
static SmkTrack *SmkTrackOpen(DWORD dwHz, bool bStereo, bool b16, DWORD dwInterval, DWORD cbFrameCap,
                              DWORD dwOpenFlags)
{
  if (!g_bSmkSoundChosen)
    SmkSoundUseDirectSound(NULL);
  SmkTrack *t = static_cast<SmkTrack *>(calloc(1, sizeof(SmkTrack)));
  t->dwHz = dwHz;
  t->bStereo = bStereo;
  t->b16 = b16;
  t->dwBytesPerSec = (bStereo ? 2 : 1) * (b16 ? 2 : 1) * dwHz;
  t->dwFrameBytes = static_cast<DWORD>(static_cast<unsigned __int64>(t->dwBytesPerSec << 10) * dwInterval / 100000);
  t->dwCushion = t->dwBytesPerSec << 9;
  t->cbRing = ((t->dwFrameBytes >> 9) + t->dwBytesPerSec + 0xFFF) & ~0xFFFu;
  if (t->cbRing < 0x2000)
    t->cbRing = 0x2000;
  t->pbRing = t->pbRead = t->pbWrite = static_cast<BYTE *>(malloc(t->cbRing));
  t->pbRingEnd = t->pbRing + t->cbRing;
  t->cbBuffer = (t->dwBytesPerSec / 2 + 0xFFF) & ~0xFFFu;
  t->dwBufferFlags = ((dwOpenFlags & 0x20) << 1) | ((dwOpenFlags & 0x40) << 1) | DSBCAPS_GETCURRENTPOSITION2 |
                     DSBCAPS_GLOBALFOCUS;
  SmkLock();
  bool bOk = SmkMakeSound();
  if (bOk) {
    WAVEFORMATEX wfx;
    SmkTrackFormat(t, wfx);
    DSBUFFERDESC desc = {};
    desc.dwSize = sizeof desc;
    desc.dwFlags = t->dwBufferFlags;
    desc.dwBufferBytes = t->cbBuffer;
    desc.lpwfxFormat = &wfx;
    if (FAILED(g_pSmkSound->CreateSoundBuffer(&desc, &t->pBuffer, NULL))) {
      desc.dwFlags &= ~DSBCAPS_GLOBALFOCUS;
      if (FAILED(g_pSmkSound->CreateSoundBuffer(&desc, &t->pBuffer, NULL)))
        t->pBuffer = NULL;
    }
    bOk = t->pBuffer != NULL;
    if (!bOk && g_bSmkOwnSound) {
      g_pSmkSound->Release();
      g_pSmkSound = NULL;
      g_bSmkOwnSound = false;
    }
  }
  if (!bOk) {
    SmkUnlock();
    free(t->pbRing);
    free(t);
    return NULL;
  }
  t->bStopped = true;
  t->dwDataEnd = 0xFFFFFFFF;
  t->cbFrameCap = cbFrameCap & ~3u;
  if (!g_pSmkTracks)
    g_uSmkTimer = timeSetEvent(75, 25, SmkTimer, 0, TIME_PERIODIC);
  t->pNext = g_pSmkTracks;
  g_pSmkTracks = t;
  SmkUnlock();
  return t;
}

static void SmkTrackClose(SmkTrack *t)
{
  if (!t)
    return;
  SmkLock();
  for (SmkTrack **pp = &g_pSmkTracks; *pp; pp = &(*pp)->pNext) {
    if (*pp == t) {
      *pp = t->pNext;
      break;
    }
  }
  if (!g_pSmkTracks && g_uSmkTimer) {
    timeKillEvent(g_uSmkTimer);
    g_uSmkTimer = 0;
  }
  t->pBuffer->Stop();
  t->pBuffer->Release();
  if (!g_pSmkTracks) {
    if (g_pSmkPrimary) {
      g_pSmkPrimary->Release();
      g_pSmkPrimary = NULL;
    }
    if (g_bSmkOwnSound) {
      g_pSmkSound->Release();
      g_pSmkSound = NULL;
      g_bSmkOwnSound = false;
    }
    g_bSmkSetPrimary = false;
  }
  SmkUnlock();
  free(t->pbRing);
  free(t);
}

// Back to the start of the sound, for the file going back to frame 0.
static void SmkTrackReset(SmkTrack *t)
{
  SmkLock();
  SmkTrackStop(t);
  t->dwDataEnd = 0xFFFFFFFF;
  t->dwLastPosition = t->dwPositionFloor = t->cbWritten = 0;
  t->cbPending = 0;
  t->pbRead = t->pbWrite;
  t->dwCushion -= t->dwClock;
  t->dwClock = t->dwClockBase = t->cbDecoded = 0;
  SmkUnlock();
}

// The bytes of the track played so far: from the play cursor, and between the
// cursor's moves from the clock, never past what was written or back.
static DWORD SmkTrackPosition(SmkTrack *t)
{
  SmkService();
  SmkLock();
  DWORD dwPosition = t->cbFree - t->cbBuffer + t->cbWritten;
  if (static_cast<LONG>(dwPosition) < 0)
    dwPosition = 0;
  const DWORD dwNow = timeGetTime();
  if (dwPosition == t->dwLastPosition && t->dwLastPosition) {
    dwPosition = t->dwLastPosition +
                 static_cast<DWORD>(static_cast<unsigned __int64>(dwNow - t->dwLastPositionTime) * t->dwBytesPerSec / 1000);
    if (dwPosition > t->cbWritten)
      dwPosition = t->cbWritten;
  } else {
    t->dwLastPosition = dwPosition;
    t->dwLastPositionTime = dwNow;
  }
  if (dwPosition < t->dwPositionFloor)
    dwPosition = t->dwPositionFloor;
  else
    t->dwPositionFloor = dwPosition;
  SmkUnlock();
  return dwPosition;
}

// Puts a frame's sound in the ring, cb bytes of it from pb, or when pb is NULL
// the ring's own bytes as they are. After the buffer ran dry, the next frame's
// sound is padded with silence to a full frame's, and the clock moved on for
// the padding.
static void SmkTrackPut(SmkTrack *t, const BYTE *pb, DWORD cb)
{
  SmkService();
  SmkLock();
  if (t->cbPending + cb > t->cbRing)
    cb = t->cbRing - t->cbPending;
  if (t->bPadded)
    t->bUnderran = t->bPadded = false;
  DWORD cbPad = 0;
  if (t->bUnderran) {
    t->bPadded = true;
    if (cb < t->cbFrameCap && t->cbDecoded) {
      cbPad = t->cbFrameCap - cb;
      if (t->cbPending + cb + cbPad > t->cbRing)
        cbPad = t->cbRing - t->cbPending - cb;
      for (DWORD i = 0; i < cbPad; ++i) {
        *t->pbWrite++ = SmkSilence(t);
        if (t->pbWrite >= t->pbRingEnd)
          t->pbWrite = t->pbRing;
      }
      t->dwClockBase += cbPad;
    }
  }
  if (pb) {
    const DWORD cbFirst = static_cast<DWORD>(t->pbRingEnd - t->pbWrite);
    if (cb > cbFirst) {
      memcpy(t->pbWrite, pb, cbFirst);
      memcpy(t->pbRing, pb + cbFirst, cb - cbFirst);
    } else {
      memcpy(t->pbWrite, pb, cb);
    }
  }
  t->cbDecoded += cbPad + cb;
  t->cbPending += cbPad + cb;
  t->pbWrite += cb;
  if (t->pbWrite >= t->pbRingEnd)
    t->pbWrite -= t->cbRing;
  SmkUnlock();
}

// ---------------------------------------------------------------------------
// The file.

static const DWORD SMK_SIG2 = 0x324B4D53;  // "SMK2"; smackw32.dll opens nothing else
static const DWORD SMK_HEADER = 0x68;
static const DWORD SMK_RING = 1;           // the header's flags: a last frame that leads back to frame 1
static const DWORD SMK_INTERLACED = 2;     // coded at half height, onto every second line
static const DWORD SMK_DOUBLED = 4;        // coded at half height, each line twice
static const DWORD SMK_OPEN_NO_SKIP = 0x400;

enum { SMK_TREE_MMAP, SMK_TREE_MCLR, SMK_TREE_FULL, SMK_TREE_TYPE };

// A horizontal run of changed blocks, in the frame's coded pixels.
struct SmkRun {
  DWORD x, y, w, h;
};

// smackw32's walk over the map of changed blocks, one byte for 16 blocks in
// order, which it reads as runs of 64 pixels a byte. Its bookkeeping for a byte
// that runs on past the end of a row is kept as it is, since the rects come out
// of it.
struct SmkMapWalk {
  bool bDone, bSpill, bStarted;
  DWORD x, y;
  const BYTE *p;
};

// The most a size from the file (a frame, the trees, a frame's sound) may be: a 4096-square
// frame unpacked is 16 MB, so a size past this is a broken or hostile file.
const DWORD kSmkPieceMost = 64u << 20;

struct SmkFile {
  HANDLE hFile;
  DWORD dwBase;            // where the file starts in hFile
  DWORD dwOpenFlags;
  DWORD dwWidth, dwHeight; // the height the caller sees, twice the coded one for a scaled file
  DWORD dwCodedHeight;
  DWORD dwFrames, dwFlags;
  DWORD dwScale;           // SMK_INTERLACED, SMK_DOUBLED or 0
  DWORD dwInterval;        // hundredths of a millisecond a frame, 0 for no pacing
  DWORD adwAudioSize[7], adwAudioRate[7];
  DWORD nPhysical;         // frames in the file, the ring frame among them
  DWORD *pdwFrameSize;
  BYTE *pbFrameType;
  DWORD *pdwFrameOffset;   // from the file's start
  SmkTree aTree[4];
  BYTE *pbFrame;           // the frame set up
  DWORD cbFrameMax;
  bool bIoFailed;          // for good, as in smackw32
  DWORD nFrame;            // the frame set up; the ring frame is dwFrames
  LONG lPaletteReported;   // the frame whose palette SmkDoFrame last handed on
  BYTE abPalette[768];
  bool bNewPalette;
  // the destination, from SmkToBuffer
  BYTE *pbDest;
  DWORD dwLeft, dwTop, dwPitch;
  BYTE *pbMap;
  DWORD cbMap;
  SmkMapWalk walk;
  // the changed rects
  int nRectState;          // 0 none, 1 a frame to report, 2 reporting, 3 its last rect made
  SmkRun run;
  DWORD adwRect[16];       // up to four {x0, y0, x1, y1}
  DWORD nRectLen;          // in dwords
  // pacing by the clock
  DWORD dwDeadline;
  bool bFrameDue;
  // the sound: a track for each that plays, the first pacing the video
  SmkTrack *apTrack[7];
  int nSync;               // -1 for none
  bool bSyncArmed;         // a frame's sound went in since the pacing last caught up
  BYTE *pbAudio;           // a frame's sound on a track, decoded
  DWORD cbAudio;
  // a full-colour video (SmkOpenVideo): its frame in RGBA, then in the palette's colours in pbFrame
  PortVideo *pVideo;
  BYTE *pbRGBA;
  BYTE *pbLut;             // 32,768 entries: a colour of 5 bits each to its nearest entry
  bool bOwnPalette;        // a palette of the video's own, made at each key frame
  void (*pfnPalette)(BYTE *pRGB);  // else the screen's, and the entries it may use
  int nPalFirst, nPalLast;
  DWORD dwPaletteError;    // the own palette's error on the frame it was made from
  bool bWhole;             // the frame painted, to be reported as one rect
  // or the frame's planes alone, for the GPU presenter (SmkOpenVideoPlanes)
  bool bPlanes;
  bool bPlanesOk;
  PortVideoPlanes planes;
};

static bool SmkRead(SmkFile *p, DWORD dwOffset, void *pv, DWORD cb)
{
  DWORD cbRead = 0;
  if (SetFilePointer(p->hFile, static_cast<LONG>(p->dwBase + dwOffset), NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
    return false;
  return ReadFile(p->hFile, pv, cb, &cbRead, NULL) && cbRead == cb;
}

static DWORD SmkDword(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<DWORD>(p[3]) << 24; }

// Leaves the handle open: it is the caller's.
void SmkClose(SmkFile *p)
{
  if (!p)
    return;
  for (int i = 0; i < 7; ++i)
    SmkTrackClose(p->apTrack[i]);
  for (int i = 0; i < 4; ++i)
    SmkTreeFree(p->aTree[i]);
  free(p->pdwFrameSize);
  free(p->pbFrameType);
  free(p->pdwFrameOffset);
  free(p->pbFrame);
  free(p->pbMap);
  free(p->pbAudio);
  PortVideoClose(p->pVideo);
  free(p->pbRGBA);
  free(p->pbLut);
  free(p);
}

// ---------------------------------------------------------------------------
// Setting up a frame: its bytes, and its palette.

// The palette's 6-bit levels as 8.
static const BYTE s_abLevel[64] = {
    0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C,
    0x41, 0x45, 0x49, 0x4D, 0x51, 0x55, 0x59, 0x5D, 0x61, 0x65, 0x69, 0x6D, 0x71, 0x75, 0x79, 0x7D,
    0x82, 0x86, 0x8A, 0x8E, 0x92, 0x96, 0x9A, 0x9E, 0xA2, 0xA6, 0xAA, 0xAE, 0xB2, 0xB6, 0xBA, 0xBE,
    0xC3, 0xC7, 0xCB, 0xCF, 0xD3, 0xD7, 0xDB, 0xDF, 0xE3, 0xE7, 0xEB, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF};

// A palette change: runs of the last palette's entries, in place or from
// elsewhere in it, and new entries, until all 256 are set.
static void SmkPalette(SmkFile *p, const BYTE *pb, DWORD cb)
{
  BYTE abOld[768];
  memcpy(abOld, p->abPalette, 768);
  DWORD nEntry = 0, i = 0;
  while (nEntry < 256 && i < cb) {
    const BYTE b = pb[i];
    if (b & 0x80) {
      nEntry += (b & 0x7F) + 1;
      i += 1;
    } else if (b & 0x40) {
      DWORD n = (b & 0x3F) + 1;
      DWORD nFrom = i + 1 < cb ? pb[i + 1] : 0;
      while (n-- && nEntry < 256 && nFrom < 256) {
        memcpy(p->abPalette + 3 * nEntry, abOld + 3 * nFrom, 3);
        ++nEntry;
        ++nFrom;
      }
      i += 2;
    } else {
      if (i + 2 >= cb)
        break;
      p->abPalette[3 * nEntry] = s_abLevel[pb[i] & 0x3F];
      p->abPalette[3 * nEntry + 1] = s_abLevel[pb[i + 1] & 0x3F];
      p->abPalette[3 * nEntry + 2] = s_abLevel[pb[i + 2] & 0x3F];
      ++nEntry;
      i += 3;
    }
  }
}

// Where a frame's chunks start: its palette's, then each track's that it has,
// then the picture's; a track's length counts its own four bytes.
static DWORD SmkChunk(const SmkFile *p, int nTrack)
{
  const DWORD cb = p->pdwFrameSize[p->nFrame];
  const BYTE bType = p->pbFrameType[p->nFrame];
  DWORD i = (bType & 1) && cb ? p->pbFrame[0] * 4u : 0;
  for (int t = 0; t < nTrack; ++t)
    if ((bType & (2 << t)) && i + 4 <= cb)
      i += SmkDword(p->pbFrame + i);
  return i < cb ? i : cb;
}

static void SmkVideoSetUpFrame(SmkFile *p);
static void SmkVideoFollowPalette(SmkFile *p);

// Reads frame nFrame and its palette. Past the last frame the file goes on from
// frame 1 after its ring frame, or from frame 0 with a black palette.
static void SmkSetUpFrame(SmkFile *p)
{
  if (p->pVideo) {
    SmkVideoSetUpFrame(p);
    return;
  }
  p->bFrameDue = false;
  if (p->nFrame >= p->dwFrames) {
    if (!(p->dwFlags & SMK_RING))
      p->nFrame = 0;
    else if (p->nFrame != p->dwFrames)
      p->nFrame = 1;
  }
  if (p->nFrame == 0) {
    memset(p->abPalette, 0, sizeof p->abPalette);
    // the sound starts again too, when this file's tracks are all that play
    if (p->nSync >= 0 && p->apTrack[p->nSync]->cbDecoded) {
      int nTracks = 0;
      for (SmkTrack *t = g_pSmkTracks; t; t = t->pNext)
        --nTracks;
      for (int i = p->nSync; i < 7; ++i)
        nTracks += p->apTrack[i] != NULL;
      if (!nTracks)
        for (int i = 0; i < 7; ++i)
          if (p->apTrack[i])
            SmkTrackReset(p->apTrack[i]);
    }
  }
  // a frame's size from the file: past kSmkPieceMost it is no video's, and is read as empty
  DWORD cb = p->pdwFrameSize[p->nFrame];
  if (cb > kSmkPieceMost) {
    cb = 0;
    p->bIoFailed = true;
  }
  if (cb > p->cbFrameMax - 8 || p->cbFrameMax < 8) {
    free(p->pbFrame);
    p->cbFrameMax = cb + 8;
    p->pbFrame = static_cast<BYTE *>(malloc(p->cbFrameMax));
    if (p->pbFrame == NULL) {
      p->cbFrameMax = 0;
      p->bIoFailed = true;
      return;
    }
  }
  memset(p->pbFrame + cb, 0, 8);
  if (!SmkRead(p, p->pdwFrameOffset[p->nFrame], p->pbFrame, cb)) {
    memset(p->pbFrame, 0, cb);
    p->bIoFailed = true;
  }
  p->bNewPalette = false;
  if ((p->pbFrameType[p->nFrame] & 1) && cb) {
    const DWORD cbPalette = p->pbFrame[0] * 4u;
    SmkPalette(p, p->pbFrame + 1, cbPalette > 1 && cbPalette <= cb ? cbPalette - 1 : 0);
    p->bNewPalette = p->lPaletteReported != static_cast<LONG>(p->nFrame);
  }
}

// ---------------------------------------------------------------------------
// Opening.

// The file at hFile's position, with smackw32's SmackOpen flags: 0x400 paints a
// frame even when the sound is behind; 0x100000, 0x200000 and 0x300000 make the
// file interlaced, doubled or neither.
SmkFile *SmkOpen(HANDLE hFile, DWORD dwOpenFlags)
{
  SmkFile *p = static_cast<SmkFile *>(calloc(1, sizeof(SmkFile)));
  p->hFile = hFile;
  p->dwOpenFlags = dwOpenFlags;
  p->nSync = -1;
  p->dwBase = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
  BYTE abHeader[SMK_HEADER];
  if (p->dwBase == INVALID_SET_FILE_POINTER || !SmkRead(p, 0, abHeader, SMK_HEADER) ||
      SmkDword(abHeader) != SMK_SIG2) {
    SmkClose(p);
    return NULL;
  }
  p->dwWidth = SmkDword(abHeader + 4);
  p->dwHeight = SmkDword(abHeader + 8);
  p->dwFrames = SmkDword(abHeader + 0xC);
  const LONG lFrameRate = static_cast<LONG>(SmkDword(abHeader + 0x10));
  p->dwFlags = SmkDword(abHeader + 0x14);
  for (int i = 0; i < 7; ++i) {
    p->adwAudioSize[i] = SmkDword(abHeader + 0x18 + 4 * i);
    p->adwAudioRate[i] = SmkDword(abHeader + 0x48 + 4 * i);
  }
  const DWORD cbTrees = SmkDword(abHeader + 0x34);
  if (cbTrees > kSmkPieceMost) {
    SmkClose(p);
    return NULL;
  }
  // a frame rate is milliseconds a frame, or below 0, hundredths of one
  p->dwInterval = lFrameRate >= 0 ? static_cast<DWORD>(lFrameRate) * 100 : static_cast<DWORD>(-lFrameRate);
  p->nPhysical = p->dwFrames + (p->dwFlags & SMK_RING);
  if (!p->dwFrames || p->dwFrames > 0x100000 || !p->dwWidth || p->dwWidth > 0x1000 || !p->dwHeight ||
      p->dwHeight > 0x1000) {
    SmkClose(p);
    return NULL;
  }
  p->pdwFrameSize = static_cast<DWORD *>(malloc(p->nPhysical * 4));
  p->pbFrameType = static_cast<BYTE *>(malloc(p->nPhysical));
  p->pdwFrameOffset = static_cast<DWORD *>(malloc(p->nPhysical * 4));
  BYTE *pbTrees = static_cast<BYTE *>(malloc(cbTrees ? cbTrees : 1));
  const bool bOk = p->pdwFrameSize != NULL && p->pbFrameType != NULL && p->pdwFrameOffset != NULL &&
                   pbTrees != NULL && SmkRead(p, SMK_HEADER, p->pdwFrameSize, p->nPhysical * 4) &&
                   SmkRead(p, SMK_HEADER + p->nPhysical * 4, p->pbFrameType, p->nPhysical) &&
                   SmkRead(p, SMK_HEADER + p->nPhysical * 5, pbTrees, cbTrees);
  if (bOk) {
    SmkBits b;
    SmkBitsInit(b, pbTrees, cbTrees);
    for (int i = 0; i < 4; ++i)
      SmkReadTree16(p->aTree[i], b);
  }
  free(pbTrees);
  if (!bOk) {
    SmkClose(p);
    return NULL;
  }
  // a frame's size is its length as it stands, low bits and all, as smackw32
  // reads it
  DWORD dwOffset = SMK_HEADER + p->nPhysical * 5 + cbTrees;
  for (DWORD i = 0; i < p->nPhysical; ++i) {
    p->pdwFrameOffset[i] = dwOffset;
    dwOffset += p->pdwFrameSize[i];
  }
  // one scale or the other; a file that asks for both gets neither
  switch (dwOpenFlags & 0x300000) {
  case 0x100000:
    p->dwFlags = (p->dwFlags & ~SMK_DOUBLED) | SMK_INTERLACED;
    break;
  case 0x200000:
    p->dwFlags = (p->dwFlags & ~SMK_INTERLACED) | SMK_DOUBLED;
    break;
  case 0x300000:
    p->dwFlags &= ~(SMK_INTERLACED | SMK_DOUBLED);
    break;
  }
  p->dwScale = p->dwFlags & (SMK_INTERLACED | SMK_DOUBLED);
  if (p->dwScale == (SMK_INTERLACED | SMK_DOUBLED))
    p->dwScale = 0;
  p->dwCodedHeight = p->dwHeight;
  if (p->dwScale)
    p->dwHeight *= 2;
  p->lPaletteReported = -1;
  SmkSetUpFrame(p);
  // each track asked for that the file has; one that cannot play is left out
  for (int i = 0; i < 7; ++i) {
    const DWORD dwRate = p->adwAudioRate[i];
    if (!(dwOpenFlags & (0x2000u << i)) || !(dwRate & 0x40000000) || !(dwRate & 0xFFFFFF))
      continue;
    p->apTrack[i] = SmkTrackOpen(dwRate & 0xFFFFFF, (dwRate & 0x10000000) != 0, (dwRate & 0x20000000) != 0,
                                 p->dwInterval, p->adwAudioSize[i], dwOpenFlags);
    if (p->apTrack[i] && p->nSync < 0)
      p->nSync = i;
  }
  p->dwDeadline = 0xFFFFFFFF;
  return p;
}

// ---------------------------------------------------------------------------
// The picture: runs of blocks, left to right and down, painted straight onto the
// destination. An unchanged block is not painted at all, so the caller keeps the
// destination from frame to frame. Each run marks the blocks it covers in the
// map of changed blocks, a byte for 16, a changed run's bytes rounded out and an
// unchanged run's in.

static const DWORD s_adwRun[64] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,  12,  13,  14,  15,   16,
                                   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,  28,  29,  30,  31,   32,
                                   33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,  44,  45,  46,  47,   48,
                                   49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,  128, 256, 512, 1024, 2048};

// Paints one coded line of a block: onto one destination line, or two.
static void SmkLine(BYTE *pOut, DWORD dwPitch, bool bDoubled, BYTE b0, BYTE b1, BYTE b2, BYTE b3)
{
  pOut[0] = b0;
  pOut[1] = b1;
  pOut[2] = b2;
  pOut[3] = b3;
  if (bDoubled)
    memcpy(pOut + dwPitch, pOut, 4);
}

static void SmkPicture(SmkFile *p, const BYTE *pb, DWORD cb)
{
  SmkBits b;
  SmkBitsInit(b, pb, cb);
  for (int i = 0; i < 4; ++i)
    SmkTreeResetMarkers(p->aTree[i]);
  memset(&p->walk, 0, sizeof p->walk);
  const DWORD nWide = p->dwWidth / 4, nBlocks = nWide * (p->dwCodedHeight / 4);
  const bool bDoubled = p->dwScale == SMK_DOUBLED;
  // one coded line on to the next
  const DWORD dwLine = p->dwScale ? 2 * p->dwPitch : p->dwPitch;
  DWORD nBlock = 0, nMarked = 0;
  while (nBlock < nBlocks) {
    const DWORD dwType = SmkTreeDecode16(p->aTree[SMK_TREE_TYPE], b);
    DWORD nRun = s_adwRun[(dwType >> 2) & 0x3F];
    const DWORD dwKind = dwType & 3;
    // the map, for the whole run even where it runs past the last block
    const DWORD nCursor = nBlock + nRun;
    if (nMarked <= nCursor) {
      const DWORD nFirst = nMarked >> 4, nEnd = dwKind == 2 ? nCursor >> 4 : (nCursor + 15) >> 4;
      for (DWORD i = nFirst; i < nEnd && i < p->cbMap; ++i)
        p->pbMap[i] = dwKind != 2;
      nMarked = nEnd << 4;
    }
    for (; nRun && nBlock < nBlocks; --nRun, ++nBlock) {
      BYTE *pOut = p->pbDest + (nBlock / nWide) * 4 * dwLine + (nBlock % nWide) * 4;
      switch (dwKind) {
      case 0: {  // two colours on a mask, bit i for pixel i, 0 for the low byte's colour
        const DWORD dwColours = SmkTreeDecode16(p->aTree[SMK_TREE_MCLR], b);
        DWORD dwMask = SmkTreeDecode16(p->aTree[SMK_TREE_MMAP], b);
        const BYTE bOne = static_cast<BYTE>(dwColours >> 8), bZero = static_cast<BYTE>(dwColours);
        for (int y = 0; y < 4; ++y, pOut += dwLine, dwMask >>= 4)
          SmkLine(pOut, p->dwPitch, bDoubled, (dwMask & 1) ? bOne : bZero, (dwMask & 2) ? bOne : bZero,
                  (dwMask & 4) ? bOne : bZero, (dwMask & 8) ? bOne : bZero);
        break;
      }
      case 1:  // each line two values, the first for its right half
        for (int y = 0; y < 4; ++y, pOut += dwLine) {
          const DWORD dwRight = SmkTreeDecode16(p->aTree[SMK_TREE_FULL], b);
          const DWORD dwLeft = SmkTreeDecode16(p->aTree[SMK_TREE_FULL], b);
          SmkLine(pOut, p->dwPitch, bDoubled, static_cast<BYTE>(dwLeft), static_cast<BYTE>(dwLeft >> 8),
                  static_cast<BYTE>(dwRight), static_cast<BYTE>(dwRight >> 8));
        }
        break;
      case 2:  // unchanged
        break;
      case 3: {  // one colour
        const BYTE c = static_cast<BYTE>(dwType >> 8);
        for (int y = 0; y < 4; ++y, pOut += dwLine)
          SmkLine(pOut, p->dwPitch, bDoubled, c, c, c, c);
        break;
      }
      }
    }
  }
  if ((nBlocks >> 4) < p->cbMap)
    p->pbMap[nBlocks >> 4] = 0;
}

// ---------------------------------------------------------------------------
// Sound.

// A track's piece of the frame, packed as differences through a tree for each
// byte of a sample, into pbAudio.
static void SmkAudio(SmkFile *p, int nTrack, const BYTE *pb, DWORD cb)
{
  p->cbAudio = 0;
  if (!(p->adwAudioRate[nTrack] & 0x80000000)) {
    p->pbAudio = static_cast<BYTE *>(realloc(p->pbAudio, cb ? cb : 1));
    memcpy(p->pbAudio, pb, cb);
    p->cbAudio = cb;
    return;
  }
  if (cb < 4)
    return;
  const DWORD cbOut = SmkDword(pb);
  if (cbOut > kSmkPieceMost)
    return;
  SmkBits b;
  SmkBitsInit(b, pb + 4, cb - 4);
  if (!SmkBit(b))
    return;
  const bool bStereo = SmkBit(b) != 0;
  const bool b16 = SmkBit(b) != 0;
  const int nChannels = bStereo ? 2 : 1, nBytes = b16 ? 2 : 1;
  // each tree's leading bit is read and not heeded: smackw32 builds the tree
  // either way
  SmkTree aTree[4] = {};
  for (int i = 0; i < nChannels * nBytes; ++i) {
    SmkBit(b);
    SmkReadNode8(aTree[i], b, 0);
    SmkBit(b);
  }
  p->pbAudio = static_cast<BYTE *>(realloc(p->pbAudio, cbOut ? cbOut : 1));
  BYTE *pOut = p->pbAudio;
  DWORD n = 0;
  if (b16) {
    int anLast[2] = {0, 0};
    // the bases, the right channel's first, each high byte first
    for (int c = nChannels - 1; c >= 0; --c) {
      const DWORD hi = SmkBitsN(b, 8);
      anLast[c] = static_cast<int>(hi << 8 | SmkBitsN(b, 8));
    }
    for (int c = 0; c < nChannels && n + 2 <= cbOut; ++c, n += 2) {
      pOut[n] = static_cast<BYTE>(anLast[c]);
      pOut[n + 1] = static_cast<BYTE>(anLast[c] >> 8);
    }
    while (n + 2 <= cbOut) {
      for (int c = 0; c < nChannels && n + 2 <= cbOut; ++c, n += 2) {
        const DWORD lo = SmkTreeDecode(aTree[2 * c], b);
        const DWORD hi = SmkTreeDecode(aTree[2 * c + 1], b);
        anLast[c] = (anLast[c] + static_cast<short>(lo | hi << 8)) & 0xFFFF;
        pOut[n] = static_cast<BYTE>(anLast[c]);
        pOut[n + 1] = static_cast<BYTE>(anLast[c] >> 8);
      }
    }
  } else {
    int anLast[2] = {0, 0};
    for (int c = nChannels - 1; c >= 0; --c)
      anLast[c] = static_cast<int>(SmkBitsN(b, 8));
    for (int c = 0; c < nChannels && n < cbOut; ++c)
      pOut[n++] = static_cast<BYTE>(anLast[c]);
    while (n < cbOut) {
      for (int c = 0; c < nChannels && n < cbOut; ++c) {
        anLast[c] = (anLast[c] + static_cast<int>(SmkTreeDecode(aTree[c], b))) & 0xFF;
        pOut[n++] = static_cast<BYTE>(anLast[c]);
      }
    }
  }
  p->cbAudio = n;
  for (int i = 0; i < 4; ++i)
    SmkTreeFree(aTree[i]);
}

// The frame's sound on track nTrack, decoded, as smackw32's SmackGetTrackData
// gives it; NULL when the frame has none.
const BYTE *SmkTrackData(SmkFile *p, int nTrack, DWORD *pcb)
{
  *pcb = 0;
  if (p->pVideo || !(p->pbFrameType[p->nFrame] & (2 << nTrack)))
    return NULL;
  const DWORD i = SmkChunk(p, nTrack), cb = p->pdwFrameSize[p->nFrame];
  if (i + 4 > cb)
    return NULL;
  const DWORD cbChunk = SmkDword(p->pbFrame + i);
  if (cbChunk < 4 || i + cbChunk > cb)
    return NULL;
  SmkAudio(p, nTrack, p->pbFrame + i + 4, cbChunk - 4);
  *pcb = p->cbAudio;
  return p->pbAudio;
}

// The frame's sound into each playing track's ring, until a track the frame has
// no sound for. The clock that paces the video moves on a frame's worth, but not
// past the sound put in.
static void SmkAudioFrame(SmkFile *p)
{
  if (p->nSync < 0)
    return;
  if (p->dwInterval)
    p->bSyncArmed = true;
  SmkTrack *s = p->apTrack[p->nSync];
  SmkLock();
  s->dwClock += s->dwFrameBytes;
  s->dwCushion += s->dwFrameBytes;
  if (s->dwClock > 0xFA00000 && s->dwCushion > 0x2000000) {
    s->dwClock -= 0xFA00000;
    s->dwCushion -= 0xFA00000;
    s->dwClockBase += 256000;
  }
  SmkUnlock();
  if (p->pVideo) {
    // the frame's share of the sound, by the samples that start within it
    size_t nSamples = 0;
    int nRate = 0, nChannels = 0;
    const short *pSound = PortVideoSound(p->pVideo, &nSamples, &nRate, &nChannels);
    const unsigned __int64 nPerFrame = static_cast<unsigned __int64>(nRate) * p->dwInterval;
    size_t nFrom = static_cast<size_t>(nPerFrame * p->nFrame / 100000);
    size_t nTo = static_cast<size_t>(nPerFrame * (p->nFrame + 1) / 100000);
    if (nTo > nSamples)
      nTo = nSamples;
    if (nFrom > nTo)
      nFrom = nTo;
    s->bDry = nFrom == nTo;
    if (!s->bDry)
      SmkTrackPut(s, reinterpret_cast<const BYTE *>(pSound + nFrom * nChannels),
                  static_cast<DWORD>((nTo - nFrom) * nChannels * 2));
  }
  for (int i = 0; i < 7 && !p->pVideo; ++i) {
    SmkTrack *t = p->apTrack[i];
    if (!t)
      continue;
    const BYTE *pbChunk = NULL;
    DWORD cb = 0;
    if (p->pbFrameType[p->nFrame] & (2 << i)) {
      pbChunk = p->pbFrame + SmkChunk(p, i);
      cb = (p->adwAudioRate[i] & 0x80000000) ? SmkDword(pbChunk + 4) : SmkDword(pbChunk) - 4;
    }
    t->bDry = !cb;
    if (t->bDry)
      break;
    DWORD cbSound = 0;
    const BYTE *pbSound = SmkTrackData(p, i, &cbSound);
    SmkTrackPut(t, cbSound >= cb ? pbSound : NULL, cb);
  }
  SmkLock();
  if ((s->dwClock >> 10) + s->dwClockBase > s->cbDecoded) {
    s->dwCushion -= s->dwClock;
    s->dwClock = 0;
    s->dwClockBase = s->cbDecoded;
  }
  SmkUnlock();
}

// Whether the sound has played past the frames decoded and half a second more,
// so that the frame should be dropped.
static DWORD SmkAudioBehind(SmkFile *p)
{
  if (p->nSync < 0)
    return 0;
  if (!p->bSyncArmed)
    return 1;
  SmkTrack *t = p->apTrack[p->nSync];
  if (t->bDry)
    return 0;
  if ((t->dwCushion >> 10) + t->dwClockBase > SmkTrackPosition(t))
    return 0;
  p->bSyncArmed = false;
  return 1;
}

// ---------------------------------------------------------------------------
// Playing: smackw32's calls, as storm.dll made them.

// Where the frames go: the pixel at (left, top) of a buffer pitch bytes a line.
// Nothing is painted until this is called.
void SmkToBuffer(SmkFile *p, DWORD dwLeft, DWORD dwTop, DWORD dwPitch, void *pvBuffer)
{
  p->dwLeft = dwLeft;
  p->dwTop = dwTop;
  p->dwPitch = dwPitch;
  p->pbDest = pvBuffer ? static_cast<BYTE *>(pvBuffer) + dwTop * dwPitch + dwLeft : NULL;
  // the map holds what smackw32's does, which it sizes with a little over
  const DWORD cbMap = ((p->dwWidth * p->dwCodedHeight / 256 + 0x43) & ~3u) - 0x38;
  if (pvBuffer && cbMap > p->cbMap) {
    free(p->pbMap);
    p->pbMap = static_cast<BYTE *>(calloc(cbMap, 1));
    p->cbMap = p->pbMap ? cbMap : 0;
  }
}

// Paints the frame set up onto the destination. Answers whether the sound has
// got behind and the frame should be dropped, in which case it is not painted,
// unless the file was opened with flag 0x400.
DWORD SmkDoFrame(SmkFile *p)
{
  if (p->bIoFailed)
    return 0;
  if (p->bNewPalette)
    p->lPaletteReported = static_cast<LONG>(p->nFrame);
  SmkAudioFrame(p);
  const DWORD dwDrop = p->dwInterval ? SmkAudioBehind(p) : 0;
  const bool bSkip = !(p->dwOpenFlags & SMK_OPEN_NO_SKIP) && dwDrop;
  if (p->pbDest && !bSkip && p->pVideo) {
    SmkVideoFollowPalette(p);
    for (DWORD y = 0; y < p->dwHeight; ++y)
      memcpy(p->pbDest + y * p->dwPitch, p->pbFrame + y * p->dwWidth, p->dwWidth);
    p->bWhole = true;
  } else if (p->pbDest && !bSkip) {
    p->nRectState = 1;
    const DWORD i = SmkChunk(p, 7), cb = p->pdwFrameSize[p->nFrame];
    SmkPicture(p, p->pbFrame + i, cb - i);
  }
  SmkService();
  return dwDrop;
}

// Sets up the next frame.
void SmkNextFrame(SmkFile *p)
{
  ++p->nFrame;
  if (p->bIoFailed)
    return;
  SmkService();
  SmkSetUpFrame(p);
  SmkService();
}

// Whether it is too early for the next frame. With sound, until the sound has
// played the frames decoded. Without, the first a whole frame's time after the
// first ask, each later one a frame's time after the last, or after now when
// playing has fallen more than a frame behind.
DWORD SmkWait(SmkFile *p)
{
  if (p->bIoFailed)
    return 0;
  SmkService();
  if (p->nSync >= 0) {
    if (!p->bSyncArmed)
      return 0;
    SmkTrack *t = p->apTrack[p->nSync];
    const DWORD dwPlayed = SmkTrackPosition(t) + 8;
    if ((t->dwClock >> 10) + t->dwClockBase > dwPlayed)
      return 1;
    p->bSyncArmed = false;
    return 0;
  }
  if (p->bFrameDue)
    return 0;
  const DWORD dwNow = timeGetTime() * 100;
  if (p->dwDeadline == 0xFFFFFFFF) {
    p->dwDeadline = p->dwInterval + dwNow;
    return 1;
  }
  if (p->dwDeadline > dwNow)
    return 1;
  const DWORD dwNext = p->dwDeadline + p->dwInterval;
  p->dwDeadline = dwNext;
  if (dwNow - dwNext > p->dwInterval)
    p->dwDeadline = dwNow + p->dwInterval;
  if (p->dwInterval)
    p->bFrameDue = true;
  return 0;
}

// The next horizontal run of changed blocks, walking the map as smackw32 does.
static bool SmkNextRun(SmkFile *p, SmkRun &r)
{
  SmkMapWalk &s = p->walk;
  const DWORD dwWidth = p->dwWidth, dwHeight = p->dwCodedHeight;
  if (s.bDone)
    return false;
  const BYTE *q = s.p;
  DWORD x;
  if (s.bSpill) {
    // the rest of a row's last byte, at the start of the next row
    if (dwHeight <= s.y) {
      s.bDone = true;
      return false;
    }
    r.y = s.y;
    r.x = 0;
    x = s.x;
    if (dwWidth <= x) {
      r.w = dwWidth;
      s.x = x - dwWidth;
      s.y += 4;
      r.h = 4;
      return true;
    }
    s.bSpill = false;
    r.w = x;
    if (!(q[0] | q[1])) {
      r.h = 4;
      return true;
    }
  } else {
    if (!s.bStarted) {
      q = p->pbMap;
      s.bStarted = true;
    }
    x = s.x;
    DWORD y = s.y;
    if (dwHeight <= y) {
      s.bDone = true;
      return false;
    }
    while (!*q) {
      ++q;
      x += 0x40;
      while (dwWidth <= x) {
        y += 4;
        if (dwHeight <= y) {
          s.bDone = true;
          return false;
        }
        x -= dwWidth;
      }
    }
    s.y = y;
    r.x = x;
    r.y = y;
    r.w = 0;
    r.h = 4;
  }
  for (;;) {
    const DWORD dwNext = x + 0x40;
    if (dwWidth <= dwNext) {
      // the row's last byte, which may run on into the next row
      const DWORD dwOver = dwNext - dwWidth;
      s.x = dwOver;
      if (dwOver)
        s.bSpill = true;
      r.w += 0x40 - dwOver;
      s.y += 4;
      ++q;
      break;
    }
    ++q;
    x = dwNext;
    r.w += 0x40;
    if (!*q) {
      s.x = x;
      break;
    }
  }
  s.p = q;
  return true;
}

// The next rect of the frame's changes, {x0, y0, x1, y1} on the destination.
// Mode 0 merges runs down the frame while their bounding box is no more than
// five quarters of their area, mode 1 makes one rect of the whole frame. The
// left edge is taken down to a multiple of 8 and the width up to one where the
// frame allows. A frame that changed only its palette gives one empty rect.
static bool SmkNextRect(SmkFile *p, int nMode, DWORD *pdwOut)
{
  if (p->nRectState == 3) {
    p->nRectState = 0;
    return false;
  }
  if (p->nRectState == 1) {
    if (!SmkNextRun(p, p->run)) {
      p->nRectState = 0;
      if (!p->bNewPalette)
        return false;
      pdwOut[0] = pdwOut[1] = pdwOut[2] = pdwOut[3] = 0;
      return true;
    }
    p->nRectState = 2;
  }
  DWORD x0 = p->run.x, y0 = p->run.y, x1 = p->run.x + p->run.w, y1 = p->run.y + p->run.h;
  DWORD dwBudget = (p->run.h * p->run.w * 5) >> 2;
  for (;;) {
    if (!SmkNextRun(p, p->run)) {
      p->nRectState = 3;
      break;
    }
    const DWORD nx0 = x0 < p->run.x ? x0 : p->run.x;
    const DWORD nx1 = x1 > p->run.x + p->run.w ? x1 : p->run.x + p->run.w;
    const DWORD ny1 = p->run.y + p->run.h;
    if (nMode != 1) {
      dwBudget += (p->run.h * p->run.w * 5) >> 2;
      if ((ny1 - y0) * (nx1 - nx0) > dwBudget)
        break;
    }
    x0 = nx0;
    x1 = nx1;
    y1 = ny1;
  }
  if (p->dwScale) {
    y0 *= 2;
    y1 *= 2;
  }
  DWORD w = x1 - x0, sx = x0, dx = x0 + p->dwLeft;
  if (dx & 7) {
    const DWORD a = dx & ~7u;
    if (a > p->dwLeft) {
      w += dx - a;
      sx -= dx - a;
      dx = a;
    }
  }
  if (w & 7) {
    const DWORD a = (w + 7) & ~7u;
    if (p->dwWidth - sx > a)
      w = a;
  }
  pdwOut[0] = dx;
  pdwOut[1] = p->dwTop + y0;
  pdwOut[2] = dx + w;
  pdwOut[3] = p->dwTop + y1;
  return true;
}

// Folds the rect at n into another it shares a whole edge with, and goes on with
// the one it went into. As in smackw32, that one's index is taken from before
// the rect at n was taken out, so when it came after n the next rect along goes
// on in its place.
static void SmkMergeRects(DWORD *pdw, DWORD n, DWORD &nLen)
{
restart:
  for (DWORD i = 0; i < nLen; i += 4) {
    if (i == n)
      continue;
    DWORD *a = pdw + i;
    const DWORD *b = pdw + n;
    if (a[0] == b[0] && a[2] == b[2]) {
      if (a[3] == b[1])
        a[3] = b[3];
      else if (a[1] == b[3])
        a[1] = b[1];
      else
        continue;
    } else if (a[1] == b[1] && a[3] == b[3]) {
      if (a[2] == b[0])
        a[2] = b[2];
      else if (a[0] == b[2])
        a[0] = b[0];
      else
        continue;
    } else {
      continue;
    }
    nLen -= 4;
    memmove(pdw + n, pdw + n + 4, (nLen - n) * 4);
    n = i;
    goto restart;
  }
}

// The next rect of the frame's changes on the destination, false when there are
// no more. Up to four are kept ready; mode 2 only hands on those.
bool SmkToBufferRect(SmkFile *p, int nMode, SmkRect *pRect)
{
  if (p->pVideo) {
    if (!p->bWhole || nMode == 2)
      return false;
    p->bWhole = false;
    pRect->x = pRect->y = 0;
    pRect->w = p->dwWidth;
    pRect->h = p->dwHeight;
    return true;
  }
  if (p->nRectState != 0) {
    if (nMode == 2)
      return false;
    while (p->nRectLen < 16) {
      if (!SmkNextRect(p, nMode, p->adwRect + p->nRectLen))
        break;
      p->nRectLen += 4;
      SmkMergeRects(p->adwRect, p->nRectLen - 4, p->nRectLen);
    }
  }
  if (!p->nRectLen)
    return false;
  p->nRectLen -= 4;
  pRect->x = p->adwRect[0];
  pRect->y = p->adwRect[1];
  pRect->w = p->adwRect[2] - p->adwRect[0];
  pRect->h = p->adwRect[3] - p->adwRect[1];
  memmove(p->adwRect, p->adwRect + 4, p->nRectLen * 4);
  return true;
}

// ---------------------------------------------------------------------------
// A full-colour video.

// The nearest of pPalette's entries nFirst to nLast for each colour of 5 bits a channel.
static void SmkVideoLut(SmkFile *p, const BYTE *pPalette, int nFirst, int nLast)
{
  if (!p->pbLut)
    p->pbLut = static_cast<BYTE *>(malloc(32768));
  for (int c = 0; c < 32768; ++c) {
    const int r = (c >> 10 & 31) * 255 / 31, g = (c >> 5 & 31) * 255 / 31, b = (c & 31) * 255 / 31;
    int nBest = nFirst, nBestD = 0x7FFFFFFF;
    for (int i = nFirst; i <= nLast; ++i) {
      const int dr = r - pPalette[3 * i], dg = g - pPalette[3 * i + 1], db = b - pPalette[3 * i + 2];
      const int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
      if (d < nBestD) {
        nBestD = d;
        nBest = i;
      }
    }
    p->pbLut[c] = static_cast<BYTE>(nBest);
  }
}

static int SmkColour15(const BYTE *pb) { return (pb[0] >> 3) << 10 | (pb[1] >> 3) << 5 | pb[2] >> 3; }

// A palette for the frame in pbRGBA, by median cut over its colours of 5 bits a channel, in
// entries 1 to 254, with 0 black and 255 white as the player keeps them.
static void SmkVideoPalette(SmkFile *p)
{
  static DWORD s_adwCount[32768];
  static WORD s_awColours[32768];
  memset(s_adwCount, 0, sizeof s_adwCount);
  const DWORD nPixels = p->dwWidth * p->dwHeight;
  for (DWORD i = 0; i < nPixels; ++i)
    ++s_adwCount[SmkColour15(p->pbRGBA + 4 * i)];
  int nColours = 0;
  for (int c = 0; c < 32768; ++c)
    if (s_adwCount[c])
      s_awColours[nColours++] = static_cast<WORD>(c);
  struct Box {
    int nStart, nEnd;  // in s_awColours
    DWORD nPixels;
  };
  Box aBox[254];
  int nBoxes = 1;
  aBox[0] = {0, nColours, nPixels};
  while (nBoxes < 254) {
    int nPick = -1;
    for (int i = 0; i < nBoxes; ++i)
      if (aBox[i].nEnd - aBox[i].nStart > 1 && (nPick < 0 || aBox[i].nPixels > aBox[nPick].nPixels))
        nPick = i;
    if (nPick < 0)
      break;
    Box &b = aBox[nPick];
    int aLo[3] = {31, 31, 31}, aHi[3] = {0, 0, 0};
    for (int i = b.nStart; i < b.nEnd; ++i)
      for (int k = 0; k < 3; ++k) {
        const int v = s_awColours[i] >> (10 - 5 * k) & 31;
        aLo[k] = v < aLo[k] ? v : aLo[k];
        aHi[k] = v > aHi[k] ? v : aHi[k];
      }
    int nAxis = 0;
    for (int k = 1; k < 3; ++k)
      if (aHi[k] - aLo[k] > aHi[nAxis] - aLo[nAxis])
        nAxis = k;
    const int nShift = 10 - 5 * nAxis;
    // sorted along the axis by counting, then cut where half the pixels are
    static WORD awSorted[32768];
    int anAt[33] = {0};
    for (int i = b.nStart; i < b.nEnd; ++i)
      ++anAt[(s_awColours[i] >> nShift & 31) + 1];
    for (int v = 0; v < 32; ++v)
      anAt[v + 1] += anAt[v];
    for (int i = b.nStart; i < b.nEnd; ++i)
      awSorted[anAt[s_awColours[i] >> nShift & 31]++] = s_awColours[i];
    memcpy(s_awColours + b.nStart, awSorted, (b.nEnd - b.nStart) * sizeof(WORD));
    DWORD nHalf = 0;
    int nCut = b.nStart;
    while (nCut < b.nEnd - 1 && nHalf + s_adwCount[s_awColours[nCut]] <= b.nPixels / 2)
      nHalf += s_adwCount[s_awColours[nCut++]];
    if (nCut == b.nStart) {
      nHalf = s_adwCount[s_awColours[nCut]];
      ++nCut;
    }
    aBox[nBoxes++] = {nCut, b.nEnd, b.nPixels - nHalf};
    b.nEnd = nCut;
    b.nPixels = nHalf;
  }
  memset(p->abPalette, 0, 768);
  memset(p->abPalette + 765, 0xFF, 3);
  for (int i = 0; i < nBoxes; ++i) {
    unsigned __int64 aSum[3] = {0, 0, 0}, n = 0;
    for (int j = aBox[i].nStart; j < aBox[i].nEnd; ++j) {
      const DWORD w = s_adwCount[s_awColours[j]];
      for (int k = 0; k < 3; ++k)
        aSum[k] += static_cast<unsigned __int64>((s_awColours[j] >> (10 - 5 * k) & 31) * 255 / 31) * w;
      n += w;
    }
    for (int k = 0; k < 3; ++k)
      p->abPalette[3 * (i + 1) + k] = static_cast<BYTE>(n ? aSum[k] / n : 0);
  }
  SmkVideoLut(p, p->abPalette, 0, 255);
}

// The frame in pbRGBA in the palette's colours. A video with a shape (the SD menus') is put on
// black, and where it is clear on entry 0, as 1.17's Smacker files have it. Returns the mean
// squared error a pixel, its three channels summed.
static DWORD SmkVideoQuantize(SmkFile *p)
{
  const DWORD nPixels = p->dwWidth * p->dwHeight;
  unsigned __int64 nError = 0;
  for (DWORD i = 0; i < nPixels; ++i) {
    const BYTE *pb = p->pbRGBA + 4 * i;
    const int a = pb[3];
    if (a < 128) {
      p->pbFrame[i] = 0;
      continue;
    }
    BYTE ab[3] = {pb[0], pb[1], pb[2]};
    if (a < 255)
      for (int k = 0; k < 3; ++k)
        ab[k] = static_cast<BYTE>(ab[k] * a / 255);
    const BYTE n = p->pbLut[SmkColour15(ab)];
    p->pbFrame[i] = n;
    for (int k = 0; k < 3; ++k) {
      const int d = ab[k] - p->abPalette[3 * n + k];
      nError += static_cast<unsigned __int64>(d * d);
    }
  }
  return nPixels ? static_cast<DWORD>(nError / nPixels) : 0;
}

// A video in the screen's colours follows the screen's palette: the menus open their
// animations before the screen has faded in from black.
static void SmkVideoFollowPalette(SmkFile *p)
{
  if (!p->pfnPalette)
    return;
  BYTE abPalette[768];
  p->pfnPalette(abPalette);
  if (p->pbLut && memcmp(abPalette, p->abPalette, sizeof abPalette) == 0)
    return;
  memcpy(p->abPalette, abPalette, sizeof abPalette);
  SmkVideoLut(p, abPalette, p->nPalFirst, p->nPalLast);
  SmkVideoQuantize(p);
}

// Frame nFrame decoded and put in the palette's colours; past the last, frame 0 again.
static void SmkVideoSetUpFrame(SmkFile *p)
{
  p->bFrameDue = false;
  if (p->nFrame >= p->dwFrames)
    p->nFrame = 0;
  p->bNewPalette = false;
  if (p->bPlanes) {
    p->bPlanesOk = PortVideoPicture(p->pVideo, p->nFrame, &p->planes);
    return;
  }
  if (!PortVideoFrame(p->pVideo, p->nFrame, p->pbRGBA, static_cast<int>(p->dwWidth) * 4))
    return;
  if (!p->bOwnPalette) {
    if (p->pbLut)
      SmkVideoQuantize(p);
    return;
  }
  // A palette of the video's own is made again at a key frame, and where a scene has moved
  // away from it, which a cinematic's long groups of pictures need: when the frame's error is
  // over twice what the palette had on its own frame.
  if (p->pbLut && !PortVideoKeyFrame(p->pVideo, p->nFrame) && p->nFrame != 0 &&
      SmkVideoQuantize(p) <= 2 * p->dwPaletteError + 16)
    return;
  SmkVideoPalette(p);
  p->dwPaletteError = SmkVideoQuantize(p);
  p->bNewPalette = p->lPaletteReported != static_cast<LONG>(p->nFrame);
}

static SmkFile *SmkVideoOpen(const void *pData, DWORD cb, DWORD dwOpenFlags, void (*pfnPalette)(BYTE *pRGB),
                             int nFirst, int nLast, bool bPlanes)
{
  PortVideo *pVideo = PortVideoOpen(pData, cb);
  if (!pVideo)
    return NULL;
  SmkFile *p = static_cast<SmkFile *>(calloc(1, sizeof(SmkFile)));
  p->hFile = INVALID_HANDLE_VALUE;
  p->pVideo = pVideo;
  p->dwOpenFlags = dwOpenFlags;
  p->nSync = -1;
  p->dwWidth = PortVideoWidth(pVideo);
  p->dwHeight = p->dwCodedHeight = PortVideoHeight(pVideo);
  p->dwFrames = p->nPhysical = PortVideoFrames(pVideo);
  p->dwInterval = PortVideoInterval(pVideo);
  p->bPlanes = bPlanes;
  if (!bPlanes) {
    p->pbRGBA = static_cast<BYTE *>(malloc(p->dwWidth * p->dwHeight * 4));
    p->cbFrameMax = p->dwWidth * p->dwHeight;
    p->pbFrame = static_cast<BYTE *>(calloc(p->cbFrameMax, 1));
  }
  p->bOwnPalette = pfnPalette == NULL && !bPlanes;
  p->pfnPalette = pfnPalette;
  p->nPalFirst = nFirst;
  p->nPalLast = nLast;
  p->lPaletteReported = -1;
  SmkVideoSetUpFrame(p);
  SmkVideoFollowPalette(p);
  size_t nSamples = 0;
  int nRate = 0, nChannels = 0;
  if ((dwOpenFlags & 0x2000) && PortVideoSound(pVideo, &nSamples, &nRate, &nChannels) && nChannels <= 2) {
    const DWORD cbFrame = static_cast<DWORD>(static_cast<unsigned __int64>(nRate) * p->dwInterval / 100000 + 2) *
                          nChannels * 2;
    p->apTrack[0] = SmkTrackOpen(nRate, nChannels == 2, true, p->dwInterval, cbFrame, dwOpenFlags);
    if (p->apTrack[0])
      p->nSync = 0;
  }
  p->dwDeadline = 0xFFFFFFFF;
  return p;
}

SmkFile *SmkOpenVideo(const void *pData, DWORD cb, DWORD dwOpenFlags, void (*pfnPalette)(BYTE *pRGB), int nFirst,
                     int nLast)
{
  return SmkVideoOpen(pData, cb, dwOpenFlags, pfnPalette, nFirst, nLast, false);
}

SmkFile *SmkOpenVideoPlanes(const void *pData, DWORD cb, DWORD dwOpenFlags)
{
  return SmkVideoOpen(pData, cb, dwOpenFlags, NULL, 0, 255, true);
}

const PortVideoPlanes *SmkVideoPlanes(const SmkFile *p) { return p->bPlanes && p->bPlanesOk ? &p->planes : NULL; }

DWORD SmkWidth(const SmkFile *p) { return p->dwWidth; }
DWORD SmkHeight(const SmkFile *p) { return p->dwHeight; }
DWORD SmkFrames(const SmkFile *p) { return p->dwFrames; }
DWORD SmkFrameNum(const SmkFile *p) { return p->nFrame; }
DWORD SmkInterval(const SmkFile *p) { return p->dwInterval; }
bool SmkNewPalette(const SmkFile *p) { return p->bNewPalette; }
const BYTE *SmkPaletteRGB(const SmkFile *p) { return p->abPalette; }
