// port_dsound.cpp -- DirectSound on SDL audio (port_dsound.h; the mixer is in
// docs/modules/data.md, "Sound").
//
// A buffer is PCM in DirectSound's terms: a size, a format (8 or 16 bits, one or two
// channels, any rate), a play position, a volume in hundredths of a decibel, a pan, a
// frequency. The mixer is SDL's callback on its audio thread, so a buffer's state is touched
// only under g_mix. The play cursor the engine reads is the mixer's position; the write
// cursor is a mixer's period ahead, as DirectSound's is. Lock hands out the buffer's own
// memory, as DirectSound's does for a software buffer, so what is written reaches the next
// mix. The primary buffer only holds a format.
#include "port_dsound.h"
#include <math.h>
#include <string.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <SDL3/SDL.h>
#include "port_sound.h"
#include "port_soundshot.h"
#include "../front/port_frontscreens.h"  // FrontLog
#include "../front/port_ui.h"  // PortCascRead
#include "../platform/port_settings.h"
#include "Storm.h"  // SMemAlloc
#include "port_audiodecode.h"  // a mod's sounds, decoded
#include "../tools/port_startup.h"  // PortStartupStep
#include "../tools/port_sysinfo.h"  // PortSysInfoSet

// gamedata.h's, which brings in more than this file uses
void *__fastcall MemFileLoad(const char *pszFile, DWORD_PTR dwArg2, DWORD *pdwSize, int nFlags, int nScope,
                             const char *pszOwnerFile, int nOwnerLine);

namespace {

const int MIX_RATE = 44100;

std::mutex g_mix;               // the buffers and their state, against the mixer's thread
std::atomic<bool> g_bForeground{true};
std::atomic<bool> g_bBackground{false};  // Settings' Sound in the background
std::atomic<float> g_fMaster{1.0f};      // Settings' Master volume, as a gain
bool g_bRemasteredVoices;                // Settings' Unit voices, read on the game's thread

class SoundBuffer;
std::vector<SoundBuffer *> g_buffers;  // every secondary buffer, playing or not
SDL_AudioStream *g_pStream;
std::vector<float> g_mixScratch;
DWORD g_dwPeriodFrames = 1024;  // the mixer's last request, in output frames

float GainFromHundredthsDb(LONG l)
{
  if (l <= DSBVOLUME_MIN)
    return 0.0f;
  return powf(10.0f, static_cast<float>(l) / 2000.0f);
}

class SoundBuffer : public IDirectSoundBuffer {
 public:
  ULONG nRefs = 1;
  bool bPrimary = false;
  DWORD dwFlags = 0;
  WAVEFORMATEX wfx = {};
  std::shared_ptr<std::vector<BYTE>> pData;  // shared with duplicates
  double dPos = 0.0;                         // in frames of the buffer's own format
  bool bPlaying = false;
  bool bLooping = false;
  LONG lVolume = 0;
  LONG lPan = 0;
  DWORD dwFrequency = 0;  // 0: the format's rate
  float fLeft = 1.0f, fRight = 1.0f;

  DWORD Frames() const { return wfx.nBlockAlign ? static_cast<DWORD>(pData->size()) / wfx.nBlockAlign : 0; }

  void Gains()
  {
    const float fVol = GainFromHundredthsDb(lVolume);
    fLeft = fVol * (lPan > 0 ? GainFromHundredthsDb(-lPan) : 1.0f);
    fRight = fVol * (lPan < 0 ? GainFromHundredthsDb(lPan) : 1.0f);
  }

  float Sample(DWORD dwFrame, int nChannel) const
  {
    const BYTE *p = pData->data() + static_cast<size_t>(dwFrame) * wfx.nBlockAlign;
    if (wfx.nChannels > 1)
      p += nChannel * (wfx.wBitsPerSample / 8);
    if (wfx.wBitsPerSample == 16)
      return static_cast<float>(static_cast<int16_t>(p[0] | (p[1] << 8))) / 32768.0f;
    return (static_cast<float>(p[0]) - 128.0f) / 128.0f;
  }

