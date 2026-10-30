// port_mp4.cpp -- MP4 and QuickTime files (ISO/IEC 14496-12), for port_video.cpp: the first
// picture track and the first sound track whose codecs it decodes, each sample's place found from
// the sample tables. A fragmented file (its samples in moof boxes) is not read.
#include <string.h>
#include <algorithm>
#include "port_videoindex.h"

namespace {

struct Box {
  const unsigned char *p;  // the box's contents, after its header
  size_t n;
  unsigned type;
};

unsigned Be16(const unsigned char *p) { return static_cast<unsigned>(p[0]) << 8 | p[1]; }
unsigned Be32(const unsigned char *p) { return Be16(p) << 16 | Be16(p + 2); }
unsigned long long Be64(const unsigned char *p) { return static_cast<unsigned long long>(Be32(p)) << 32 | Be32(p + 4); }
constexpr unsigned Fourcc(const char *s)
{
  return static_cast<unsigned>(static_cast<unsigned char>(s[0])) << 24 |
         static_cast<unsigned>(static_cast<unsigned char>(s[1])) << 16 |
         static_cast<unsigned>(static_cast<unsigned char>(s[2])) << 8 | static_cast<unsigned char>(s[3]);
}

// The boxes in [p, p + n), in order; false at a box that runs past the end.
template <class F>
bool EachBox(const unsigned char *p, size_t n, F f)
{
  size_t i = 0;
  while (i + 8 <= n) {
    unsigned long long cb = Be32(p + i);
    const unsigned type = Be32(p + i + 4);
    size_t nHead = 8;
    if (cb == 1) {
      if (i + 16 > n)
        return false;
      cb = Be64(p + i + 8);
      nHead = 16;
    } else if (cb == 0) {
      cb = n - i;
    }
    if (cb < nHead || cb > n - i)
      return false;
    if (!f(Box{p + i + nHead, static_cast<size_t>(cb) - nHead, type}))
      return true;
    i += static_cast<size_t>(cb);
  }
  return true;
}

// The first child of a type, or an empty box.
Box Child(const Box &b, unsigned type, size_t nSkip = 0)
{
  Box found{NULL, 0, 0};
  if (b.n >= nSkip)
    EachBox(b.p + nSkip, b.n - nSkip, [&](const Box &c) {
      if (c.type != type)
        return true;
      found = c;
      return false;
    });
  return found;
}

struct Track {
  unsigned handler = 0;  // 'vide' or 'soun'
  unsigned timescale = 0;
  VideoCodec video = VIDEO_CODEC_NONE;
  SoundCodec sound = SOUND_CODEC_NONE;
  int nWidth = 0, nHeight = 0, nRate = 0, nChannels = 0;
  std::vector<unsigned char> setup;
  long long nSkip = 0;  // where the edit list starts the track, in the timescale
  struct Sample {
    unsigned long long pos;
    unsigned len;
    long long time;  // when it is shown, in the timescale
    bool bKey;
  };
  std::vector<Sample> samples;
};

// The Opus sample entry's dOps (big-endian) as the OpusHead a Matroska file keeps (little-endian).
void OpusHeadOf(const Box &dops, std::vector<unsigned char> *pOut)
{
  if (dops.n < 11)
    return;
  const unsigned char *d = dops.p;
  static const char kHead[8] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};
  pOut->assign(kHead, kHead + 8);
  pOut->push_back(1);
  pOut->push_back(d[1]);  // channels
  const unsigned nPreSkip = Be16(d + 2), nRate = Be32(d + 4), nGain = Be16(d + 8);
  pOut->push_back(static_cast<unsigned char>(nPreSkip));
  pOut->push_back(static_cast<unsigned char>(nPreSkip >> 8));
  for (int k = 0; k < 4; ++k)
    pOut->push_back(static_cast<unsigned char>(nRate >> (8 * k)));
  pOut->push_back(static_cast<unsigned char>(nGain));
  pOut->push_back(static_cast<unsigned char>(nGain >> 8));
  pOut->insert(pOut->end(), d + 10, d + dops.n);  // the mapping family and its table
}

