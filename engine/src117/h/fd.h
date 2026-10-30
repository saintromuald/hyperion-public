// fd.h -- the file-download layer's teardown and callback registration,
// oracle 0x440640-0x44093F. The subsystem is three TUs in .text order: fd.cpp,
// fd_sessn.cpp (the session list) and fd_user.cpp (the per-user records and the
// transfer state machine). The record types live here because all three reach
// both of them.

#ifndef OC117_FD_H
#define OC117_FD_H

#include "oc_data.h"  // Hyperion: OC_VA
#include "platform.h"
#include "TSList.h"

#pragma pack(push, 1)

// The inner list's node, named from the RTTI object at 0x51076C
// (".?AUTUSER_INFO@@"). sub_4409B0 SMemAllocs 0x10 of these bytes, zeroes the
// link, then writes a BYTE id at +8, 0x10 at +9 and a zero DWORD at +0xC.
struct TUSER_INFO {
  /*0x00*/ TSLink<TUSER_INFO> link;
  /*0x08*/ BYTE bId;
  /*0x09*/ BYTE bState;
  /*0x0A*/ BYTE _unmapped_0A[2];
  /*0x0C*/ DWORD dwUnmapped_0C;
};
OC_X86_LAYOUT(sizeof(TUSER_INFO) == 0x10, "TUSER_INFO: SMemAlloc(0x10) in sub_4409B0");
OC_X86_LAYOUT(offsetof(TUSER_INFO, bId) == 0x08, "TUSER_INFO.bId");
OC_X86_LAYOUT(offsetof(TUSER_INFO, bState) == 0x09, "TUSER_INFO.bState");
OC_X86_LAYOUT(offsetof(TUSER_INFO, dwUnmapped_0C) == 0x0C, "TUSER_INFO.dwUnmapped_0C");

// A downloading peer's record, named from the RTTI object at 0x5107A4
// (".?AUTFD_USER@@"). sub_441150 SMemAllocs 0x48 of these bytes and initialises
// every field below in one run; the list is walked from dword_5032C4.
struct TFD_USER {
  /*0x00*/ TSLink<TFD_USER> link;
  /*0x08*/ BYTE bId;
  /*0x09*/ BYTE bPlayer;       // compared against the local id at 0x665F80
  /*0x0A*/ BYTE _unmapped_0A[2];
  /*0x0C*/ DWORD dwSize;       // byte length of the buffer at +0x44
  /*0x10*/ DWORD dwCrc;        // compared against comp_crc32 over that buffer
  /*0x14*/ char szName[0x20];  // SStrCopy capacity 0x20, and 0x14+0x20 == 0x34
  /*0x34*/ BYTE bFlags;        // 1 sending, 2 receiving, 8 failed
  /*0x35*/ BYTE _unmapped_35[3];
  /*0x38*/ DWORD dwSent;
  /*0x3C*/ BYTE b3C;           // zeroed together with b3D by one WORD store
  /*0x3D*/ BYTE b3D;
  /*0x3E*/ BYTE _unmapped_3E[2];
  /*0x40*/ DWORD dwReceived;
  /*0x44*/ void *pOwned;       // SMemAlloc(dwSize) tagged ("fd_user.cpp", 0x198)
};
OC_X86_LAYOUT(sizeof(TFD_USER) == 0x48, "TFD_USER: SMemAlloc(0x48) in sub_441150");
OC_X86_LAYOUT(offsetof(TFD_USER, bId) == 0x08, "TFD_USER.bId");
OC_X86_LAYOUT(offsetof(TFD_USER, bPlayer) == 0x09, "TFD_USER.bPlayer");
OC_X86_LAYOUT(offsetof(TFD_USER, dwSize) == 0x0C, "TFD_USER.dwSize");
OC_X86_LAYOUT(offsetof(TFD_USER, dwCrc) == 0x10, "TFD_USER.dwCrc");
OC_X86_LAYOUT(offsetof(TFD_USER, szName) == 0x14, "TFD_USER.szName");
OC_X86_LAYOUT(offsetof(TFD_USER, bFlags) == 0x34, "TFD_USER.bFlags");
OC_X86_LAYOUT(offsetof(TFD_USER, dwSent) == 0x38, "TFD_USER.dwSent");
OC_X86_LAYOUT(offsetof(TFD_USER, b3C) == 0x3C, "TFD_USER.b3C");
OC_X86_LAYOUT(offsetof(TFD_USER, b3D) == 0x3D, "TFD_USER.b3D");
OC_X86_LAYOUT(offsetof(TFD_USER, dwReceived) == 0x40, "TFD_USER.dwReceived");
OC_X86_LAYOUT(offsetof(TFD_USER, pOwned) == 0x44, "TFD_USER.pOwned");

// A download session, named from the RTTI object at 0x510788
// (".?AUTFD_SESSION@@"). TSList<TFD_SESSION>::NewNode allocates 0x128 plus a
// caller-supplied tail, and sub_440AB0 fills every field below right after it.
// dwSize and dwCrc are sub_440E70's two out-parameters, EDX carrying the size
// and the stack word the CRC.
struct TFD_SESSION {
  /*0x000*/ TSLink<TFD_SESSION> link;
  /*0x008*/ BYTE bId;              // from the counter at 0x665F81
  /*0x009*/ BYTE _unmapped_009[3];
  /*0x00C*/ DWORD dwUnmapped_00C;  // zeroed at creation, tested against 0
  /*0x010*/ DWORD dwSize;
  /*0x014*/ DWORD dwCrc;
  /*0x018*/ char szPath[0x104];    // SStrCopy capacity 0x104
  /*0x11C*/ TSList<TUSER_INFO, TSGetLink<TUSER_INFO> > nodes;

