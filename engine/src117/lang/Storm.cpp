// Storm.cpp -- the global operator new and operator delete, which forward to
// Storm's SMemAlloc and SMemFree (src117/h/Storm.h).
#include "Storm.h"

// The two owner tags are the oracle's .rdata strings, 0x4EB7D0 "new" and
// 0x4EB7C8 "delete". Storm logs an owner tag and a line with each allocation;
// these pass a bare tag and the literal -1, not __FILE__ and __LINE__.
#define OC_TAG_NEW    "new"
#define OC_TAG_DELETE "delete"

// Storm.h:1024 (declaration).
//            The callee is __stdcall and EAX passes straight through. No null
//            check: Storm's allocator is fail-fast.
void* __cdecl operator new(size_t bytes) {
  return SMemAlloc((DWORD)bytes, OC_TAG_NEW, -1, 0);
}

// Storm.h:1028 (declaration).
//            SMemFree's BOOL is discarded, as in the oracle.
void __cdecl operator delete(void* ptr) {
  if (ptr) {
    SMemFree(ptr, OC_TAG_DELETE, -1, 0);
  }
}
