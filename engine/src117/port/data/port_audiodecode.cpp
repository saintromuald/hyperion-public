// port_audiodecode.cpp -- the audio decoders (port_audiodecode.h), for a mod's sounds and
// music: dr_mp3, dr_flac and dr_wav (tools/fetch_drlibs.py)
// for MP3, FLAC and WAV, each read off the disk a piece at a time through SDL's files or out of
// memory; stb_vorbis for Ogg Vorbis, the file in memory (the WebM videos' copy, built into
// webm.lib); and libopus for Ogg Opus, the streamed seats', its pages read here. A
// browser's build has neither stb_vorbis (port_video.cpp) nor libopus, so there an Ogg file
// does not open.
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "port_audiodecode.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_WAV_NO_WCHAR
#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"

#if !defined(__EMSCRIPTEN__)
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#include <opus/opus_multistream.h>  // libopus, the streamed seats'; none in a browser
#endif

namespace {

enum Kind { KIND_NONE, KIND_MP3, KIND_OGG, KIND_FLAC, KIND_WAV, KIND_OPUS };

bool EndsWith(const char *psz, const char *pszEnd)
{
  const size_t n = strlen(psz), m = strlen(pszEnd);
  return n >= m && SDL_strcasecmp(psz + n - m, pszEnd) == 0;
}

Kind KindOfName(const char *psz)
{
  if (psz == NULL)
    return KIND_NONE;
  if (EndsWith(psz, ".mp3"))
    return KIND_MP3;
  if (EndsWith(psz, ".ogg") || EndsWith(psz, ".oga"))
    return KIND_OGG;
  if (EndsWith(psz, ".flac"))
    return KIND_FLAC;
  if (EndsWith(psz, ".wav"))
    return KIND_WAV;
  if (EndsWith(psz, ".opus"))
    return KIND_OPUS;
  return KIND_NONE;
}

// By the first bytes: a RIFF or Wave64 header, an Ogg page (Opus when its first packet is
// OpusHead), FLAC's mark, an ID3 tag or an MPEG frame's sync.
Kind KindOfBytes(const unsigned char *p, size_t n)
{
  if (n >= 4 && (memcmp(p, "RIFF", 4) == 0 || memcmp(p, "RIFX", 4) == 0 || memcmp(p, "riff", 4) == 0))
    return KIND_WAV;
  if (n >= 27 && memcmp(p, "OggS", 4) == 0) {
    const size_t nBody = 27 + static_cast<size_t>(p[26]);
    return n >= nBody + 8 && memcmp(p + nBody, "OpusHead", 8) == 0 ? KIND_OPUS : KIND_OGG;
  }
  if (n >= 4 && memcmp(p, "OggS", 4) == 0)
    return KIND_OGG;
  if (n >= 4 && memcmp(p, "fLaC", 4) == 0)
    return KIND_FLAC;
  if (n >= 3 && memcmp(p, "ID3", 3) == 0)
    return KIND_MP3;
  if (n >= 2 && p[0] == 0xFF && (p[1] & 0xE0) == 0xE0)
    return KIND_MP3;
  return KIND_NONE;
}

// ---- tags ------------------------------------------------------------------------------------

// Latin-1 or UTF-16 (with its byte order mark, or big-endian) into UTF-8.
void AppendUtf8(std::string *pOut, unsigned c)
{
  if (c < 0x80) {
    pOut->push_back(static_cast<char>(c));
  } else if (c < 0x800) {
    pOut->push_back(static_cast<char>(0xC0 | (c >> 6)));
    pOut->push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else if (c < 0x10000) {
    pOut->push_back(static_cast<char>(0xE0 | (c >> 12)));
    pOut->push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    pOut->push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else {
    pOut->push_back(static_cast<char>(0xF0 | (c >> 18)));
    pOut->push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    pOut->push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    pOut->push_back(static_cast<char>(0x80 | (c & 0x3F)));
  }
}

std::string Utf16(const unsigned char *p, size_t n, bool bBigEndian)
{
  std::string s;
  for (size_t i = 0; i + 1 < n; i += 2) {
    unsigned c = bBigEndian ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
    if (c == 0)
      break;
    if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
      const unsigned d = bBigEndian ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
      if (d >= 0xDC00 && d < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
        i += 2;
      }
    }
    AppendUtf8(&s, c);
  }
  return s;
}

std::string Latin1(const unsigned char *p, size_t n)
{
  std::string s;
  for (size_t i = 0; i < n && p[i] != 0; ++i)
    AppendUtf8(&s, p[i]);
  return s;
}

std::string Trimmed(std::string s)
{
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && s[i] == ' ')
    ++i;
  return s.substr(i);
}

// An ID3v2 text frame's body: its encoding byte, then the text.
std::string Id3Text(const unsigned char *p, size_t n)
{
  if (n < 1)
    return std::string();
  const unsigned char enc = p[0];
  ++p;
  --n;
  if (enc == 1 && n >= 2)
    return Utf16(p + 2, n - 2, p[0] == 0xFE && p[1] == 0xFF);
  if (enc == 2)
    return Utf16(p, n, true);
  if (enc == 3)
    return std::string(reinterpret_cast<const char *>(p), strnlen(reinterpret_cast<const char *>(p), n));
  return Latin1(p, n);
}

// TIT2 and TPE1 from an ID3v2.3 or 2.4 tag, its 10-byte header first.
void Id3v2(const unsigned char *p, size_t n, std::string *pTitle, std::string *pArtist)
{
  if (n < 10 || memcmp(p, "ID3", 3) != 0 || (p[3] != 3 && p[3] != 4))
    return;
  const bool b24 = p[3] == 4;
  size_t at = 10;
  if (p[5] & 0x40) {  // an extended header, passed over
    if (n < 14)
      return;
    const size_t nExt = b24 ? ((p[10] & 0x7F) << 21 | (p[11] & 0x7F) << 14 | (p[12] & 0x7F) << 7 | (p[13] & 0x7F))
                            : (static_cast<size_t>(p[10]) << 24 | p[11] << 16 | p[12] << 8 | p[13]) + 4;
    at += nExt;
  }
  while (at + 10 <= n) {
    const unsigned char *f = p + at;
    if (f[0] == 0)
      break;
    const size_t nSize = b24 ? ((f[4] & 0x7F) << 21 | (f[5] & 0x7F) << 14 | (f[6] & 0x7F) << 7 | (f[7] & 0x7F))
                             : (static_cast<size_t>(f[4]) << 24 | f[5] << 16 | f[6] << 8 | f[7]);
    if (nSize == 0 || at + 10 + nSize > n)
      break;
    if (memcmp(f, "TIT2", 4) == 0 && pTitle->empty())
      *pTitle = Trimmed(Id3Text(f + 10, nSize));
    else if (memcmp(f, "TPE1", 4) == 0 && pArtist->empty())
      *pArtist = Trimmed(Id3Text(f + 10, nSize));
    at += 10 + nSize;
  }
}

// An ID3v1 tag's title and artist, 30 bytes each.
void Id3v1(const unsigned char *p, size_t n, std::string *pTitle, std::string *pArtist)
{
  if (n < 128 || memcmp(p, "TAG", 3) != 0)
    return;
  if (pTitle->empty())
    *pTitle = Trimmed(Latin1(p + 3, 30));
  if (pArtist->empty())
    *pArtist = Trimmed(Latin1(p + 33, 30));
}

// A Vorbis comment, KEY=value, its key in any case.
void Comment(const char *psz, size_t n, std::string *pTitle, std::string *pArtist)
{
  if (n > 6 && SDL_strncasecmp(psz, "TITLE=", 6) == 0 && pTitle->empty())
    *pTitle = Trimmed(std::string(psz + 6, n - 6));
  else if (n > 7 && SDL_strncasecmp(psz, "ARTIST=", 7) == 0 && pArtist->empty())
    *pArtist = Trimmed(std::string(psz + 7, n - 7));
}

// ---- Opus in Ogg (RFC 7845) ------------------------------------------------------------------
// libopus decodes the packets; the pages are read here, the file in memory. Always 48 kHz.

#if !defined(__EMSCRIPTEN__)
struct OggOpus {
  const unsigned char *pData = NULL;
  size_t nData = 0;
  size_t nPage = 0;                 // the next page's offset
  std::vector<unsigned char> segs;  // the page's lacing values not yet read
  size_t nSeg = 0, nBody = 0;       // the next of them, and where its bytes are
  OpusMSDecoder *pDec = NULL;
  int nChannels = 0;
  int nPreSkip = 0;
  float fGain = 1.0f;
  std::vector<float> pcm;  // a packet's frames not yet read
  size_t nPcmAt = 0;
  uint64_t nDiscard = 0;   // frames still to drop: the pre-skip, or up to a seek's frame
  // where the first audio packet starts, after the two header packets
  size_t nFirstPage = 0, nFirstSeg = 0, nFirstBody = 0;
  std::vector<unsigned char> firstSegs;
};

// The page at nAt: its lacing values and where its body starts. False past the last.
bool OggPage(OggOpus *o, size_t nAt, uint64_t *pnGranule, bool *pbContinued)
{
  const unsigned char *p = o->pData + nAt;
  if (nAt + 27 > o->nData || memcmp(p, "OggS", 4) != 0)
    return false;
  const size_t nSegs = p[26];
  if (nAt + 27 + nSegs > o->nData)
    return false;
  size_t nBytes = 0;
  for (size_t i = 0; i < nSegs; ++i)
    nBytes += p[27 + i];
  if (nAt + 27 + nSegs + nBytes > o->nData)
    return false;
  uint64_t g = 0;
  for (int i = 7; i >= 0; --i)
    g = g << 8 | p[6 + i];
  *pnGranule = g;
  *pbContinued = (p[5] & 1) != 0;
  o->segs.assign(p + 27, p + 27 + nSegs);
  o->nSeg = 0;
  o->nBody = nAt + 27 + nSegs;
  o->nPage = o->nBody + nBytes;
  return true;
}

// The next whole packet, across pages.
bool OggPacket(OggOpus *o, std::vector<unsigned char> *pOut)
{
  pOut->clear();
  for (;;) {
    while (o->nSeg < o->segs.size()) {
      const size_t n = o->segs[o->nSeg++];
      pOut->insert(pOut->end(), o->pData + o->nBody, o->pData + o->nBody + n);
      o->nBody += n;
      if (n < 255)
        return true;
    }
    uint64_t g;
    bool bContinued;
    if (!OggPage(o, o->nPage, &g, &bContinued))
      return false;
  }
}

bool OpusStart(OggOpus *o, const unsigned char *pData, size_t nData, std::string *pTitle, std::string *pArtist,
               uint64_t *pnFrames)
{
  o->pData = pData;
  o->nData = nData;
  o->nPage = 0;
  o->segs.clear();
  o->nSeg = 0;
  std::vector<unsigned char> head, tags;
  if (!OggPacket(o, &head) || head.size() < 19 || memcmp(head.data(), "OpusHead", 8) != 0 || (head[8] >> 4) != 0)
    return false;
  o->nChannels = head[9];
  o->nPreSkip = head[10] | head[11] << 8;
  const short nGain = static_cast<short>(head[16] | head[17] << 8);
  o->fGain = SDL_powf(10.0f, nGain / (20.0f * 256.0f));
  // mapping family 0, one stream of one or two channels; else its table
  int nStreams = 1, nCoupled = o->nChannels > 1 ? 1 : 0;
  unsigned char abMap[255] = {0, 1};
  if (head[18] != 0) {
    if (head.size() < 21u + o->nChannels)
      return false;
    nStreams = head[19];
    nCoupled = head[20];
    memcpy(abMap, head.data() + 21, o->nChannels);
  } else if (o->nChannels < 1 || o->nChannels > 2) {
    return false;
  }
  int nError = 0;
  o->pDec = opus_multistream_decoder_create(48000, o->nChannels, nStreams, nCoupled, abMap, &nError);
  if (o->pDec == NULL)
    return false;
  // OpusTags: the vendor, then KEY=value comments
  if (OggPacket(o, &tags) && tags.size() >= 16 && memcmp(tags.data(), "OpusTags", 8) == 0) {
    auto u32 = [&](size_t at) { return at + 4 <= tags.size() ? tags[at] | tags[at + 1] << 8 | tags[at + 2] << 16 | static_cast<size_t>(tags[at + 3]) << 24 : 0; };
    size_t at = 12 + u32(8);
    const size_t nComments = u32(at);
    at += 4;
    for (size_t i = 0; i < nComments && at + 4 <= tags.size(); ++i) {
      const size_t n = u32(at);
      at += 4;
      if (at + n > tags.size())
        break;
      Comment(reinterpret_cast<const char *>(tags.data() + at), n, pTitle, pArtist);
      at += n;
    }
  }
  o->nFirstPage = o->nPage;
  o->firstSegs = o->segs;
  o->nFirstSeg = o->nSeg;
  o->nFirstBody = o->nBody;
  // the length: the last page's granule position, less the pre-skip
  *pnFrames = 0;
  for (size_t at = nData >= 27 ? nData - 27 : 0;; --at) {
    if (memcmp(pData + at, "OggS", 4) == 0) {
      uint64_t g = 0;
      for (int i = 7; i >= 0; --i)
        g = g << 8 | pData[at + 6 + i];
      *pnFrames = g > static_cast<uint64_t>(o->nPreSkip) ? g - o->nPreSkip : 0;
      break;
    }
    if (at == 0)
      break;
  }
  o->nDiscard = static_cast<uint64_t>(o->nPreSkip);
  o->pcm.clear();
  o->nPcmAt = 0;
  return true;
}

size_t OpusRead(OggOpus *o, float *pOut, size_t nFrames)
{
  const size_t nCh = static_cast<size_t>(o->nChannels);
  size_t nDone = 0;
  std::vector<unsigned char> packet;
  while (nDone < nFrames) {
    if (o->nPcmAt * nCh >= o->pcm.size()) {
      if (!OggPacket(o, &packet))
        break;
      if (packet.empty())
        continue;
      const int nMost = 5760;  // 120 ms, the longest packet
      o->pcm.resize(static_cast<size_t>(nMost) * nCh);
      const int n = opus_multistream_decode_float(o->pDec, packet.data(), static_cast<opus_int32>(packet.size()),
                                                  o->pcm.data(), nMost, 0);
      o->pcm.resize(n > 0 ? static_cast<size_t>(n) * nCh : 0);
      o->nPcmAt = 0;
      const uint64_t nDrop = SDL_min(o->nDiscard, static_cast<uint64_t>(n > 0 ? n : 0));
      o->nPcmAt = static_cast<size_t>(nDrop);
      o->nDiscard -= nDrop;
      continue;
    }
    const size_t nHave = o->pcm.size() / nCh - o->nPcmAt;
    const size_t nTake = SDL_min(nHave, nFrames - nDone);
    for (size_t i = 0; i < nTake * nCh; ++i)
      pOut[nDone * nCh + i] = o->pcm[o->nPcmAt * nCh + i] * o->fGain;
    o->nPcmAt += nTake;
    nDone += nTake;
  }
  return nDone;
}

// To frame nFrame: from the last page that ends 80 ms or more before it (the decoder's
// settling), decoding on and dropping up to it.
bool OpusSeek(OggOpus *o, uint64_t nFrame)
{
  const uint64_t nTarget = nFrame + static_cast<uint64_t>(o->nPreSkip);
  const uint64_t nFrom = nTarget > 3840 ? nTarget - 3840 : 0;
  size_t nAt = 0, nStart = 0;
  uint64_t nStartGranule = 0, g = 0;
  bool bContinued = false;
  // the header pages end with granule 0; a page past nFrom stops the walk
  while (OggPage(o, nAt, &g, &bContinued)) {
    if (g == ~0ull) {  // no packet ends on this page
      nAt = o->nPage;
      continue;
    }
    if (g != 0 && g > nFrom)
      break;
    if (g != 0) {
      nStart = o->nPage;
      nStartGranule = g;
    }
    nAt = o->nPage;
  }
  if (nStart == 0) {
    // from the first audio packet
    o->nPage = o->nFirstPage;
    o->segs = o->firstSegs;
    o->nSeg = o->nFirstSeg;
    o->nBody = o->nFirstBody;
  } else {
    if (!OggPage(o, nStart, &g, &bContinued))
      return false;
    // a packet the page begins in the middle of is passed over
    if (bContinued)
      while (o->nSeg < o->segs.size()) {
        const size_t n = o->segs[o->nSeg++];
        o->nBody += n;
        if (n < 255)
          break;
      }
  }
  opus_multistream_decoder_ctl(o->pDec, OPUS_RESET_STATE);
  o->pcm.clear();
  o->nPcmAt = 0;
  o->nDiscard = nTarget > nStartGranule ? nTarget - nStartGranule : 0;
  return true;
}
#endif

}  // namespace

