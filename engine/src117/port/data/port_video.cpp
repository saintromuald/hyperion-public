// port_video.cpp -- videos in full colour, for the Smacker player (port_smacker.cpp), the HD
// portraits and the HUD's pictures. The container's reader (port_webm.cpp, port_mp4.cpp) finds
// the parts; VP8 and VP9 are decoded by libvpx (tools/fetch_libvpx.py), AV1 by dav1d
// (tools/fetch_dav1d.py), Vorbis by stb_vorbis and Opus by libopus, and H.264, H.265 and AAC by
// the system's own decoders (port_sysvideo.h). The sound is decoded whole when the video opens:
// Vorbis's packets are put in Ogg pages for stb_vorbis, one a page.
//
// Remastered's are VP9 in 4:4:4 with the sRGB colour space, so a picture's three planes are its
// green, blue and red, as an AV1 picture with the identity matrix has them; a video in YUV is
// taken as BT.601's, BT.709's or BT.2020's. A picture of 10 or 12 bits a sample keeps its top 8
// in RGBA; PortVideoPicture hands the planes on at their own depth.
//
// A browser's build has none of this: its page holds the classic files alone, and libvpx's way
// out of a bad frame, setjmp and longjmp, breaks its link's rewriting for asynchronous calls;
// there a video that is no Smacker file does not open.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <functional>
#include <thread>
#include <vector>
#include "port_sysvideo.h"
#include "port_video.h"
#include "port_videoindex.h"

static bool EndsWith(const char *psz, const char *pszEnd)
{
  const size_t n = strlen(psz), m = strlen(pszEnd);
  if (n < m)
    return false;
  for (size_t i = 0; i < m; ++i) {
    char c = psz[n - m + i];
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
    if (c != pszEnd[i])
      return false;
  }
  return true;
}

const char *const *PortVideoExtensions(void)
{
  static const char *const kExtensions[] = {".webm", ".mkv", ".mp4", ".m4v", ".mov", NULL};
  return kExtensions;
}

bool PortVideoFileKind(const char *pszName)
{
  for (const char *const *ppsz = PortVideoExtensions(); *ppsz; ++ppsz)
    if (EndsWith(pszName, *ppsz))
      return true;
  return false;
}

#if defined(__EMSCRIPTEN__)

bool PortVideoProbe(const void *, size_t, PortVideoInfo *) { return false; }
PortVideo *PortVideoOpen(const void *, size_t) { return NULL; }
void PortVideoClose(PortVideo *) {}
int PortVideoWidth(const PortVideo *) { return 0; }
int PortVideoHeight(const PortVideo *) { return 0; }
unsigned PortVideoFrames(const PortVideo *) { return 0; }
unsigned PortVideoInterval(const PortVideo *) { return 0; }
bool PortVideoKeyFrame(const PortVideo *, unsigned) { return false; }
bool PortVideoFrame(PortVideo *, unsigned, unsigned char *, int) { return false; }
bool PortVideoPicture(PortVideo *, unsigned, PortVideoPlanes *) { return false; }
const short *PortVideoSound(const PortVideo *, size_t *pnSamples, int *pnRate, int *pnChannels)
{
  *pnSamples = 0;
  *pnRate = *pnChannels = 0;
  return NULL;
}

#else

#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "dav1d/dav1d.h"
#include <opus/opus_multistream.h>
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#include "../platform/port_log.h"  // PortLogger

static const PortLogger Log("hyperion_video.log");

struct PortVideo {
  std::vector<unsigned char> data;
  VideoIndex index;
  unsigned nInterval = 0;
  unsigned nNext = 0;  // the frame the decoder takes next
  bool bHave = false;  // whether the decoder has given a picture since it started
  // libvpx's, VP8 and VP9
  vpx_codec_ctx_t codec;
  bool bCodec = false;
  vpx_image_t *pImage = NULL;  // the last frame it gave
  vpx_codec_ctx_t alpha;  // the alpha's decoder, when a frame has an alpha picture
  bool bAlpha = false;
  vpx_image_t *pAlphaImage = NULL;
  // dav1d's, AV1
  Dav1dContext *pDav1d = NULL;
  Dav1dPicture picture;
  bool bPicture = false;
  unsigned nSent = 0;  // the frames given to it, ahead of those it has given back
  unsigned nOut = 0;
  Dav1dData pending;  // what it has not yet taken of the last frame given
  bool bPending = false;
  // the system's, H.264 and H.265: frames are given in decoding order and pictures come in
  // showing order, a frame's place in it its rank
  SysVideo *pSys = NULL;
  SysVideoSetup sysSetup;
  std::vector<unsigned> ranks, byRank;  // a frame's rank, and the frame of a rank
  std::vector<unsigned> shown;  // the lowest rank of the frames after each
  SysPicture sysPicture;
  bool bSysPicture = false;
  unsigned nSysKey = 0;  // the frame it started from
  bool bSysDrained = false;
  std::vector<short> sound;
  int nRate = 0, nChannels = 0;
};

// ---------------------------------------------------------------------------
// The sound.

static uint32_t g_aOggCrc[256];

static void OggCrcInit(void)
{
  if (g_aOggCrc[1])
    return;
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t r = i << 24;
    for (int k = 0; k < 8; ++k)
      r = r & 0x80000000u ? (r << 1) ^ 0x04C11DB7u : r << 1;
    g_aOggCrc[i] = r;
  }
}