// AAC's AudioSpecificConfig, the DecoderSpecificInfo inside an esds's descriptors.
void AacConfigOf(const Box &esds, std::vector<unsigned char> *pOut)
{
  if (esds.n < 4)
    return;
  const unsigned char *d = esds.p + 4, *pEnd = esds.p + esds.n;
  while (d + 2 <= pEnd) {
    const unsigned char tag = *d++;
    size_t cb = 0;
    for (int k = 0; k < 4 && d < pEnd; ++k) {
      const unsigned char b = *d++;
      cb = cb << 7 | (b & 0x7F);
      if (!(b & 0x80))
        break;
    }
    if (tag == 3) {  // ES_Descriptor: ES_ID, flags and what they add, then its descriptors
      if (d + 3 > pEnd)
        return;
      const unsigned char flags = d[2];
      d += 3;
      if (flags & 0x80)
        d += 2;
      if ((flags & 0x40) && d < pEnd)
        d += 1 + *d;
      if (flags & 0x20)
        d += 2;
    } else if (tag == 4) {  // DecoderConfigDescriptor: 13 bytes, then its descriptors
      d += 13;
    } else if (tag == 5) {
      if (d + cb <= pEnd)
        pOut->assign(d, d + cb);
      return;
    } else {
      d += cb;
    }
  }
}

void ReadSampleEntry(const Box &stsd, Track *t)
{
  if (stsd.n < 8 || Be32(stsd.p + 4) == 0)
    return;
  EachBox(stsd.p + 8, stsd.n - 8, [&](const Box &e) {
    if (t->handler == Fourcc("vide") && e.n >= 78) {
      const unsigned f = e.type;
      t->video = f == Fourcc("vp08")                             ? VIDEO_CODEC_VP8
                 : f == Fourcc("vp09")                           ? VIDEO_CODEC_VP9
                 : f == Fourcc("av01")                           ? VIDEO_CODEC_AV1
                 : f == Fourcc("avc1") || f == Fourcc("avc3")    ? VIDEO_CODEC_H264
                 : f == Fourcc("hvc1") || f == Fourcc("hev1")    ? VIDEO_CODEC_H265
                                                                 : VIDEO_CODEC_NONE;
      t->nWidth = static_cast<int>(Be16(e.p + 24));
      t->nHeight = static_cast<int>(Be16(e.p + 26));
      const Box box{e.p, e.n, 0};
      const Box c = t->video == VIDEO_CODEC_AV1    ? Child(box, Fourcc("av1C"), 78)
                    : t->video == VIDEO_CODEC_H264 ? Child(box, Fourcc("avcC"), 78)
                    : t->video == VIDEO_CODEC_H265 ? Child(box, Fourcc("hvcC"), 78)
                                                   : Box{NULL, 0, 0};
      if (c.p)
        t->setup.assign(c.p, c.p + c.n);
    } else if (t->handler == Fourcc("soun") && e.n >= 28) {
      // the sound entry: 8 bytes, then version, revision, vendor, channels, sample size,
      // compression ID, packet size and the rate in 16.16; QuickTime's versions 1 and 2 add more
      const unsigned nVersion = Be16(e.p + 8);
      size_t nSkip = nVersion == 1 ? 44 : nVersion == 2 ? 64 : 28;
      t->nChannels = static_cast<int>(Be16(e.p + 16));
      t->nRate = static_cast<int>(Be32(e.p + 24) >> 16);
      const Box box{e.p, e.n, 0};
      if (e.type == Fourcc("Opus")) {
        t->sound = SOUND_CODEC_OPUS;
        OpusHeadOf(Child(box, Fourcc("dOps"), nSkip), &t->setup);
      } else if (e.type == Fourcc("mp4a")) {
        t->sound = SOUND_CODEC_AAC;
        Box esds = Child(box, Fourcc("esds"), nSkip);
        if (!esds.p) {
          const Box wave = Child(box, Fourcc("wave"), nSkip);
          if (wave.p)
            esds = Child(wave, Fourcc("esds"));
        }
        AacConfigOf(esds, &t->setup);
      }
      if (t->setup.empty())
        t->sound = SOUND_CODEC_NONE;
    }
    return false;  // the first entry only
  });
}