struct AudioDecoder {
  Kind kind = KIND_NONE;
  SDL_IOStream *pIo = NULL;            // a file on disk
  std::vector<unsigned char> memory;   // or the file in memory
  uint64_t nBytes = 0;
  int nRate = 0;
  int nChannels = 0;
  uint64_t nFrames = 0;
  std::string title, artist;
  drmp3 mp3;
  drwav wav;
  drflac *pFlac = NULL;
#if !defined(__EMSCRIPTEN__)
  stb_vorbis *pVorbis = NULL;
  OggOpus *pOpus = NULL;
#endif
};

namespace {

size_t IoRead(void *pUser, void *pOut, size_t n)
{
  return SDL_ReadIO(static_cast<AudioDecoder *>(pUser)->pIo, pOut, n);
}

bool IoSeek(void *pUser, int nOffset, int nOrigin)
{
  const SDL_IOWhence w = nOrigin == 0 ? SDL_IO_SEEK_SET : nOrigin == 1 ? SDL_IO_SEEK_CUR : SDL_IO_SEEK_END;
  return SDL_SeekIO(static_cast<AudioDecoder *>(pUser)->pIo, nOffset, w) >= 0;
}

template <typename Int64>
bool IoTell(void *pUser, Int64 *pnCursor)
{
  const Sint64 n = SDL_TellIO(static_cast<AudioDecoder *>(pUser)->pIo);
  *pnCursor = static_cast<Int64>(n);
  return n >= 0;
}

// each library's callbacks, in its own types; the origins are SET, CUR and END in all three
size_t Mp3Read(void *p, void *o, size_t n) { return IoRead(p, o, n); }
drmp3_bool32 Mp3Seek(void *p, int n, drmp3_seek_origin o) { return IoSeek(p, n, static_cast<int>(o)); }
drmp3_bool32 Mp3Tell(void *p, drmp3_int64 *pn) { return IoTell(p, pn); }
size_t WavRead(void *p, void *o, size_t n) { return IoRead(p, o, n); }
drwav_bool32 WavSeek(void *p, int n, drwav_seek_origin o) { return IoSeek(p, n, static_cast<int>(o)); }
drwav_bool32 WavTell(void *p, drwav_int64 *pn) { return IoTell(p, pn); }
size_t FlacRead(void *p, void *o, size_t n) { return IoRead(p, o, n); }
drflac_bool32 FlacSeek(void *p, int n, drflac_seek_origin o) { return IoSeek(p, n, static_cast<int>(o)); }
drflac_bool32 FlacTell(void *p, drflac_int64 *pn) { return IoTell(p, pn); }

void Mp3Meta(void *pUser, const drmp3_metadata *pMeta)
{
  AudioDecoder *p = static_cast<AudioDecoder *>(pUser);
  const unsigned char *pRaw = static_cast<const unsigned char *>(pMeta->pRawData);
  if (pMeta->type == DRMP3_METADATA_TYPE_ID3V2)
    Id3v2(pRaw, pMeta->rawDataSize, &p->title, &p->artist);
  else if (pMeta->type == DRMP3_METADATA_TYPE_ID3V1)
    Id3v1(pRaw, pMeta->rawDataSize, &p->title, &p->artist);
}

void FlacMeta(void *pUser, drflac_metadata *pMeta)
{
  if (pMeta->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT)
    return;
  AudioDecoder *p = static_cast<AudioDecoder *>(pUser);
  drflac_vorbis_comment_iterator it;
  drflac_init_vorbis_comment_iterator(&it, pMeta->data.vorbis_comment.commentCount, pMeta->data.vorbis_comment.pComments);
  drflac_uint32 n = 0;
  for (const char *psz; (psz = drflac_next_vorbis_comment(&it, &n)) != NULL;)
    Comment(psz, n, &p->title, &p->artist);
}

// Opens the decoder of its kind over its file or memory.
bool Start(AudioDecoder *p)
{
  const bool bMem = p->pIo == NULL;
  const void *pData = p->memory.data();
  const size_t nData = p->memory.size();
  switch (p->kind) {
    case KIND_MP3: {
      const bool bOk = bMem ? drmp3_init_memory_with_metadata(&p->mp3, pData, nData, Mp3Meta, p, NULL)
                            : drmp3_init(&p->mp3, Mp3Read, Mp3Seek, Mp3Tell, Mp3Meta, p, NULL);
      if (!bOk)
        return false;
      p->nRate = static_cast<int>(p->mp3.sampleRate);
      p->nChannels = static_cast<int>(p->mp3.channels);
      p->nFrames = drmp3_get_pcm_frame_count(&p->mp3);
      drmp3_seek_to_pcm_frame(&p->mp3, 0);
      return true;
    }
    case KIND_WAV: {
      const bool bOk = bMem ? drwav_init_memory(&p->wav, pData, nData, NULL)
                            : drwav_init(&p->wav, WavRead, WavSeek, WavTell, p, NULL);
      if (!bOk)
        return false;
      p->nRate = static_cast<int>(p->wav.sampleRate);
      p->nChannels = static_cast<int>(p->wav.channels);
      p->nFrames = p->wav.totalPCMFrameCount;
      return true;
    }
    case KIND_FLAC: {
      p->pFlac = bMem ? drflac_open_memory_with_metadata(pData, nData, FlacMeta, p, NULL)
                      : drflac_open_with_metadata(FlacRead, FlacSeek, FlacTell, FlacMeta, p, NULL);
      if (p->pFlac == NULL)
        return false;
      p->nRate = static_cast<int>(p->pFlac->sampleRate);
      p->nChannels = static_cast<int>(p->pFlac->channels);
      p->nFrames = p->pFlac->totalPCMFrameCount;
      return true;
    }
    case KIND_OGG: {
#if !defined(__EMSCRIPTEN__)
      if (!bMem || nData > 0x7FFFFFFF)
        return false;
      int nError = 0;
      p->pVorbis = stb_vorbis_open_memory(p->memory.data(), static_cast<int>(nData), &nError, NULL);
      if (p->pVorbis == NULL)
        return false;
      const stb_vorbis_info info = stb_vorbis_get_info(p->pVorbis);
      p->nRate = static_cast<int>(info.sample_rate);
      p->nChannels = info.channels;
      p->nFrames = stb_vorbis_stream_length_in_samples(p->pVorbis);
      const stb_vorbis_comment c = stb_vorbis_get_comment(p->pVorbis);
      for (int i = 0; i < c.comment_list_length; ++i)
        Comment(c.comment_list[i], strlen(c.comment_list[i]), &p->title, &p->artist);
      return true;
#else
      return false;
#endif
    }
    case KIND_OPUS: {
#if !defined(__EMSCRIPTEN__)
      if (!bMem)
        return false;
      p->pOpus = new OggOpus;
      if (!OpusStart(p->pOpus, p->memory.data(), nData, &p->title, &p->artist, &p->nFrames))
        return false;
      p->nRate = 48000;
      p->nChannels = p->pOpus->nChannels;
      return true;
#else
      return false;
#endif
    }
    default:
      return false;
  }
}

}  // namespace