  // Adds nFrames of this buffer into pOut (stereo), from the play position on.
  void Mix(float *pOut, int nFrames)
  {
    const DWORD dwFrames = Frames();
    if (dwFrames == 0) {
      bPlaying = false;
      return;
    }
    const bool bSilent = !g_bForeground.load(std::memory_order_relaxed) && !g_bBackground.load(std::memory_order_relaxed) &&
                         (dwFlags & DSBCAPS_GLOBALFOCUS) == 0;
    const double dStep = static_cast<double>(dwFrequency ? dwFrequency : wfx.nSamplesPerSec) / MIX_RATE;
    for (int i = 0; i < nFrames; i++) {
      if (!bSilent) {
        const DWORD dw0 = static_cast<DWORD>(dPos);
        DWORD dw1 = dw0 + 1;
        if (dw1 >= dwFrames)
          dw1 = bLooping ? 0 : dw0;
        const float fT = static_cast<float>(dPos - dw0);
        const float fL = Sample(dw0, 0) + (Sample(dw1, 0) - Sample(dw0, 0)) * fT;
        const float fR = wfx.nChannels > 1 ? Sample(dw0, 1) + (Sample(dw1, 1) - Sample(dw0, 1)) * fT : fL;
        pOut[2 * i] += fL * fLeft;
        pOut[2 * i + 1] += fR * fRight;
      }
      dPos += dStep;
      if (dPos >= dwFrames) {
        if (bLooping) {
          dPos = fmod(dPos, static_cast<double>(dwFrames));
        } else {
          dPos = 0.0;
          bPlaying = false;
          return;
        }
      }
    }
  }

  // IUnknown
  HRESULT __stdcall QueryInterface(REFIID, void **ppv) override
  {
    if (ppv)
      *ppv = NULL;
    return DSERR_NOINTERFACE;
  }
  ULONG __stdcall AddRef() override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    return ++nRefs;
  }
  ULONG __stdcall Release() override
  {
    std::unique_lock<std::mutex> lock(g_mix);
    const ULONG n = --nRefs;
    if (n == 0) {
      for (size_t i = 0; i < g_buffers.size(); i++)
        if (g_buffers[i] == this) {
          g_buffers.erase(g_buffers.begin() + i);
          break;
        }
      lock.unlock();
      delete this;
    }
    return n;
  }

