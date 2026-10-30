// port_sysvideo_win.cpp -- port_sysvideo.h on Windows, through Media Foundation: the system's
// H.264 and H.265 decoders and its AAC decoder, each a transform fed one frame at a time.
// mfplat.dll is loaded when first wanted, since Windows' N editions come without it. Only
// synchronous transforms are used; a computer whose only H.265 decoder is its graphics card's
// does not play H.265.
#if defined(_WIN32)

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <string.h>
#include <mutex>
#include "port_sysvideo.h"

namespace {

typedef HRESULT(WINAPI *StartupFn)(ULONG, DWORD);
typedef HRESULT(WINAPI *EnumFn)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO *, const MFT_REGISTER_TYPE_INFO *,
                                IMFActivate ***, UINT32 *);
typedef HRESULT(WINAPI *CreateTypeFn)(IMFMediaType **);
typedef HRESULT(WINAPI *CreateSampleFn)(IMFSample **);
typedef HRESULT(WINAPI *CreateBufferFn)(DWORD, IMFMediaBuffer **);

struct Mf {
  bool bOk = false;
  EnumFn pfnEnum = NULL;
  CreateTypeFn pfnCreateType = NULL;
  CreateSampleFn pfnCreateSample = NULL;
  CreateBufferFn pfnCreateBuffer = NULL;
};
Mf g_mf;
std::once_flag g_mfOnce;

bool MfLoad(void)
{
  std::call_once(g_mfOnce, [] {
    HMODULE h = LoadLibraryExW(L"mfplat.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h)
      return;
    StartupFn pfnStartup = reinterpret_cast<StartupFn>(GetProcAddress(h, "MFStartup"));
    g_mf.pfnEnum = reinterpret_cast<EnumFn>(GetProcAddress(h, "MFTEnumEx"));
    g_mf.pfnCreateType = reinterpret_cast<CreateTypeFn>(GetProcAddress(h, "MFCreateMediaType"));
    g_mf.pfnCreateSample = reinterpret_cast<CreateSampleFn>(GetProcAddress(h, "MFCreateSample"));
    g_mf.pfnCreateBuffer = reinterpret_cast<CreateBufferFn>(GetProcAddress(h, "MFCreateMemoryBuffer"));
    g_mf.bOk = pfnStartup && g_mf.pfnEnum && g_mf.pfnCreateType && g_mf.pfnCreateSample && g_mf.pfnCreateBuffer &&
               SUCCEEDED(pfnStartup(MF_VERSION, MFSTARTUP_LITE));
  });
  if (!g_mf.bOk)
    return false;
  // a thread already in a single-threaded apartment keeps it
  static thread_local bool bCom = false;
  if (!bCom) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bCom = true;
  }
  return true;
}

template <class T> void Release(T *&p)
{
  if (p) {
    p->Release();
    p = NULL;
  }
}

IMFTransform *MfDecoder(const GUID &category, const GUID &major, const GUID &subtype)
{
  MFT_REGISTER_TYPE_INFO in = {major, subtype};
  IMFActivate **ppActivate = NULL;
  UINT32 n = 0;
  if (FAILED(g_mf.pfnEnum(category, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER, &in,
                          NULL, &ppActivate, &n)))
    return NULL;
  IMFTransform *pT = NULL;
  for (UINT32 i = 0; i < n; ++i) {
    if (!pT && FAILED(ppActivate[i]->ActivateObject(IID_PPV_ARGS(&pT))))
      pT = NULL;
    ppActivate[i]->Release();
  }
  CoTaskMemFree(ppActivate);
  return pT;
}

IMFSample *MfSample(const void *pData, DWORD cb)
{
  IMFSample *pSample = NULL;
  IMFMediaBuffer *pBuffer = NULL;
  if (FAILED(g_mf.pfnCreateSample(&pSample)) || FAILED(g_mf.pfnCreateBuffer(cb ? cb : 1, &pBuffer))) {
    Release(pSample);
    return NULL;
  }
  if (pData) {
    BYTE *pBytes = NULL;
    if (SUCCEEDED(pBuffer->Lock(&pBytes, NULL, NULL))) {
      memcpy(pBytes, pData, cb);
      pBuffer->Unlock();
      pBuffer->SetCurrentLength(cb);
    }
  }
  pSample->AddBuffer(pBuffer);
  pBuffer->Release();
  return pSample;
}

// an output sample the transform fills, when it does not give its own
IMFSample *MfOutSample(IMFTransform *pT)
{
  MFT_OUTPUT_STREAM_INFO info = {};
  if (FAILED(pT->GetOutputStreamInfo(0, &info)) ||
      (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)))
    return NULL;
  return MfSample(NULL, info.cbSize);
}