  // Unlinks the inner node list, then unlinks the session itself. Non-virtual,
  // so the layout above is unaffected.
  ~TFD_SESSION();
};
OC_X86_LAYOUT(sizeof(TFD_SESSION) == 0x128, "TFD_SESSION: SMemAlloc(arg + 0x128) in NewNode");
OC_X86_LAYOUT(offsetof(TFD_SESSION, bId) == 0x008, "TFD_SESSION.bId");
OC_X86_LAYOUT(offsetof(TFD_SESSION, dwUnmapped_00C) == 0x00C, "TFD_SESSION.dwUnmapped_00C");
OC_X86_LAYOUT(offsetof(TFD_SESSION, dwSize) == 0x010, "TFD_SESSION.dwSize");
OC_X86_LAYOUT(offsetof(TFD_SESSION, dwCrc) == 0x014, "TFD_SESSION.dwCrc");
OC_X86_LAYOUT(offsetof(TFD_SESSION, szPath) == 0x018, "TFD_SESSION.szPath");
OC_X86_LAYOUT(offsetof(TFD_SESSION, nodes) == 0x11C, "TFD_SESSION.nodes");

#pragma pack(pop)

// The two list heads and the transmit hook, which all three TUs reach.

// The session list, an explicit specialization.
#pragma pack(push, 1)
template <> struct TSList<TFD_SESSION,TSGetLink<TFD_SESSION> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TFD_SESSION> link;
  // 0x440C10 (160 B), `ret 0Ch`. Unlinks pNode, then links it at the head
  // (dwWhere 1) or before pBefore.
  void LinkNode(TFD_SESSION *pNode, ULONG dwWhere, TFD_SESSION *pBefore);
  // 0x440CB0 (129 B), `ret 0Ch`. Allocates and primes a session, then links it
  // when dwWhere is nonzero.
  TFD_SESSION *NewNode(ULONG dwWhere, int nExtra, DWORD dwFlags);
};
#pragma pack(pop)
typedef TSList<TFD_SESSION, TSGetLink<TFD_SESSION> > TFD_SESSION_LIST;
OC_X86_LAYOUT(sizeof(TFD_SESSION_LIST) == 12, "TSList.h layout");
static_assert(offsetof(TFD_SESSION_LIST, _unmapped_00) == 0x00, "LinkNode: mov ebx, [ebx]");
static_assert(offsetof(TFD_SESSION_LIST, link) == 0x04, "LinkNode: lea edx, [ebx+4h]");
typedef TSList<TFD_USER, TSGetLink<TFD_USER> > TFD_USER_LIST;

// The registered transmit hook. FDTransmission at 0x471800 is what
// set_fd_callbacks' one caller loads into EDX, and it ends `ret 0Ch`, so the
// callee pops the three dwords. The third parameter is one byte wide:
// sub_440E30 sets only DL and pushes EDX with the top 24 bits stale.
typedef void(__stdcall *PFN_FD_SEND)(void *pMsg, int nLen, char cArg);

// 0x5032B0 and 0x5032BC. Each is a TSList with its link at +4; the static
// constructor stores the complement of the link's address in its next field as
// the terminator.
#ifdef OC_DLL_MODE
  #define gFDSessions (*reinterpret_cast<TFD_SESSION_LIST *>(OC_VA(0x5032B0u)))
#else
extern TFD_SESSION_LIST gFDSessions;
#endif

#ifdef OC_DLL_MODE
  #define gFDUsers (*reinterpret_cast<TFD_USER_LIST *>(OC_VA(0x5032BCu)))
#else
extern TFD_USER_LIST gFDUsers;
#endif

// 0x665F80, the local peer id set_fd_callbacks stores from CL.
#ifdef OC_DLL_MODE
  #define gbFdLocalId (*reinterpret_cast<BYTE *>(OC_VA(0x665F80u)))
#else
extern BYTE gbFdLocalId;
#endif

// 0x665F88.
#ifdef OC_DLL_MODE
  #define gpfnFdSend (*reinterpret_cast<PFN_FD_SEND *>(OC_VA(0x665F88u)))
#else
extern PFN_FD_SEND gpfnFdSend;
#endif

// fd.cpp's own functions, in oracle address order.

// 0x4406C0 (74 B). `ret 4h`, ECX a pointer and EDX a second register argument.
// Dispatches on the byte at [ecx] through a seven-entry jump table into
// fd_user.cpp and tail-jumps; the default arm returns 0 with an explicit
// `xor eax, eax`, which makes the return a BOOL. The stack word is never read.
BOOL on_fd_transfer(void *pState, intptr_t nArg1, int nArg2);

// 0x440730 (22 B). Drains the session list, then tail-jumps to clear_fd_users;
// the tail pins the return type.
void fd_cleanup();

// 0x440750 (153 B). ECX is read as 8 bits and EDX as 32; three stack words, the
// last of them dereferenced as an array of string pointers. The one caller,
// sub_477090, loads EDX with FDTransmission's address.
void set_fd_callbacks(BYTE bArg0, PFN_FD_SEND pfnSend, int nArg2, const char *pszDownloadDir,
                      const char **apszDirs);

// 0x4407F0 (115 B). Frees the session's inner node list, runs the destructor,
// then frees the session.
void delete_fd_session(intptr_t nArg0);

// 0x440870 (100 B). Frees the user's +0x44 allocation, unlinks, frees the node.
void delete_fd_user(intptr_t nArg0);

// 0x4408E0 (106 B). delete_fd_user over every node reachable from dword_5032C4.
void clear_fd_users();

#endif  // OC117_FD_H
