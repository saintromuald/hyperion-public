// port_benchview.h -- the benchmark's view: the game in a window with the run's panels round it
// (port_benchview.cpp).
#ifndef PORT_BENCHVIEW_H
#define PORT_BENCHVIEW_H

// The run's overlay: the game in a window inside the screen with the run's panels round it, or
// filling the screen with them over it (port_benchview.cpp), a layer over the game
// (port_overlay.h), which places the game, draws the panels and takes the mouse and keys meant
// for them.
bool PortBenchViewShown(void);
void PortBenchViewWheel(void);  // the wheel takes the camera from the run's ways
// The game's Menu in a run (do_gamemenu_dlg: the HUD's or the console's Menu button, F10): the
// overlay shown if it was hidden, and Stop the benchmark? asked, or a finished run's summary shown
// again, so a run always has a way out. The replay's own pause (order_replay_play) as the
// overlay's Pause, so paused time is left out of the numbers.
void PortBenchViewMenu(void);
void PortBenchViewPause(void);

#endif