// A sample's time, in 100 ns, from a frame's rank.
const LONGLONG kTick = 400000;

}  // namespace

// ---------------------------------------------------------------------------
// Pictures.

struct SysVideo {
  IMFTransform *pT = NULL;
  SysVideoSetup setup;
  std::vector<unsigned char> annexB;
  IMFSample *pWaiting = NULL;  // a frame the transform did not take yet
  GUID format = GUID_NULL;
  UINT32 nFrameW = 0, nFrameH = 0, nShowW = 0, nShowH = 0;
  LONG nStride = 0;
  int nMatrix = 0, nRange = 0;
  // the picture given last, locked
  IMFSample *pSample = NULL;
  IMFMediaBuffer *pBuffer = NULL;
  IMF2DBuffer *p2d = NULL;
};

static void Unlock(SysVideo *p)
{
  if (p->p2d)
    p->p2d->Unlock2D();
  else if (p->pBuffer)
    p->pBuffer->Unlock();
  Release(p->p2d);
  Release(p->pBuffer);
  Release(p->pSample);
}

// The output's format, NV12 or P010 as the decoder prefers, and its sizes.
static bool SetOutput(SysVideo *p)
{
  for (DWORD i = 0;; ++i) {
    IMFMediaType *pType = NULL;
    if (FAILED(p->pT->GetOutputAvailableType(0, i, &pType)))
      return false;
    GUID sub = GUID_NULL;
    pType->GetGUID(MF_MT_SUBTYPE, &sub);
    if ((sub == MFVideoFormat_NV12 || sub == MFVideoFormat_P010) && SUCCEEDED(p->pT->SetOutputType(0, pType, 0))) {
      p->format = sub;
      MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &p->nFrameW, &p->nFrameH);
      p->nShowW = p->nFrameW;
      p->nShowH = p->nFrameH;
      MFVideoArea area;
      if (SUCCEEDED(pType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8 *>(&area), sizeof area,
                                   NULL))) {
        p->nShowW = static_cast<UINT32>(area.Area.cx);
        p->nShowH = static_cast<UINT32>(area.Area.cy);
      }
      UINT32 nStride = 0;
      p->nStride = SUCCEEDED(pType->GetUINT32(MF_MT_DEFAULT_STRIDE, &nStride))
                       ? static_cast<LONG>(nStride)
                       : static_cast<LONG>(p->nFrameW * (sub == MFVideoFormat_P010 ? 2 : 1));
      const UINT32 nMatrix = MFGetAttributeUINT32(pType, MF_MT_YUV_MATRIX, MFVideoTransferMatrix_Unknown);
      p->nMatrix = nMatrix == MFVideoTransferMatrix_BT709   ? 2
                   : nMatrix == MFVideoTransferMatrix_BT601 ? 1
                   : nMatrix == MFVideoTransferMatrix_BT2020_10 || nMatrix == MFVideoTransferMatrix_BT2020_12 ? 3
                                                                                                             : 0;
      const UINT32 nRange = MFGetAttributeUINT32(pType, MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_Unknown);
      p->nRange = nRange == MFNominalRange_0_255 ? 2 : nRange == MFNominalRange_16_235 ? 1 : 0;
      pType->Release();
      return true;
    }
    pType->Release();
  }
}

SysVideo *SysVideoOpen(const SysVideoSetup &setup, int nWidth, int nHeight)
{
  if (!MfLoad() || (setup.codec != VIDEO_CODEC_H264 && setup.codec != VIDEO_CODEC_H265))
    return NULL;
  const GUID &sub = setup.codec == VIDEO_CODEC_H264 ? MFVideoFormat_H264 : MFVideoFormat_HEVC;
  IMFTransform *pT = MfDecoder(MFT_CATEGORY_VIDEO_DECODER, MFMediaType_Video, sub);
  if (!pT)
    return NULL;
  SysVideo *p = new SysVideo();
  p->pT = pT;
  p->setup = setup;
  IMFMediaType *pIn = NULL;
  bool bOk = SUCCEEDED(g_mf.pfnCreateType(&pIn));
  if (bOk) {
    pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pIn->SetGUID(MF_MT_SUBTYPE, sub);
    MFSetAttributeSize(pIn, MF_MT_FRAME_SIZE, static_cast<UINT32>(nWidth), static_cast<UINT32>(nHeight));
    pIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive);
    bOk = SUCCEEDED(pT->SetInputType(0, pIn, 0)) && SetOutput(p);
    pIn->Release();
  }
  if (!bOk) {
    SysVideoClose(p);
    return NULL;
  }
  pT->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  pT->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  return p;
}

