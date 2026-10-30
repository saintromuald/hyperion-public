// fd_user.cpp -- the download layer's per-user records and the transfer state
// machine, oracle 0x440DB0-0x44197F. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/fd_user.h"
#include "../h/fd_sessn.h"     // sub_440D40
#include "../h/gamedata.h"     // MemFileLoad
#include "../h/glues.h"        // mem_free
#include "../h/net_cmds.h"     // get_valid_str_len, is_bad_filename
#include "../h/opt_dlgs.h"     // _stricmp
#include "../h/rtti.h"         // type_info::raw_name -- OURS, not <typeinfo>
#include "../h/saveload.h"     // pfwrite
#include "../h/Storm.h"        // SMemAlloc / SStrCopy / SStrPack
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.
// PFN_FD_SEND, gbFdLocalId, gpfnFdSend, gFDSessions and gFDUsers are in fd.h.

// The rest of the transfer state block. 0x665F84 is the caller's budget minus
// the 9-byte slice header, which sub_441520 hands to sub_441720 as its clamp;
// 0x665F8C is the number of search directories set_fd_callbacks filled, and its
// loop bounds that at two, which is the extent of the 0x104-byte rows at
// 0x666098. 0x665F90 is the save directory, copied with capacity 0x104.
#ifdef OC_DLL_MODE
  #ifndef gdwFdChunkMax
  #define gdwFdChunkMax (*reinterpret_cast<DWORD *>(OC_VA(0x665F84u)))
  #endif
#else
extern DWORD gdwFdChunkMax;
#endif

#ifdef OC_DLL_MODE
  #ifndef gdwFdSearchDirs
  #define gdwFdSearchDirs (*reinterpret_cast<DWORD *>(OC_VA(0x665F8Cu)))
  #endif
#else
extern DWORD gdwFdSearchDirs;
#endif

#ifdef OC_DLL_MODE
  #ifndef gszFdSavePath
  #define gszFdSavePath (*reinterpret_cast<char (*)[0x104]>(OC_VA(0x665F90u)))
  #endif
#else
extern char gszFdSavePath[0x104];
#endif

#ifdef OC_DLL_MODE
  #ifndef gszFdSearchDir
  #define gszFdSearchDir (*reinterpret_cast<char (*)[2][0x104]>(OC_VA(0x666098u)))
  #endif
#else
extern char gszFdSearchDir[2][0x104] OC_UNALIGNED;
#endif

// The RTTI descriptor SMemAlloc's owner tag comes from, at 0x5107A4. Its bytes
// are the type_info vftable 0x4F1EDC, a zero DWORD and ".?AUTFD_USER@@", so the
// name is Blizzard's own. Referenced as a global rather than through typeid so
// that the shipped form can bake this address; see src117/h/rtti.h.
#ifdef OC_DLL_MODE
  #ifndef gTypeInfo_TFD_USER
  #define gTypeInfo_TFD_USER (*reinterpret_cast<type_info *>(OC_VA(0x5107A4u)))
  #endif
#else
extern type_info gTypeInfo_TFD_USER;
#endif

// The bodies, in oracle VA order.

// Apply a peer's progress report to the session node it names. Both walks emit
// `jle` to the exit and `jnz` back to the top because the guard is a signed
// test and the loop test a pointer one.
BOOL sub_440DB0(intptr_t nArg0, intptr_t nArg1, BYTE bArg2) {
  const BYTE *pMsg = reinterpret_cast<const BYTE *>(nArg0);
  if (nArg1 == 8 && pMsg[2] == gbFdLocalId) {
    TFD_SESSION *pSess = gFDSessions.link.next;
    if (reinterpret_cast<intptr_t>(pSess) > 0) {
      do {
        if (pSess->bId == pMsg[1]) {
          TUSER_INFO *pNode = pSess->nodes.link.next;
          if (reinterpret_cast<intptr_t>(pNode) > 0) {
            do {
              if (pNode->bId == bArg2) {
                pNode->bState = pMsg[3];
                pNode->dwUnmapped_0C =
                    *reinterpret_cast<const DWORD *>(pMsg + 4);
                sub_440D40(reinterpret_cast<intptr_t>(pSess));
                return 1;
              }
              pNode = pNode->link.next;
              if (reinterpret_cast<intptr_t>(pNode) <= 0) {
                break;
              }
            } while (pNode != NULL);
          }
          return 0;
        }
        pSess = pSess->link.next;
        if (reinterpret_cast<intptr_t>(pSess) <= 0) {
          break;
        }
      } while (pSess != NULL);
    }
  }
  return 0;
}