  // IDirectSoundBuffer
  HRESULT __stdcall GetCaps(LPDSBCAPS pCaps) override
  {
    if (pCaps == NULL)
      return DSERR_INVALIDPARAM;
    pCaps->dwFlags = dwFlags | DSBCAPS_LOCSOFTWARE;
    pCaps->dwBufferBytes = bPrimary ? 0 : static_cast<DWORD>(pData->size());
    pCaps->dwUnlockTransferRate = 0;
    pCaps->dwPlayCpuOverhead = 0;
    return DS_OK;
  }
  HRESULT __stdcall GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite) override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    const DWORD dwBytes = bPrimary ? 0 : static_cast<DWORD>(pData->size());
    const DWORD dwPlay = static_cast<DWORD>(dPos) * wfx.nBlockAlign;
    // the write cursor is a mixer period ahead, at the buffer's own rate
    const double dRate = static_cast<double>(dwFrequency ? dwFrequency : wfx.nSamplesPerSec);
    DWORD dwAhead = static_cast<DWORD>(g_dwPeriodFrames * dRate / MIX_RATE + 1) * wfx.nBlockAlign;
    if (dwBytes != 0 && dwAhead >= dwBytes)
      dwAhead = dwBytes > wfx.nBlockAlign ? dwBytes - wfx.nBlockAlign : 0;
    if (pdwPlay)
      *pdwPlay = dwPlay;
    if (pdwWrite)
      *pdwWrite = dwBytes ? (bPlaying ? (dwPlay + dwAhead) % dwBytes : dwPlay) : 0;
    return DS_OK;
  }
  HRESULT __stdcall GetFormat(WAVEFORMATEX *pwfx, DWORD cbSize, DWORD *pcbWritten) override
  {
    const DWORD cb = cbSize < sizeof wfx ? cbSize : static_cast<DWORD>(sizeof wfx);
    if (pwfx)
      memcpy(pwfx, &wfx, cb);
    if (pcbWritten)
      *pcbWritten = pwfx ? cb : static_cast<DWORD>(sizeof wfx);
    return DS_OK;
  }
  HRESULT __stdcall GetVolume(LONG *plVolume) override
  {
    if (plVolume)
      *plVolume = lVolume;
    return DS_OK;
  }
  HRESULT __stdcall GetPan(LONG *plPan) override
  {
    if (plPan)
      *plPan = lPan;
    return DS_OK;
  }
  HRESULT __stdcall GetFrequency(DWORD *pdwFrequency) override
  {
    if (pdwFrequency)
      *pdwFrequency = dwFrequency ? dwFrequency : wfx.nSamplesPerSec;
    return DS_OK;
  }
  HRESULT __stdcall GetStatus(DWORD *pdwStatus) override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    if (pdwStatus == NULL)
      return DSERR_INVALIDPARAM;
    *pdwStatus = (bPlaying ? DSBSTATUS_PLAYING : 0) | (bPlaying && bLooping ? DSBSTATUS_LOOPING : 0);
    return DS_OK;
  }
  HRESULT __stdcall Initialize(IDirectSound *, LPCDSBUFFERDESC) override { return DSERR_ALREADYINITIALIZED; }
  HRESULT __stdcall Lock(DWORD dwOffset, DWORD dwBytes, void **ppv1, DWORD *pcb1, void **ppv2, DWORD *pcb2,
                         DWORD dwLockFlags) override
  {
    if (bPrimary || ppv1 == NULL || pcb1 == NULL)
      return DSERR_INVALIDCALL;
    const DWORD dwSize = static_cast<DWORD>(pData->size());
    if (dwLockFlags & DSBLOCK_FROMWRITECURSOR) {
      DWORD dwPlay;
      GetCurrentPosition(&dwPlay, &dwOffset);
    }
    if (dwLockFlags & DSBLOCK_ENTIREBUFFER)
      dwBytes = dwSize;
    if (dwOffset >= dwSize || dwBytes > dwSize)
      return DSERR_INVALIDPARAM;
    BYTE *p = pData->data();
    const DWORD dwFirst = dwOffset + dwBytes <= dwSize ? dwBytes : dwSize - dwOffset;
    *ppv1 = p + dwOffset;
    *pcb1 = dwFirst;
    if (ppv2)
      *ppv2 = dwFirst < dwBytes ? p : NULL;
    if (pcb2)
      *pcb2 = dwBytes - dwFirst;
    return DS_OK;
  }
  HRESULT __stdcall Play(DWORD, DWORD, DWORD dwPlayFlags) override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    bPlaying = true;
    bLooping = (dwPlayFlags & DSBPLAY_LOOPING) != 0;
    return DS_OK;
  }
  HRESULT __stdcall SetCurrentPosition(DWORD dwPosition) override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    if (bPrimary)
      return DSERR_INVALIDCALL;
    const DWORD dwFrame = wfx.nBlockAlign ? dwPosition / wfx.nBlockAlign : 0;
    dPos = dwFrame < Frames() ? static_cast<double>(dwFrame) : 0.0;
    return DS_OK;
  }
  HRESULT __stdcall SetFormat(const WAVEFORMATEX *pwfx) override
  {
    if (!bPrimary)
      return DSERR_INVALIDCALL;
    if (pwfx)
      wfx = *pwfx;
    return DS_OK;
  }
  HRESULT __stdcall SetVolume(LONG lNew) override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    lVolume = lNew < DSBVOLUME_MIN ? DSBVOLUME_MIN : lNew > DSBVOLUME_MAX ? DSBVOLUME_MAX : lNew;
    Gains();
    return DS_OK;
  }
  HRESULT __stdcall SetPan(LONG lNew) override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    lPan = lNew < DSBPAN_LEFT ? DSBPAN_LEFT : lNew > DSBPAN_RIGHT ? DSBPAN_RIGHT : lNew;
    Gains();
    return DS_OK;
  }
  HRESULT __stdcall SetFrequency(DWORD dwNew) override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    dwFrequency = dwNew;
    return DS_OK;
  }
  HRESULT __stdcall Stop() override
  {
    std::lock_guard<std::mutex> lock(g_mix);
    bPlaying = false;
    return DS_OK;
  }
  HRESULT __stdcall Unlock(void *, DWORD, void *, DWORD) override { return DS_OK; }
  HRESULT __stdcall Restore() override { return DS_OK; }
};

PortSoundTapFn g_pfnTap;        // guarded by g_mix
PortSoundSourceFn g_pfnSource;  // an add-on's voice (port_sound.h), guarded by g_mix