bool SysVideoSend(SysVideo *p, const unsigned char *pFrame, size_t cb, bool bKey, unsigned nRank, unsigned)
{
  if (p->pWaiting)
    return false;
  SysVideoAnnexB(p->setup, pFrame, cb, bKey, &p->annexB);
  IMFSample *pSample = MfSample(p->annexB.data(), static_cast<DWORD>(p->annexB.size()));
  if (!pSample)
    return false;
  pSample->SetSampleTime(static_cast<LONGLONG>(nRank) * kTick);
  pSample->SetSampleDuration(kTick);
  if (bKey)
    pSample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
  const HRESULT hr = p->pT->ProcessInput(0, pSample, 0);
  if (hr == MF_E_NOTACCEPTING) {
    p->pWaiting = pSample;  // given once its pictures are taken
    return true;
  }
  pSample->Release();
  return SUCCEEDED(hr);
}

void SysVideoDrain(SysVideo *p) { p->pT->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0); }

bool SysVideoPicture(SysVideo *p, SysPicture *pOut)
{
  Unlock(p);
  for (int nTries = 0; nTries < 8; ++nTries) {
    MFT_OUTPUT_DATA_BUFFER out = {};
    out.pSample = MfOutSample(p->pT);
    DWORD dwStatus = 0;
    const HRESULT hr = p->pT->ProcessOutput(0, 1, &out, &dwStatus);
    Release(out.pEvents);
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
      Release(out.pSample);
      if (!SetOutput(p))
        return false;
      continue;
    }
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT && p->pWaiting) {
      Release(out.pSample);
      const HRESULT hrIn = p->pT->ProcessInput(0, p->pWaiting, 0);
      Release(p->pWaiting);
      if (FAILED(hrIn))
        return false;
      continue;
    }
    if (FAILED(hr) || !out.pSample) {
      Release(out.pSample);
      return false;
    }
    p->pSample = out.pSample;
    LONGLONG t = 0;
    p->pSample->GetSampleTime(&t);
    BYTE *pBits = NULL;
    LONG nPitch = p->nStride;
    if (FAILED(p->pSample->GetBufferByIndex(0, &p->pBuffer)))
      return false;
    if (SUCCEEDED(p->pBuffer->QueryInterface(IID_PPV_ARGS(&p->p2d))) && FAILED(p->p2d->Lock2D(&pBits, &nPitch)))
      Release(p->p2d);
    if (!p->p2d && FAILED(p->pBuffer->Lock(&pBits, NULL, NULL))) {
      Release(p->pBuffer);
      return false;
    }
    memset(pOut, 0, sizeof *pOut);
    const bool b16 = p->format == MFVideoFormat_P010;
    pOut->apPlane[0] = pBits;
    pOut->apPlane[1] = pBits + static_cast<ptrdiff_t>(nPitch) * p->nFrameH;
    pOut->apPlane[2] = pOut->apPlane[1] + (b16 ? 2 : 1);
    pOut->anStride[0] = pOut->anStride[1] = pOut->anStride[2] = nPitch;
    pOut->nWidth = static_cast<int>(p->nShowW);
    pOut->nHeight = static_cast<int>(p->nShowH);
    pOut->xs = pOut->ys = 1;
    pOut->nBits = b16 ? 16 : 8;
    pOut->bPaired = true;
    pOut->nMatrix = p->nMatrix;
    pOut->nRange = p->nRange;
    pOut->nRank = static_cast<unsigned>((t + kTick / 2) / kTick);
    return true;
  }
  return false;
}

void SysVideoRestart(SysVideo *p)
{
  Unlock(p);
  Release(p->pWaiting);
  p->pT->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
}

void SysVideoClose(SysVideo *p)
{
  if (!p)
    return;
  Unlock(p);
  Release(p->pWaiting);
  if (p->pT) {
    p->pT->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    p->pT->Release();
  }
  delete p;
}

const char *SysVideoNeeds(VideoCodec codec)
{
  return codec == VIDEO_CODEC_H265 ? "Microsoft's HEVC Video Extensions"
                                   : "the Media Feature Pack, on Windows' N editions";
}

const char *SysSoundNeeds(void) { return "the Media Feature Pack, on Windows' N editions"; }

// ---------------------------------------------------------------------------
// AAC.

