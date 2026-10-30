// port_layouts.h -- 1.17's dialog layouts from Hyperion's source
// (port_layouts.cpp, gen/layouts117.cpp).
#ifndef PORT_LAYOUTS_H
#define PORT_LAYOUTS_H

// One picture of an animated button (a .bin's 0x1E-byte item): its flags, its video and where.
struct PortLayoutItem {
  unsigned dwFlags;
  const char *pszFile;
  short nX, nY;
};

// One control of a layout (a .bin's 0x56-byte record), the fields 1.17's layouts set.
struct PortLayoutCtrl {
  short nId;
  unsigned short wType;
  short aRect[4];  // left, top, right, bottom
  unsigned short wWidth, wHeight;
  unsigned dwFlags;
  short aResponse[4];
  const char *pszText;
  short aItems[4];  // an animated button's pictures' rect
  const PortLayoutItem *pItems;
  int nItems;
};

// A layout: its file's name, its controls in the order the game walks them (the dialog
// first), and any bytes the file has after what its records name.
struct PortLayout {
  const char *pszName;
  const PortLayoutCtrl *pCtrls;
  int nCtrls;
  const char *pTail;
  int cbTail;
};

extern const PortLayout g_aPortLayouts117[];
extern const int g_nPortLayouts117;

// The layout file pszName (rez\<name>.bin) made from the table, in a block from SDL_malloc the
// caller frees with SDL_free, and its size; NULL when the table has no such layout.
void *PortLayoutFile(const char *pszName, unsigned long *pcbSize);
// The table's layout for the file pszName, or NULL.
const PortLayout *PortLayoutFind(const char *pszName);
// The same for a layout of another table (port_local.cpp's).
void *PortLayoutMake(const PortLayout *pLayout, unsigned long *pcbSize);

#endif
