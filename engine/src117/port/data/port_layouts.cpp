// port_layouts.cpp -- 1.17's dialog layouts from Hyperion's source (milestone B).
//
// The game builds every dialog from a layout file, rez\<name>.bin, which 1.17's MPQs have and
// a StarCraft: Remastered install does not. gen/layouts117.cpp keeps 1.17's layouts as a table
// (tools/layouts117.py, the user's word of 2026-10-20 in the PRD) and PortLayoutFile makes a
// file back from it, byte for byte as 1.17's: the records in walk order from 0, the animated
// buttons' items after them, then the strings, each control's text and its items' files in
// walk order. dlg_load reads it as it read the file (port_formats.cpp's PortDialogNative), so
// a layout changed in the table is a dialog changed in the game: the way to Hyperion's own
// layouts and to custom HUDs.
#include <SDL3/SDL.h>
#include <string.h>
#include <string>
#include "port_layouts.h"

// The table is generated beside the engine's other generated source and compiled here.
#include "../../gen/layouts117.cpp"

namespace {

const size_t kRecord = 0x56;
const size_t kItem = 0x1E;

void Put16(std::string &s, size_t at, int v)
{
  s[at] = static_cast<char>(v & 0xFF);
  s[at + 1] = static_cast<char>((v >> 8) & 0xFF);
}

void Put32(std::string &s, size_t at, unsigned v)
{
  Put16(s, at, static_cast<int>(v & 0xFFFF));
  Put16(s, at + 2, static_cast<int>(v >> 16));
}

}  // namespace

const PortLayout *PortLayoutFind(const char *pszName)
{
  for (int i = 0; i < g_nPortLayouts117; ++i)
    if (SDL_strcasecmp(g_aPortLayouts117[i].pszName, pszName) == 0)
      return &g_aPortLayouts117[i];
  return NULL;
}

void *PortLayoutFile(const char *pszName, unsigned long *pcbSize)
{
  *pcbSize = 0;
  const PortLayout *pLayout = PortLayoutFind(pszName);
  return pLayout != NULL ? PortLayoutMake(pLayout, pcbSize) : NULL;
}

void *PortLayoutMake(const PortLayout *pLayout, unsigned long *pcbSize)
{
  *pcbSize = 0;
  const int n = pLayout->nCtrls;
  size_t nItems = 0;
  for (int i = 0; i < n; ++i)
    nItems += static_cast<size_t>(pLayout->pCtrls[i].nItems);
  const size_t atItems = n * kRecord;
  const size_t atStrings = atItems + nItems * kItem;
  std::string file(atStrings, '\0');
  // a string's place, appended after the others; 0 for none
  auto PutString = [&](const char *psz) -> unsigned {
    if (psz == NULL)
      return 0;
    const size_t at = file.size();
    file.append(psz, strlen(psz) + 1);
    return static_cast<unsigned>(at);
  };
  size_t k = 0;
  for (int i = 0; i < n; ++i) {
    const PortLayoutCtrl &c = pLayout->pCtrls[i];
    const size_t r = i * kRecord;
    const unsigned nNext = i + 1 < n ? static_cast<unsigned>((i + 1) * kRecord) : 0;
    for (int j = 0; j < 4; ++j)
      Put16(file, r + 0x04 + 2 * j, c.aRect[j]);
    Put16(file, r + 0x0C, c.wWidth);
    Put16(file, r + 0x0E, c.wHeight);
    Put32(file, r + 0x14, PutString(c.pszText));
    Put32(file, r + 0x18, c.dwFlags);
    Put16(file, r + 0x20, c.nId);
    Put16(file, r + 0x22, c.wType);
    for (int j = 0; j < 4; ++j)
      Put16(file, r + 0x36 + 2 * j, c.aResponse[j]);
    // the dialog's controls hang from its +0x42, the others follow along +0x00
    Put32(file, i == 0 ? r + 0x42 : r, nNext);
    if (c.wType == 0x0E) {
      for (int j = 0; j < 4; ++j)
        Put16(file, r + 0x46 + 2 * j, c.aItems[j]);
      Put32(file, r + 0x42, c.nItems ? static_cast<unsigned>(atItems + k * kItem) : 0);
      for (int j = 0; j < c.nItems; ++j) {
        const PortLayoutItem &it = c.pItems[j];
        const size_t q = atItems + (k + j) * kItem;
        Put32(file, q, j + 1 < c.nItems ? static_cast<unsigned>(atItems + (k + j + 1) * kItem) : 0);
        Put32(file, q + 0x04, it.dwFlags);
        Put32(file, q + 0x0A, PutString(it.pszFile));
        Put16(file, q + 0x12, it.nX);
        Put16(file, q + 0x14, it.nY);
      }
      k += static_cast<size_t>(c.nItems);
    }
  }
  if (pLayout->cbTail > 0)
    file.append(pLayout->pTail, static_cast<size_t>(pLayout->cbTail));
  void *p = SDL_malloc(file.size());
  if (p == NULL)
    return NULL;
  memcpy(p, file.data(), file.size());
  *pcbSize = static_cast<unsigned long>(file.size());
  return p;
}