// One packet as one page. No granule position is given (all ones), which stb_vorbis takes as
// a page it knows no place for; the last page ends the stream.
static void OggPage(std::vector<unsigned char> &out, const unsigned char *p, size_t n, bool bFirst, bool bLast,
                    uint32_t nPage)
{
  const size_t nStart = out.size();
  const size_t nSegments = n / 255 + 1;
  if (nSegments > 255)
    return;
  static const unsigned char kHead[4] = {'O', 'g', 'g', 'S'};
  out.insert(out.end(), kHead, kHead + 4);
  out.push_back(0);
  out.push_back(static_cast<unsigned char>((bFirst ? 2 : 0) | (bLast ? 4 : 0)));
  for (int i = 0; i < 8; ++i)
    out.push_back(0xFF);
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<unsigned char>(0x48595052u >> (8 * i)));  // the stream's serial number
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<unsigned char>(nPage >> (8 * i)));
  for (int i = 0; i < 4; ++i)
    out.push_back(0);
  out.push_back(static_cast<unsigned char>(nSegments));
  for (size_t i = 0; i + 1 < nSegments; ++i)
    out.push_back(255);
  out.push_back(static_cast<unsigned char>(n % 255));
  out.insert(out.end(), p, p + n);
  uint32_t crc = 0;
  for (size_t i = nStart; i < out.size(); ++i)
    crc = (crc << 8) ^ g_aOggCrc[((crc >> 24) & 0xFF) ^ out[i]];
  for (int i = 0; i < 4; ++i)
    out[nStart + 22 + i] = static_cast<unsigned char>(crc >> (8 * i));
}

// Vorbis's codec data: the three header packets, the first two sizes in Xiph lacing.
static bool VorbisHeaders(const unsigned char *p, size_t n, std::vector<unsigned char> &ogg, uint32_t &nPage)
{
  if (n < 3 || p[0] != 2)
    return false;
  size_t i = 1, aSize[2] = {0, 0};
  for (int k = 0; k < 2; ++k) {
    while (i < n && p[i] == 255)
      aSize[k] += p[i++];
    if (i >= n)
      return false;
    aSize[k] += p[i++];
  }
  if (i + aSize[0] + aSize[1] > n)
    return false;
  OggPage(ogg, p + i, aSize[0], true, false, nPage++);
  OggPage(ogg, p + i + aSize[0], aSize[1], false, false, nPage++);
  OggPage(ogg, p + i + aSize[0] + aSize[1], n - i - aSize[0] - aSize[1], false, false, nPage++);
  return true;
}

static void SoundVorbis(PortVideo *p)
{
  OggCrcInit();
  std::vector<unsigned char> ogg;
  uint32_t nPage = 0;
  const VideoIndex &x = p->index;
  if (!VorbisHeaders(x.soundSetup.data(), x.soundSetup.size(), ogg, nPage))
    return;
  for (size_t i = 0; i < x.packets.size(); ++i)
    OggPage(ogg, p->data.data() + x.packets[i].first, static_cast<size_t>(x.packets[i].second), false,
            i + 1 == x.packets.size(), nPage++);
  int nChannels = 0, nRate = 0;
  short *pSound = NULL;
  const int nSamples = stb_vorbis_decode_memory(ogg.data(), static_cast<int>(ogg.size()), &nChannels, &nRate,
                                                &pSound);
  if (nSamples > 0 && pSound && nChannels > 0) {
    p->sound.assign(pSound, pSound + static_cast<size_t>(nSamples) * nChannels);
    p->nRate = nRate;
    p->nChannels = nChannels;
  }
  free(pSound);
}

// Opus's packets, after the OpusHead (RFC 7845): always 48 kHz, its pre-skip left out.
static void SoundOpus(PortVideo *p)
{
  const std::vector<unsigned char> &h = p->index.soundSetup;
  if (h.size() < 19 || memcmp(h.data(), "OpusHead", 8) != 0)
    return;
  const int nChannels = h[9];
  const size_t nPreSkip = h[10] | h[11] << 8;
  int nStreams = 1, nCoupled = nChannels == 2 ? 1 : 0;
  unsigned char abMap[255] = {0, 1};
  if (h[18] != 0) {
    if (h.size() < 21 + static_cast<size_t>(nChannels))
      return;
    nStreams = h[19];
    nCoupled = h[20];
    memcpy(abMap, h.data() + 21, nChannels);
  } else if (nChannels < 1 || nChannels > 2) {
    return;
  }
  int nError = 0;
  OpusMSDecoder *pDec = opus_multistream_decoder_create(48000, nChannels, nStreams, nCoupled, abMap, &nError);
  if (!pDec)
    return;
  std::vector<short> buffer(static_cast<size_t>(5760) * nChannels);
  for (const std::pair<long long, long> &k : p->index.packets) {
    const int n = opus_multistream_decode(pDec, p->data.data() + k.first, static_cast<opus_int32>(k.second),
                                          buffer.data(), 5760, 0);
    if (n > 0)
      p->sound.insert(p->sound.end(), buffer.begin(), buffer.begin() + static_cast<size_t>(n) * nChannels);
  }
  opus_multistream_decoder_destroy(pDec);
  const size_t nSkip = std::min(nPreSkip * nChannels, p->sound.size());
  p->sound.erase(p->sound.begin(), p->sound.begin() + nSkip);
  p->nRate = 48000;
  p->nChannels = nChannels;
}

