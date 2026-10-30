// port_remastered.h -- a StarCraft: Remastered install as one of Storm's archives
// (port_remastered.cpp).
#ifndef PORT_REMASTERED_H
#define PORT_REMASTERED_H

#include <stddef.h>

// Where the game's files come from: 1.17's MPQs, a Remastered install alone, or (for
// development) Remastered's archive searched before the MPQs, which serve what it lacks.
enum {
  PORT_SOURCE_MPQ = 0,
  PORT_SOURCE_REMASTERED = 1,
  PORT_SOURCE_BOTH = 2,
};

// The source this run reads, decided once: HYPERION_DATA_SOURCE, else the "data" setting,
// each "mpq", "remastered", "both" or "auto"; auto, the default, is the MPQs when StarDat.mpq
// is in the game's folder and Remastered when it is not and an install is found.
int PortGameSource(void);

// The name open_archives opens Remastered's archive by (init.cpp), which Storm takes as that
// archive and no file (port_stormfile.cpp).
extern const char PORT_REMASTERED_ARCHIVE[];

// The file 1.17 asks for by pszName, out of the install or made from what it has, in a block
// from SDL_malloc the caller frees with SDL_free; NULL when the install has nothing for it.
void *PortRemasteredLoad(const char *pszName, unsigned long *pcbSize);

// English's stat_txt.tbl, the classic's form, whatever the game's language, as PortRemasteredLoad's
// block; NULL when the game does not read the install.
void *PortRemasteredStatTxtEnglish(unsigned long *pcbSize);

// A cinematic's copy in HD (smk\terran1.webm): 960 x 540 in HD2\ for a window of nRows or fewer
// of them, else 1920 x 1080 at the top; the other when the install lacks that one. As
// PortRemasteredLoad's block; NULL when the install has neither, or while HD is held
// (PortCascRead).
void *PortRemasteredVideoHd(const char *pszName, int nRows, unsigned long *pcbSize);

// When no file is at pszPath (the game folder's Local.dll), the install's Local.dll for 1.17's
// locale (locales\enUS\Local.dll) in its place, and true; false when it leaves pszPath be.
bool PortRemasteredLocalDll(char *pszPath, size_t cchPath);

#endif
