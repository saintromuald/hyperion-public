// PriorityQueue.h -- the binary heap the two AI path searches share, as
// PriorityQueue<areauser_t,125> in sai_PathFindArea.cpp and
// PriorityQueue<pathlocaluser_t,150> in sai_PathFindLocal.cpp.
//
// m_elems is N+1 pointers with slot 0 reserved, m_count follows it, then
// m_pSentinel: heapInsert and heapUpdate park the sentinel in slot 0 so the
// sift-up loop needs no index test. The key is a signed int, and the smallest
// key sits at index 1.

#ifndef OC117_PRIORITYQUEUE_H
#define OC117_PRIORITYQUEUE_H

#include "platform.h"

// Only the primary template is declared here. Each instantiation is an explicit
// specialization in its own TU's header, its members defined in that TU; the
// argument lists are spelled without spaces, as the game's own symbols spell them.
template <class T, int N> struct PriorityQueue;

#endif  // OC117_PRIORITYQUEUE_H