static void SoundAac(PortVideo *p)
{
  if (!SysSoundAac(p->index.soundSetup, p->data.data(), p->index.packets, &p->sound, &p->nRate, &p->nChannels)) {
    p->sound.clear();
    p->nRate = p->nChannels = 0;
    const char *pszNeeds = SysSoundNeeds();
    Log("video: its AAC sound is not played: no decoder for it here%s%s", pszNeeds ? "; it needs " : "",
        pszNeeds ? pszNeeds : "");
    return;
  }
  const size_t nSkip = static_cast<size_t>((p->index.nSoundSkipNs * p->nRate + 500000000LL) / 1000000000LL) * p->nChannels;
  p->sound.erase(p->sound.begin(), p->sound.begin() + std::min(nSkip, p->sound.size()));
}

// More than two channels mixed to two: a centre to both sides, each side's to its own, the low
// frequencies left out. Vorbis and Opus order them as Vorbis does, AAC's decoders as WAVE does.
static void SoundToStereo(PortVideo *p, bool bWave)
{
  const int n = p->nChannels;
  if (n <= 2)
    return;
  // which side each channel is on: 0 left, 1 right, 2 both, 3 neither
  static const char *const kSides[] = {"", "", "", "021", "0101", "02101", "021013", "0210123", "02101013"};
  static const char *const kWave[] = {"", "", "", "012", "0122", "01201", "012301", "0123201", "01230101"};
  const char *pszSides = n <= 8 ? (bWave ? kWave : kSides)[n] : NULL;
  const size_t nSamples = p->sound.size() / n;
  std::vector<short> out(nSamples * 2);
  for (size_t i = 0; i < nSamples; ++i) {
    int l = 0, r = 0;
    for (int c = 0; c < n; ++c) {
      const int v = p->sound[i * n + c];
      const char s = pszSides ? pszSides[c] : static_cast<char>('0' + c % 2);
      if (s == '0')
        l += v;
      else if (s == '1')
        r += v;
      else if (s == '2') {
        l += v * 181 / 256;
        r += v * 181 / 256;
      }
    }
    out[2 * i] = static_cast<short>(std::max(-32768, std::min(32767, l / 2)));
    out[2 * i + 1] = static_cast<short>(std::max(-32768, std::min(32767, r / 2)));
  }
  p->sound.swap(out);
  p->nChannels = 2;
}

// ---------------------------------------------------------------------------
// H.264 and H.265's frames, for the system's decoders.

static size_t Be16(const unsigned char *p) { return static_cast<size_t>(p[0]) << 8 | p[1]; }

// a list of parameter sets, each its length in two bytes, then its bytes
static bool SetsOf(const std::vector<unsigned char> &c, size_t &i, unsigned n, SysVideoSetup *pOut)
{
  for (; n > 0; --n) {
    if (i + 2 > c.size() || i + 2 + Be16(&c[i]) > c.size())
      return false;
    const size_t cb = Be16(&c[i]);
    pOut->sets.emplace_back(c.begin() + static_cast<ptrdiff_t>(i + 2), c.begin() + static_cast<ptrdiff_t>(i + 2 + cb));
    i += 2 + cb;
  }
  return true;
}

bool SysVideoSetupOf(VideoCodec codec, const std::vector<unsigned char> &c, SysVideoSetup *pOut)
{
  pOut->codec = codec;
  pOut->sets.clear();
  size_t i = 0;
  if (codec == VIDEO_CODEC_H264) {
    // avcC: a version, three bytes of profile and level, the length's size, the SPSs, the PPSs
    if (c.size() < 7 || c[0] != 1)
      return false;
    pOut->nLengthSize = (c[4] & 3) + 1;
    i = 6;
    if (!SetsOf(c, i, c[5] & 0x1F, pOut) || i >= c.size())
      return false;
    const unsigned nPps = c[i++];
    return SetsOf(c, i, nPps, pOut);
  }
  if (codec == VIDEO_CODEC_H265) {
    // hvcC: 22 bytes of version, profile and level, the length's size in the last, then arrays
    // of NAL units, each its type and a count
    if (c.size() < 23)
      return false;
    pOut->nLengthSize = (c[21] & 3) + 1;
    i = 23;
    for (unsigned nArrays = c[22]; nArrays > 0; --nArrays) {
      if (i + 3 > c.size())
        return false;
      const unsigned n = static_cast<unsigned>(Be16(&c[i + 1]));
      i += 3;
      if (!SetsOf(c, i, n, pOut))
        return false;
    }
    return true;
  }
  return false;
}

// each NAL unit of a frame, its length before it
template <class F> static void EachUnit(int nLengthSize, const unsigned char *p, size_t cb, F f)
{
  for (size_t i = 0; i + nLengthSize <= cb;) {
    size_t n = 0;
    for (int k = 0; k < nLengthSize; ++k)
      n = n << 8 | p[i + k];
    i += nLengthSize;
    if (n > cb - i)
      return;
    f(p + i, n);
    i += n;
  }
}

