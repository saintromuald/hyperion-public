// port_sysvideo_mac.cpp -- port_sysvideo.h on a Mac: H.264 and H.265 through VideoToolbox, which
// takes the frames as the container keeps them and gives each picture as it is decoded, in
// decoding order, so pictures are held here until those shown before them have come; AAC through
// AudioToolbox's converter.
#if defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>
#include <string.h>
#include <algorithm>
#include "port_sysvideo.h"

// ---------------------------------------------------------------------------
// Pictures.

namespace {

struct Held {
  CVPixelBufferRef pBuffer;
  unsigned nRank;
};

}  // namespace

struct SysVideo {
  CMVideoFormatDescriptionRef pFormat = NULL;
  VTDecompressionSessionRef pSession = NULL;
  std::vector<Held> held;  // decoded, not yet given
  unsigned nShown = 0;  // every picture shown before it has been decoded
  bool bDrained = false;
  CVPixelBufferRef pLocked = NULL;  // the picture given last
};

static void Decoded(void *pRefcon, void *, OSStatus status, VTDecodeInfoFlags flags, CVImageBufferRef pImage,
                    CMTime pts, CMTime)
{
  SysVideo *p = static_cast<SysVideo *>(pRefcon);
  if (status != noErr || !pImage || (flags & kVTDecodeInfo_FrameDropped))
    return;
  CVPixelBufferRetain(pImage);
  p->held.push_back({pImage, static_cast<unsigned>(pts.value)});
}

static void Unlock(SysVideo *p)
{
  if (p->pLocked) {
    CVPixelBufferUnlockBaseAddress(p->pLocked, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferRelease(p->pLocked);
    p->pLocked = NULL;
  }
}

static CFNumberRef Number(int n) { return CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &n); }

SysVideo *SysVideoOpen(const SysVideoSetup &setup, int, int)
{
  if ((setup.codec != VIDEO_CODEC_H264 && setup.codec != VIDEO_CODEC_H265) || setup.sets.empty() ||
      setup.nLengthSize == 3)
    return NULL;
  std::vector<const uint8_t *> sets;
  std::vector<size_t> sizes;
  for (const std::vector<unsigned char> &s : setup.sets) {
    sets.push_back(s.data());
    sizes.push_back(s.size());
  }
  SysVideo *p = new SysVideo();
  const OSStatus st =
      setup.codec == VIDEO_CODEC_H264
          ? CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, sets.size(), sets.data(),
                                                                sizes.data(), setup.nLengthSize, &p->pFormat)
          : CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault, sets.size(), sets.data(),
                                                                sizes.data(), setup.nLengthSize, NULL, &p->pFormat);
  if (st != noErr) {
    SysVideoClose(p);
    return NULL;
  }
  // 4:2:0 in two planes, 8 or 10 bits, limited or full range
  CFNumberRef aFormats[4] = {Number(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
                             Number(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
                             Number(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange),
                             Number(kCVPixelFormatType_420YpCbCr10BiPlanarFullRange)};
  CFArrayRef pFormats = CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void **>(aFormats), 4,
                                      &kCFTypeArrayCallBacks);
  const void *pKey = kCVPixelBufferPixelFormatTypeKey;
  const void *pValue = pFormats;
  CFDictionaryRef pAttrs = CFDictionaryCreate(kCFAllocatorDefault, &pKey, &pValue, 1, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
  VTDecompressionOutputCallbackRecord cb = {Decoded, p};
  const OSStatus stSession =
      VTDecompressionSessionCreate(kCFAllocatorDefault, p->pFormat, NULL, pAttrs, &cb, &p->pSession);
  CFRelease(pAttrs);
  CFRelease(pFormats);
  for (CFNumberRef n : aFormats)
    CFRelease(n);
  if (stSession != noErr) {
    SysVideoClose(p);
    return NULL;
  }
  return p;
}

bool SysVideoSend(SysVideo *p, const unsigned char *pFrame, size_t cb, bool, unsigned nRank, unsigned nShown)
{
  CMBlockBufferRef pBlock = NULL;
  CMSampleBufferRef pSample = NULL;
  OSStatus st = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, const_cast<unsigned char *>(pFrame), cb,
                                                   kCFAllocatorNull, NULL, 0, cb, 0, &pBlock);
  if (st == noErr) {
    const CMSampleTimingInfo timing = {kCMTimeInvalid, CMTimeMake(nRank, 1), kCMTimeInvalid};
    st = CMSampleBufferCreateReady(kCFAllocatorDefault, pBlock, p->pFormat, 1, 1, &timing, 1, &cb, &pSample);
  }
  if (st == noErr) {
    VTDecodeInfoFlags info = 0;
    st = VTDecompressionSessionDecodeFrame(p->pSession, pSample, 0, NULL, &info);  // decoded before it returns
  }
  if (pSample)
    CFRelease(pSample);
  if (pBlock)
    CFRelease(pBlock);
  p->nShown = std::max(p->nShown, nShown);
  return st == noErr;
}

