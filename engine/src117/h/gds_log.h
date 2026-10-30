// gds_log.h -- declarations for src117/lang/gds_log.cpp, the crash-log writer:
// a 4 KB in-memory buffer that is flushed to a per-user .ERR file, plus the
// version and machine banner appcritf writes at the top of a crash report.
//
// No <windows.h> here: a src117/h header that includes it redefines _toupper
// after dlg_ctrl.h undoes it. The two Win32 types these signatures name are a
// tag-name forward declaration and `void *`, the same types <windows.h> defines.
//
// CreateDirectoriesRecursive is spelled exactly as fd_user.h spells it.

#ifndef OC117_GDS_LOG_H
#define OC117_GDS_LOG_H

#include "platform.h"

// VS_FIXEDFILEINFO, <windows.h>'s own tag. Only ever reached by pointer here.
struct tagVS_FIXEDFILEINFO;

// 0x4CC4C0 (201 B). Creates every missing directory along the path, walking it
// one backslash at a time.
BOOL __fastcall CreateDirectoriesRecursive(char *pszPath);

// 0x4CC590 (504 B). Opens the .ERR file, building its name on the first call
// and falling back to a name at the drive root when the user path fails.
// Returns the Win32 handle, or INVALID_HANDLE_VALUE, as a `void *`, which is
// HANDLE.
void *log_create(void);

// 0x4CC790 (166 B). __cdecl, and the arity is the callers': one of three
// pushes a word and the other two push none. The argument asks for the file to
// be closed afterwards. EAX at the ret is WriteFile's and nobody reads it.
void __cdecl log_flush(int bCloseFile);

// 0x4CC840 (47 B). Fills ppszOut with pointers to the log file names recorded
// so far, up to nMax of them and never more than the ten the table holds.
void __fastcall log_get_filenames(char **ppszOut, unsigned nMax);

// 0x4CC870 (87 B). Empties the ten name slots, once.
void log_init(void);

// 0x4CC8D0 (196 B). Writes the version, machine and time banner.
void __cdecl log_dump_computer_info(void);

// 0x4CC9A0 (194 B). __cdecl varargs. Appends to the buffer, flushing first when
// the line would not fit.
void __cdecl log_printf(const char *pszFormat, ...);

// 0x4CCA70 (180 B). Reads the running executable's own fixed version block into
// the caller's 0x34-byte buffer, zeroed first so a failure leaves it blank.
void __fastcall log_get_version(tagVS_FIXEDFILEINFO *pInfo);

#endif  // OC117_GDS_LOG_H