void SysVideoAnnexB(const SysVideoSetup &setup, const unsigned char *p, size_t cb, bool bKey,
                    std::vector<unsigned char> *pOut)
{
  static const unsigned char kStart[4] = {0, 0, 0, 1};
  pOut->clear();
  if (bKey)
    for (const std::vector<unsigned char> &set : setup.sets) {
      pOut->insert(pOut->end(), kStart, kStart + 4);
      pOut->insert(pOut->end(), set.begin(), set.end());
    }
  EachUnit(setup.nLengthSize, p, cb, [pOut](const unsigned char *pUnit, size_t n) {
    pOut->insert(pOut->end(), kStart, kStart + 4);
    pOut->insert(pOut->end(), pUnit, pUnit + n);
  });
}

// avc3 and hev1 keep the parameter sets in the frames; a decoder made from them wants them first
static void SetsFromFrame(SysVideoSetup *s, const unsigned char *p, size_t cb)
{
  std::vector<std::vector<unsigned char>> kinds[3];  // VPS, SPS and PPS
  const bool b264 = s->codec == VIDEO_CODEC_H264;
  EachUnit(s->nLengthSize, p, cb, [&](const unsigned char *pUnit, size_t n) {
    if (n == 0)
      return;
    const int t = b264 ? pUnit[0] & 0x1F : pUnit[0] >> 1 & 0x3F;
    const int k = b264 ? (t == 7 ? 1 : t == 8 ? 2 : -1) : (t >= 32 && t <= 34 ? t - 32 : -1);
    if (k >= 0)
      kinds[k].emplace_back(pUnit, pUnit + n);
  });
  for (const std::vector<std::vector<unsigned char>> &kind : kinds)
    s->sets.insert(s->sets.end(), kind.begin(), kind.end());
}

// each frame's place in showing order, from its time
static void SysRanks(PortVideo *p)
{
  const std::vector<VideoPiece> &f = p->index.frames;
  const unsigned n = static_cast<unsigned>(f.size());
  p->byRank.resize(n);
  for (unsigned i = 0; i < n; ++i)
    p->byRank[i] = i;
  std::sort(p->byRank.begin(), p->byRank.end(),
            [&f](unsigned a, unsigned b) { return f[a].ns < f[b].ns || (f[a].ns == f[b].ns && a < b); });
  p->ranks.resize(n);
  for (unsigned r = 0; r < n; ++r)
    p->ranks[p->byRank[r]] = r;
  p->shown.resize(n);
  unsigned nLowest = n;
  for (unsigned i = n; i-- > 0;) {
    p->shown[i] = nLowest;
    nLowest = std::min(nLowest, p->ranks[i]);
  }
}

// ---------------------------------------------------------------------------
// The decoders.

static void Dav1dNoFree(const uint8_t *, void *) {}

static void DecoderStop(PortVideo *p)
{
  if (p->bCodec)
    vpx_codec_destroy(&p->codec);
  if (p->bAlpha)
    vpx_codec_destroy(&p->alpha);
  p->bCodec = p->bAlpha = false;
  if (p->bPicture)
    dav1d_picture_unref(&p->picture);
  if (p->bPending)
    dav1d_data_unref(&p->pending);
  p->bPicture = p->bPending = false;
  p->nSent = p->nOut = 0;
  if (p->pDav1d)
    dav1d_close(&p->pDav1d);
  p->bSysPicture = p->bSysDrained = false;  // the system's decoder is kept, to start again
}

static bool DecoderStart(PortVideo *p)
{
  DecoderStop(p);
  p->nNext = 0;
  p->bHave = false;
  p->pImage = p->pAlphaImage = NULL;
  const VideoIndex &x = p->index;
  if (x.video == VIDEO_CODEC_VP8 || x.video == VIDEO_CODEC_VP9) {
    vpx_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.threads = 1;  // libvpx's threads, as built here, do not give the same pictures
    vpx_codec_iface_t *pIface = x.video == VIDEO_CODEC_VP8 ? vpx_codec_vp8_dx() : vpx_codec_vp9_dx();
    p->bCodec = vpx_codec_dec_init(&p->codec, pIface, &cfg, 0) == VPX_CODEC_OK;
    bool bAlpha = false;
    for (size_t i = 0; i < x.frames.size() && !bAlpha; ++i)
      bAlpha = x.frames[i].alphaLen != 0;
    p->bAlpha = bAlpha && vpx_codec_dec_init(&p->alpha, pIface, &cfg, 0) == VPX_CODEC_OK;
    return p->bCodec;
  }
  if (x.video == VIDEO_CODEC_AV1) {
    // a large picture decodes several frames at once, given the frames ahead of the one asked for
    const unsigned nCores = std::max(1u, std::thread::hardware_concurrency());
    Dav1dSettings s;
    dav1d_default_settings(&s);
    s.n_threads = static_cast<int>(x.nWidth * x.nHeight > 1280 * 720 ? std::min(16u, nCores)
                                                                       : std::max(1u, std::min(4u, nCores / 2)));
    s.all_layers = 0;
    if (dav1d_open(&p->pDav1d, &s) != 0)
      return false;
    // av1C: four bytes, then the sequence header's OBUs, which the first key frame may not repeat
    if (x.videoSetup.size() > 4) {
      Dav1dData data;
      if (dav1d_data_wrap(&data, x.videoSetup.data() + 4, x.videoSetup.size() - 4, Dav1dNoFree, NULL) == 0) {
        dav1d_send_data(p->pDav1d, &data);
        dav1d_data_unref(&data);
      }
    }
    return true;
  }
  if (x.video == VIDEO_CODEC_H264 || x.video == VIDEO_CODEC_H265) {
    if (p->pSys)
      SysVideoRestart(p->pSys);
    else
      p->pSys = SysVideoOpen(p->sysSetup, x.nWidth, x.nHeight);
    return p->pSys != NULL;
  }
  return false;
}

