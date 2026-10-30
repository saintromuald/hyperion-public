// debug.cpp -- the screen invalidation helper.

#include "../h/debug.h"
#include <string.h>
#include "../h/rect32.h"
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../h/img.h"  // Hyperion: _img_list

// One byte per 16x16 screen tile, 40 across and 30 down. Hyperion: the
// grid is the view's, g_portView.pbInvalMap (port_view.cpp).

// Converts a pixel rectangle to tiles in place and marks those tiles dirty.
// The post-increment in the loop test leaves top one past bottom on exit.
void invalidate_rect(rect32_t *prc) {
  prc->left /= 16;
  prc->top /= 16;
  prc->right = (prc->right + 15) / 16;
  prc->bottom = (prc->bottom + 15) / 16;
  // Hyperion: clamped to the grid, its one spare column and row included, which 1.17's
  // own rectangles reach at the screen's right and foot. A rectangle off the screen comes
  // from an image whose screen fields a larger view drew and marks nothing.
  if (prc->left < 0)
    prc->left = 0;
  if (prc->top < 0)
    prc->top = 0;
  if (prc->right > g_portView.nGridWidth)
    prc->right = g_portView.nGridWidth;
  if (prc->bottom > g_portView.nGridHeight)
    prc->bottom = g_portView.nGridHeight;
  if (prc->left > prc->right || prc->top > prc->bottom) {
    byte_6D0BF5 |= 2;
    return;
  }
  BYTE *pRow = &g_portView.pbInvalMap[prc->top * g_portView.nGridWidth + prc->left];
  int nWidth = prc->right - prc->left + 1;
  while (prc->top++ <= prc->bottom) {
    memset(pRow, 1, nWidth);
    pRow += g_portView.nGridWidth;
  }
  byte_6D0BF5 |= 2;
}
