// port_labworker.h -- a worker of the AI lab's farm: a Hyperion the lab starts with
// --lab-worker, hidden, with no sound and nothing drawn, playing the benchmark's game of
// computers (--bench) as fast as it goes and telling the lab about it over a connection on this
// machine (port_labproto.h). What it reads of the game it only reads: its game is the
// benchmark's, and plays as one does.
#ifndef PORT_LABWORKER_H
#define PORT_LABWORKER_H

// Started with --lab-worker <port>:<token>:<worker>.
bool PortLabWorker(void);
// From HyperionMain, before SDL starts: the worker's own environment (hidden, no sound, SDL's
// renderer, one thread), and its connection to the lab, which says HELLO. Without the lab the
// worker ends.
void PortLabWorkerStart(void);
// Each game frame's end (gameloop.cpp, after the benchmark's): what the lab sent, the game's
// start, samples of its units and its status, and its end, sent before the program ends. In the
// lab's own process, a farm game watched (PortLabWatchFrame).
void PortLabFrame(void);
// --lab-out, or the lab's JOB: the folder the game's files go in (the benchmark's run folder),
// "" when none.
const char *PortLabWorkerOut(void);
// The glue loop, after a game (glues.cpp): a worker started without --bench waits for the lab's
// next game (JOB) and starts it (TRUE), or ends when the lab has no more (STOP) or is gone.
// FALSE for any other program.
bool PortLabWorkerNext(void);

#endif
