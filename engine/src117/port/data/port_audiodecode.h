// port_audiodecode.h -- the audio decoders (port_audiodecode.cpp): a file of
// MP3, Ogg Vorbis, Ogg Opus, FLAC or WAV, from disk or from memory, read as float frames at its own rate
// and channels, with its length, a seek and the title and artist it names. A decoder is used by
// one thread at a time.
#ifndef PORT_AUDIODECODE_H
#define PORT_AUDIODECODE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

struct AudioDecoder;

// A file on disk, its path UTF-8; or a file in memory, which the decoder keeps, its kind told by
// its first bytes or else by pszName's extension. NULL when it is none of the five or broken.
AudioDecoder *AudioDecoderOpenFile(const char *pszPath);
AudioDecoder *AudioDecoderOpenMemory(std::vector<unsigned char> &&data, const char *pszName);
void AudioDecoderClose(AudioDecoder *p);

int AudioDecoderRate(const AudioDecoder *p);
int AudioDecoderChannels(const AudioDecoder *p);
// The length in frames, 0 when the file does not say.
uint64_t AudioDecoderFrames(const AudioDecoder *p);
// The file's size, for the readout's kbps.
uint64_t AudioDecoderBytes(const AudioDecoder *p);
// Up to nFrames frames, interleaved at the file's channels, into pOut: the frames read, 0 at the
// end.
size_t AudioDecoderRead(AudioDecoder *p, float *pOut, size_t nFrames);
bool AudioDecoderSeek(AudioDecoder *p, uint64_t nFrame);
// The title and artist the file's tags name (ID3, Vorbis comments), empty where they name none.
void AudioDecoderTags(const AudioDecoder *p, std::string *pTitle, std::string *pArtist);

// Whether a path's extension is one of the five kinds.
bool AudioFileKind(const char *pszPath);

#endif
