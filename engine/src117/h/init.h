// init.h -- declarations for src117/lang/init.cpp, the program's start-up:
// command-line tokenising, locale selection, the MPQ archive set (game, patch,
// CD, no-CD), the CD search, the font tables, the video bring-up and the
// matching exit hooks.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default. get_cd_archive and get_patch_archive are
// declared in glues.h, the Storm imports in Storm.h. FreeLocal, exit_snd,
// exit_video and init_speed_options are one-jump thunks to FreeLocaleModule,
// snd_close, deinit_video and init_spd_opts.

#ifndef OC117_INIT_H
#define OC117_INIT_H

#include "platform.h"
#include "oc_data.h"  // Hyperion: OC_VA

// Hyperion: moved here from src117/lang/init.cpp
// so net_glue.cpp and gluModem.cpp, which hand its address to
// Storm, name it with its own type (its layout changes at 64 bits).
// gVersion @0x66CCA8 -- the five-dword record open_archives fills before it
// touches an archive. Its first field is its own size, and the store is
// `mov dword ptr [0x66CCA8], 14h`, so the extent is 20 bytes; 0x66CCA8 + 0x14
// is `nosound`, which bounds it from above. Each pointer field is identified
// by the global the oracle stores into it. Sheet: init.cpp/"gVersion".
struct VersionRecord {
  /*0x00*/ DWORD cb;
  /*0x04*/ char *pszVersion;
  /*0x08*/ char *pszProgramName;
  /*0x0C*/ char *pszMainArchive;
  /*0x10*/ char *pszPatchArchive;
};
OC_X86_LAYOUT(sizeof(VersionRecord) == 0x14, "mov dword ptr [0x66CCA8], 14h");
static_assert(offsetof(VersionRecord, cb) == 0x00, "0x66CCA8");
OC_X86_LAYOUT(offsetof(VersionRecord, pszVersion) == 0x04, "0x66CCAC");
OC_X86_LAYOUT(offsetof(VersionRecord, pszProgramName) == 0x08, "0x66CCB0");
OC_X86_LAYOUT(offsetof(VersionRecord, pszMainArchive) == 0x0C, "0x66CCB4");
OC_X86_LAYOUT(offsetof(VersionRecord, pszPatchArchive) == 0x10, "0x66CCB8");

#ifndef OC_GVERSION
#define OC_GVERSION
#ifdef OC_DLL_MODE
#define gVersion (*reinterpret_cast<VersionRecord *>(OC_VA(0x66CCA8u)))
#else
extern VersionRecord gVersion;
#endif
#endif

// Nullary.

// 0x465590 (107 B). Two callers read the full EAX.
int GetLocaleFourCC();

// 0x465760 (101 B). An app exit hook, reached only by address; no caller reads
// EAX.
void close_archives();

// 0x4657D0 (26 B). No caller reads EAX.
void close_cd_archive();

// 0x465850 (46 B). Takes a BYTE it never reads: font_init_game and
// font_init_glues both clear CL before the call. EAX is never written.
void font_close(BYTE bUnused);

// 0x465880 / 0x465890 / 0x4658A0 / 0x465C30 -- the four 5-byte thunks.
void FreeLocal();
void exit_snd();
void exit_video();
void init_speed_options();

// 0x465960 (190 B). No caller reads EAX.
void get_program_version_info();

// 0x465AA0 (277 B). It ends in a tail jump to app_add_exit and main.cpp's one
// call discards EAX, so void; spelled BOOL, the fall-off is C4716.
void init_program();

// 0x465BC0 (104 B). No caller reads EAX.
void init_video();

// 0x465C40 / 0x465CE0 (151 B each). EAX is never written.
void font_init_game();
void font_init_glues();

// 0x465D80 (81 B). Another app exit hook, reached only by address; it ends in
// a tail jump to gamedata_free_data.
void free_gamedata();

// 0x465DE0 (473 B). No caller reads EAX.
void open_archives();

// 0x4661B0 (99 B). No caller reads EAX.
void parse_args();

// One register argument.

// 0x4656C0 (159 B). ECX only; no caller reads EAX.
void validate_data_version(intptr_t nArg0);

// 0x4658C0 (159 B). ECX only; two callers read the full EAX.
intptr_t cmdtok(intptr_t nArg0);

// 0x465A20 (128 B). ECX only; no caller reads EAX.
void InitLocal(intptr_t nArg0);

// 0x466220 (130 B). ECX only; every path returns 0 or 1, which its one caller
// discards.
BOOL parse_argument(intptr_t nArg0);

// Two register arguments.

// 0x465470 (42 B). ECX and EDX; five callers read the full EAX.
int FileExists(intptr_t nArg0, intptr_t nArg1);

// 0x4660F0 (179 B). ECX and EDX; it returns 0 or 1, and four callers read it.
BOOL FindCDArchive(int nArg0, intptr_t nArg1);

// Register and stack arguments.

// 0x4654A0 (228 B), `ret 8h`: ECX, EDX and two stack words. It returns a value
// its one caller, OpenCDArchive, passes straight through as its own result.
intptr_t FindCDArchiveFile(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3);

// 0x465FC0 (165 B), `ret 4h`: ECX, EDX and one stack word. Two callers read the
// full EAX.
intptr_t OpenCDArchive(intptr_t nArg0, int nArg1, intptr_t nArg2);

// 0x466070 (121 B), `ret 8h`: ECX, EDX and two stack words. Two callers read
// the full EAX.
intptr_t OpenNoCDArchive(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3);

// Callbacks, reached only by address: __stdcall, the whole argument list on
// the stack.

// 0x465640 (124 B), `ret 10h`. The DLGPROC handed to DialogBoxParamA; it
// returns the "handled" flag USER32 reads.
BOOL __stdcall DialogFunc(intptr_t nArg0, int nArg1, int nArg2, intptr_t nArg3);

// 0x4657F0 (90 B), `ret 14h`: five stack words, the second never read. The
// font-table loader callback, its address taken eight times in this TU.
intptr_t LoadFontFile(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3, int nArg4, intptr_t nArg5, int nArg6);

#endif  // OC117_INIT_H
