// snd_streamed.h -- the streamed-music list node and its TSList
// specialization, in a header of their own so snd.h's includers need not take
// TSList.h. Included by snd.cpp.

#ifndef OC117_SND_STREAMED_H
#define OC117_SND_STREAMED_H

#include "platform.h"
#include "TSList.h"

// One streamed track. +0/+4 are the link (Remove reads [esi] as prev and
// [esi+4] as next); +8 is the DDA file handle, which snd_update_streams hands
// to SFileDdaGetPos.
struct STREAMED {
  /*0x00*/ TSLink<STREAMED> link;
  /*0x08*/ void *hFile;  // Hyperion: `int` in 1.17
};
OC_X86_LAYOUT(sizeof(STREAMED) == 0x0C, "push 0Ch / call SMemAlloc in play_digital");
static_assert(offsetof(STREAMED, link) == 0x00, "Remove reads [esi] as prev");
OC_X86_LAYOUT(offsetof(STREAMED, hFile) == 0x08,
              "snd_update_streams: push dword ptr [esi+8h] -> SFileDdaGetPos");

// The list itself, laid out as TSList.h's.
template <> struct TSList<STREAMED,TSGetLink<STREAMED> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<STREAMED> link;
  // 0x49D520 (107 B). Stop, close and free every track.
  void Clear(void);
  // 0x49D590 (126 B), `ret 4h`. Stop, close and free pNode; return what
  // followed it, or NULL at the terminator.
  // noinline: snd_update_streams calls it, and in this TU /Ob2 would inline it.
  __declspec(noinline) STREAMED *Remove(STREAMED *pNode);
};
typedef TSList<STREAMED, TSGetLink<STREAMED> > STREAMEDLIST;

static_assert(offsetof(STREAMEDLIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(STREAMEDLIST, link) == 0x04,
              "snd_end_streamed passes 0x5093C8 as this; the head is read at 0x5093D0");
OC_X86_LAYOUT(sizeof(STREAMEDLIST) == 12, "0x5093C8 .. 0x5093D4");

#endif  // OC117_SND_STREAMED_H
