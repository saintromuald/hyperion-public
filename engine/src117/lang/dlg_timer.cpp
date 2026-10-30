// dlg_timer.cpp -- the dialog manager's timer list: create a timer for a
// dialog, tick the due ones, and tear them down per id or per dialog. Bodies
// are in oracle VA order.
//
// Timers live on two lists. The active one is kept sorted by due time through
// timer_sort_fn, and timer_update parks each node it fires on the inactive list
// so a callback that creates or removes a timer cannot corrupt the walk; the
// tail of timer_update pushes them all back. dlg_list.cpp's primitives thread
// both unchanged, because a timer node's next pointer is at +0x00 too.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/dlg_timer.h"
#include "../h/dialog.h"
#include "../h/dlg_dlg.h"
#include "../h/minimap.h"
#include "../h/Storm.h"
#include "../gen/globals117.h"

// GetTickCount, which timer_create and timer_update call through the import
// table.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// The timer node, 0x1C bytes.
struct dlgtimer_t {
  /*0x00*/ dlgtimer_t *pNext;
  /*0x04*/ ctrl_t *pDlg;
  /*0x08*/ void (__fastcall *pfn)(ctrl_t *pDlg, dlg_word_t wId);
  /*0x0C*/ LONG nLastTick;
  /*0x10*/ LONG nInterval;
  /*0x14*/ dlg_word_t wId;
  /*0x16*/ dlg_byte_t _pad16[2];
  /*0x18*/ LONG nSlack;
};

OC_X86_LAYOUT(sizeof(dlgtimer_t) == 0x1C, "dlgtimer_t size   timer_create: push 1Ch / call SMemAlloc");
static_assert(offsetof(dlgtimer_t, pNext) == 0x00, "dlgtimer_t::pNext      timer_update: mov ebx, dword ptr [esi]");
OC_X86_LAYOUT(offsetof(dlgtimer_t, pDlg) == 0x04, "dlgtimer_t::pDlg       timer_update: mov edi, [esi+4h] / mov eax, [edi+2Ah]");
OC_X86_LAYOUT(offsetof(dlgtimer_t, pfn) == 0x08, "dlgtimer_t::pfn        timer_update: mov eax, [esi+8h] / call eax");
OC_X86_LAYOUT(offsetof(dlgtimer_t, nLastTick) == 0x0C, "dlgtimer_t::nLastTick  timer_create: mov dword ptr [esi+0Ch], eax");
OC_X86_LAYOUT(offsetof(dlgtimer_t, nInterval) == 0x10, "dlgtimer_t::nInterval  timer_update: mov edx, dword ptr [esi+10h]");
OC_X86_LAYOUT(offsetof(dlgtimer_t, wId) == 0x14, "dlgtimer_t::wId        timer_create: mov word ptr [esi+14h], di");
OC_X86_LAYOUT(offsetof(dlgtimer_t, nSlack) == 0x18, "dlgtimer_t::nSlack     timer_update: sub ecx, [esi+18h] / sar ecx, 5h");

// Globals this TU reaches, dual-spelled.

// 0x6CF798 / 0x6CF79C -- the two list heads, passed to list_find_node by value
// and to list_remove_node by address.
#ifdef OC_DLL_MODE
#define sgTimerActiveNodes_g (*reinterpret_cast<dlgtimer_t **>(OC_VA(0x6CF798u)))
#define sgTimerInactiveNodes_g (*reinterpret_cast<dlgtimer_t **>(OC_VA(0x6CF79Cu)))
#else
extern dlgtimer_t *sgTimerActiveNodes_g;
extern dlgtimer_t *sgTimerInactiveNodes_g;
#endif

// 0x6CF7A0 / 0x6CF7A4 -- the search keys the two find predicates read, a
// ctrl_t * and a WORD.
#ifdef OC_DLL_MODE
#define sgTimerFindCtrl_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x6CF7A0u)))
#define sgwTimerFindIdx_g (*reinterpret_cast<dlg_word_t *>(OC_VA(0x6CF7A4u)))
#else
extern ctrl_t *sgTimerFindCtrl_g;
extern dlg_word_t sgwTimerFindIdx_g;
#endif

// 0x4F118C -- "timer.cpp", this TU's allocator owner tag, which timer_create's
// SMemAlloc and the SMemFree calls pass.
#ifdef OC_DLL_MODE
#define sgszTimerCpp (reinterpret_cast<const char *>(OC_VA(0x4F118Cu)))
#else
extern const char sgszTimerCpp[1];
#endif

