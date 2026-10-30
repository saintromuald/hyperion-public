// sai_influence.cpp -- segment clipping against the influence map's rectangle.

#include "../h/sai_influence.h"
#include "../h/point16.h"
#include "../h/rect16.h"
#include "../gen/globals117.h"

// The Cohen-Sutherland region code: bit 1 left, 2 right, 8 above, 4 below.
// The point comes in by address, which keeps 1.17's stack slots for 2, 8 and 4.
static short OutCode(const mpoint_t *pPt, const mrect_t *pRect)
{
  short nCode = static_cast<short>(pPt->x < pRect->left);
  nCode |= static_cast<short>(pPt->x > pRect->right ? 2 : 0);
  nCode |= static_cast<short>(pPt->y < pRect->top ? 8 : 0);
  nCode |= static_cast<short>(pPt->y > pRect->bottom ? 4 : 0);
  return nCode;
}

// 0x48C6D0 (616 B). Clip the segment to the rectangle in place; TRUE if any of
// it survived. The done/accept flags, the do-while and the (b - a) * (edge - a)
// operand order follow 1.17's loop and its separate top and bottom arms.
BOOL LineClip(mpoint_t *pA, mpoint_t *pB, const mrect_t *pRect)
{
  mpoint_t a = *pA;  // Hyperion: map points and box
  mpoint_t b = *pB;

  short nCodeA = OutCode(&a, pRect);
  short nCodeB = OutCode(&b, pRect);
  BOOL bAccept = FALSE;
  BOOL bDone = FALSE;
  do
  {
    if (!(nCodeA | nCodeB))
    {
      bAccept = TRUE;
      bDone = TRUE;
    }
    else if (nCodeA & nCodeB)
    {
      bDone = TRUE;
    }
    else
    {
      short nCode = nCodeA ? nCodeA : nCodeB;
      spos_t x;
      spos_t y;
      if (nCode & 8)
      {
        x = PosS(a.x + (b.x - a.x) * (pRect->top - a.y) / (b.y - a.y));
        y = pRect->top;
      }
      else if (nCode & 4)
      {
        x = PosS(a.x + (b.x - a.x) * (pRect->bottom - a.y) / (b.y - a.y));
        y = pRect->bottom;
      }
      else if (nCode & 2)
      {
        y = PosS(a.y + (b.y - a.y) * (pRect->right - a.x) / (b.x - a.x));
        x = pRect->right;
      }
      else
      {
        y = PosS(a.y + (b.y - a.y) * (pRect->left - a.x) / (b.x - a.x));
        x = pRect->left;
      }
      if (nCode == nCodeA)
      {
        a.x = x;
        a.y = y;
        nCodeA = OutCode(&a, pRect);
      }
      else
      {
        b.x = x;
        b.y = y;
        nCodeB = OutCode(&b, pRect);
      }
    }
  } while (!bDone);

  if (bAccept)
  {
    *pA = a;
    *pB = b;
  }
  return bAccept;
}