static bool DecoderRunning(const PortVideo *p) { return p->bCodec || p->pDav1d || p->pSys; }

// The next frame's picture, which the decoder keeps until the one after.
static void DecodeNext(PortVideo *p)
{
  const unsigned char *d = p->data.data();
  if (p->pDav1d) {
    // each frame gives one picture, in order: frames are given until the n-th picture comes
    // back, and once all are given, asking twice drains those still being decoded
    ++p->nNext;
    int nWaits = 0;
    while (p->nOut < p->nNext && nWaits < 2) {
      Dav1dPicture pic;
      memset(&pic, 0, sizeof pic);
      const int nGot = dav1d_get_picture(p->pDav1d, &pic);
      if (nGot == 0) {
        if (p->bPicture)
          dav1d_picture_unref(&p->picture);
        p->picture = pic;
        p->bPicture = p->bHave = true;
        ++p->nOut;
        nWaits = 0;
        continue;
      }
      if (nGot != DAV1D_ERR(EAGAIN))
        break;
      if (!p->bPending && p->nSent < p->index.frames.size()) {
        const VideoPiece &f = p->index.frames[p->nSent++];
        p->bPending = dav1d_data_wrap(&p->pending, d + f.pos, static_cast<size_t>(f.len), Dav1dNoFree, NULL) == 0;
      }
      if (p->bPending) {
        const int nSent = dav1d_send_data(p->pDav1d, &p->pending);
        if ((nSent < 0 && nSent != DAV1D_ERR(EAGAIN)) || p->pending.sz == 0) {
          dav1d_data_unref(&p->pending);
          p->bPending = false;
        }
      } else {
        ++nWaits;
      }
    }
    return;
  }
  const VideoPiece &f = p->index.frames[p->nNext++];
  if (p->bAlpha) {
    p->pAlphaImage = NULL;
    if (f.alphaLen &&
        vpx_codec_decode(&p->alpha, d + f.alphaPos, static_cast<unsigned>(f.alphaLen), NULL, 0) == VPX_CODEC_OK) {
      vpx_codec_iter_t it = NULL;
      vpx_image_t *img;
      while ((img = vpx_codec_get_frame(&p->alpha, &it)) != NULL)
        p->pAlphaImage = img;
    }
  }
  if (vpx_codec_decode(&p->codec, d + f.pos, static_cast<unsigned>(f.len), NULL, 0) != VPX_CODEC_OK)
    return;
  vpx_codec_iter_t it = NULL;
  vpx_image_t *img;
  while ((img = vpx_codec_get_frame(&p->codec, &it)) != NULL) {
    p->pImage = img;
    p->bHave = true;
  }
}

// ---------------------------------------------------------------------------
// Pictures.