// Each sample's place, length, time and whether it is a key, from the sample tables.
bool ReadSamples(const Box &stbl, Track *t)
{
  const Box stsz = Child(stbl, Fourcc("stsz")), stz2 = Child(stbl, Fourcc("stz2"));
  const Box stco = Child(stbl, Fourcc("stco")), co64 = Child(stbl, Fourcc("co64"));
  const Box stsc = Child(stbl, Fourcc("stsc")), stts = Child(stbl, Fourcc("stts"));
  const Box ctts = Child(stbl, Fourcc("ctts")), stss = Child(stbl, Fourcc("stss"));
  // sizes
  std::vector<unsigned> sizes;
  if (stsz.p && stsz.n >= 12) {
    const unsigned nSize = Be32(stsz.p + 4), nCount = Be32(stsz.p + 8);
    if (nSize == 0 && 12 + static_cast<size_t>(nCount) * 4 > stsz.n)
      return false;
    // one size for all: past 16 million samples (days of video) it is no file's
    if (nSize != 0 && nCount > (1u << 24))
      return false;
    for (unsigned i = 0; i < nCount; ++i)
      sizes.push_back(nSize ? nSize : Be32(stsz.p + 12 + 4 * i));
  } else if (stz2.p && stz2.n >= 12) {
    const unsigned nBits = stz2.p[7], nCount = Be32(stz2.p + 8);
    if ((nBits != 4 && nBits != 8 && nBits != 16) || 12 + (static_cast<size_t>(nCount) * nBits + 7) / 8 > stz2.n)
      return false;
    for (unsigned i = 0; i < nCount; ++i) {
      const unsigned char *q = stz2.p + 12;
      sizes.push_back(nBits == 16 ? Be16(q + 2 * i) : nBits == 8 ? q[i] : (q[i / 2] >> (i % 2 ? 0 : 4)) & 15);
    }
  } else {
    return false;
  }
  // chunks' places
  std::vector<unsigned long long> chunks;
  if (stco.p && stco.n >= 8) {
    const unsigned n = Be32(stco.p + 4);
    for (unsigned i = 0; i < n && 8 + 4 * static_cast<size_t>(i) + 4 <= stco.n; ++i)
      chunks.push_back(Be32(stco.p + 8 + 4 * i));
  } else if (co64.p && co64.n >= 8) {
    const unsigned n = Be32(co64.p + 4);
    for (unsigned i = 0; i < n && 8 + 8 * static_cast<size_t>(i) + 8 <= co64.n; ++i)
      chunks.push_back(Be64(co64.p + 8 + 8 * i));
  }
  if (chunks.empty() || !stsc.p || stsc.n < 8)
    return false;
  // samples into chunks: each stsc run holds from its first chunk to the next run's
  const unsigned nRuns = Be32(stsc.p + 4);
  if (8 + static_cast<size_t>(nRuns) * 12 > stsc.n || nRuns == 0)
    return false;
  t->samples.reserve(sizes.size());
  size_t s = 0;
  for (unsigned r = 0; r < nRuns && s < sizes.size(); ++r) {
    const unsigned nFirst = Be32(stsc.p + 8 + 12 * r), nPer = Be32(stsc.p + 12 + 12 * r);
    const unsigned nLast = r + 1 < nRuns ? Be32(stsc.p + 8 + 12 * (r + 1)) : static_cast<unsigned>(chunks.size()) + 1;
    for (unsigned c = nFirst; c < nLast && c >= 1 && c <= chunks.size() && s < sizes.size(); ++c) {
      unsigned long long pos = chunks[c - 1];
      for (unsigned k = 0; k < nPer && s < sizes.size(); ++k, ++s) {
        t->samples.push_back({pos, sizes[s], 0, stss.p == NULL});
        pos += sizes[s];
      }
    }
  }
  // times: decode times from stts, each shown its ctts offset later
  long long nTime = 0;
  size_t i = 0;
  if (stts.p && stts.n >= 8) {
    const unsigned n = Be32(stts.p + 4);
    for (unsigned e = 0; e < n && 8 + 8 * static_cast<size_t>(e) + 8 <= stts.n; ++e) {
      const unsigned nCount = Be32(stts.p + 8 + 8 * e), nDelta = Be32(stts.p + 12 + 8 * e);
      for (unsigned k = 0; k < nCount && i < t->samples.size(); ++k, ++i) {
        t->samples[i].time = nTime;
        nTime += nDelta;
      }
    }
  }
  if (ctts.p && ctts.n >= 8) {
    const unsigned n = Be32(ctts.p + 4);
    i = 0;
    for (unsigned e = 0; e < n && 8 + 8 * static_cast<size_t>(e) + 8 <= ctts.n; ++e) {
      const unsigned nCount = Be32(ctts.p + 8 + 8 * e);
      const long long nOffset = static_cast<int>(Be32(ctts.p + 12 + 8 * e));
      for (unsigned k = 0; k < nCount && i < t->samples.size(); ++k, ++i)
        t->samples[i].time += nOffset;
    }
  }
  if (stss.p && stss.n >= 8) {
    const unsigned n = Be32(stss.p + 4);
    for (unsigned e = 0; e < n && 8 + 4 * static_cast<size_t>(e) + 4 <= stss.n; ++e) {
      const unsigned k = Be32(stss.p + 8 + 4 * e);
      if (k >= 1 && k <= t->samples.size())
        t->samples[k - 1].bKey = true;
    }
  }
  return !t->samples.empty();
}