// Hyperion's own sounds (PortSoundOneShot): each played once from its start, guarded by g_mix.
struct OneShot {
  std::shared_ptr<const std::vector<float>> pcm;
  size_t nPos;  // the next frame
  float fGain;
};
std::vector<OneShot> g_oneShots;

// SDL's callback: the next stretch of output, mixed from every playing buffer.
void SDLCALL MixCallback(void *, SDL_AudioStream *pStream, int nAdditional, int)
{
  if (nAdditional <= 0)
    return;
  const int nFrames = nAdditional / static_cast<int>(2 * sizeof(float));
  if (nFrames <= 0)
    return;
  std::lock_guard<std::mutex> lock(g_mix);
  g_dwPeriodFrames = static_cast<DWORD>(nFrames);
  g_mixScratch.assign(static_cast<size_t>(nFrames) * 2, 0.0f);
  for (SoundBuffer *p : g_buffers)
    if (p->bPlaying)
      p->Mix(g_mixScratch.data(), nFrames);
  // muted behind another window as the game's buffers are; they play on, unheard
  const bool bShotsHeard = g_bForeground.load(std::memory_order_relaxed) || g_bBackground.load(std::memory_order_relaxed);
  for (size_t i = 0; i < g_oneShots.size();) {
    OneShot &o = g_oneShots[i];
    const size_t nLeft = o.pcm->size() / 2 - o.nPos, n = nLeft < static_cast<size_t>(nFrames) ? nLeft : nFrames;
    if (bShotsHeard)
      for (size_t k = 0; k < n * 2; ++k)
        g_mixScratch[k] += (*o.pcm)[o.nPos * 2 + k] * o.fGain;
    o.nPos += n;
    if (o.nPos * 2 >= o.pcm->size())
      g_oneShots.erase(g_oneShots.begin() + i);
    else
      ++i;
  }
  if (g_pfnSource != NULL)
    g_pfnSource(g_mixScratch.data(), nFrames, MIX_RATE, bShotsHeard);
  for (float &f : g_mixScratch)
    f = f > 1.0f ? 1.0f : f < -1.0f ? -1.0f : f;
  if (g_pfnTap != NULL)
    g_pfnTap(g_mixScratch.data(), nFrames, MIX_RATE);
  const float fMaster = g_fMaster.load(std::memory_order_relaxed);
  if (fMaster != 1.0f)
    for (float &f : g_mixScratch)
      f *= fMaster;
  SDL_PutAudioStreamData(pStream, g_mixScratch.data(), nFrames * static_cast<int>(2 * sizeof(float)));
}

