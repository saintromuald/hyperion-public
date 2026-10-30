// dlg_timer.h -- declarations for src117/lang/dlg_timer.cpp, the dialog
// manager's timer list: create, tick, and the two ways a timer is torn down.
// Free functions are __fastcall through /Gr and are declared without the
// keyword.
//
// timer_create, timer_remove_node (minimap.h) and timer_update (event_Win.h)
// are declared by their callers' headers. The node type is local to the .cpp.

#ifndef OC117_DLG_TIMER_H
#define OC117_DLG_TIMER_H

#include "platform.h"

struct ctrl_t;
struct dlgtimer_t;

// 0x4C5840. Frees every timer either list holds for one dialog. ECX is read as
// 32 bits and lands in sgTimerFindCtrl_g, which timer_find_ctrl_fn compares
// against a node's +0x04; the single caller discards EAX.
void timer_remove_ctrl(ctrl_t *pDlg);

// 0x4C5B30 / 0x4C5B60 / 0x4C5B70. The three predicates the list primitives
// call, each only an address handed to list_find_node or list_insert_node, so
// they take dlg_dlg.h's PFN_NODE_TEST type and return int. list_insert_node
// compares the sort key signed.
int timer_find_idx_fn(dlgtimer_t *pNode);
int timer_find_ctrl_fn(dlgtimer_t *pNode);
int timer_sort_fn(dlgtimer_t *pNode);

#endif  // OC117_DLG_TIMER_H