namespace {

enum Matrix { MATRIX_601, MATRIX_709, MATRIX_2020, MATRIX_RGB };

// A decoded picture's planes, whichever decoder gave them.
struct Planes {
  const unsigned char *apPlane[3];
  ptrdiff_t anStride[3];
  int nWidth, nHeight;
  int xs, ys;  // the chroma's shifts
  int nBits;  // 8, or 10 to 16 in 16-bit samples, the value in the low bits
  bool bMono;
  bool bPaired;  // the chroma's two in one plane, U first, apPlane[2] at its V
  Matrix matrix;
  bool bFull;
  const unsigned char *pAlpha;  // 8-bit, or NULL
  ptrdiff_t nAlphaStride;
  int nAlphaWidth, nAlphaHeight;
};

unsigned char Clamp(int v) { return static_cast<unsigned char>(v < 0 ? 0 : v > 255 ? 255 : v); }

// Y'CbCr to R'G'B', fixed point of 16 bits: a matrix's chroma factors, for limited range (scaled
// by 255/224) and full
const int kFactors[3][2][4] = {
    {{104597, 25675, 53279, 132201}, {91881, 22554, 46802, 116130}},   // BT.601
    {{117489, 13975, 34925, 138438}, {103206, 12276, 30679, 121608}},  // BT.709
    {{110013, 12276, 42626, 140363}, {96639, 10784, 37444, 123299}},   // BT.2020
};

// Rows [y0, y1) of the picture; T is the sample's type, 16-bit past 8 bits.
template <class T, bool bRGB>
void RowsToRGBA(const Planes &s, unsigned char *pOut, int nPitch, int w, int y0, int y1)
{
  const int nShift = s.nBits - 8;
  const int nStep = s.bPaired ? 2 : 1;
  const int *k = bRGB ? NULL : kFactors[s.matrix][s.bFull ? 1 : 0];
  for (int y = y0; y < y1; ++y) {
    const T *pY = reinterpret_cast<const T *>(s.apPlane[0] + y * s.anStride[0]);
    const T *pU = s.bMono ? NULL : reinterpret_cast<const T *>(s.apPlane[1] + (y >> s.ys) * s.anStride[1]);
    const T *pV = s.bMono ? NULL : reinterpret_cast<const T *>(s.apPlane[2] + (y >> s.ys) * s.anStride[2]);
    const unsigned char *pA = s.pAlpha && y < s.nAlphaHeight ? s.pAlpha + y * s.nAlphaStride : NULL;
    const int wA = pA ? std::min(w, s.nAlphaWidth) : 0;
    unsigned char *o = pOut + y * nPitch;
    for (int x = 0; x < w; ++x, o += 4) {
      const int Y = pY[x] >> nShift;
      const int c0 = (x >> s.xs) * nStep;
      const int U = pU ? pU[c0] >> nShift : 128, V = pV ? pV[c0] >> nShift : 128;
      if (bRGB) {
        o[0] = static_cast<unsigned char>(V);
        o[1] = static_cast<unsigned char>(Y);
        o[2] = static_cast<unsigned char>(U);
      } else {
        const int c = s.bFull ? (Y << 16) : (Y - 16) * 76309;
        const int d = U - 128, e = V - 128;
        o[0] = Clamp((c + k[0] * e + 32768) >> 16);
        o[1] = Clamp((c - k[1] * d - k[2] * e + 32768) >> 16);
        o[2] = Clamp((c + k[3] * d + 32768) >> 16);
      }
      o[3] = 255;
    }
    for (int x = 0; x < wA; ++x)
      pOut[y * nPitch + 4 * x + 3] = pA[x];  // the alpha picture's luma, as it was written
  }
}

void RowsToRGBAOf(const Planes &s, unsigned char *pOut, int nPitch, int w, int y0, int y1)
{
  const bool bRGB = s.matrix == MATRIX_RGB;
  if (s.nBits == 8)
    (bRGB ? RowsToRGBA<uint8_t, true> : RowsToRGBA<uint8_t, false>)(s, pOut, nPitch, w, y0, y1);
  else
    (bRGB ? RowsToRGBA<uint16_t, true> : RowsToRGBA<uint16_t, false>)(s, pOut, nPitch, w, y0, y1);
}

// A large picture is shared among threads of their own, a band of rows each.
void PlanesToRGBA(const Planes &s, unsigned char *pOut, int nPitch, int nWidth, int nHeight)
{
  const int w = std::min(s.nWidth, nWidth), h = std::min(s.nHeight, nHeight);
  const int nBands = w * h >= 1920 * 1080 ? static_cast<int>(std::min(8u, std::max(1u, std::thread::hardware_concurrency())))
                                          : 1;
  if (nBands <= 1) {
    RowsToRGBAOf(s, pOut, nPitch, w, 0, h);
    return;
  }
  std::vector<std::thread> threads;
  for (int b = 1; b < nBands; ++b)
    threads.emplace_back(RowsToRGBAOf, std::cref(s), pOut, nPitch, w, h * b / nBands, h * (b + 1) / nBands);
  RowsToRGBAOf(s, pOut, nPitch, w, 0, h / nBands);
  for (std::thread &th : threads)
    th.join();
}

Matrix MatrixOfVpx(vpx_color_space_t cs)
{
  return cs == VPX_CS_SRGB ? MATRIX_RGB : cs == VPX_CS_BT_709 ? MATRIX_709 : cs == VPX_CS_BT_2020 ? MATRIX_2020
                                                                                                  : MATRIX_601;
}

// a matrix the video does not name is taken as its size's usual one
Matrix MatrixOfAv1(int mc, int nHeight)
{
  return mc == DAV1D_MC_IDENTITY                                 ? MATRIX_RGB
         : mc == DAV1D_MC_BT709                                  ? MATRIX_709
         : mc == DAV1D_MC_BT2020_NCL || mc == DAV1D_MC_BT2020_CL ? MATRIX_2020
         : mc == DAV1D_MC_UNKNOWN                                ? (nHeight > 576 ? MATRIX_709 : MATRIX_601)
                                                                 : MATRIX_601;
}

}  // namespace

// ---------------------------------------------------------------------------
// The video.

void PortVideoClose(PortVideo *p)
{
  if (!p)
    return;
  DecoderStop(p);
  SysVideoClose(p->pSys);
  delete p;
}

static const char *CodecName(VideoCodec c)
{
  return c == VIDEO_CODEC_VP8 ? "VP8" : c == VIDEO_CODEC_VP9 ? "VP9" : c == VIDEO_CODEC_AV1 ? "AV1"
       : c == VIDEO_CODEC_H264 ? "H.264" : c == VIDEO_CODEC_H265 ? "H.265" : "none";
}

bool PortVideoProbe(const void *pData, size_t cb, PortVideoInfo *pOut)
{
  VideoIndex x;
  const unsigned char *d = static_cast<const unsigned char *>(pData);
  if (!pData || cb < 16 || (!VideoIndexMatroska(d, cb, &x) && !VideoIndexMp4(d, cb, &(x = VideoIndex()))))
    return false;
  pOut->pszVideo = CodecName(x.video);
  pOut->pszSound = x.sound == SOUND_CODEC_VORBIS ? "Vorbis" : x.sound == SOUND_CODEC_OPUS ? "Opus"
                 : x.sound == SOUND_CODEC_AAC    ? "AAC"
                                                 : "";
  pOut->nWidth = x.nWidth;
  pOut->nHeight = x.nHeight;
  pOut->nFrames = static_cast<unsigned>(x.frames.size());
  return true;
}

