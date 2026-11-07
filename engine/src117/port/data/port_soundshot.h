// port_soundshot.h -- Hyperion's own sounds on the mixer (port_dsound.cpp), apart from
// port_sound.h, which lang/snd.cpp includes and which takes no standard library header.
#ifndef PORT_SOUNDSHOT_H
#define PORT_SOUNDSHOT_H

#include <memory>
#include <vector>

// A sound of Hyperion's own (the HUD's cues): interleaved stereo floats at the mixer's
// rate (PortSoundRate), played once at a gain over the game's sounds, muted behind another window
// as they are. At most 32 at once; false when it could not be queued.
bool PortSoundOneShot(std::shared_ptr<const std::vector<float>> pcm, float fGain);
int PortSoundRate(void);
// The worker-specific advisor recording for race 0 Zerg, 1 Terran or 2 Protoss, loaded from
// Hyperion's packaged huds/files and queued on the mixer. It never falls back to an attack cue.
bool PortSoundWorkerAlert(int nRace, float fGain);

#endif