// The list primitives are typed over ctrl_t *, the manager's generic node; a
// timer node is threaded by the same +0x00 link and needs the cast at each use.
#define TIMER_HEAD(h) (reinterpret_cast<ctrl_t **>(&(h)))
#define TIMER_NODE(p) (reinterpret_cast<ctrl_t *>(p))
#define TIMER_TEST(f) (reinterpret_cast<PFN_NODE_TEST>(f))

// 0x4C5840 (166 B). Drop every timer either list holds for one dialog. Each
// pass re-reads the head and re-runs the search, so a list that still has
// nodes but none of this dialog's ends the pass on the null find.
void timer_remove_ctrl(ctrl_t *pDlg)
{
  dlgtimer_t *pNode;

  if (sgTimerActiveNodes_g != 0) {
    do {
      sgTimerFindCtrl_g = pDlg;
      pNode = reinterpret_cast<dlgtimer_t *>(
          list_find_node(TIMER_NODE(sgTimerActiveNodes_g),
                         TIMER_TEST(timer_find_ctrl_fn)));
      if (pNode != 0) {
        list_remove_node(TIMER_HEAD(sgTimerActiveNodes_g), TIMER_NODE(pNode));
        SMemFree(pNode, sgszTimerCpp, 0xAE, 0);
      }
    } while (sgTimerActiveNodes_g != 0 && pNode != 0);
  }

  if (sgTimerInactiveNodes_g != 0) {
    do {
      sgTimerFindCtrl_g = pDlg;
      pNode = reinterpret_cast<dlgtimer_t *>(
          list_find_node(TIMER_NODE(sgTimerInactiveNodes_g),
                         TIMER_TEST(timer_find_ctrl_fn)));
      if (pNode != 0) {
        list_remove_node(TIMER_HEAD(sgTimerInactiveNodes_g), TIMER_NODE(pNode));
        SMemFree(pNode, sgszTimerCpp, 0xBB, 0);
      }
    } while (sgTimerInactiveNodes_g != 0 && pNode != 0);
  }
}

// 0x4C58F0 (107 B). Drop one timer by dialog and id, searching the active list
// first.
void timer_remove_node(ctrl_t *pDlg, int nId)
{
  ctrl_t **ppHead = 0;
  dlgtimer_t *pNode = 0;

  sgTimerFindCtrl_g = pDlg;
  sgwTimerFindIdx_g = static_cast<dlg_word_t>(nId);
  if (sgTimerActiveNodes_g != 0) {
    ppHead = TIMER_HEAD(sgTimerActiveNodes_g);
    pNode = reinterpret_cast<dlgtimer_t *>(
        list_find_node(TIMER_NODE(sgTimerActiveNodes_g),
                       TIMER_TEST(timer_find_idx_fn)));
  }
  if (pNode == 0 && sgTimerInactiveNodes_g != 0) {
    ppHead = TIMER_HEAD(sgTimerInactiveNodes_g);
    pNode = reinterpret_cast<dlgtimer_t *>(
        list_find_node(TIMER_NODE(sgTimerInactiveNodes_g),
                       TIMER_TEST(timer_find_idx_fn)));
  }
  if (pNode != 0) {
    list_remove_node(ppHead, TIMER_NODE(pNode));
    SMemFree(pNode, sgszTimerCpp, 0x9B, 0);
  }
}

// timer_create
// Recycle this dialog's timer of the same id from either list, else allocate, then
// re-insert in due order. The search keys are stored only on the active arm, so an
// empty active list searches the inactive one with whatever the previous caller left
// behind: 1.17's bug, kept.
void timer_create(ctrl_t *pDlg, dlg_word_t wId, int nInterval, PFN_TIMER_FN *pfn)
{
  dlgtimer_t *pNode = 0;

  if (sgTimerActiveNodes_g != 0) {
    sgTimerFindCtrl_g = pDlg;
    sgwTimerFindIdx_g = wId;
    pNode = reinterpret_cast<dlgtimer_t *>(
        list_find_node(TIMER_NODE(sgTimerActiveNodes_g),
                       TIMER_TEST(timer_find_idx_fn)));
    if (pNode != 0)
      list_remove_node(TIMER_HEAD(sgTimerActiveNodes_g), TIMER_NODE(pNode));
  }
  if (pNode == 0 && sgTimerInactiveNodes_g != 0) {
    pNode = reinterpret_cast<dlgtimer_t *>(
        list_find_node(TIMER_NODE(sgTimerInactiveNodes_g),
                       TIMER_TEST(timer_find_idx_fn)));
    if (pNode != 0)
      list_remove_node(TIMER_HEAD(sgTimerInactiveNodes_g), TIMER_NODE(pNode));
  }
  if (pNode == 0)
    pNode = static_cast<dlgtimer_t *>(SMemAlloc(sizeof(dlgtimer_t), sgszTimerCpp, 0x75, 0));

  pNode->pfn = reinterpret_cast<void(__fastcall *)(ctrl_t *, dlg_word_t)>(pfn);
  pNode->pDlg = pDlg;
  pNode->wId = wId;
  pNode->nInterval = nInterval;
  pNode->nLastTick = static_cast<LONG>(GetTickCount());
  pNode->nSlack = 0;
  list_insert_node(TIMER_HEAD(sgTimerActiveNodes_g), TIMER_NODE(pNode),
                   TIMER_TEST(timer_sort_fn));
}

