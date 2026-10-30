// gds_file.h -- declarations for src117/lang/gds_file.cpp, the stdio wrappers
// the character, save, replay and log paths share.
//
// Every one is __fastcall with the file handle or the buffer in ECX. The
// handle is spelled `void *`: it is passed to the CRT, never dereferenced.
//
// The same signatures are also declared in fd_user.h (pfclose, pfopen),
// saveload.h (pfread, pfwrite), and in replay.cpp and saveload.cpp; each spells
// the parameters as this file does.

#ifndef OC117_GDS_FILE_H
#define OC117_GDS_FILE_H

#include "platform.h"

// 0x4CC370 pfclose (10 B). Forwards ECX to fclose. Four of twelve consuming
// callers use the full EAX; the other eight discard it.
int __fastcall pfclose(void *pFile);

// 0x4CC380 pfdelete (43 B). ECX is the path, EDX asks for the read-only bit to
// be cleared first. One of six consuming callers uses the full EAX, which is
// DeleteFileA's own.
BOOL __fastcall pfdelete(char *pszPath, BOOL bAlsoReadOnly);

// 0x4CC3B0 pfgetc (10 B). Forwards ECX to fgetc; all four callers use the full
// EAX, which they must -- EOF is -1.
int __fastcall pfgetc(void *pFile);

// 0x4CC3C0 pfopen (108 B). EDX is read with movsx as 16 bits, so the mode
// argument is signed. Mode is a file-kind id, not an fopen string: the body
// maps it through a 23-entry index table to "rb", "wb" or "wb+".
void *__fastcall pfopen(const char *pszName, short sMode);

// 0x4CC460 pfread (28 B) / 0x4CC4A0 pfwrite (28 B). Both call the CRT with a
// count of one element of nSize bytes, then fold the result to (n == 1), so the
// return is "did the single element transfer" and not a byte count. EDX goes to
// the CRT's size_t untouched; nSize is an int as the callers pass it.
BOOL __fastcall pfread(void *pBuf, int nSize, void *pFile);
BOOL __fastcall pfwrite(const void *pBuf, int nSize, void *pFile);

// 0x4CC480 pfseek (20 B). ECX is the file, EDX the offset, the one stack word
// the origin, in fseek's own order. Its single consuming caller
// (ReadCharacterFile) uses the full EAX.
int __fastcall pfseek(void *pFile, int nOffset, int nOrigin);

#endif  // OC117_GDS_FILE_H