// Report one record's progress to the send callback. The player byte is read
// twice rather than held in a local, as 1.17 does.
__declspec(noinline) void sub_440E30(void *pArg0) {
  TFD_USER *pUser = reinterpret_cast<TFD_USER *>(pArg0);
  PFN_FD_SEND pfnSend = gpfnFdSend;
  if (pfnSend != NULL) {
    BYTE abMsg[8];
    abMsg[0] = 0;
    abMsg[1] = pUser->bId;
    abMsg[2] = pUser->bPlayer;
    abMsg[3] = pUser->bFlags;
    *reinterpret_cast<DWORD *>(&abMsg[4]) = pUser->dwReceived;
    pfnSend(abMsg, 8, static_cast<char>(pUser->bPlayer));
  }
}

// Load a file and hand back its length and CRC. Both owner tags are the same
// pooled "fd_user.cpp" at 0x4EC384, with two different line numbers.
int sub_440E70(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2) {
  DWORD dwSize;
  void *pFile = MemFileLoad(reinterpret_cast<const char *>(nArg0), 0, &dwSize,
                            1, 1, "fd_user.cpp", 0x1E);
  if (pFile == NULL) {
    return 0;
  }
  int nCrc = comp_crc32(reinterpret_cast<intptr_t>(pFile), static_cast<int>(dwSize));
  mem_free(pFile, "fd_user.cpp", 0x23);
  if (nArg1 != 0) {
    *reinterpret_cast<DWORD *>(nArg1) = dwSize;
  }
  if (nArg2 != 0) {
    *reinterpret_cast<int *>(nArg2) = nCrc;
  }
  return 1;
}

// Overall transfer progress as a percentage, and TRUE when no record has
// failed. The flag is a cmov, so it is an assignment rather than a branch.
BOOL sub_440EE0(void *pArg0) {
  TFD_USER *p = gFDUsers.link.next;
  DWORD dwDone = 0;
  DWORD dwTotal = 0;
  int nFailed = 0;
  if (reinterpret_cast<intptr_t>(p) > 0) {
    do {
      dwTotal += p->dwSize;
      dwDone += p->dwReceived;
      nFailed = (p->bFlags == 8) ? 1 : nFailed;
      p = p->link.next;
    } while (reinterpret_cast<intptr_t>(p) > 0);
  }
  if (dwTotal == 0) {
    *reinterpret_cast<BYTE *>(pArg0) = 0;
    return nFailed == 0;
  }
  *reinterpret_cast<BYTE *>(pArg0) = static_cast<BYTE>(dwDone * 100 / dwTotal);
  return nFailed == 0;
}

// Drop the record a leaving player owns, then clear that player out of every
// other record's pending state and report each one.
void sub_440F40(BYTE bArg0) {
  TFD_USER *pUser = gFDUsers.link.next;
  if (reinterpret_cast<intptr_t>(pUser) > 0) {
    for (TFD_USER *p = gFDUsers.link.next; reinterpret_cast<intptr_t>(p) > 0;
         p = p->link.next) {
      if (p->bPlayer == bArg0) {
        delete_fd_user(reinterpret_cast<intptr_t>(p));
        pUser = gFDUsers.link.next;
        break;
      }
    }
    for (; reinterpret_cast<intptr_t>(pUser) > 0; pUser = pUser->link.next) {
      BYTE bFlags = pUser->bFlags;
      if ((bFlags & 4) != 0 && pUser->b3D == bArg0) {
        pUser->bFlags = static_cast<BYTE>(bFlags & ~4);
      }
      bFlags = pUser->bFlags;
      if ((bFlags & 2) != 0 && pUser->b3C == bArg0) {
        pUser->bFlags = static_cast<BYTE>(bFlags & ~2);
      }
      sub_440E30(pUser);
    }
  }
}