bool AudioFileKind(const char *pszPath)
{
  return KindOfName(pszPath) != KIND_NONE;
}

AudioDecoder *AudioDecoderOpenFile(const char *pszPath)
{
  const Kind byName = KindOfName(pszPath);
  if (byName == KIND_NONE)
    return NULL;
  AudioDecoder *p = new AudioDecoder;
  p->pIo = SDL_IOFromFile(pszPath, "rb");
  if (p->pIo == NULL) {
    delete p;
    return NULL;
  }
  const Sint64 nSize = SDL_GetIOSize(p->pIo);
  p->nBytes = nSize > 0 ? static_cast<uint64_t>(nSize) : 0;
  unsigned char abHead[12] = {};
  const size_t nHead = SDL_ReadIO(p->pIo, abHead, sizeof abHead);
  SDL_SeekIO(p->pIo, 0, SDL_IO_SEEK_SET);
  p->kind = KindOfBytes(abHead, nHead);
  if (p->kind == KIND_NONE)
    p->kind = byName;
  // stb_vorbis and the Opus pages read from memory: an Ogg file is read whole, a few megabytes
  if ((p->kind == KIND_OGG || p->kind == KIND_OPUS) && nSize > 0 && nSize < 0x7FFFFFFF) {
    p->memory.resize(static_cast<size_t>(nSize));
    const size_t nRead = SDL_ReadIO(p->pIo, p->memory.data(), p->memory.size());
    SDL_CloseIO(p->pIo);
    p->pIo = NULL;
    if (nRead != p->memory.size()) {
      delete p;
      return NULL;
    }
  }
  if (!Start(p)) {
    AudioDecoderClose(p);
    return NULL;
  }
  return p;
}