// 0x4C5A20 (272 B). Fire every timer whose interval has elapsed. A node that
// fires is parked on the inactive list before its callback runs, so the
// callback may create or destroy timers; the walk only continues into a
// successor the active list still holds. The tail moves the parked nodes back.
void timer_update()
{
  dlgevt_t evt;
  dlgtimer_t *pNode;
  dlgtimer_t *pNext;
  LONG nNow;

  pNode = sgTimerActiveNodes_g;
  nNow = static_cast<LONG>(GetTickCount());
  while (pNode != 0) {
    LONG nInterval = pNode->nInterval;
    LONG nLate = nNow - pNode->nLastTick;

    if (nLate - nInterval <= 0)
      break;
    nLate -= nInterval;
    if (nLate < 500)
      pNode->nSlack += (nLate - pNode->nSlack) >> 5;
    pNode->nLastTick = nNow;
    pNext = pNode->pNext;
    if (pNode->pfn != 0) {
      pNode->pfn(pNode->pDlg, pNode->wId);
    } else {
      // __restrict, so the handler load can precede the stores into evt, as in
      // 1.17.
      ctrl_t *__restrict pDlg = pNode->pDlg;

      evt.wTag = 0x0E;
      evt.dwParam = pNode->wId;
      evt.nX = static_cast<short>(gCursorPos_g[0]);
      evt.nY = static_cast<short>(gCursorPos_g[1]);
      *reinterpret_cast<LONG *>(evt._unmapped08) = nInterval;
      evt.dwId = 8;
      pDlg->pfnEvt(pDlg, &evt);
    }
    if (TIMER_NODE(pNode) == list_remove_node(TIMER_HEAD(sgTimerActiveNodes_g),
                                              TIMER_NODE(pNode)))
      list_add_node_head(TIMER_HEAD(sgTimerInactiveNodes_g), TIMER_NODE(pNode));
    if (pNext != 0
        && list_has_node(reinterpret_cast<ctrl_t *>(&sgTimerActiveNodes_g),
                         TIMER_NODE(pNext)) == 0)
      break;
    pNode = pNext;
  }

  pNode = sgTimerInactiveNodes_g;
  while (sgTimerInactiveNodes_g != 0) {
    list_remove_node(TIMER_HEAD(sgTimerInactiveNodes_g),
                     TIMER_NODE(sgTimerInactiveNodes_g));
    list_insert_node(TIMER_HEAD(sgTimerActiveNodes_g), TIMER_NODE(pNode),
                     TIMER_TEST(timer_sort_fn));
    pNode = sgTimerInactiveNodes_g;
  }
}

// 0x4C5B30 (33 B). The predicate timer_create and timer_remove_node search
// with: the node's dialog and id against the two published keys.
int timer_find_idx_fn(dlgtimer_t *pNode)
{
  if (pNode->pDlg == sgTimerFindCtrl_g && pNode->wId == sgwTimerFindIdx_g)
    return 1;
  return 0;
}

// 0x4C5B60 (15 B). The predicate timer_remove_ctrl searches with: the dialog
// alone, whatever the id.
int timer_find_ctrl_fn(dlgtimer_t *pNode)
{
  return pNode->pDlg == sgTimerFindCtrl_g;
}

// 0x4C5B70 (9 B). The ordering key for the active list. Negated, because
// list_insert_node walks while the new key is below the current one, so the
// smallest due time ends up first.
int timer_sort_fn(dlgtimer_t *pNode)
{
  return -(pNode->nInterval + pNode->nLastTick);
}