static void TakePcm(IMFSample *pSample, std::vector<short> *pOut)
{
  IMFMediaBuffer *pBuffer = NULL;
  if (FAILED(pSample->ConvertToContiguousBuffer(&pBuffer)))
    return;
  BYTE *pBytes = NULL;
  DWORD cb = 0;
  if (SUCCEEDED(pBuffer->Lock(&pBytes, NULL, &cb))) {
    const short *ps = reinterpret_cast<const short *>(pBytes);
    pOut->insert(pOut->end(), ps, ps + cb / 2);
    pBuffer->Unlock();
  }
  pBuffer->Release();
}

// every sample the transform has ready
static void TakeAllPcm(IMFTransform *pT, std::vector<short> *pOut)
{
  for (;;) {
    MFT_OUTPUT_DATA_BUFFER out = {};
    out.pSample = MfOutSample(pT);
    DWORD dwStatus = 0;
    const HRESULT hr = pT->ProcessOutput(0, 1, &out, &dwStatus);
    Release(out.pEvents);
    if (SUCCEEDED(hr) && out.pSample)
      TakePcm(out.pSample, pOut);
    Release(out.pSample);
    if (FAILED(hr))
      return;
  }
}

bool SysSoundAac(const std::vector<unsigned char> &config, const unsigned char *pData,
                 const std::vector<std::pair<long long, long>> &packets, std::vector<short> *pOut, int *pnRate,
                 int *pnChannels)
{
  if (config.size() < 2 || !MfLoad())
    return false;
  // the AudioSpecificConfig's rate and channels
  static const int kRates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
  const int nIndex = (config[0] & 7) << 1 | config[1] >> 7;
  int nChannels = (config[1] >> 3) & 15;
  if (nIndex >= 13 || nChannels == 0 || nChannels > 7)
    return false;
  nChannels = nChannels == 7 ? 8 : nChannels;
  IMFTransform *pT = MfDecoder(MFT_CATEGORY_AUDIO_DECODER, MFMediaType_Audio, MFAudioFormat_AAC);
  if (!pT)
    return false;
  IMFMediaType *pIn = NULL;
  bool bOk = SUCCEEDED(g_mf.pfnCreateType(&pIn));
  if (bOk) {
    pIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    pIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(kRates[nIndex]));
    pIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(nChannels));
    pIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pIn->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);  // raw blocks
    // HEAACWAVEINFO past its WAVEFORMATEX, then the AudioSpecificConfig
    std::vector<UINT8> info(12, 0);
    info[2] = 0xFE;  // wAudioProfileLevelIndication, not given
    info.insert(info.end(), config.begin(), config.end());
    pIn->SetBlob(MF_MT_USER_DATA, info.data(), static_cast<UINT32>(info.size()));
    bOk = SUCCEEDED(pT->SetInputType(0, pIn, 0));
    pIn->Release();
  }
  UINT32 nRate = 0, nOutChannels = 0;
  for (DWORD i = 0; bOk; ++i) {
    IMFMediaType *pType = NULL;
    if (FAILED(pT->GetOutputAvailableType(0, i, &pType))) {
      bOk = false;
      break;
    }
    GUID sub = GUID_NULL;
    pType->GetGUID(MF_MT_SUBTYPE, &sub);
    const bool bPcm16 = sub == MFAudioFormat_PCM && MFGetAttributeUINT32(pType, MF_MT_AUDIO_BITS_PER_SAMPLE, 0) == 16;
    if (bPcm16 && SUCCEEDED(pT->SetOutputType(0, pType, 0))) {
      nRate = MFGetAttributeUINT32(pType, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
      nOutChannels = MFGetAttributeUINT32(pType, MF_MT_AUDIO_NUM_CHANNELS, 0);
      pType->Release();
      break;
    }
    pType->Release();
  }
  if (bOk && nRate && nOutChannels) {
    pT->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    pT->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    for (const std::pair<long long, long> &k : packets) {
      IMFSample *pSample = MfSample(pData + k.first, static_cast<DWORD>(k.second));
      if (!pSample)
        continue;
      HRESULT hr = pT->ProcessInput(0, pSample, 0);
      if (hr == MF_E_NOTACCEPTING) {
        TakeAllPcm(pT, pOut);
        hr = pT->ProcessInput(0, pSample, 0);
      }
      pSample->Release();
      TakeAllPcm(pT, pOut);
    }
    pT->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    TakeAllPcm(pT, pOut);
    pT->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    *pnRate = static_cast<int>(nRate);
    *pnChannels = static_cast<int>(nOutChannels);
  }
  pT->Release();
  return bOk && !pOut->empty();
}

#endif
