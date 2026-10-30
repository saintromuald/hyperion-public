// port_video.h -- videos in full colour (port_video.cpp): WebM and Matroska, MP4 and QuickTime,
// with VP8, VP9 or AV1 pictures and Vorbis or Opus sound. A StarCraft: Remastered install keeps
// its cinematics and portraits as WebM; a mod's portraits may be any of them.
#ifndef PORT_VIDEO_H
#define PORT_VIDEO_H

#include <stddef.h>

struct PortVideo;

// A video out of its file's bytes, which are copied; NULL when it is none of the containers or
// its picture is in no codec Hyperion decodes. Its sound is decoded whole here.
PortVideo *PortVideoOpen(const void *pData, size_t cb);
void PortVideoClose(PortVideo *p);
int PortVideoWidth(const PortVideo *p);
int PortVideoHeight(const PortVideo *p);
unsigned PortVideoFrames(const PortVideo *p);
// A frame's length in hundredths of a millisecond, as port_smacker.h counts it.
unsigned PortVideoInterval(const PortVideo *p);
// Whether frame n starts a group of pictures, which needs none before it.
bool PortVideoKeyFrame(const PortVideo *p, unsigned n);
// Frame n as RGBA, nPitch bytes a row; false when it does not decode. The alpha is the video's
// own when it has one, else 255. Frames are decoded in order: asking for an earlier one decodes
// again from the key frame before it.
bool PortVideoFrame(PortVideo *p, unsigned n, unsigned char *pRGBA, int nPitch);
// Frame n's planes as its decoder gives them, for the GPU presenter to put in colour at their
// own depth (port_gpu_video.cpp); false when it does not decode. They stay as they are until
// the next call on the video. The alpha is left out.
struct PortVideoPlanes {
  const unsigned char *apPlane[3];  // Y, Cb, Cr; G, B, R for an RGB video; with bPaired Cb and
                                    // Cr in plane 1, Cb first, and apPlane[2] at its Cr
  ptrdiff_t anStride[3];
  int nWidth, nHeight;
  int xs, ys;     // the chroma's shifts
  int nBits;      // 8, or 9 to 16 in 16-bit samples, the value in the low bits (16 for P010)
  bool bMono;     // no chroma
  bool bPaired;
  int nMatrix;    // 0 BT.601, 1 BT.709, 2 BT.2020, 3 RGB
  bool bFull;     // full range, else limited
};
bool PortVideoPicture(PortVideo *p, unsigned n, PortVideoPlanes *pOut);
// The sound, 16-bit samples with the channels interleaved (more than two mixed to two), or NULL
// with none; *pnSamples is samples a channel.
const short *PortVideoSound(const PortVideo *p, size_t *pnSamples, int *pnRate, int *pnChannels);
// What a video's container says, without decoding it: its picture's codec ("VP8", "VP9", "AV1",
// "H.264" or "H.265"), its sound's ("Vorbis", "Opus", "AAC", or "" for none), its size and frames;
// false when it is none of the containers, and always in a browser's build (--mod-check).
struct PortVideoInfo {
  const char *pszVideo;
  const char *pszSound;
  int nWidth, nHeight;
  unsigned nFrames;
};
bool PortVideoProbe(const void *pData, size_t cb, PortVideoInfo *pOut);
// The containers' extensions, in the order a name's are tried, ending in NULL: .webm, .mkv,
// .mp4, .m4v, .mov.
const char *const *PortVideoExtensions(void);
// Whether a file name ends in one of them.
bool PortVideoFileKind(const char *pszName);

#endif
