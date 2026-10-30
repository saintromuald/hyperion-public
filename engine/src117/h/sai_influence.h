// sai_influence.h -- declarations for sai_influence.cpp: LineClip, the AI's
// segment-to-rectangle clip.

#ifndef OC117_SAI_INFLUENCE_H
#define OC117_SAI_INFLUENCE_H

#include "platform.h"
#include "point16.h"
#include "rect16.h"

// 0x48C6D0, `ret 4h`. Cohen-Sutherland: clips the segment to the rectangle in
// place and returns whether anything survived. The endpoints, in ECX and EDX,
// are in/out; the stack argument is the rectangle. 1.17 loads each endpoint
// whole and splits y off with a shift, so a DWORD * would fit as well; point16_t
// is what the rest of the cluster uses.
#include "pos.h"  // Hyperion: mpoint_t, mrect_t
BOOL LineClip(mpoint_t *pA, mpoint_t *pB, const mrect_t *pRect);
#if defined(_WIN64) || defined(__LP64__)
// Hyperion: 1.17's points and box, for the callers that still take them.
inline BOOL LineClip(point16_t *pA, point16_t *pB, const rect16_t *pRect) {
  mpoint_t a = *pA, b = *pB;
  const mrect_t rc = *pRect;
  BOOL bAccept = LineClip(&a, &b, &rc);
  *pA = a;
  *pB = b;
  return bAccept;
}
#endif

#endif  // OC117_SAI_INFLUENCE_H