// True when the named file already matches the record's CRC and length. Its
// address is handed to sub_499450 as a callback, which is what pins __stdcall.
BOOL __stdcall sub_440FB0(intptr_t nArg0, void *pArg1) {
  DWORD dwSize;
  void *pFile = MemFileLoad(reinterpret_cast<const char *>(nArg0), 0, &dwSize,
                            1, 1, "fd_user.cpp", 0x1E);
  if (pFile != NULL) {
    int nCrc =
        comp_crc32(reinterpret_cast<intptr_t>(pFile), static_cast<int>(dwSize));
    mem_free(pFile, "fd_user.cpp", 0x23);
    TFD_USER *pUser = reinterpret_cast<TFD_USER *>(pArg1);
    if (static_cast<DWORD>(nCrc) == pUser->dwCrc && dwSize == pUser->dwSize) {
      return 1;
    }
  }
  return 0;
}

// True when the path ends in one of the three map or replay extensions. It uses
// the `const char *` strrchr overload on purpose: the `char *` inline wrapper
// emits a second COMDAT of the name.
BOOL fd_has_map_extension(intptr_t nArg0) {
  const char *pszDot = strrchr(reinterpret_cast<const char *>(nArg0), '.');
  if (pszDot != NULL) {
    const char *pszExt = pszDot + 1;
    // One `||` chain, not three ifs: the shared `mov eax,1` has to sit after
    // the last test for the first two to reach it with a forward `je`.
    if (_stricmp(pszExt, "scx") == 0 || _stricmp(pszExt, "scm") == 0 ||
        _stricmp(pszExt, "rep") == 0) {
      return 1;
    }
  }
  return 0;
}

// Resolve a record to a path: for our own uploads the session's directory, for
// a download the first search directory the file is actually found in. The
// walk's doubled `jle`/`jnz` is the signed guard plus the pointer loop test.
BOOL sub_441080(void *pArg0, intptr_t nArg1, int nArg2) {
  TFD_USER *pUser = reinterpret_cast<TFD_USER *>(pArg0);
  char *pszDest = reinterpret_cast<char *>(nArg1);
  if (pUser->bPlayer == gbFdLocalId) {
    TFD_SESSION *pSess = gFDSessions.link.next;
    if (reinterpret_cast<intptr_t>(pSess) > 0) {
      do {
        if (pSess->bId == pUser->bId) {
          SStrCopy(pszDest, pSess->szPath, nArg2);
          return 1;
        }
        pSess = pSess->link.next;
        if (reinterpret_cast<intptr_t>(pSess) <= 0) {
          break;
        }
      } while (pSess != NULL);
    }
    return 0;
  }
  for (DWORD i = 0; i < gdwFdSearchDirs; i++) {
    SStrCopy(pszDest, gszFdSearchDir[i], nArg2);
    if (sub_499450(reinterpret_cast<intptr_t>(pUser->szName),
                   reinterpret_cast<intptr_t>(pszDest), nArg2,
                   reinterpret_cast<intptr_t>(&sub_440FB0),
                   reinterpret_cast<intptr_t>(pUser)) != 0) {
      set_map_filename(reinterpret_cast<intptr_t>(pszDest), 0);
      return 1;
    }
  }
  return 0;
}

