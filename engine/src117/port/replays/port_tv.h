// port_tv.h -- where the camera looks on its own: following a unit, and replay TV (port_tv.cpp).
#ifndef PORT_TV_H
#define PORT_TV_H

// Where the camera looks on its own (port_tv.cpp): following the selected
// unit (any game), and replay TV, which looks where the fighting is. The camera asks each
// for its aim; anything the viewer does pauses TV.
void PortTvToggle(void);

int PortTvOn(void);
void PortTvInput(void);
int PortTvTarget(double *pfX, double *pfY, double *pfZoom);
// Replay TV's score of the map without its shot: the hottest place, if fighting, in map pixels, and its
// heat (a LAN room's copy of a match; which reads ahead for those watching).
int PortTvHottest(int *pnX, int *pnY, float *pfHeat);
void PortFollowToggle(void);
void PortFollowStop(void);
// A unit is being followed (the HUD's Follow button, lit while it is).
int PortFollowOn(void);
int PortFollowAt(double *pfX, double *pfY);

#endif