bool OpenStream(void)
{
  if (g_pStream)
    return true;
  double dMaster = 1.0, dBackground = 0.0, dVoices = 0.0;
  PortSettingNumber("hyperion", "masterVolume", &dMaster);
  PortSettingNumber("hyperion", "backgroundSound", &dBackground);
  PortSettingNumber("hyperion", "unitVoices", &dVoices);
  PortSoundVoicesSet(dVoices != 0.0);
  PortSoundMaster(static_cast<float>(dMaster));
  PortSoundBackground(dBackground != 0.0);
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    FrontKit::FrontLog("sound: SDL audio did not start: %s", SDL_GetError());
    PortStartupStep("sound", "SDL's audio did not start: %s; the game plays silent", SDL_GetError());
    return false;
  }
  SDL_AudioSpec spec;
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = MIX_RATE;
  g_pStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, MixCallback, NULL);
  if (g_pStream == NULL) {
    FrontKit::FrontLog("sound: no audio device: %s", SDL_GetError());
    PortStartupStep("sound", "no audio device: %s; the game plays silent", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(g_pStream);
  const char *pszDriver = SDL_GetCurrentAudioDriver();
  PortStartupStep("sound", "%s", pszDriver != NULL ? pszDriver : "started");
  PortSysInfoSet("system", "sound", "%s", pszDriver != NULL ? pszDriver : "?");
  return true;
}

class SoundDevice : public IDirectSound {
 public:
  ULONG nRefs = 1;
  SoundBuffer *pPrimary = NULL;

  HRESULT __stdcall QueryInterface(REFIID, void **ppv) override
  {
    if (ppv)
      *ppv = NULL;
    return DSERR_NOINTERFACE;
  }
  ULONG __stdcall AddRef() override { return ++nRefs; }
  ULONG __stdcall Release() override
  {
    const ULONG n = --nRefs;
    if (n == 0) {
      if (pPrimary)
        pPrimary->Release();
      delete this;
    }
    return n;
  }
  HRESULT __stdcall CreateSoundBuffer(LPCDSBUFFERDESC pDesc, IDirectSoundBuffer **ppBuffer, IUnknown *) override
  {
    if (pDesc == NULL || ppBuffer == NULL)
      return DSERR_INVALIDPARAM;
    *ppBuffer = NULL;
    if (pDesc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
      if (pPrimary == NULL) {
        pPrimary = new SoundBuffer;
        pPrimary->bPrimary = true;
        pPrimary->dwFlags = pDesc->dwFlags;
        pPrimary->pData = std::make_shared<std::vector<BYTE>>();
        pPrimary->wfx.wFormatTag = WAVE_FORMAT_PCM;
        pPrimary->wfx.nChannels = 2;
        pPrimary->wfx.nSamplesPerSec = 22050;
        pPrimary->wfx.wBitsPerSample = 16;
        pPrimary->wfx.nBlockAlign = 4;
        pPrimary->wfx.nAvgBytesPerSec = 22050 * 4;
      }
      pPrimary->AddRef();
      *ppBuffer = pPrimary;
      return DS_OK;
    }
    const WAVEFORMATEX *pwfx = pDesc->lpwfxFormat;
    if (pwfx == NULL || pDesc->dwBufferBytes == 0 || pwfx->wFormatTag != WAVE_FORMAT_PCM ||
        (pwfx->wBitsPerSample != 8 && pwfx->wBitsPerSample != 16) || pwfx->nChannels < 1 || pwfx->nChannels > 2 ||
        pwfx->nSamplesPerSec == 0)
      return DSERR_BADFORMAT;
    SoundBuffer *p = new SoundBuffer;
    p->dwFlags = pDesc->dwFlags;
    memcpy(&p->wfx, pwfx, sizeof(PCMWAVEFORMAT));
    p->wfx.cbSize = 0;
    p->wfx.nBlockAlign = static_cast<WORD>(pwfx->nChannels * pwfx->wBitsPerSample / 8);
    // silence: 8-bit PCM is unsigned, centred on 0x80
    p->pData = std::make_shared<std::vector<BYTE>>(pDesc->dwBufferBytes,
                                                   static_cast<BYTE>(pwfx->wBitsPerSample == 8 ? 0x80 : 0));
    p->Gains();
    {
      std::lock_guard<std::mutex> lock(g_mix);
      g_buffers.push_back(p);
    }
    *ppBuffer = p;
    return DS_OK;
  }
  HRESULT __stdcall GetCaps(void *) override { return DSERR_UNSUPPORTED; }
  HRESULT __stdcall DuplicateSoundBuffer(IDirectSoundBuffer *pOriginal, IDirectSoundBuffer **ppDuplicate) override
  {
    if (pOriginal == NULL || ppDuplicate == NULL)
      return DSERR_INVALIDPARAM;
    const SoundBuffer *pFrom = static_cast<const SoundBuffer *>(pOriginal);
    if (pFrom->bPrimary)
      return DSERR_INVALIDCALL;
    SoundBuffer *p = new SoundBuffer;
    {
      std::lock_guard<std::mutex> lock(g_mix);
      p->dwFlags = pFrom->dwFlags;
      p->wfx = pFrom->wfx;
      p->pData = pFrom->pData;
      p->lVolume = pFrom->lVolume;
      p->lPan = pFrom->lPan;
      p->dwFrequency = pFrom->dwFrequency;
      p->Gains();
      g_buffers.push_back(p);
    }
    *ppDuplicate = p;
    return DS_OK;
  }
  HRESULT __stdcall SetCooperativeLevel(HWND, DWORD) override { return DS_OK; }
  HRESULT __stdcall Compact() override { return DS_OK; }
  HRESULT __stdcall GetSpeakerConfig(DWORD *pdwConfig) override
  {
    if (pdwConfig)
      *pdwConfig = 4;  // DSSPEAKER_STEREO
    return DS_OK;
  }
  HRESULT __stdcall SetSpeakerConfig(DWORD) override { return DS_OK; }
  HRESULT __stdcall Initialize(LPCGUID) override { return DSERR_ALREADYINITIALIZED; }
};

}  // namespace

