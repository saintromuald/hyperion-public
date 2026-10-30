// error.h -- declarations for error.cpp, the game's error-string table; not
// net_error.h or gluError.h, which are other subsystems. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_ERROR_H
#define OC117_ERROR_H

#include "platform.h"

// 0x43F0D0. Nullary. EAX at the ret comes from str_load_table, and init_program,
// its one caller, discards it.
intptr_t error_init();

// 0x43F0A0. Nullary; registered with app_add_exit by error_init.
void error_free();

// 0x43F0C0. Tail-jumps str_get_string with the loaded table, so it returns that
// function's string. The id is a WORD.
extern "C" const char *error_get_str(WORD wErrId);
// C linkage lets net_misc.cpp call it through its own view (an int id, which
// sub_477A60's 32-bit add needs) on the same symbol, @error_get_str@4. The plain
// redeclaration keeps that linkage.
const char *error_get_str(WORD wErrId);

#endif  // OC117_ERROR_H
