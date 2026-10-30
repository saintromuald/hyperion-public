// port_sysvideo_ffmpeg.cpp -- port_sysvideo.h on Linux, through the player's own FFmpeg.
// libavcodec is loaded when first wanted, the newest of versions 58 to 63 (FFmpeg 4 to 9), and
// only the calls those all have are made. AVFrame's and AVPacket's fields are read at their
// places, which have not moved in those versions; each picture's time is checked against the
// times given, so a library that has moved them plays nothing rather than wrong pictures.
#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include "port_sysvideo.h"
#include "../platform/port_log.h"

static_assert(sizeof(void *) == 8, "AVFrame's places are a 64-bit build's");

namespace {

const PortLogger Log("hyperion_video.log");

// AVFrame's and AVPacket's fields
const size_t kFrameData = 0, kFrameLinesize = 64, kFrameExtended = 96, kFrameWidth = 104, kFrameHeight = 108,
             kFrameSamples = 112, kFrameFormat = 116, kFramePts = 136;
const size_t kPacketPts = 8, kPacketDts = 16, kPacketData = 24, kPacketSize = 32, kPacketFlags = 40;
const int kPacketKey = 1;               // AV_PKT_FLAG_KEY
const int kAgain = -11;                 // AVERROR(EAGAIN)
const int64_t kNoTime = INT64_MIN;      // AV_NOPTS_VALUE
const int64_t kTimeBase = 1LL << 40;    // a frame's time is its rank past this
const int kSampleS16 = 1, kSampleFloat = 3, kSampleS16Planar = 6, kSampleFloatPlanar = 8;  // AVSampleFormat

template <class T> T Get(const void *p, size_t off)
{
  T v;
  memcpy(&v, static_cast<const unsigned char *>(p) + off, sizeof v);
  return v;
}
template <class T> void Set(void *p, size_t off, T v) { memcpy(static_cast<unsigned char *>(p) + off, &v, sizeof v); }

struct Av {
  bool bOk = false;
  const void *(*pfnFindDecoder)(const char *) = NULL;
  void *(*pfnAllocContext)(const void *) = NULL;
  int (*pfnOpen)(void *, const void *, void **) = NULL;
  void (*pfnFreeContext)(void **) = NULL;
  int (*pfnSendPacket)(void *, const void *) = NULL;
  int (*pfnReceiveFrame)(void *, void *) = NULL;
  void (*pfnFlush)(void *) = NULL;
  void *(*pfnPacketAlloc)(void) = NULL;
  void (*pfnPacketFree)(void **) = NULL;
  void *(*pfnFrameAlloc)(void) = NULL;
  void (*pfnFrameFree)(void **) = NULL;
  void (*pfnFrameUnref)(void *) = NULL;
  const char *(*pfnPixFmtName)(int) = NULL;
  int (*pfnOptSetInt)(void *, const char *, int64_t, int) = NULL;
  int (*pfnOptGetInt)(void *, const char *, int, int64_t *) = NULL;
};
Av g_av;
std::once_flag g_avOnce;

template <class T> bool Sym(void *h, const char *pszName, T &pfn)
{
  pfn = reinterpret_cast<T>(dlsym(h, pszName));
  return pfn != NULL;
}

bool AvLoad(void)
{
  std::call_once(g_avOnce, [] {
    static const char *const kNames[] = {"libavcodec.so.63", "libavcodec.so.62", "libavcodec.so.61",
                                         "libavcodec.so.60", "libavcodec.so.59", "libavcodec.so.58"};
    void *h = NULL;
    for (const char *pszName : kNames)
      if ((h = dlopen(pszName, RTLD_NOW | RTLD_LOCAL)) != NULL) {
        Log("video: H.264, H.265 and AAC through %s", pszName);
        break;
      }
    if (!h) {
      Log("video: no libavcodec here, so no H.264, H.265 or AAC");
      return;
    }
    // libavutil's are found through libavcodec, which loads it
    g_av.bOk = Sym(h, "avcodec_find_decoder_by_name", g_av.pfnFindDecoder) &&
               Sym(h, "avcodec_alloc_context3", g_av.pfnAllocContext) && Sym(h, "avcodec_open2", g_av.pfnOpen) &&
               Sym(h, "avcodec_free_context", g_av.pfnFreeContext) &&
               Sym(h, "avcodec_send_packet", g_av.pfnSendPacket) &&
               Sym(h, "avcodec_receive_frame", g_av.pfnReceiveFrame) &&
               Sym(h, "avcodec_flush_buffers", g_av.pfnFlush) && Sym(h, "av_packet_alloc", g_av.pfnPacketAlloc) &&
               Sym(h, "av_packet_free", g_av.pfnPacketFree) && Sym(h, "av_frame_alloc", g_av.pfnFrameAlloc) &&
               Sym(h, "av_frame_free", g_av.pfnFrameFree) && Sym(h, "av_frame_unref", g_av.pfnFrameUnref) &&
               Sym(h, "av_get_pix_fmt_name", g_av.pfnPixFmtName) && Sym(h, "av_opt_set_int", g_av.pfnOptSetInt) &&
               Sym(h, "av_opt_get_int", g_av.pfnOptGetInt);
  });
  return g_av.bOk;
}

void *OpenDecoder(const char *pszName, int nThreads)
{
  const void *pCodec = g_av.pfnFindDecoder(pszName);
  void *pCtx = pCodec ? g_av.pfnAllocContext(pCodec) : NULL;
  if (!pCtx)
    return NULL;
  g_av.pfnOptSetInt(pCtx, "threads", nThreads, 0);
  if (g_av.pfnOpen(pCtx, pCodec, NULL) < 0)
    g_av.pfnFreeContext(&pCtx);
  return pCtx;
}

int64_t OptionOf(void *pCtx, const char *pszName)
{
  int64_t v = 0;
  return g_av.pfnOptGetInt(pCtx, pszName, 0, &v) >= 0 ? v : -1;
}

// a picture's planes, from its format's name: yuv420p, yuvj420p, yuv422p10le, nv12, p010le...
bool FormatOf(const char *psz, SysPicture *p)
{
  if (!psz)
    return false;
  p->xs = p->ys = 1;
  if (strcmp(psz, "nv12") == 0 || strcmp(psz, "p010le") == 0) {
    p->bPaired = true;
    p->nBits = psz[0] == 'p' ? 16 : 8;
    return true;
  }
  if (strncmp(psz, "yuv", 3) != 0)
    return false;
  psz += 3;
  if (*psz == 'j') {
    p->nRange = 2;
    ++psz;
  }
  if (strncmp(psz, "420p", 4) == 0)
    p->xs = p->ys = 1;
  else if (strncmp(psz, "422p", 4) == 0)
    p->ys = 0;
  else if (strncmp(psz, "444p", 4) == 0)
    p->xs = p->ys = 0;
  else
    return false;
  psz += 4;
  p->nBits = 8;
  if (*psz) {
    char *pszEnd = NULL;
    p->nBits = static_cast<int>(strtol(psz, &pszEnd, 10));
    if (strcmp(pszEnd, "le") != 0 || p->nBits < 9 || p->nBits > 16)
      return false;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Pictures.

struct SysVideo {
  void *pCtx = NULL;
  void *pPacket = NULL;
  void *pFrame = NULL;
  SysVideoSetup setup;
  std::vector<unsigned char> annexB;
  bool bWaiting = false;  // annexB was not taken yet
  unsigned nWaitingRank = 0;
  bool bWaitingKey = false;
  bool bMoved = false;  // the library's frames are not as read here
};

static int Give(SysVideo *p, unsigned nRank, bool bKey)
{
  Set<const unsigned char *>(p->pPacket, kPacketData, p->annexB.data());
  Set<int>(p->pPacket, kPacketSize, static_cast<int>(p->annexB.size()));
  Set<int64_t>(p->pPacket, kPacketPts, kTimeBase + nRank);
  Set<int64_t>(p->pPacket, kPacketDts, kNoTime);
  Set<int>(p->pPacket, kPacketFlags, bKey ? kPacketKey : 0);
  const int r = g_av.pfnSendPacket(p->pCtx, p->pPacket);
  p->bWaiting = r == kAgain;
  p->nWaitingRank = nRank;
  p->bWaitingKey = bKey;
  return r;
}

SysVideo *SysVideoOpen(const SysVideoSetup &setup, int, int)
{
  if ((setup.codec != VIDEO_CODEC_H264 && setup.codec != VIDEO_CODEC_H265) || !AvLoad())
    return NULL;
  void *pCtx = OpenDecoder(setup.codec == VIDEO_CODEC_H264 ? "h264" : "hevc", 0);  // as many threads as cores
  if (!pCtx)
    return NULL;
  SysVideo *p = new SysVideo();
  p->pCtx = pCtx;
  p->pPacket = g_av.pfnPacketAlloc();
  p->pFrame = g_av.pfnFrameAlloc();
  p->setup = setup;
  if (!p->pPacket || !p->pFrame) {
    SysVideoClose(p);
    return NULL;
  }
  return p;
}

bool SysVideoSend(SysVideo *p, const unsigned char *pFrame, size_t cb, bool bKey, unsigned nRank, unsigned)
{
  if (p->bWaiting || p->bMoved)
    return false;
  SysVideoAnnexB(p->setup, pFrame, cb, bKey, &p->annexB);
  const int r = Give(p, nRank, bKey);
  return r >= 0 || r == kAgain;
}

void SysVideoDrain(SysVideo *p) { g_av.pfnSendPacket(p->pCtx, NULL); }

bool SysVideoPicture(SysVideo *p, SysPicture *pOut)
{
  g_av.pfnFrameUnref(p->pFrame);
  if (p->bMoved)
    return false;
  int r;
  // a frame the decoder had no room for is given once it has none to give
  for (int nTries = 0; (r = g_av.pfnReceiveFrame(p->pCtx, p->pFrame)) == kAgain && p->bWaiting && nTries < 2; ++nTries)
    if (Give(p, p->nWaitingRank, p->bWaitingKey) < 0 && !p->bWaiting)
      return false;
  if (r < 0)
    return false;
  const void *f = p->pFrame;
  const int64_t nTime = Get<int64_t>(f, kFramePts) - kTimeBase;
  memset(pOut, 0, sizeof *pOut);
  if (nTime < 0 || nTime >= INT32_MAX || !FormatOf(g_av.pfnPixFmtName(Get<int>(f, kFrameFormat)), pOut)) {
    Log("video: this libavcodec's pictures are not as Hyperion reads them (format %d)", Get<int>(f, kFrameFormat));
    p->bMoved = true;
    return false;
  }
  for (int k = 0; k < 3; ++k) {
    pOut->apPlane[k] = Get<const unsigned char *>(f, kFrameData + 8 * k);
    pOut->anStride[k] = Get<int>(f, kFrameLinesize + 4 * k);
  }
  if (pOut->bPaired) {
    pOut->apPlane[2] = pOut->apPlane[1] + (pOut->nBits > 8 ? 2 : 1);
    pOut->anStride[2] = pOut->anStride[1];
  }
  pOut->nWidth = Get<int>(f, kFrameWidth);
  pOut->nHeight = Get<int>(f, kFrameHeight);
  const int64_t nSpace = OptionOf(p->pCtx, "colorspace");  // AVColorSpace
  pOut->nMatrix = nSpace == 1 ? 2 : nSpace == 5 || nSpace == 6 ? 1 : nSpace == 9 || nSpace == 10 ? 3 : 0;
  if (pOut->nRange == 0) {
    const int64_t nRange = OptionOf(p->pCtx, "color_range");  // AVColorRange
    pOut->nRange = nRange == 2 ? 2 : nRange == 1 ? 1 : 0;
  }
  pOut->nRank = static_cast<unsigned>(nTime);
  return true;
}

void SysVideoRestart(SysVideo *p)
{
  g_av.pfnFrameUnref(p->pFrame);
  g_av.pfnFlush(p->pCtx);
  p->bWaiting = false;
}

void SysVideoClose(SysVideo *p)
{
  if (!p)
    return;
  if (p->pFrame)
    g_av.pfnFrameFree(&p->pFrame);
  if (p->pPacket)
    g_av.pfnPacketFree(&p->pPacket);
  if (p->pCtx)
    g_av.pfnFreeContext(&p->pCtx);
  delete p;
}

const char *SysVideoNeeds(VideoCodec) { return "FFmpeg's libavcodec, version 58 to 63"; }
const char *SysSoundNeeds(void) { return "FFmpeg's libavcodec, version 58 to 63"; }

// ---------------------------------------------------------------------------
// AAC, each packet given an ADTS header made from the AudioSpecificConfig, since the decoder's
// set-up has no place that stays put.

static short ToShort(float v)
{
  const float s = v * 32768.0f;
  return static_cast<short>(s >= 32767.0f ? 32767 : s <= -32768.0f ? -32768 : s);
}

static void TakeSamples(const void *f, int nChannels, std::vector<short> *pOut)
{
  const int nSamples = Get<int>(f, kFrameSamples), nFormat = Get<int>(f, kFrameFormat);
  const unsigned char *const *ppPlanes = Get<const unsigned char *const *>(f, kFrameExtended);
  if (nSamples <= 0 || !ppPlanes)
    return;
  const size_t nAt = pOut->size();
  pOut->resize(nAt + static_cast<size_t>(nSamples) * nChannels);
  short *o = pOut->data() + nAt;
  for (int i = 0; i < nSamples; ++i)
    for (int c = 0; c < nChannels; ++c, ++o) {
      if (nFormat == kSampleFloatPlanar)
        *o = ToShort(reinterpret_cast<const float *>(ppPlanes[c])[i]);
      else if (nFormat == kSampleFloat)
        *o = ToShort(reinterpret_cast<const float *>(ppPlanes[0])[i * nChannels + c]);
      else if (nFormat == kSampleS16Planar)
        *o = reinterpret_cast<const short *>(ppPlanes[c])[i];
      else if (nFormat == kSampleS16)
        *o = reinterpret_cast<const short *>(ppPlanes[0])[i * nChannels + c];
      else
        *o = 0;
    }
}

bool SysSoundAac(const std::vector<unsigned char> &config, const unsigned char *pData,
                 const std::vector<std::pair<long long, long>> &packets, std::vector<short> *pOut, int *pnRate,
                 int *pnChannels)
{
  if (config.size() < 2 || !AvLoad())
    return false;
  const int nType = config[0] >> 3, nIndex = (config[0] & 7) << 1 | config[1] >> 7;
  const int nConfig = (config[1] >> 3) & 15;
  if (nType == 0 || nType == 31 || nIndex >= 13 || nConfig == 0 || nConfig > 7)
    return false;
  const int nProfile = nType <= 4 ? nType - 1 : 1;  // HE-AAC's core is AAC LC
  void *pCtx = OpenDecoder("aac", 1);
  if (!pCtx)
    return false;
  void *pPacket = g_av.pfnPacketAlloc(), *pFrame = g_av.pfnFrameAlloc();
  int nChannels = 0;
  auto take = [&] {
    while (g_av.pfnReceiveFrame(pCtx, pFrame) >= 0) {
      if (nChannels == 0) {
        // "ac" is gone from FFmpeg 7, whose AAC decoder gives the configuration's channels
        const int64_t n = OptionOf(pCtx, "ac");
        nChannels = n > 0 && n <= 8 ? static_cast<int>(n) : nConfig == 7 ? 8 : nConfig;
        *pnRate = static_cast<int>(OptionOf(pCtx, "ar"));
      }
      TakeSamples(pFrame, nChannels, pOut);
      g_av.pfnFrameUnref(pFrame);
    }
  };
  std::vector<unsigned char> adts;
  for (size_t i = 0; pPacket && pFrame && i <= packets.size(); ++i) {
    if (i < packets.size()) {
      const size_t cb = static_cast<size_t>(packets[i].second) + 7;
      if (cb > 8191)
        continue;
      const unsigned char aHead[7] = {0xFF, 0xF1, static_cast<unsigned char>(nProfile << 6 | nIndex << 2 | nConfig >> 2),
                                      static_cast<unsigned char>((nConfig & 3) << 6 | cb >> 11),
                                      static_cast<unsigned char>(cb >> 3), static_cast<unsigned char>((cb & 7) << 5 | 0x1F),
                                      0xFC};
      adts.assign(aHead, aHead + 7);
      adts.insert(adts.end(), pData + packets[i].first, pData + packets[i].first + packets[i].second);
      Set<const unsigned char *>(pPacket, kPacketData, adts.data());
      Set<int>(pPacket, kPacketSize, static_cast<int>(adts.size()));
      if (g_av.pfnSendPacket(pCtx, pPacket) == kAgain) {
        take();
        g_av.pfnSendPacket(pCtx, pPacket);
      }
    } else {
      g_av.pfnSendPacket(pCtx, NULL);  // the end: what it holds comes out
    }
    take();
  }
  if (pFrame)
    g_av.pfnFrameFree(&pFrame);
  if (pPacket)
    g_av.pfnPacketFree(&pPacket);
  g_av.pfnFreeContext(&pCtx);
  *pnChannels = nChannels;
  return nChannels > 0 && *pnRate > 0 && !pOut->empty();
}

#endif
