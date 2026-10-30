// Hyperion: not built. The Battle.net screens are gone from the port; the file
// is kept so that later changes to the reconstruction still apply. An address of
// its functions in 1.17's data goes to PortOriginalCodeReached.
//
// gluBNJoin.cpp -- the Battle.net join screen's two Storm UI callbacks. Both
// are reached only through function pointers. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluBNJoin.h"
#include "../h/gametype.h"  // TGAMEPARAMS, TEMPLATEPROC, IterateMainTemplates,
                       // gametype_template_allowed
#include "../h/glues.h"     // glues_get_str
#include "../h/gluBN.h"     // glubn_get_stats
#include "../gen/globals117.h"

// The category callback Storm hands UiCategoryListCallback: __stdcall, with a
// row's text, its code and a literal (0xFF80 for the "all" row, 0xFFFF for a
// template's).
typedef void(__stdcall *PFN_BNCATEGORY)(const char *pszText, int nCode,
                                        int nValue);

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x667B00 -- the category callback, live while UiCategoryListCallback walks
// the templates.
#ifdef OC_DLL_MODE
#define dword_667B00 (*reinterpret_cast<PFN_BNCATEGORY *>(OC_VA(0x667B00u)))
#else
extern PFN_BNCATEGORY dword_667B00;
#endif

// 0x667B04 -- the category read out of the UI record, handed to
// gametype_template_allowed.
#ifdef OC_DLL_MODE
#define dword_667B04 (*reinterpret_cast<int *>(OC_VA(0x667B04u)))
#else
extern int dword_667B04;
#endif

// 0x44E060 (101 B). Storm hands this the UI record and a category callback. It
// reads the record's category into the two globals with the callback, emits the
// "all categories" row itself, then lets IterateMainTemplates emit one row per
// game template through sub_44E0D0. The second argument is the callback, cast
// from the header's integer; the record is read only at +8, by subscript.
void __stdcall UiCategoryListCallback(void *pArg0, intptr_t nArg1) {
  const PFN_BNCATEGORY pfn = reinterpret_cast<PFN_BNCATEGORY>(nArg1);
  int nCategory = 0;

  glubn_get_stats(reinterpret_cast<void **>(pArg0)[2], 0, 0, 0, 0, &nCategory, 0);
  dword_667B04 = nCategory;
  pfn(glues_get_str(0x5C), 0, 0xFF80);
  dword_667B00 = pfn;
  IterateMainTemplates(reinterpret_cast<TEMPLATEPROC>(sub_44E0D0));
  dword_667B00 = 0;
}

// 0x44E0D0 (62 B). A TEMPLATEPROC: IterateMainTemplates calls it with a
// template's name, parameter, TGAMEPARAMS and metadata, all four spelled as
// integers in the header and cast here. A template the category allows becomes
// a row coded (bSub << 8) | bCategory.
void __stdcall sub_44E0D0(intptr_t nArg0, int nArg1, intptr_t nArg2, int nArg3) {
  const TGAMEPARAMS *const pParams =
      reinterpret_cast<const TGAMEPARAMS *>(nArg2);

  if (gametype_template_allowed(reinterpret_cast<const BYTE *>(pParams), dword_667B04) &&
      dword_667B00 != 0) {
    dword_667B00(reinterpret_cast<const char *>(nArg0),
                 (pParams->bSub << 8) | pParams->bCategory, 0xFFFF);
  }
}
