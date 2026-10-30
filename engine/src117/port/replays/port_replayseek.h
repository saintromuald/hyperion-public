// port_replayseek.h -- a replay taken to any frame of it, back as well as on (the
// HUD's replay.seek). The trainer's snapshots (port_snap.cpp) are kept as the replay plays;
// a seek puts back the latest at or before the frame and plays the recorded commands on from
// there, at once and with a picture now and then, to the frame.
#ifndef PORT_REPLAYSEEK_H
#define PORT_REPLAYSEEK_H

// From the game loop, at a game frame's end: the snapshots on their frames, and a seek's end.
void PortReplaySeekFrame(void);
// From main.cpp, when a game is over: a seek's end, and its snapshots freed.
void PortReplaySeekAfterGame(void);

// A recorded replay plays that can be moved: not a game watched as it is played, nor the trainer's.
bool PortReplaySeekable(void);
// The replay to a frame (clamped to its length, and to the first frame kept). False when it
// cannot be moved now.
bool PortReplaySeekTo(unsigned nFrame);
// A seek is playing the replay on to its frame: the loop runs the frames at once, past the
// replay's pause, and draws only now and then.
bool PortReplaySeeking(void);
// While seeking: a picture is due (one every 50 ms).
bool PortReplaySeekPictureDue(void);
// The frame a seek is going to, or the replay's frame when none runs.
unsigned PortReplaySeekTarget(void);
// The replay's length in frames, 0 for none.
unsigned PortReplaySeekLength(void);

#endif