PortVideo *PortVideoOpen(const void *pData, size_t cb)
{
  if (!pData || cb < 16)
    return NULL;
  PortVideo *p = new PortVideo();
  const unsigned char *d = static_cast<const unsigned char *>(pData);
  VideoIndex &x = p->index;
  if (!VideoIndexMatroska(d, cb, &x) && !VideoIndexMp4(d, cb, &(x = VideoIndex()))) {
    delete p;
    return NULL;
  }
  if (x.nWidth <= 0 || x.nHeight <= 0 || x.nWidth > 8192 || x.nHeight > 8192) {
    Log("video: a %s video of %d x %d is not played", CodecName(x.video), x.nWidth, x.nHeight);
    delete p;
    return NULL;
  }
  p->data.assign(d, d + cb);
  if (x.video == VIDEO_CODEC_H264 || x.video == VIDEO_CODEC_H265) {
    if (!SysVideoSetupOf(x.video, x.videoSetup, &p->sysSetup)) {
      Log("video: a %s video is not played: its parameter sets are not readable", CodecName(x.video));
      delete p;
      return NULL;
    }
    if (p->sysSetup.sets.empty())
      for (const VideoPiece &f : x.frames)
        if (f.bKey) {
          SetsFromFrame(&p->sysSetup, p->data.data() + f.pos, static_cast<size_t>(f.len));
          break;
        }
    SysRanks(p);
  }
  if (!DecoderStart(p)) {
    const char *pszNeeds =
        x.video == VIDEO_CODEC_H264 || x.video == VIDEO_CODEC_H265 ? SysVideoNeeds(x.video) : NULL;
    Log("video: a %s video is not played: no decoder for it here%s%s", CodecName(x.video),
        pszNeeds ? "; it needs " : "", pszNeeds ? pszNeeds : "");
    PortVideoClose(p);
    return NULL;
  }
  // a frame's length from the frames' times, else the track's, else a fifteenth of a second
  const size_t n = x.frames.size();
  long long nFirst = x.frames[0].ns, nLast = x.frames[0].ns;
  for (const VideoPiece &f : x.frames) {
    nFirst = std::min(nFirst, f.ns);
    nLast = std::max(nLast, f.ns);
  }
  long long ns = n > 1 ? (nLast - nFirst) / static_cast<long long>(n - 1) : 0;
  if (ns <= 0)
    ns = x.nFrameNs;
  if (ns <= 0)
    ns = 1000000000LL / 15;
  p->nInterval = static_cast<unsigned>(ns / 10000);
  if (x.sound == SOUND_CODEC_VORBIS)
    SoundVorbis(p);
  else if (x.sound == SOUND_CODEC_OPUS)
    SoundOpus(p);
  else if (x.sound == SOUND_CODEC_AAC)
    SoundAac(p);
  SoundToStereo(p, x.sound == SOUND_CODEC_AAC);
  return p;
}

int PortVideoWidth(const PortVideo *p) { return p->index.nWidth; }
int PortVideoHeight(const PortVideo *p) { return p->index.nHeight; }
unsigned PortVideoFrames(const PortVideo *p) { return static_cast<unsigned>(p->index.frames.size()); }
unsigned PortVideoInterval(const PortVideo *p) { return p->nInterval; }
bool PortVideoKeyFrame(const PortVideo *p, unsigned n)
{
  return n < p->index.frames.size() && p->index.frames[p->byRank.empty() ? n : p->byRank[n]].bKey;
}

const short *PortVideoSound(const PortVideo *p, size_t *pnSamples, int *pnRate, int *pnChannels)
{
  *pnSamples = p->nChannels ? p->sound.size() / p->nChannels : 0;
  *pnRate = p->nRate;
  *pnChannels = p->nChannels;
  return p->sound.empty() ? NULL : p->sound.data();
}

// The picture of rank n from the system's decoder: frames are given until it comes out, or the
// next shown when it was not decoded. Going back starts again at the key frame before its frame.
static bool SysFrame(PortVideo *p, unsigned n)
{
  const std::vector<VideoPiece> &frames = p->index.frames;
  if (p->bSysPicture && p->sysPicture.nRank == n)
    return true;
  const unsigned nFrame = p->byRank[n];
  if (!p->pSys || nFrame < p->nSysKey || (p->bSysPicture ? p->sysPicture.nRank > n : p->nNext > p->nSysKey)) {
    unsigned nKey = nFrame;
    while (nKey > 0 && !frames[nKey].bKey)
      --nKey;
    if (!DecoderStart(p))
      return false;
    p->nNext = p->nSysKey = nKey;
  }
  const unsigned char *d = p->data.data();
  for (;;) {
    SysPicture pic;
    if (SysVideoPicture(p->pSys, &pic)) {
      p->sysPicture = pic;
      p->bSysPicture = true;
      if (pic.nRank >= n)
        return true;
      continue;
    }
    p->bSysPicture = false;
    if (p->nNext < frames.size()) {
      const VideoPiece &f = frames[p->nNext];
      SysVideoSend(p->pSys, d + f.pos, static_cast<size_t>(f.len), f.bKey, p->ranks[p->nNext], p->shown[p->nNext]);
      ++p->nNext;
    } else if (!p->bSysDrained) {
      SysVideoDrain(p->pSys);
      p->bSysDrained = true;
    } else {
      return false;
    }
  }
}