// A peer offers a file. Validate the name, pick a search directory it fits in,
// allocate the record, link it at the head of gFDUsers and prime it. The
// allocation is not re-tested before the field stores, so a failed SMemAlloc
// writes through NULL; that is 1.17's bug, kept.
BOOL sub_441150(intptr_t nArg0, void *pArg1, int nArg2) {
  const BYTE *pMsg = reinterpret_cast<const BYTE *>(nArg0);
  if (reinterpret_cast<DWORD_PTR>(pArg1) < 0x0A) {
    return 0;
  }
  if (*reinterpret_cast<const DWORD *>(pMsg + 2) == 0) {
    return 0;
  }
  const char *pszName = reinterpret_cast<const char *>(pMsg + 0x0A);
  if (get_valid_str_len(pszName, 0x20,
                        (DWORD)(reinterpret_cast<DWORD_PTR>(pArg1) - 0x4A), 0, 0) == 0) {
    return 0;
  }
  DWORD dwNameLen = static_cast<DWORD>(strlen(pszName));
  for (DWORD i = 0; i < gdwFdSearchDirs; i++) {
    if (strlen(gszFdSearchDir[i]) + dwNameLen < 0x104) {
      const char *pszLeaf = reinterpret_cast<const char *>(nArg0) + 0x0A;
      if (fd_has_map_extension(reinterpret_cast<intptr_t>(pszLeaf)) == 0) {
        return 0;
      }
      if (is_bad_filename(pszLeaf,
                          static_cast<DWORD>(strlen(pszLeaf)) + 1, 0) != 0) {
        return 0;
      }
      void *pv = SMemAlloc(sizeof(TFD_USER), gTypeInfo_TFD_USER.raw_name(),
                           -2, 8);
      TFD_USER *pNew;
      TSLink<TFD_USER> *nl;
      if (pv != NULL) {
        pNew = reinterpret_cast<TFD_USER *>(pv);
        pNew->link.prev = NULL;
        pNew->link.next = NULL;
        nl = &pNew->link;
      } else {
        pNew = NULL;
        nl = &gFDUsers.link;
      }
      TFD_USER *pPrev = nl->prev;
      if (pPrev != NULL) {
        TFD_USER *pSlot = nl->next;
        if (reinterpret_cast<intptr_t>(pSlot) <= 0) {
          pSlot = reinterpret_cast<TFD_USER *>(~reinterpret_cast<intptr_t>(pSlot));
        } else {
          pSlot = reinterpret_cast<TFD_USER *>(
              reinterpret_cast<char *>(pSlot) +
              (reinterpret_cast<char *>(nl) -
               reinterpret_cast<char *>(pPrev->link.next)));
        }
        *reinterpret_cast<TFD_USER **>(pSlot) = pPrev;
        nl->prev->link.next = nl->next;
        nl->prev = NULL;
        nl->next = NULL;
      }
      nl->prev = reinterpret_cast<TFD_USER *>(&gFDUsers.link);
      nl->next = gFDUsers.link.next;
      TFD_USER *pHead = gFDUsers.link.next;
      int nLinkOff = static_cast<int>(gFDUsers._unmapped_00);
      if (reinterpret_cast<intptr_t>(pHead) <= 0) {
        pHead = reinterpret_cast<TFD_USER *>(~reinterpret_cast<intptr_t>(pHead));
      } else {
        if (nLinkOff < 0) {
          nLinkOff = static_cast<int>(
              reinterpret_cast<char *>(&gFDUsers.link) -
              reinterpret_cast<char *>(gFDUsers.link.prev->link.next));
        }
        pHead = reinterpret_cast<TFD_USER *>(
            reinterpret_cast<char *>(pHead) + nLinkOff);
      }
      *reinterpret_cast<TFD_USER **>(pHead) = reinterpret_cast<TFD_USER *>(nl);
      gFDUsers.link.next = pNew;

      pNew->bId = pMsg[1];
      pNew->bPlayer = static_cast<BYTE>(nArg2);
      pNew->dwSize = *reinterpret_cast<const DWORD *>(pMsg + 2);
      pNew->dwCrc = *reinterpret_cast<const DWORD *>(pMsg + 6);
      pNew->bFlags = 1;
      pNew->dwSent = 0;
      pNew->b3C = 0;
      pNew->b3D = 0;
      pNew->dwReceived = 0;
      pNew->pOwned = NULL;
      SStrCopy(pNew->szName, pszLeaf, 0x20);

      pNew->pOwned = SMemAlloc(pNew->dwSize, "fd_user.cpp", 0x198, 0);
      memset(pNew->pOwned, 0, pNew->dwSize);

      char szPath[0x104];
      if (sub_441080(pNew, reinterpret_cast<intptr_t>(szPath), 0x104) != 0) {
        void *pFile = MemFileLoad(szPath, 0,
                                  reinterpret_cast<DWORD *>(&nArg2), 1, 1,
                                  "fd_user.cpp", 0x1A1);
        if (pFile == NULL) {
          pNew->bFlags = 8;
          return 0;
        }
        memcpy(pNew->pOwned, pFile, static_cast<size_t>(nArg2));
        mem_free(pFile, "fd_user.cpp", 0x1AB);
        pNew->dwReceived = static_cast<DWORD>(nArg2);
      }
      sub_440E30(pNew);
      return 1;
    }
  }
  return 0;
}