AudioDecoder *AudioDecoderOpenMemory(std::vector<unsigned char> &&data, const char *pszName)
{
  AudioDecoder *p = new AudioDecoder;
  p->memory = std::move(data);
  p->nBytes = p->memory.size();
  p->kind = KindOfBytes(p->memory.data(), p->memory.size());
  if (p->kind == KIND_NONE)
    p->kind = KindOfName(pszName);
  if (!Start(p)) {
    AudioDecoderClose(p);
    return NULL;
  }
  return p;
}

void AudioDecoderClose(AudioDecoder *p)
{
  if (p == NULL)
    return;
  if (p->nRate != 0) {
    switch (p->kind) {
      case KIND_MP3:
        drmp3_uninit(&p->mp3);
        break;
      case KIND_WAV:
        drwav_uninit(&p->wav);
        break;
      default:
        break;
    }
  }
  if (p->pFlac != NULL)
    drflac_close(p->pFlac);
#if !defined(__EMSCRIPTEN__)
  if (p->pVorbis != NULL)
    stb_vorbis_close(p->pVorbis);
  if (p->pOpus != NULL) {
    if (p->pOpus->pDec != NULL)
      opus_multistream_decoder_destroy(p->pOpus->pDec);
    delete p->pOpus;
  }
#endif
  if (p->pIo != NULL)
    SDL_CloseIO(p->pIo);
  delete p;
}

