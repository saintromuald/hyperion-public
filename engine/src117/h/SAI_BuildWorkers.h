// SAI_BuildWorkers.h -- the worker list container of SAI_Build.cpp.
//
// Split from SAI_Build.h so that one header declares one `Free(`; include
// SAI_Build.h, not this file.
#pragma once

#include "platform.h"  // OC_X86_LAYOUT
#include "SAI_Build.h"

template <> struct TSaveContainer<TWorker,1000> {
    /*0x00*/ TSaveNode<TWorker,1000> *pPool;
    /*0x04*/ TWorker                 *pHead;
    void Free(TWorker *pNode);
};
typedef TSaveContainer<TWorker,1000> TWorkerSaveContainer;
static_assert(offsetof(TWorkerSaveContainer, pPool) == 0x00, "TSaveContainer<TWorker,1000>.pPool [ecx]");
OC_X86_LAYOUT(offsetof(TWorkerSaveContainer, pHead) == 0x04, "TSaveContainer<TWorker,1000>.pHead [ecx+4]");
OC_X86_LAYOUT(sizeof(TWorkerSaveContainer) == 8, "TSaveContainer<TWorker,1000>");
