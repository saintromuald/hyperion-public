// port_sysvideo.h -- H.264 and H.265 pictures and AAC sound through the decoders the system has,
// for port_video.cpp: Media Foundation on Windows (port_sysvideo_win.cpp), VideoToolbox and
// AudioToolbox on a Mac (port_sysvideo_mac.cpp), and on Linux the player's own FFmpeg, loaded
// when first asked for (port_sysvideo_ffmpeg.cpp). Hyperion has no decoder of its own for them.
#ifndef PORT_SYSVIDEO_H
#define PORT_SYSVIDEO_H

#include <stddef.h>
#include <utility>
#include <vector>
#include "port_videoindex.h"

// The codec's parameter sets, from its avcC or hvcC, and the bytes of each unit's length before
// it in a frame.
struct SysVideoSetup {
  VideoCodec codec = VIDEO_CODEC_NONE;
  int nLengthSize = 4;
  std::vector<std::vector<unsigned char>> sets;  // H.264's SPS and PPS; H.265's VPS, SPS and PPS
};

// port_video.cpp's: the set-up from an avcC or hvcC, false when it is not one; and a frame as the
// container keeps it, in Annex B's form, each unit after a start code, a key frame's parameter
// sets first.
bool SysVideoSetupOf(VideoCodec codec, const std::vector<unsigned char> &config, SysVideoSetup *pOut);
void SysVideoAnnexB(const SysVideoSetup &setup, const unsigned char *p, size_t cb, bool bKey,
                    std::vector<unsigned char> *pOut);

// A picture as a system decoder gives it, valid until the next is asked for or the decoder starts
// again.
struct SysPicture {
  const unsigned char *apPlane[3];
  ptrdiff_t anStride[3];
  int nWidth, nHeight;
  int xs, ys;    // the chroma's shifts
  int nBits;     // 8, or 10 to 16 in 16-bit samples, the value in the low bits; P010's is 16
  bool bPaired;  // the chroma's two in one plane, U first (NV12, P010)
  int nMatrix;   // 0 not said, 1 BT.601, 2 BT.709, 3 BT.2020
  int nRange;    // 0 not said, 1 limited, 2 full
  unsigned nRank;  // its place in showing order, as its frame was given
};

struct SysVideo;
// NULL when the system has no decoder for the codec
SysVideo *SysVideoOpen(const SysVideoSetup &setup, int nWidth, int nHeight);
// One frame, as the container keeps it, in decoding order, with its place in showing order.
// Once it is given, so has every frame shown before nShown; a decoder that gives its pictures in
// decoding order holds them until then.
bool SysVideoSend(SysVideo *p, const unsigned char *pFrame, size_t cb, bool bKey, unsigned nRank,
                  unsigned nShown);
// no more frames: those it holds come out
void SysVideoDrain(SysVideo *p);
// the next picture in showing order; false when it has none ready
bool SysVideoPicture(SysVideo *p, SysPicture *pOut);
// what it holds let go, to start again at a key frame
void SysVideoRestart(SysVideo *p);
void SysVideoClose(SysVideo *p);

// what a player installs for the system to have a decoder it lacks, for the log; NULL when the
// system always has them
const char *SysVideoNeeds(VideoCodec codec);
const char *SysSoundNeeds(void);  // AAC's

// AAC's packets decoded whole to 16-bit samples, interleaved, at the decoder's rate; more than two
// channels in WAVE's order (front left, right and centre, low frequencies, back, sides). False
// when the system has no AAC decoder.
bool SysSoundAac(const std::vector<unsigned char> &config, const unsigned char *pData,
                 const std::vector<std::pair<long long, long>> &packets, std::vector<short> *pOut, int *pnRate,
                 int *pnChannels);

#endif
