// dlg_list.cpp -- the dialog manager's singly-linked list primitives. Bodies
// are in oracle VA order.
//
// Every node type the manager threads puts its `next` pointer at +0x00, so
// these five take `ctrl_t *` and the timer list reuses them unchanged. The two
// that can insert or unlink take `&head` rather than the head.

#include "../h/dlg_list.h"
#include "../h/dlg_dlg.h"
#include "../gen/globals117.h"

// 0x4CAB80. EAX is left holding the old head; no caller reads it.
ctrl_t *list_add_node_head(ctrl_t **ppHead, ctrl_t *pNode)
{
  ctrl_t *pOld = *ppHead;

  pNode->pNext = pOld;
  *ppHead = pNode;
  return pOld;
}

// 0x4CAB90. The first node the predicate accepts, or 0. The null test is
// hoisted above the loop, as in 1.17: a while over a do-while body.
ctrl_t *list_find_node(ctrl_t *pHead, PFN_NODE_TEST pfn)
{
  ctrl_t *pNode = pHead;

  if (pNode != 0) {
    do {
      if (pfn(pNode) != 0)
        return pNode;
      pNode = pNode->pNext;
    } while (pNode != 0);
  }
  return 0;
}

// 0x4CABC0. Identity, not equality: the compare is on the pointer.
BOOL list_has_node(ctrl_t *pHead, ctrl_t *pFind)
{
  ctrl_t *pNode = pHead;

  while (pNode != 0) {
    if (pNode == pFind)
      return 1;
    pNode = pNode->pNext;
  }
  return 0;
}

// 0x4CABE0. Ordered insert. The walk goes on while the new node's key is below
// the current node's, so the list runs high key first.
void list_insert_node(ctrl_t **ppHead, ctrl_t *pNode, PFN_NODE_TEST pfn)
{
  ctrl_t **ppCur = ppHead;
  int nKey = pfn(pNode);

  if (*ppCur != 0) {
    do {
      if (nKey >= pfn(*ppCur))
        break;
      ppCur = &(*ppCur)->pNext;
    } while (*ppCur != 0);
  }
  pNode->pNext = *ppCur;
  *ppCur = pNode;
}

// 0x4CAC30. Returns the node it unlinked, or 0 when it is not in the list. The
// head slot itself is never compared, only the links out of it.
ctrl_t *list_remove_node(ctrl_t **ppHead, ctrl_t *pNode)
{
  ctrl_t **ppCur = ppHead;

  while (*ppCur != 0) {
    if (*ppCur == pNode) {
      *ppCur = pNode->pNext;
      return pNode;
    }
    ppCur = &(*ppCur)->pNext;
  }
  return 0;
}