int AudioDecoderRate(const AudioDecoder *p) { return p->nRate; }
int AudioDecoderChannels(const AudioDecoder *p) { return p->nChannels; }
uint64_t AudioDecoderFrames(const AudioDecoder *p) { return p->nFrames; }
uint64_t AudioDecoderBytes(const AudioDecoder *p) { return p->nBytes; }

void AudioDecoderTags(const AudioDecoder *p, std::string *pTitle, std::string *pArtist)
{
  *pTitle = p->title;
  *pArtist = p->artist;
}

size_t AudioDecoderRead(AudioDecoder *p, float *pOut, size_t nFrames)
{
  switch (p->kind) {
    case KIND_MP3:
      return static_cast<size_t>(drmp3_read_pcm_frames_f32(&p->mp3, nFrames, pOut));
    case KIND_WAV:
      return static_cast<size_t>(drwav_read_pcm_frames_f32(&p->wav, nFrames, pOut));
    case KIND_FLAC:
      return static_cast<size_t>(drflac_read_pcm_frames_f32(p->pFlac, nFrames, pOut));
    case KIND_OGG: {
#if !defined(__EMSCRIPTEN__)
      const size_t nFloats = nFrames * static_cast<size_t>(p->nChannels);
      if (nFloats > 0x7FFFFFFF)
        return 0;
      return static_cast<size_t>(
          stb_vorbis_get_samples_float_interleaved(p->pVorbis, p->nChannels, pOut, static_cast<int>(nFloats)));
#else
      return 0;
#endif
    }
    case KIND_OPUS:
#if !defined(__EMSCRIPTEN__)
      return OpusRead(p->pOpus, pOut, nFrames);
#else
      return 0;
#endif
    default:
      return 0;
  }
}

bool AudioDecoderSeek(AudioDecoder *p, uint64_t nFrame)
{
  switch (p->kind) {
    case KIND_MP3:
      return drmp3_seek_to_pcm_frame(&p->mp3, nFrame) != 0;
    case KIND_WAV:
      return drwav_seek_to_pcm_frame(&p->wav, nFrame) != 0;
    case KIND_FLAC:
      return drflac_seek_to_pcm_frame(p->pFlac, nFrame) != 0;
    case KIND_OGG:
#if !defined(__EMSCRIPTEN__)
      return nFrame <= 0xFFFFFFFFu && stb_vorbis_seek(p->pVorbis, static_cast<unsigned int>(nFrame)) != 0;
#else
      return false;
#endif
    case KIND_OPUS:
#if !defined(__EMSCRIPTEN__)
      return OpusSeek(p->pOpus, nFrame);
#else
      return false;
#endif
    default:
      return false;
  }
}
