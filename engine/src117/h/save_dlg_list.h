// save_dlg_list.h -- the save-game list node and its TSList specialization,
// kept out of save_dlg.h because snd.cpp avoids TSList.h and gluLoad.cpp owns
// the name SAVEGAMELIST for a different list. Included by save_dlg.cpp.

#ifndef OC117_SAVE_DLG_LIST_H
#define OC117_SAVE_DLG_LIST_H

#include "platform.h"
#include "TSList.h"

// The save-game list node, 0x28 bytes. Its name and struct key are Blizzard's,
// from the RTTI descriptor at 0x5109CC.
struct TSAVEGAME {
  /*0x00*/ TSLink<TSAVEGAME> link;
  /*0x08*/ DWORD dwValue;
  /*0x0C*/ char szName[0x1C];
};
static_assert(offsetof(TSAVEGAME, link) == 0x00,
              "mov [esi],0h / mov [esi+4h],0h straight after SMemAlloc");
OC_X86_LAYOUT(offsetof(TSAVEGAME, dwValue) == 0x08,
              "mov dword ptr [esi+8h], eax  <- header blob +0x28");
OC_X86_LAYOUT(offsetof(TSAVEGAME, szName) == 0x0C,
              "add esi, 0Ch is the inlined strcpy's destination");
OC_X86_LAYOUT(sizeof(TSAVEGAME) == 0x28, "push 28h / call SMemAlloc");

// The list as an explicit specialization, which declares its Clear member; the
// layout is TSList.h's.
template <> struct TSList<TSAVEGAME,TSGetLink<TSAVEGAME> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TSAVEGAME> link;
  // 0x49AC80 (84 B). Unlink and free every node.
  void Clear(void);
};
typedef TSList<TSAVEGAME, TSGetLink<TSAVEGAME> > TSAVEGAMELIST;

static_assert(offsetof(TSAVEGAMELIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(TSAVEGAMELIST, link) == 0x04, "Clear reads [this+8] as link.next");
OC_X86_LAYOUT(sizeof(TSAVEGAMELIST) == 12, "TSList.h layout");

#endif  // OC117_SAVE_DLG_LIST_H
