// port_webm.cpp -- WebM and Matroska files, as a StarCraft: Remastered install keeps the
// cinematics and the units' portraits, and as a mod may give its videos: the file's parts found
// by libwebm's mkvparser (tools/fetch_libvpx.py) for port_video.cpp to decode.
//
// The SD menus' animations are VP8 with their shape beside: each frame's block carries, as its
// additional data of ID 1, a second VP8 picture whose luma is the alpha. mkvparser does not read
// a block's additions, so they are found after the block (WebmAlpha).
#include <string.h>
#include "port_videoindex.h"

#if defined(__EMSCRIPTEN__)

bool VideoIndexMatroska(const unsigned char *, size_t, VideoIndex *) { return false; }

#else

#include "mkvparser/mkvparser.h"

namespace {

// The file's bytes, which the parser reads where it likes.
class WebmReader : public mkvparser::IMkvReader {
 public:
  WebmReader(const unsigned char *p, long long n) : m_p(p), m_n(n) {}
  virtual ~WebmReader() {}
  int Read(long long pos, long len, unsigned char *buf) override
  {
    if (pos < 0 || len < 0 || pos + len > m_n)
      return -1;
    memcpy(buf, m_p + pos, static_cast<size_t>(len));
    return 0;
  }
  int Length(long long *total, long long *available) override
  {
    if (total)
      *total = m_n;
    if (available)
      *available = m_n;
    return 0;
  }

 private:
  const unsigned char *m_p;
  long long m_n;
};

}  // namespace

// An EBML element's ID (with its length marker) or size (without), at *pPos before pEnd.
static bool WebmVint(const unsigned char *pData, long long *pPos, long long nEnd, bool bId, unsigned long long *pValue)
{
  if (*pPos >= nEnd)
    return false;
  const unsigned char b = pData[*pPos];
  int nLen = 1;
  while (nLen <= 8 && !(b & (0x80 >> (nLen - 1))))
    ++nLen;
  if (nLen > (bId ? 4 : 8) || *pPos + nLen > nEnd)
    return false;
  unsigned long long v = bId ? b : b & (0xFF >> nLen);
  for (int i = 1; i < nLen; ++i)
    v = v << 8 | pData[*pPos + i];
  *pPos += nLen;
  *pValue = v;
  return true;
}

// The alpha picture of the block group whose block ends at nPos: its BlockAdditions, among the
// group's other elements after the block, hold a BlockMore of ID 1.
static void WebmAlpha(const unsigned char *d, long long nEnd, long long nPos, VideoPiece *pPiece)
{
  for (;;) {
    unsigned long long id, cb;
    if (!WebmVint(d, &nPos, nEnd, true, &id) || !WebmVint(d, &nPos, nEnd, false, &cb) ||
        cb > static_cast<unsigned long long>(nEnd - nPos))
      return;
    const long long nNext = nPos + static_cast<long long>(cb);
    // BlockAdditions; ReferenceBlock, BlockDuration, DiscardPadding, ReferencePriority, CodecState
    if (id == 0x75A1) {
      while (nPos < nNext) {
        unsigned long long idMore, cbMore;
        if (!WebmVint(d, &nPos, nNext, true, &idMore) || !WebmVint(d, &nPos, nNext, false, &cbMore) ||
            cbMore > static_cast<unsigned long long>(nNext - nPos))
          return;
        const long long nMoreEnd = nPos + static_cast<long long>(cbMore);
        if (idMore == 0xA6) {
          unsigned long long nAddId = 1;
          long long nData = -1, cbData = 0;
          while (nPos < nMoreEnd) {
            unsigned long long idIn, cbIn;
            if (!WebmVint(d, &nPos, nMoreEnd, true, &idIn) || !WebmVint(d, &nPos, nMoreEnd, false, &cbIn) ||
                cbIn > static_cast<unsigned long long>(nMoreEnd - nPos))
              return;
            if (idIn == 0xEE && cbIn <= 8) {
              nAddId = 0;
              for (unsigned long long i = 0; i < cbIn; ++i)
                nAddId = nAddId << 8 | d[nPos + i];
            } else if (idIn == 0xA5) {
              nData = nPos;
              cbData = static_cast<long long>(cbIn);
            }
            nPos += static_cast<long long>(cbIn);
          }
          if (nAddId == 1 && nData >= 0 && cbData > 0) {
            pPiece->alphaPos = nData;
            pPiece->alphaLen = static_cast<long>(cbData);
            return;
          }
        }
        nPos = nMoreEnd;
      }
    } else if (id != 0xFB && id != 0x9B && id != 0x75A2 && id != 0xFA && id != 0xA4) {
      return;
    }
    nPos = nNext;
  }
}