// A slice of a download arrives: copy it in at the offset it claims, and when
// the record is full check the CRC and write the file out. An offset that has
// already been passed answers with the give-up message instead. The return-1
// label sits in the CRC-mismatch arm so the early exit branches to that copy.
BOOL sub_4413B0(intptr_t nArg0, intptr_t nArg1, int nArg2) {
  const BYTE *pMsg = reinterpret_cast<const BYTE *>(nArg0);
  if (static_cast<DWORD>(nArg1) < 9) {
    return 0;
  }
  if (static_cast<DWORD>(nArg1) !=
      *reinterpret_cast<const WORD *>(pMsg + 7) + 9) {
    return 0;
  }
  BYTE bPlayer = pMsg[2];
  BYTE bId = pMsg[1];
  for (TFD_USER *p = gFDUsers.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next) {
    if (p->bId == bId && p->bPlayer == bPlayer) {
      if (*reinterpret_cast<const DWORD *>(pMsg + 3) != p->dwSize &&
          *reinterpret_cast<const DWORD *>(pMsg + 3) +
                  *reinterpret_cast<const WORD *>(pMsg + 7) <= p->dwSize) {
        if (*reinterpret_cast<const DWORD *>(pMsg + 3) == p->dwReceived) {
        p->bFlags |= 4;
        p->b3D = static_cast<BYTE>(nArg2);
        memcpy(reinterpret_cast<char *>(p->pOwned) +
                    *reinterpret_cast<const DWORD *>(pMsg + 3),
                pMsg + 9, *reinterpret_cast<const WORD *>(pMsg + 7));
        p->dwReceived += *reinterpret_cast<const WORD *>(pMsg + 7);
        sub_4417E0(p);
        if (p->dwReceived != p->dwSize)
          goto ret1;
        if (static_cast<DWORD>(comp_crc32(reinterpret_cast<intptr_t>(p->pOwned),
                                          static_cast<int>(p->dwSize))) !=
            p->dwCrc) {
          p->bFlags &= ~4;
          p->dwReceived = 0;
          sub_440E30(p);
ret1:
          return 1;
        }
        if (sub_4418C0(reinterpret_cast<intptr_t>(p)) != 0) {
          p->bFlags &= ~4;
          sub_440E30(p);
          goto ret1;
        }
        p->bFlags = 8;
        return 0;
        }
        sub_441820(nArg2, p);
        sub_440E30(p);
        return 0;
      }
      PFN_FD_SEND pfnSend = gpfnFdSend;
      if (pfnSend != NULL) {
        BYTE abMsg[3];
        abMsg[0] = 6;
        abMsg[1] = p->bId;
        abMsg[2] = p->bPlayer;
        pfnSend(abMsg, 3, static_cast<char>(nArg2));
      }
      sub_440E30(p);
      return 0;
    }
  }
  return 0;
}

// A peer asks for the next slice of a file we are sending: either ship it or,
// once the offset has caught up with the length, stop sending and report.
BOOL sub_441520(intptr_t nArg0, intptr_t nArg1, int nArg2) {
  const BYTE *pMsg = reinterpret_cast<const BYTE *>(nArg0);
  if (nArg1 != 7) {
    return 0;
  }
  BYTE bPlayer = pMsg[2];
  BYTE bId = pMsg[1];
  for (TFD_USER *p = gFDUsers.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next) {
    if (p->bId == bId && p->bPlayer == bPlayer) {
      BYTE bFlags = p->bFlags;
      if ((bFlags & 2) == 0) {
        return 0;
      }
      if (*reinterpret_cast<const DWORD *>(pMsg + 3) < p->dwSize) {
        p->dwSent += sub_441720(reinterpret_cast<intptr_t>(p), gdwFdChunkMax);
        return 1;
      }
      p->bFlags = static_cast<BYTE>(bFlags & ~2);
      sub_440E30(p);
      return 1;
    }
  }
  return 0;
}

// A peer cancels a transfer it was receiving from us.
BOOL sub_4415A0(void *pArg0, intptr_t nArg1, int nArg2) {
  const BYTE *pMsg = reinterpret_cast<const BYTE *>(pArg0);
  if (nArg1 != 3) {
    return 0;
  }
  BYTE bPlayer = pMsg[2];
  BYTE bId = pMsg[1];
  for (TFD_USER *p = gFDUsers.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next) {
    if (p->bId == bId && p->bPlayer == bPlayer) {
      p->bFlags &= ~2;
      sub_440E30(p);
      return 1;
    }
  }
  return 0;
}