void SysVideoDrain(SysVideo *p)
{
  VTDecompressionSessionWaitForAsynchronousFrames(p->pSession);
  p->bDrained = true;
}

static int MatrixOf(CVPixelBufferRef pBuffer)
{
  CFTypeRef pValue = CVBufferCopyAttachment(pBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
  if (!pValue)
    return 0;
  const int n = CFEqual(pValue, kCVImageBufferYCbCrMatrix_ITU_R_709_2)   ? 2
                : CFEqual(pValue, kCVImageBufferYCbCrMatrix_ITU_R_601_4) ? 1
                : CFEqual(pValue, kCVImageBufferYCbCrMatrix_ITU_R_2020)  ? 3
                                                                         : 0;
  CFRelease(pValue);
  return n;
}

bool SysVideoPicture(SysVideo *p, SysPicture *pOut)
{
  Unlock(p);
  if (p->held.empty())
    return false;
  const std::vector<Held>::iterator it = std::min_element(
      p->held.begin(), p->held.end(), [](const Held &a, const Held &b) { return a.nRank < b.nRank; });
  if (it->nRank >= p->nShown && !p->bDrained)
    return false;  // one shown before it is still to come
  p->pLocked = it->pBuffer;
  const unsigned nRank = it->nRank;
  p->held.erase(it);
  CVPixelBufferLockBaseAddress(p->pLocked, kCVPixelBufferLock_ReadOnly);
  const OSType format = CVPixelBufferGetPixelFormatType(p->pLocked);
  memset(pOut, 0, sizeof *pOut);
  const bool b10 = format == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange ||
                   format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
  if (!b10 && format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange &&
      format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
    return false;
  for (int k = 0; k < 2; ++k) {
    pOut->apPlane[k] = static_cast<const unsigned char *>(CVPixelBufferGetBaseAddressOfPlane(p->pLocked, k));
    pOut->anStride[k] = static_cast<ptrdiff_t>(CVPixelBufferGetBytesPerRowOfPlane(p->pLocked, k));
  }
  pOut->apPlane[2] = pOut->apPlane[1] + (b10 ? 2 : 1);
  pOut->anStride[2] = pOut->anStride[1];
  pOut->nWidth = static_cast<int>(CVPixelBufferGetWidth(p->pLocked));
  pOut->nHeight = static_cast<int>(CVPixelBufferGetHeight(p->pLocked));
  pOut->xs = pOut->ys = 1;
  pOut->nBits = b10 ? 16 : 8;  // the value in the top ten bits
  pOut->bPaired = true;
  pOut->nMatrix = MatrixOf(p->pLocked);
  pOut->nRange = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
                         format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange
                     ? 2
                     : 1;
  pOut->nRank = nRank;
  return pOut->apPlane[0] && pOut->apPlane[1];
}

void SysVideoRestart(SysVideo *p)
{
  Unlock(p);
  VTDecompressionSessionWaitForAsynchronousFrames(p->pSession);
  for (const Held &h : p->held)
    CVPixelBufferRelease(h.pBuffer);
  p->held.clear();
  p->nShown = 0;
  p->bDrained = false;
}

void SysVideoClose(SysVideo *p)
{
  if (!p)
    return;
  if (p->pSession) {
    SysVideoRestart(p);
    VTDecompressionSessionInvalidate(p->pSession);
    CFRelease(p->pSession);
  }
  if (p->pFormat)
    CFRelease(p->pFormat);
  delete p;
}

const char *SysVideoNeeds(VideoCodec) { return NULL; }
const char *SysSoundNeeds(void) { return NULL; }

// ---------------------------------------------------------------------------
// AAC: the converter's magic cookie is an MPEG-4 ES descriptor holding the AudioSpecificConfig.

namespace {

void Descriptor(std::vector<unsigned char> &out, unsigned char nTag, const std::vector<unsigned char> &body)
{
  out.push_back(nTag);
  const size_t n = body.size();
  out.push_back(static_cast<unsigned char>(0x80 | (n >> 21 & 0x7F)));
  out.push_back(static_cast<unsigned char>(0x80 | (n >> 14 & 0x7F)));
  out.push_back(static_cast<unsigned char>(0x80 | (n >> 7 & 0x7F)));
  out.push_back(static_cast<unsigned char>(n & 0x7F));
  out.insert(out.end(), body.begin(), body.end());
}

std::vector<unsigned char> Cookie(const std::vector<unsigned char> &config)
{
  std::vector<unsigned char> info, decoder = {0x40, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, es = {0, 0, 0}, out;
  Descriptor(info, 0x05, config);  // DecoderSpecificInfo
  decoder.insert(decoder.end(), info.begin(), info.end());  // after the DecoderConfigDescriptor's fields
  Descriptor(es, 0x04, decoder);
  Descriptor(es, 0x06, std::vector<unsigned char>(1, 0x02));  // SLConfigDescriptor
  Descriptor(out, 0x03, es);  // ES_Descriptor, its ID and flags first
  return out;
}

struct Feed {
  const unsigned char *pData;
  const std::vector<std::pair<long long, long>> *pPackets;
  size_t nNext;
  int nChannels;
  AudioStreamPacketDescription desc;
};

OSStatus FeedOne(AudioConverterRef, UInt32 *pnPackets, AudioBufferList *pList, AudioStreamPacketDescription **ppDesc,
                 void *pUser)
{
  Feed *f = static_cast<Feed *>(pUser);
  if (f->nNext >= f->pPackets->size()) {
    *pnPackets = 0;  // the end
    return noErr;
  }
  const std::pair<long long, long> &k = (*f->pPackets)[f->nNext++];
  pList->mNumberBuffers = 1;
  pList->mBuffers[0].mData = const_cast<unsigned char *>(f->pData + k.first);
  pList->mBuffers[0].mDataByteSize = static_cast<UInt32>(k.second);
  pList->mBuffers[0].mNumberChannels = static_cast<UInt32>(f->nChannels);
  f->desc.mStartOffset = 0;
  f->desc.mVariableFramesInPacket = 0;
  f->desc.mDataByteSize = static_cast<UInt32>(k.second);
  if (ppDesc)
    *ppDesc = &f->desc;
  *pnPackets = 1;
  return noErr;
}

}  // namespace

bool SysSoundAac(const std::vector<unsigned char> &config, const unsigned char *pData,
                 const std::vector<std::pair<long long, long>> &packets, std::vector<short> *pOut, int *pnRate,
                 int *pnChannels)
{
  if (config.size() < 2)
    return false;
  const std::vector<unsigned char> cookie = Cookie(config);
  AudioStreamBasicDescription in;
  memset(&in, 0, sizeof in);
  in.mFormatID = kAudioFormatMPEG4AAC;
  UInt32 cb = sizeof in;
  if (AudioFormatGetProperty(kAudioFormatProperty_FormatInfo, static_cast<UInt32>(cookie.size()), cookie.data(), &cb,
                             &in) != noErr ||
      in.mChannelsPerFrame == 0 || in.mSampleRate <= 0)
    return false;
  AudioStreamBasicDescription out;
  memset(&out, 0, sizeof out);
  out.mSampleRate = in.mSampleRate;
  out.mFormatID = kAudioFormatLinearPCM;
  out.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  out.mChannelsPerFrame = in.mChannelsPerFrame;
  out.mBitsPerChannel = 16;
  out.mBytesPerFrame = out.mBytesPerPacket = 2 * in.mChannelsPerFrame;
  out.mFramesPerPacket = 1;
  AudioConverterRef pConv = NULL;
  if (AudioConverterNew(&in, &out, &pConv) != noErr)
    return false;
  AudioConverterSetProperty(pConv, kAudioConverterDecompressionMagicCookie, static_cast<UInt32>(cookie.size()),
                            cookie.data());
  // more than two channels in WAVE's order, as the other systems' decoders give them
  static const AudioChannelLayoutTag kWave[9] = {0, 0, 0, kAudioChannelLayoutTag_MPEG_3_0_A,
                                                 kAudioChannelLayoutTag_MPEG_4_0_A, kAudioChannelLayoutTag_MPEG_5_0_A,
                                                 kAudioChannelLayoutTag_MPEG_5_1_A, kAudioChannelLayoutTag_MPEG_6_1_A,
                                                 kAudioChannelLayoutTag_MPEG_7_1_C};
  if (out.mChannelsPerFrame > 2 && out.mChannelsPerFrame <= 8) {
    AudioChannelLayout layout;
    memset(&layout, 0, sizeof layout);
    layout.mChannelLayoutTag = kWave[out.mChannelsPerFrame];
    AudioConverterSetProperty(pConv, kAudioConverterOutputChannelLayout, sizeof layout, &layout);
  }
  Feed feed = {pData, &packets, 0, static_cast<int>(in.mChannelsPerFrame), {}};
  const UInt32 nFrames = 4096;
  std::vector<short> buffer(static_cast<size_t>(nFrames) * out.mChannelsPerFrame);
  for (;;) {
    AudioBufferList list;
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = out.mChannelsPerFrame;
    list.mBuffers[0].mDataByteSize = static_cast<UInt32>(buffer.size() * sizeof(short));
    list.mBuffers[0].mData = buffer.data();
    UInt32 n = nFrames;
    const OSStatus st = AudioConverterFillComplexBuffer(pConv, FeedOne, &feed, &n, &list, NULL);
    if (n > 0)
      pOut->insert(pOut->end(), buffer.begin(), buffer.begin() + static_cast<size_t>(n) * out.mChannelsPerFrame);
    if (st != noErr || n == 0)
      break;
  }
  AudioConverterDispose(pConv);
  *pnRate = static_cast<int>(out.mSampleRate);
  *pnChannels = static_cast<int>(out.mChannelsPerFrame);
  return !pOut->empty();
}

#endif