HRESULT __stdcall PortDirectSoundCreate(LPCGUID, LPDIRECTSOUND *ppDS, LPUNKNOWN pOuter)
{
  if (ppDS == NULL)
    return DSERR_INVALIDPARAM;
  *ppDS = NULL;
  if (pOuter != NULL)
    return DSERR_NOAGGREGATION;
  if (!OpenStream())
    return DSERR_NODRIVER;
  *ppDS = new SoundDevice;
  return DS_OK;
}

// The same for a caller with lang/snd.h's declaration of the interfaces.
LONG __stdcall PortDirectSoundCreateAny(const void *pGuid, void **ppDS, void *pOuter)
{
  return PortDirectSoundCreate(static_cast<LPCGUID>(pGuid), reinterpret_cast<LPDIRECTSOUND *>(ppDS),
                               static_cast<LPUNKNOWN>(pOuter));
}

// The game's window came to the front or went behind (port_winmsg.cpp): buffers made
// without DSBCAPS_GLOBALFOCUS are heard only in front.
void PortSoundForeground(bool bForeground)
{
  g_bForeground.store(bForeground, std::memory_order_relaxed);
}

// A level of 0 to 1, heard as its square, so the slider's middle is about a quarter of the
// loudness and 1 leaves the mix as it is.
void PortSoundMaster(float fLevel)
{
  const float f = fLevel < 0.0f ? 0.0f : fLevel > 1.0f ? 1.0f : fLevel;
  g_fMaster.store(f * f, std::memory_order_relaxed);
}

void PortSoundBackground(bool bOn)
{
  g_bBackground.store(bOn, std::memory_order_relaxed);
}

// Remastered's recordings are its paid content (PortRemasteredOwned, port_casc.cpp), held for
// now: the classic sounds play.
void PortSoundVoicesSet(bool bRemastered)
{
  g_bRemasteredVoices = bRemastered && PortRemasteredOwned();
}

// Remastered keeps its recordings under the names 1.17's MPQs use, as 16-bit PCM at 44.1 or
// 22.05 kHz, which the buffers play as they are.
void *PortSoundRemastered(const char *pszPath, DWORD *pcbSize)
{
  if (!g_bRemasteredVoices || SDL_strncasecmp(pszPath, "sound\\", 6) != 0)
    return NULL;
  size_t nSize = 0;
  void *pCasc = PortCascRead(pszPath, &nSize);
  if (pCasc == NULL)
    return NULL;
  void *pFile = nSize != 0 && nSize < 0x7FFFFFFF ? SMemAlloc(static_cast<DWORD>(nSize), "port_dsound.cpp", __LINE__, 0) : NULL;
  if (pFile != NULL) {
    memcpy(pFile, pCasc, nSize);
    *pcbSize = static_cast<DWORD>(nSize);
  }
  SDL_free(pCasc);
  return pFile;
}

// Whether a file is a WAV the buffers play as it is: PCM of 8 or 16 bits, one or two channels.
static bool SoundPlayable(const BYTE *p, DWORD cb)
{
  if (cb < 12 || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0)
    return false;
  for (DWORD at = 12; at + 8 <= cb;) {
    const DWORD cbChunk = p[at + 4] | p[at + 5] << 8 | p[at + 6] << 16 | static_cast<DWORD>(p[at + 7]) << 24;
    if (memcmp(p + at, "fmt ", 4) == 0) {
      if (cbChunk < 16 || at + 8 + 16 > cb)
        return false;
      const BYTE *f = p + at + 8;
      const int nTag = f[0] | f[1] << 8, nChannels = f[2] | f[3] << 8, nBits = f[14] | f[15] << 8;
      return nTag == 1 && (nChannels == 1 || nChannels == 2) && (nBits == 8 || nBits == 16);
    }
    if (cbChunk > cb - at - 8)
      return false;
    at += 8 + cbChunk + (cbChunk & 1);
  }
  return false;
}