// A peer accepts our offer: mark the record as sending, seed the offset and
// prime the pipe with eight slices. The trailing `lea eax,[edi+1]` is the
// counter, which the loop has just driven to zero, spelling `return 1`.
BOOL sub_4415E0(intptr_t nArg0, intptr_t nArg1, BYTE bArg2) {
  const BYTE *pMsg = reinterpret_cast<const BYTE *>(nArg0);
  if (nArg1 != 7) {
    return 0;
  }
  BYTE bId = pMsg[1];
  for (TFD_USER *p = gFDUsers.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next) {
    if (p->bId == bId && p->bPlayer == bArg2) {
      BYTE bFlags = p->bFlags;
      if ((bFlags & 2) != 0) {
        return 0;
      }
      p->bFlags = static_cast<BYTE>(bFlags | 2);
      p->dwSent = *reinterpret_cast<const DWORD *>(pMsg + 3);
      p->b3C = pMsg[2];
      for (int i = 8; i != 0; i--) {
        p->dwSent += sub_441720(reinterpret_cast<intptr_t>(p), 8);
      }
      return 1;
    }
  }
  return 0;
}

// A peer abandons a download; drop our record of it.
BOOL sub_441660(void *pArg0, intptr_t nArg1, BYTE bArg2) {
  const BYTE *pMsg = reinterpret_cast<const BYTE *>(pArg0);
  if (nArg1 != 2) {
    return 0;
  }
  BYTE bId = pMsg[1];
  for (TFD_USER *p = gFDUsers.link.next; reinterpret_cast<intptr_t>(p) > 0;
       p = p->link.next) {
    if (p->bId == bId && p->bPlayer == bArg2) {
      delete_fd_user(reinterpret_cast<intptr_t>(p));
      return 1;
    }
  }
  return 0;
}

// Announce a session: its id, length, CRC and the leaf of its path, which has
// to fit the message's 0x20-byte name field. The callback is spelled through
// the global at both the test and the call, never cached in a local.
void sub_4416A0(BYTE bArg0, intptr_t nArg1) {
  TFD_SESSION *pSess = reinterpret_cast<TFD_SESSION *>(nArg1);
  if (gpfnFdSend != NULL) {
    const char *pszLeaf =
        strrchr(const_cast<const char *>(pSess->szPath), '\\');
    if (pszLeaf == NULL) {
      pszLeaf = pSess->szPath;
    } else {
      pszLeaf++;
    }
    size_t nLen = strlen(pszLeaf) + 1;
    if (nLen <= 0x20) {
      BYTE abMsg[0x2A];
      abMsg[0] = 1;
      abMsg[1] = pSess->bId;
      *reinterpret_cast<DWORD *>(&abMsg[2]) = pSess->dwSize;
      *reinterpret_cast<DWORD *>(&abMsg[6]) = pSess->dwCrc;
      SStrCopy(reinterpret_cast<char *>(&abMsg[0x0A]), pszLeaf, 0x20);
      gpfnFdSend(abMsg, static_cast<int>(nLen + 10),
                 static_cast<char>(bArg0));
    }
  }
}

// Ship one slice of the file we are sending and answer how many bytes went.
// Running past what we actually hold stops the send, reloads the callback and
// ships an empty slice.
__declspec(noinline) int sub_441720(intptr_t nArg0, int nArg1) {
  TFD_USER *pUser = reinterpret_cast<TFD_USER *>(nArg0);
  PFN_FD_SEND pfnSend = gpfnFdSend;
  if (pfnSend == NULL) {
    return 0;
  }
  DWORD dwSent = pUser->dwSent;
  DWORD dwChunk = 0x80;
  if (static_cast<DWORD>(nArg1) < dwChunk) {
    dwChunk = static_cast<DWORD>(nArg1);
  }
  DWORD dwSize = pUser->dwSize;
  if (dwSent >= dwSize) {
    return 0;
  }
  if (pUser->dwSent + dwChunk > dwSize) {
    dwChunk = dwSize - dwSent;
  }
  DWORD dwHave = pUser->dwReceived;
  if (pUser->dwSent + dwChunk > dwHave) {
    pUser->bFlags &= ~2;
    dwSent = dwHave;
    pfnSend = gpfnFdSend;
    dwChunk = 0;
  }
  BYTE abMsg[0x89];
  abMsg[0] = 4;
  abMsg[1] = pUser->bId;
  abMsg[2] = pUser->bPlayer;
  *reinterpret_cast<DWORD *>(&abMsg[3]) = dwSent;
  *reinterpret_cast<WORD *>(&abMsg[7]) = static_cast<WORD>(dwChunk);
  memcpy(&abMsg[9], reinterpret_cast<const char *>(pUser->pOwned) + dwSent,
          dwChunk);
  pfnSend(abMsg, static_cast<int>(dwChunk + 9),
          static_cast<char>(pUser->b3C));
  return static_cast<int>(dwChunk);
}