static VideoCodec MatroskaVideoCodec(const char *psz)
{
  return strcmp(psz, "V_VP8") == 0             ? VIDEO_CODEC_VP8
         : strcmp(psz, "V_VP9") == 0           ? VIDEO_CODEC_VP9
         : strcmp(psz, "V_AV1") == 0           ? VIDEO_CODEC_AV1
         : strcmp(psz, "V_MPEG4/ISO/AVC") == 0 ? VIDEO_CODEC_H264
         : strcmp(psz, "V_MPEGH/ISO/HEVC") == 0 ? VIDEO_CODEC_H265
                                                : VIDEO_CODEC_NONE;
}

static SoundCodec MatroskaSoundCodec(const char *psz)
{
  return strcmp(psz, "A_VORBIS") == 0       ? SOUND_CODEC_VORBIS
         : strcmp(psz, "A_OPUS") == 0       ? SOUND_CODEC_OPUS
         : strncmp(psz, "A_AAC", 5) == 0    ? SOUND_CODEC_AAC
                                            : SOUND_CODEC_NONE;
}

bool VideoIndexMatroska(const unsigned char *pData, size_t cb, VideoIndex *x)
{
  if (!pData || cb < 4 || memcmp(pData, "\x1A\x45\xDF\xA3", 4) != 0)
    return false;
  WebmReader reader(pData, static_cast<long long>(cb));
  mkvparser::EBMLHeader header;
  long long pos = 0;
  mkvparser::Segment *pSegment = NULL;
  if (header.Parse(&reader, pos) < 0 || mkvparser::Segment::CreateInstance(&reader, pos, pSegment) != 0 ||
      !pSegment || pSegment->Load() < 0) {
    delete pSegment;
    return false;
  }
  const mkvparser::Tracks *pTracks = pSegment->GetTracks();
  long long nVideo = -1, nAudio = -1;
  for (unsigned long i = 0; pTracks && i < pTracks->GetTracksCount(); ++i) {
    const mkvparser::Track *t = pTracks->GetTrackByIndex(i);
    if (!t || !t->GetCodecId())
      continue;
    size_t cbSetup = 0;
    const unsigned char *pSetup = t->GetCodecPrivate(cbSetup);
    if (t->GetType() == mkvparser::Track::kVideo && nVideo < 0 && MatroskaVideoCodec(t->GetCodecId())) {
      const mkvparser::VideoTrack *v = static_cast<const mkvparser::VideoTrack *>(t);
      nVideo = t->GetNumber();
      x->video = MatroskaVideoCodec(t->GetCodecId());
      x->nWidth = static_cast<int>(v->GetWidth());
      x->nHeight = static_cast<int>(v->GetHeight());
      x->nFrameNs = static_cast<long long>(t->GetDefaultDuration());
      if (pSetup)
        x->videoSetup.assign(pSetup, pSetup + cbSetup);
    } else if (t->GetType() == mkvparser::Track::kAudio && nAudio < 0 && MatroskaSoundCodec(t->GetCodecId())) {
      const mkvparser::AudioTrack *a = static_cast<const mkvparser::AudioTrack *>(t);
      nAudio = t->GetNumber();
      x->sound = MatroskaSoundCodec(t->GetCodecId());
      x->nSoundRate = static_cast<int>(a->GetSamplingRate());
      x->nSoundChannels = static_cast<int>(a->GetChannels());
      x->nSoundSkipNs = static_cast<long long>(t->GetCodecDelay());
      if (pSetup)
        x->soundSetup.assign(pSetup, pSetup + cbSetup);
    }
  }
  if (nVideo >= 0) {
    for (const mkvparser::Cluster *c = pSegment->GetFirst(); c && !c->EOS(); c = pSegment->GetNext(c)) {
      const mkvparser::BlockEntry *e = NULL;
      for (long st = c->GetFirst(e); st == 0 && e && !e->EOS(); st = c->GetNext(e, e)) {
        const mkvparser::Block *b = e->GetBlock();
        if (!b)
          break;
        for (int i = 0; i < b->GetFrameCount(); ++i) {
          const mkvparser::Block::Frame &f = b->GetFrame(i);
          if (b->GetTrackNumber() == nVideo) {
            x->frames.push_back({f.pos, f.len, b->GetTime(c), b->IsKey() && i == 0, 0, 0});
            if (e->GetKind() == mkvparser::BlockEntry::kBlockGroup && b->GetFrameCount() == 1)
              WebmAlpha(pData, static_cast<long long>(cb), b->m_start + b->m_size, &x->frames.back());
          } else if (b->GetTrackNumber() == nAudio) {
            x->packets.push_back({f.pos, f.len});
          }
        }
      }
    }
  }
  delete pSegment;
  return nVideo >= 0 && !x->frames.empty();
}

#endif