// The file decoded (port_audiodecode.cpp) into a WAV of 16-bit PCM in Storm's memory: its own
// rate, and two channels at most, a third and later ones mixed into both.
static void *SoundDecoded(const char *pszPath, const BYTE *p, DWORD cb, DWORD *pcbSize)
{
  AudioDecoder *pDec = AudioDecoderOpenMemory(std::vector<unsigned char>(p, p + cb), pszPath);
  if (pDec == NULL)
    return NULL;
  const int nIn = AudioDecoderChannels(pDec), nOut = nIn >= 2 ? 2 : 1, nRate = AudioDecoderRate(pDec);
  std::vector<short> pcm;
  std::vector<float> buf(4096 * static_cast<size_t>(nIn > 0 ? nIn : 1));
  for (size_t n; nIn > 0 && (n = AudioDecoderRead(pDec, buf.data(), 4096)) > 0;) {
    for (size_t i = 0; i < n; ++i) {
      const float *f = &buf[i * nIn];
      float l = f[0], r = nIn >= 2 ? f[1] : f[0];
      for (int c = 2; c < nIn; ++c) {
        l += 0.5f * f[c];
        r += 0.5f * f[c];
      }
      for (int c = 0; c < nOut; ++c) {
        const float v = (c == 0 ? l : r) * 32767.0f;
        pcm.push_back(static_cast<short>(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v));
      }
    }
    if (pcm.size() > 0x3FFFFFF0u / 2)
      break;
  }
  AudioDecoderClose(pDec);
  if (pcm.empty() || nRate <= 0)
    return NULL;
  const DWORD cbData = static_cast<DWORD>(pcm.size() * 2);
  BYTE *pOut = static_cast<BYTE *>(SMemAlloc(44 + cbData, "port_dsound.cpp", __LINE__, 0));
  if (pOut == NULL)
    return NULL;
  auto put32 = [&](int at, DWORD v) { memcpy(pOut + at, &v, 4); };
  auto put16 = [&](int at, WORD v) { memcpy(pOut + at, &v, 2); };
  memcpy(pOut, "RIFF", 4);
  put32(4, 36 + cbData);
  memcpy(pOut + 8, "WAVEfmt ", 8);
  put32(16, 16);
  put16(20, 1);
  put16(22, static_cast<WORD>(nOut));
  put32(24, static_cast<DWORD>(nRate));
  put32(28, static_cast<DWORD>(nRate) * nOut * 2);
  put16(32, static_cast<WORD>(nOut * 2));
  put16(34, 16);
  memcpy(pOut + 36, "data", 4);
  put32(40, cbData);
  memcpy(pOut + 44, pcm.data(), cbData);
  *pcbSize = 44 + cbData;
  return pOut;
}

void *PortSoundLoad(const char *pszPath, DWORD *pcbSize)
{
  void *pFile = PortSoundRemastered(pszPath, pcbSize);
  if (pFile == NULL)
    pFile = MemFileLoad(pszPath, 0, pcbSize, 1, 1, "port_dsound.cpp", __LINE__);
  if (pFile == NULL || *pcbSize == 0 || SoundPlayable(static_cast<const BYTE *>(pFile), *pcbSize))
    return pFile;
  DWORD cbDecoded = 0;
  void *pDecoded = SoundDecoded(pszPath, static_cast<const BYTE *>(pFile), *pcbSize, &cbDecoded);
  SMemFree(pFile, "port_dsound.cpp", __LINE__, 0);
  if (pDecoded == NULL) {
    FrontKit::FrontLog("sound: %s does not decode", pszPath);
    return NULL;
  }
  const BYTE *pFmt = static_cast<const BYTE *>(pDecoded) + 20;
  FrontKit::FrontLog("sound: %s decoded, %u Hz, %u channels", pszPath,
                     static_cast<unsigned>(pFmt[4] | pFmt[5] << 8 | pFmt[6] << 16), static_cast<unsigned>(pFmt[2]));
  *pcbSize = cbDecoded;
  return pDecoded;
}

void PortSoundOneShot(std::shared_ptr<const std::vector<float>> pcm, float fGain)
{
  if (pcm == NULL || pcm->size() < 2 || fGain <= 0.0f || !OpenStream())
    return;
  std::lock_guard<std::mutex> lock(g_mix);
  if (g_oneShots.size() < 32)
    g_oneShots.push_back({std::move(pcm), 0, fGain});
}

int PortSoundRate(void)
{
  return MIX_RATE;
}

void PortSoundTap(PortSoundTapFn pfn)
{
  std::lock_guard<std::mutex> lock(g_mix);
  g_pfnTap = pfn;
}

bool PortSoundSource(PortSoundSourceFn pfn)
{
  if (pfn != NULL && !OpenStream())
    return false;
  std::lock_guard<std::mutex> lock(g_mix);
  g_pfnSource = pfn;
  return true;
}
