// port_subtitles.h -- Settings' "Cinematic subtitles": Remastered's subtitles for the
// cinematics, drawn over them (port_subtitles.cpp, docs/modules/data.md).
#ifndef PORT_SUBTITLES_H
#define PORT_SUBTITLES_H

// The setting ("cinematicSubtitles", on unless set off).
void PortSubtitlesSet(bool bOn);
// From the video player (port_stormvid.cpp): a video began on the screen, by its file name, and
// the path of a mod's own copy when it plays that, whose .srt beside it is read in place of
// Remastered's; the time into it, in seconds, as each frame is drawn; its end.
void PortSubtitlesBegin(const char *pszFile, const char *pszOwn = NULL);
void PortSubtitlesAt(double dSeconds);
void PortSubtitlesEnd(void);

#endif
