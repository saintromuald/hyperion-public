// error.cpp -- the network error string table. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/error.h"
#include "../h/glues.h"         // mem_free
#include "../h/dlg_str.h"       // str_load_table, str_get_string
#include "../h/gds_appaddex.h"  // app_add_exit
#include "../h/snd.h"           // load_game_data
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled.

// The loaded network string table.
#ifdef OC_DLL_MODE
#define sgpErrStrings (*reinterpret_cast<void **>(OC_VA(0x665DE0u)))
#else
extern void *sgpErrStrings;
#endif

#define OC_ERROR_FILE "error.cpp"

// Frees the string table at exit.
void error_free() {
  mem_free(sgpErrStrings, OC_ERROR_FILE, 0x15);
  sgpErrStrings = NULL;
}

// Returns error string wErrId.
const char *error_get_str(WORD wErrId) {
  return reinterpret_cast<const char *>(str_get_string(sgpErrStrings, wErrId));
}

// Loads the string table and registers its release.
intptr_t error_init() {
  app_add_exit(error_free);
  sgpErrStrings = reinterpret_cast<void *>(
      str_load_table(reinterpret_cast<intptr_t>("rez\\network.tbl"),
                     reinterpret_cast<intptr_t>(load_game_data),
                     reinterpret_cast<intptr_t>(OC_ERROR_FILE), 0x1E));
  return reinterpret_cast<intptr_t>(sgpErrStrings);
}
