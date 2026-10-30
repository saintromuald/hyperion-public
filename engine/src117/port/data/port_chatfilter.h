// port_chatfilter.h -- Settings' "Mature language filter": players' chat shown with
// Remastered's list of profanity starred out (port_chatfilter.cpp, docs/modules/data.md).
#ifndef PORT_CHATFILTER_H
#define PORT_CHATFILTER_H

#include <stddef.h>

// The setting ("chatFilter", on unless set off). The first time it is on, a thread reads and
// compiles the list from the Remastered install; until then chat shows as sent.
void PortChatFilterSet(bool bOn);
// The text as shown: pszText itself when nothing in it is starred out, else pszOut with the
// stars, cut to nOut bytes. Only what is shown changes; a replay keeps what was sent.
const char *PortChatClean(const char *pszText, char *pszOut, size_t nOut);

#endif
