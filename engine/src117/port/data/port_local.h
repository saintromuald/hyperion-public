// port_local.h -- Hyperion's stand-in for what an install may lack of the classic game's small
// files: Local.dll, and the game types (port_local.cpp, gen/local117.cpp).
#ifndef PORT_LOCAL_H
#define PORT_LOCAL_H

#include <stddef.h>
#include "port_layouts.h"

// A file of the generated source: its name, bytes and size.
struct PortLocalFile {
  const char *pszName;
  const unsigned char *pData;
  size_t cbData;
};

extern const PortLocalFile g_aPortLocalAccels[];
extern const int g_nPortLocalAccels;
extern const PortLayout g_aPortLocalLayouts[];
extern const int g_nPortLocalLayouts;
extern const PortLocalFile g_aPortLocalTemplates[];
extern const int g_nPortLocalTemplates;

// No Local.dll was found or loaded (InitLocal): the game runs on the stand-in from here on.
void PortLocalUseOwn(void);
bool PortLocalOwn(void);

// Local.dll's named resource (gds_local.cpp's GetResource): the menus' error pop-up's layouts
// (GLUEOK_BIN, GLUEOKCANCEL_BIN) and pictures (GLUEOK_PCX, GLUEOKCANCEL_PCX). The block stays,
// as a module's resource does; NULL for any other name.
const void *PortLocalResource(const char *pszName, unsigned long *pcbSize);

// Local.dll's key table nResId (0x65, 0x66 or 0x67), or NULL.
const void *PortLocalAccelerators(int nResId, unsigned long *pcbSize);

// One of Local.dll's dialogs (gds_appfat.cpp), in Hyperion's words: what the classic dialog
// returned, 1 for going on.
int PortLocalDialog(int nTemplate, const char *pszText);

// Templates\<name>, the classic game types and their list, in a block from SDL_malloc; NULL
// when the name is not one.
void *PortLocalTemplate(const char *pszName, unsigned long *pcbSize);

#endif
