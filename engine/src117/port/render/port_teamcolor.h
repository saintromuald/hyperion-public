// port_teamcolor.h -- each player slot's colour in full RGB (port_teamcolor.cpp, stage G).
// Presentation only.
#ifndef PORT_TEAMCOLOR_H
#define PORT_TEAMCOLOR_H

// Each player slot's colour in full RGB (port_teamcolor.cpp, stage G): set by CRGB, the
// command line's --colour and the live channel; PortTeamShades gives the full-colour
// drawing a slot's eight shades. Presentation only.
void PortTeamColorsBegin(void);
void PortTeamColorsApply(void);
void PortTeamColorSet(int nSlot, unsigned char r, unsigned char g, unsigned char b, int bRamp);
void PortTeamColorClear(int nSlot);
int PortTeamColorGet(int nSlot, unsigned char *pRgb);
int PortTeamColorParse(const char *psz, unsigned char *pRgb);
void PortTeamShades(int nSlot, const unsigned char *pLive, float (*pafOut)[3]);
int PortTeamSlots(void);
void PortRgbToLab(const float *pRgb, float *pLab);
bool PortLabToRgb(const float *pLab, float *pRgb);
void PortTeamRamp(int nSlot, unsigned char *pRamp);
// Settings' "Allies blue, enemies red" and "Colour-blind player colours": the ramp and the
// minimap colour a slot is drawn with, its own or the ones Settings show it in; the tables
// the game keeps are left alone. PortTeamShownReread: one of the two changed.
const unsigned char *PortTeamRampShown(int nSlot);
unsigned char PortTeamMinimapShown(int nSlot);
void PortTeamShownReread(void);

#endif
