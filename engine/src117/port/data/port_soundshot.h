// port_soundshot.h -- Hyperion's own sounds on the mixer (port_dsound.cpp), apart from
// port_sound.h, which lang/snd.cpp includes and which takes no standard library header.
#ifndef PORT_SOUNDSHOT_H
#define PORT_SOUNDSHOT_H

#include <memory>
#include <vector>

// A sound of Hyperion's own (the HUD's cues): interleaved stereo floats at the mixer's
// rate (PortSoundRate), played once at a gain over the game's sounds, muted behind another window
// as they are. At most 32 at once; another is not played.
void PortSoundOneShot(std::shared_ptr<const std::vector<float>> pcm, float fGain);
int PortSoundRate(void);

#endif