// Acknowledge received bytes back to the sender.
void sub_4417E0(void *pArg0) {
  TFD_USER *pUser = reinterpret_cast<TFD_USER *>(pArg0);
  PFN_FD_SEND pfnSend = gpfnFdSend;
  if (pfnSend != NULL) {
    BYTE abMsg[7];
    abMsg[0] = 5;
    abMsg[1] = pUser->bId;
    abMsg[2] = pUser->bPlayer;
    *reinterpret_cast<DWORD *>(&abMsg[3]) = pUser->dwReceived;
    pfnSend(abMsg, 7, static_cast<char>(pUser->b3D));
  }
}

// Tell the sender we are giving up on this record.
void sub_441820(BYTE bArg0, void *pArg1) {
  TFD_USER *pUser = reinterpret_cast<TFD_USER *>(pArg1);
  PFN_FD_SEND pfnSend = gpfnFdSend;
  if (pfnSend != NULL) {
    BYTE abMsg[3];
    abMsg[0] = 6;
    abMsg[1] = pUser->bId;
    abMsg[2] = pUser->bPlayer;
    pfnSend(abMsg, 3, static_cast<char>(bArg0));
  }
}

// Ask a peer for the file a session node names.
void sub_441850(BYTE bArg0, BYTE bArg1, void *pArg2) {
  PFN_FD_SEND pfnSend = gpfnFdSend;
  if (pfnSend != NULL) {
    TUSER_INFO *pNode = reinterpret_cast<TUSER_INFO *>(pArg2);
    BYTE abMsg[7];
    abMsg[0] = 2;
    abMsg[1] = bArg1;
    abMsg[2] = pNode->bId;
    *reinterpret_cast<DWORD *>(&abMsg[3]) = pNode->dwUnmapped_0C;
    pfnSend(abMsg, 7, static_cast<char>(bArg0));
  }
}

// The two-byte poll.
void sub_441890(BYTE bArg0, BYTE bArg1) {
  PFN_FD_SEND pfnSend = gpfnFdSend;
  if (pfnSend != NULL) {
    BYTE abMsg[2];
    abMsg[0] = 3;
    abMsg[1] = bArg1;
    pfnSend(abMsg, 2, static_cast<char>(bArg0));
  }
}

// Write a completed download out under the save path, deleting the file again
// if either the write or the close failed. The two DeleteFileA sites are one
// source statement: MSVC duplicates it into the arm that has just zeroed the
// flag it would otherwise re-test.
int sub_4418C0(intptr_t nArg0) {
  TFD_USER *pUser = reinterpret_cast<TFD_USER *>(nArg0);
  char szPath[0x104];
  SStrCopy(szPath, gszFdSavePath, 0x104);
  SStrPack(szPath, pUser->szName, 0x104);
  set_map_filename(reinterpret_cast<intptr_t>(szPath), 0);
  if (CreateDirectoriesRecursive(gszFdSavePath) == 0) {
    return 0;
  }
  void *pFile = pfopen(szPath, 0x17);
  if (pFile == 0) {
    return 0;
  }
  int nWrote = pfwrite(pUser->pOwned, static_cast<int>(pUser->dwSize), pFile);
  if (pfclose(pFile) != 0) {
    nWrote = 0;
  }
  if (nWrote == 0) {
    DeleteFileA(szPath);
  }
  return nWrote;
}
