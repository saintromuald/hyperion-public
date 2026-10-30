// port_videoindex.h -- a video file's parts, as its container's reader finds them, for the
// decoders in port_video.cpp: Matroska and WebM (port_webm.cpp), MP4 and QuickTime (port_mp4.cpp).
#ifndef PORT_VIDEOINDEX_H
#define PORT_VIDEOINDEX_H

#include <utility>
#include <vector>

enum VideoCodec { VIDEO_CODEC_NONE, VIDEO_CODEC_VP8, VIDEO_CODEC_VP9, VIDEO_CODEC_AV1, VIDEO_CODEC_H264, VIDEO_CODEC_H265 };
enum SoundCodec { SOUND_CODEC_NONE, SOUND_CODEC_VORBIS, SOUND_CODEC_OPUS, SOUND_CODEC_AAC };

// A picture's bytes in the file, in the order they are decoded.
struct VideoPiece {
  long long pos;
  long len;
  long long ns;  // when it is shown
  bool bKey;
  long long alphaPos;  // WebM's alpha picture, when alphaLen is not 0
  long alphaLen;
};

struct VideoIndex {
  VideoCodec video = VIDEO_CODEC_NONE;
  int nWidth = 0, nHeight = 0;
  // the codec's set-up as the container keeps it: av1C, avcC or hvcC
  std::vector<unsigned char> videoSetup;
  std::vector<VideoPiece> frames;
  long long nFrameNs = 0;  // a frame's length as the track gives it, 0 when it does not
  SoundCodec sound = SOUND_CODEC_NONE;
  // Vorbis's three headers in Xiph lacing, an OpusHead, or AAC's AudioSpecificConfig
  std::vector<unsigned char> soundSetup;
  int nSoundRate = 0, nSoundChannels = 0;
  std::vector<std::pair<long long, long>> packets;  // the sound's, in order
  // the encoder's lead-in to the sound, left out (MP4's edit list, Matroska's CodecDelay)
  long long nSoundSkipNs = 0;
};

// false when the bytes are not that container, or hold no picture of a codec above
bool VideoIndexMatroska(const unsigned char *pData, size_t cb, VideoIndex *pIndex);
bool VideoIndexMp4(const unsigned char *pData, size_t cb, VideoIndex *pIndex);

#endif
