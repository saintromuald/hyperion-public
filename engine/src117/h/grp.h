// grp.h -- the GRP graphic-file structs: a group header and its frame table.
//
// They are not in src117/h/CImage.h because src117/gen/oracle_thunks.cpp includes CImage.h
// together with the reference tree's GDS/gds.h, which defines GROUP and FRAME itself, and
// two definitions in one TU is C2011. CImage.h keeps the forward declarations and its two
// pointer typedefs, which is all its own signatures need; the definitions live here,
// and only src117/lang/CImage.cpp, the one TU that dereferences a GROUP, includes this
// file. Merge it back into CImage.h once oracle_thunks.cpp stops including gds.h.
#pragma once

#include "platform.h"   // BYTE / WORD, and <stddef.h> for offsetof
#include <string.h>     // Hyperion: memcpy, GroupHasFrame's count

// Both structs are packed, and the packing is derived rather than assumed: GROUP::frames
// begins at +0x06 while a FRAME is 8 bytes containing a DWORD, and under natural alignment
// the compiler would slide `frames` to +0x08 and every frame lookup would miss. That is
// what a file-format struct looks like -- the GRP header is read straight off disk.
#pragma pack(push, 1)

// Hyperion: a frame's place and size in its group. Classic's are bytes; at 64 bits they
// are WORDs, so that a frame Hyperion makes from a mod's sheet can be past 255 a side
// (port_modsheet.cpp). The loader widens the file's bytes (PortGroupNative).
#if defined(_WIN64) || defined(__LP64__)
typedef WORD frame_dim_t;
#else
typedef BYTE frame_dim_t;
#endif

// One GRP frame header, 8 bytes: CImage::GetDimensions and CImage::ClickableAt both reach
// the table with `lea <p>, [<group> + <frameIndex>*8]`.
struct FRAME {
  //   +0x00  bOffsetX, this frame's X offset inside the group: CImage::ClickableAt's
  //          flipped arm doubles it and subtracts it from the group's own width, which is
  //          the mirror transform. A BYTE load with movzx, so unsigned.
  /*0x00*/ frame_dim_t bOffsetX;
  //   +0x01  the Y offset by GRP convention, but nothing in 1.17 reads it, so it stays
  //          unmapped rather than named on a reader's expectation.
  /*0x01*/ frame_dim_t _unmapped_01;
  //   +0x02  bWidth, and +0x03 bHeight. CImage::GetDimensions adds each to the image's
  //          map X and Y after a `dec`, filling `right = left + w - 1` and
  //          `bottom = top + h - 1` of the rect it writes. movzx loads, so unsigned.
  /*0x02*/ frame_dim_t bWidth;
  /*0x03*/ frame_dim_t bHeight;
  //   +0x04  pData, the frame's per-row RLE line-offset table. frame_clickable_at
  //          (0x40FD50) loads the dword, indexes through it and adds the loaded value
  //          back to it, which makes it a base pointer at run time and not a file offset.

  // Hyperion: in the file the +4 dword is the frame's offset from the group,
  // bit 31 adding 256 to the width, and 1.17 turns it into a pointer in place for
  // the groups it draws through pData (the images, the cursors, the creep edges).
  // At 64 bits the pointer does not fit, so there every GRP the engine loads is
  // given a frame table of its own ahead of the file's bytes (PortGroupNative,
  // src117/port/data/port_formats.cpp): the file's dword in dwData and the frame's data
  // in pData, whether or not 1.17 would have made a pointer of it
  //. GrpFrameWord, below, reads the file's dword on
  // either width.
#if defined(_WIN64) || defined(__LP64__)
  /*0x04*/ DWORD dwData;
#endif
  /*0x04*/ BYTE *pData;
#if defined(_WIN64) || defined(__LP64__)
  // Hyperion: a frame made from a mod's sheet may hold more than 64 KiB, past what the
  // WORDs of classic's line table reach, so its rows' offsets from pData are DWORDs here; NULL
  // for every frame of a file, whose table is at pData (GrpFrameRow).
  const DWORD *pLines;
#endif
};