// Frame n's planes, decoded.
static bool FramePlanes(PortVideo *p, unsigned n, Planes &s)
{
  const std::vector<VideoPiece> &frames = p->index.frames;
  if (n >= frames.size())
    return false;
  memset(&s, 0, sizeof s);
  if (p->index.video == VIDEO_CODEC_H264 || p->index.video == VIDEO_CODEC_H265) {
    if (!SysFrame(p, n))
      return false;
    const SysPicture &pic = p->sysPicture;
    for (int k = 0; k < 3; ++k) {
      s.apPlane[k] = pic.apPlane[k];
      s.anStride[k] = pic.anStride[k];
    }
    s.nWidth = pic.nWidth;
    s.nHeight = pic.nHeight;
    s.xs = pic.xs;
    s.ys = pic.ys;
    s.nBits = pic.nBits;
    s.bPaired = pic.bPaired;
    s.matrix = pic.nMatrix == 2   ? MATRIX_709
               : pic.nMatrix == 3 ? MATRIX_2020
               : pic.nMatrix == 1 ? MATRIX_601
                                  : (pic.nHeight > 576 ? MATRIX_709 : MATRIX_601);
    s.bFull = pic.nRange == 2;
    return s.nBits >= 8 && s.nBits <= 16;
  }
  if (n < p->nNext - (p->bHave ? 1 : 0) || !DecoderRunning(p)) {
    unsigned nKey = n;
    while (nKey > 0 && !frames[nKey].bKey)
      --nKey;
    if (!DecoderStart(p))
      return false;
    p->nNext = nKey;
  }
  while (p->nNext <= n)
    DecodeNext(p);
  if (p->pDav1d) {
    if (!p->bPicture)
      return false;
    const Dav1dPicture &pic = p->picture;
    s.apPlane[0] = static_cast<const unsigned char *>(pic.data[0]);
    s.apPlane[1] = static_cast<const unsigned char *>(pic.data[1]);
    s.apPlane[2] = static_cast<const unsigned char *>(pic.data[2]);
    s.anStride[0] = pic.stride[0];
    s.anStride[1] = s.anStride[2] = pic.stride[1];
    s.nWidth = pic.p.w;
    s.nHeight = pic.p.h;
    s.bMono = pic.p.layout == DAV1D_PIXEL_LAYOUT_I400;
    s.xs = pic.p.layout == DAV1D_PIXEL_LAYOUT_I444 ? 0 : 1;
    s.ys = pic.p.layout == DAV1D_PIXEL_LAYOUT_I420 ? 1 : 0;
    s.nBits = pic.p.bpc;
    s.matrix = pic.seq_hdr ? MatrixOfAv1(pic.seq_hdr->mtrx, pic.p.h) : MATRIX_601;
    s.bFull = pic.seq_hdr && pic.seq_hdr->color_range;
  } else {
    const vpx_image_t *img = p->pImage;
    if (!img || (img->bit_depth != 8 && !(img->fmt & VPX_IMG_FMT_HIGHBITDEPTH)))
      return false;
    for (int k = 0; k < 3; ++k) {
      s.apPlane[k] = img->planes[k];
      s.anStride[k] = img->stride[k];
    }
    s.nWidth = static_cast<int>(img->d_w);
    s.nHeight = static_cast<int>(img->d_h);
    s.xs = img->x_chroma_shift;
    s.ys = img->y_chroma_shift;
    s.nBits = img->fmt & VPX_IMG_FMT_HIGHBITDEPTH ? static_cast<int>(img->bit_depth) : 8;
    s.matrix = MatrixOfVpx(img->cs);
    s.bFull = img->range == VPX_CR_FULL_RANGE;
    if (const vpx_image_t *a = p->pAlphaImage) {
      s.pAlpha = a->planes[0];
      s.nAlphaStride = a->stride[0];
      s.nAlphaWidth = static_cast<int>(a->d_w);
      s.nAlphaHeight = static_cast<int>(a->d_h);
    }
  }
  return s.nBits >= 8 && s.nBits <= 16;
}

bool PortVideoFrame(PortVideo *p, unsigned n, unsigned char *pRGBA, int nPitch)
{
  Planes s;
  if (!FramePlanes(p, n, s))
    return false;
  PlanesToRGBA(s, pRGBA, nPitch, p->index.nWidth, p->index.nHeight);
  return true;
}

bool PortVideoPicture(PortVideo *p, unsigned n, PortVideoPlanes *pOut)
{
  Planes s;
  if (!FramePlanes(p, n, s))
    return false;
  for (int k = 0; k < 3; ++k) {
    pOut->apPlane[k] = s.apPlane[k];
    pOut->anStride[k] = s.anStride[k];
  }
  pOut->nWidth = std::min(s.nWidth, p->index.nWidth);
  pOut->nHeight = std::min(s.nHeight, p->index.nHeight);
  pOut->xs = s.xs;
  pOut->ys = s.ys;
  pOut->nBits = s.nBits;
  pOut->bMono = s.bMono;
  pOut->bPaired = s.bPaired;
  pOut->nMatrix = s.matrix;
  pOut->bFull = s.bFull;
  return true;
}

#endif