bool ReadTrack(const Box &trak, Track *t)
{
  const Box mdia = Child(trak, Fourcc("mdia"));
  if (!mdia.p)
    return false;
  const Box hdlr = Child(mdia, Fourcc("hdlr")), mdhd = Child(mdia, Fourcc("mdhd"));
  if (!hdlr.p || hdlr.n < 12 || !mdhd.p || mdhd.n < 24)
    return false;
  t->handler = Be32(hdlr.p + 8);
  t->timescale = mdhd.p[0] == 1 ? (mdhd.n >= 24 ? Be32(mdhd.p + 20) : 0) : Be32(mdhd.p + 12);
  const Box stbl = Child(Child(mdia, Fourcc("minf")), Fourcc("stbl"));
  if (!stbl.p || t->timescale == 0)
    return false;
  ReadSampleEntry(Child(stbl, Fourcc("stsd")), t);
  if (t->video == VIDEO_CODEC_NONE && t->sound == SOUND_CODEC_NONE)
    return false;
  // the edit list's first edit that is not empty
  const Box elst = Child(Child(trak, Fourcc("edts")), Fourcc("elst"));
  if (elst.p && elst.n >= 8) {
    const bool b64 = elst.p[0] == 1;
    const size_t nEntry = b64 ? 20 : 12;
    for (unsigned e = 0, n = Be32(elst.p + 4); e < n && 8 + nEntry * (e + 1) <= elst.n; ++e) {
      const unsigned char *q = elst.p + 8 + nEntry * e + (b64 ? 8 : 4);
      const long long nTime = b64 ? static_cast<long long>(static_cast<unsigned long long>(Be32(q)) << 32 | Be32(q + 4))
                                  : static_cast<int>(Be32(q));
      if (nTime >= 0) {
        t->nSkip = nTime;
        break;
      }
    }
  }
  return ReadSamples(stbl, t);
}

}  // namespace

bool VideoIndexMp4(const unsigned char *pData, size_t cb, VideoIndex *x)
{
  if (!pData || cb < 16)
    return false;
  const unsigned nFirst = Be32(pData + 4);
  if (nFirst != Fourcc("ftyp") && nFirst != Fourcc("moov") && nFirst != Fourcc("mdat") && nFirst != Fourcc("wide") &&
      nFirst != Fourcc("free") && nFirst != Fourcc("skip"))
    return false;
  Box moov{NULL, 0, 0};
  EachBox(pData, cb, [&](const Box &b) {
    if (b.type == Fourcc("moov"))
      moov = b;
    return moov.p == NULL;
  });
  if (!moov.p)
    return false;
  Track video, sound;
  EachBox(moov.p, moov.n, [&](const Box &b) {
    if (b.type != Fourcc("trak"))
      return true;
    Track t;
    if (ReadTrack(b, &t)) {
      if (t.video != VIDEO_CODEC_NONE && video.samples.empty())
        video = std::move(t);
      else if (t.sound != SOUND_CODEC_NONE && sound.samples.empty())
        sound = std::move(t);
    }
    return true;
  });
  if (video.samples.empty())
    return false;
  x->video = video.video;
  x->nWidth = video.nWidth;
  x->nHeight = video.nHeight;
  x->videoSetup = video.setup;
  const long long nScale = video.timescale;
  long long nFirstTime = video.samples[0].time;
  for (const Track::Sample &s : video.samples)
    nFirstTime = std::min(nFirstTime, s.time);
  for (const Track::Sample &s : video.samples) {
    if (s.pos > cb || s.len > cb - s.pos)  // no sum that wraps
      break;
    x->frames.push_back({static_cast<long long>(s.pos), static_cast<long>(s.len),
                         (s.time - nFirstTime) * 1000000000LL / nScale, s.bKey, 0, 0});
  }
  if (!sound.samples.empty()) {
    x->sound = sound.sound;
    x->soundSetup = sound.setup;
    x->nSoundRate = sound.nRate;
    x->nSoundChannels = sound.nChannels;
    x->nSoundSkipNs = sound.nSkip * 1000000000LL / sound.timescale;
    for (const Track::Sample &s : sound.samples)
      if (s.pos <= cb && s.len <= cb - s.pos)
        x->packets.push_back({static_cast<long long>(s.pos), static_cast<long>(s.len)});
  }
  return !x->frames.empty();
}