// A GRP file's group header, followed by its variable-length frame table. `frames[1]` is
// the usual stand-in for that table: only its address is ever used.
struct GROUP {
  //   +0x00  the frame count by GRP convention. Nothing in 1.17 reads it -- CImage
  //          carries its own frame index at +0x1A and never bounds-checks against the
  //          group -- so it stays unmapped.
  /*0x00*/ BYTE _unmapped_00[2];
  //   +0x02  wMaxWidth, the group's own width: CImage::ClickableAt mirrors a click about
  //          it. A WORD load with movzx, so two bytes and unsigned.
  /*0x02*/ WORD wMaxWidth;
  //   +0x04  wMaxHeight. CSprite::Init reads it two instructions after wMaxWidth, clamps
  //          it the same way and stores it as the sprite's cached draw height.
  /*0x04*/ WORD wMaxHeight;
  //   +0x06  frames. GetJuiceFrame (0x40F670) returns exactly `GROUP* + 6`, and
  //          CImage::ClickableAt forms the same address before passing it as a frame
  //          pointer.
  /*0x06*/ FRAME frames[1];
};

#pragma pack(pop)

OC_X86_LAYOUT(sizeof(FRAME)              == 8,    "FRAME size       (lea [g+i*8] in GetDimensions + ClickableAt)");
static_assert(offsetof(FRAME, bOffsetX)  == 0x00, "FRAME::bOffsetX  [frame+0]");
OC_X86_LAYOUT(offsetof(FRAME, bWidth)    == 0x02, "FRAME::bWidth    [esi+8h], esi = GROUP + i*8");
OC_X86_LAYOUT(offsetof(FRAME, bHeight)   == 0x03, "FRAME::bHeight   [esi+9h], esi = GROUP + i*8");
OC_X86_LAYOUT(offsetof(FRAME, pData)     == 0x04, "FRAME::pData     frame_clickable_at `mov ecx,[ecx+4h]` then `add eax, ecx` -- a base pointer");
#if defined(_WIN64) || defined(__LP64__)
static_assert(offsetof(FRAME, dwData)    == 0x08, "FRAME::dwData    the file's +4 dword, after the four WORDs");
#endif

// Hyperion: the file's +4 dword of a frame, the offset of its data from the
// group and bit 31: on x86 pData itself, where 1.17's code has not made a pointer of
// it; at 64 bits the copy the loader keeps beside the pointer.
inline DWORD GrpFrameWord(const FRAME *pFrame)
{
#if defined(_WIN64) || defined(__LP64__)
  return pFrame->dwData;
#else
  return static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(pFrame->pData));
#endif
}

// Hyperion: where row y of a frame's codes starts: by classic's WORD offsets at pData, or
// by the DWORDs of a frame made from a mod's sheet (pLines).
inline const BYTE *GrpFrameRow(const FRAME *pFrame, int y)
{
#if defined(_WIN64) || defined(__LP64__)
  if (pFrame->pLines != NULL)
    return pFrame->pData + pFrame->pLines[y];
#endif
  WORD wOffset;
  memcpy(&wOffset, pFrame->pData + 2 * y, sizeof(wOffset));
  return pFrame->pData + wOffset;
}

// Hyperion: whether a group has the frame an image is on, by the file's count. 1.17 never
// tests it, and a frame past the table is the bytes after it, which drawing or a click follows as
// a pointer; an EUD map can put an image there (an 8 by 8 group of 2 frames on frame 4), and
// Hyperion then neither draws it nor clicks it. Frame 0 is always there, also in the groups the
// healthbars and placement boxes size in place, whose counts are not set.
inline unsigned GroupFrames(const GROUP *pGroup)
{
  WORD nFrames;
  memcpy(&nFrames, pGroup->_unmapped_00, sizeof(nFrames));
  return nFrames;
}
inline bool GroupHasFrame(const GROUP *pGroup, unsigned nFrame)
{
  return nFrame == 0 || nFrame < GroupFrames(pGroup);
}
OC_X86_LAYOUT(sizeof(GROUP)              == 14,   "GROUP size       6-byte header + one FRAME (the table is variable-length)");
static_assert(offsetof(GROUP, wMaxWidth) == 0x02, "GROUP::wMaxWidth [edx+2h]");
static_assert(offsetof(GROUP, wMaxHeight)== 0x04, "GROUP::wMaxHeight [eax+4h] -- CSprite::Init's clamped height read");
static_assert(offsetof(GROUP, frames)    == 0x06, "GROUP::frames    GetJuiceFrame `add eax,6h`");
