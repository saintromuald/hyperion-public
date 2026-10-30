// port_sound.h -- the sound device as the game reaches it (port_dsound.cpp):
// DirectSoundCreate, the window in front or behind, and a listener on the mix. Apart from
// port_dsound.h, which stands in for <dsound.h>, so lang/snd.cpp includes it beside its own
// DirectSound.
#ifndef PORT_SOUND_H
#define PORT_SOUND_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// Sound (port_dsound.cpp): DirectSoundCreate on SDL audio, for lang/snd.cpp,
// which declares DirectSound's interfaces itself (h/snd.h), and the window's place in
// front or behind, which mutes the buffers DirectSound would mute.
LONG __stdcall PortDirectSoundCreateAny(const void *pGuid, void **ppDS, void *pOuter);
void PortSoundForeground(bool bForeground);
// What the mixer sends out, to a listener as well (a streamed seat's, port_lanstream.cpp):
// interleaved stereo floats at the mixer's rate, on the mixer's thread. NULL ends it.
typedef void (*PortSoundTapFn)(const float *pSamples, int nFrames, int nRate);
void PortSoundTap(PortSoundTapFn pfn);
// A voice of its own in the mix, for an add-on's sound: called on the mixer's thread
// for each stretch, it adds its interleaved stereo floats at the mixer's rate into pMix, before
// a listener hears the mix and Master volume scales it. bHeard is false while the window is
// behind another and Sound in the background is off, when the game's own buffers go silent too.
// NULL ends it; false when there is no sound device.
typedef void (*PortSoundSourceFn)(float *pMix, int nFrames, int nRate, bool bHeard);
bool PortSoundSource(PortSoundSourceFn pfn);
// Settings' sound rows on the mixer: Master volume, a level of 0 to 1 over everything sent to
// the speakers (a listener above hears the mix before it), and Sound in the background, which
// keeps the buffers DirectSound would mute playing behind another window.
void PortSoundMaster(float fLevel);
void PortSoundBackground(bool bOn);
// Settings' Unit voices: with Remastered ("unitVoices" 1), open_wav_file (snd.cpp) takes a sound
// under sound\ from the Remastered install when it has one by that name. PortSoundRemastered
// gives the file in Storm's memory, which the caller frees, or NULL to load 1.17's.
void PortSoundVoicesSet(bool bRemastered);
void *PortSoundRemastered(const char *pszPath, DWORD *pcbSize);
// A game sound for its buffer (snd.cpp's open_wav_file and loading thread), in Storm's memory,
// which the caller frees: Remastered's recording as above, else the file (a mod's from its
// folder, port_stormfile.cpp), as a WAV of 8 or 16-bit PCM in one or two channels. Another
// format, OGG, MP3, FLAC, Opus or another WAV, is decoded to one. NULL
// when there is none or it does not decode.
void *PortSoundLoad(const char *pszPath, DWORD *pcbSize);

#endif
