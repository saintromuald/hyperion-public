// production_strip_test.cpp -- focused checks for the permanent production strip's grouping.
//
// Include the implementation so the test uses the same cells, representatives and progress as
// the overlay. Suppress only its overlay self-registration; function/data sections and
// --gc-sections then leave the unrelated game and drawing systems unlinked.
#include <stdio.h>

#include "../../src117/port/render/port_overlay.h"
#undef PORT_OVERLAY_LAYER
#define PORT_OVERLAY_LAYER(layer)
#include "../../src117/port/gameplay/port_production.cpp"

static Row MakeRow(RowKind kind, unsigned id, DWORD dwSource, int nQueue, int nProgress)
{
  Row r;
  r.kind = kind;
  r.id = id;
  r.dwSource = dwSource;
  r.nQueue = nQueue;
  r.nProgress = nProgress;
  r.bActive = nQueue == 0;
  return r;
}

static int Check(bool bOK, const char *pszWhat)
{
  if (bOK)
    return 0;
  fprintf(stderr, "%s\n", pszWhat);
  return 1;
}

int main()
{
  int nFailed = 0;
  StripCell cells[kStripMax];

  // Four Marines on two barracks are one x4 cell, not one cell per source or queue slot. The
  // later, more-complete source owns the icon/progress/click; the Medic keeps its own cell.
  std::vector<Row> rows;
  rows.push_back(MakeRow(ROW_TRAIN, 0, 101, 0, 20));
  rows.push_back(MakeRow(ROW_TRAIN, 0, 102, 0, 65));
  rows.push_back(MakeRow(ROW_TRAIN, 0, 101, 1, -1));
  rows.push_back(MakeRow(ROW_TRAIN, 0, 102, 2, -1));
  rows.push_back(MakeRow(ROW_TRAIN, 1, 103, 0, 10));
  int nCells = LayoutStripCells(rows, cells, kStripMax);
  nFailed += Check(nCells == 2, "equal unit ids across sources must share one cell");
  nFailed += Check(cells[0].nCount == 4 && cells[1].nCount == 1,
                   "group counts must include active and waiting rows");
  nFailed += Check(cells[0].nRow == 1 && rows[cells[0].nRow].dwSource == 102,
                   "highest progress must represent the group");
  nFailed += Check(cells[1].nRow == 4, "first-seen group order must remain stable");

  // Rebuilding after that representative finishes immediately exposes the next closest source.
  rows.erase(rows.begin() + 1);
  nCells = LayoutStripCells(rows, cells, kStripMax);
  nFailed += Check(nCells == 2 && cells[0].nCount == 3,
                   "refresh must retain the group and subtract its finished row");
  nFailed += Check(cells[0].nRow == 0 && rows[cells[0].nRow].dwSource == 101,
                   "refresh must choose the next closest representative");

  // Progress wins first, activity wins an equal-progress tie, and otherwise the earliest row
  // stays. The last two rows deliberately have equal progress to check that there is no churn.
  rows.clear();
  rows.push_back(MakeRow(ROW_TRAIN, 2, 201, 1, 40));
  rows.push_back(MakeRow(ROW_TRAIN, 2, 202, 0, 40));
  rows.push_back(MakeRow(ROW_TRAIN, 2, 203, 0, 40));
  LayoutStripCells(rows, cells, kStripMax);
  nFailed += Check(cells[0].nRow == 1, "active row must win an equal-progress tie");
  nFailed += Check(!BetterStripRepresentative(rows[2], rows[1]),
                   "equal active rows must retain their stable first representative");

  // Unknown active work has an empty zero-percent well, not a missing bar; waiting work keeps
  // its distinct no-bar state. Activity also selects the live source when both times are unknown.
  rows.clear();
  rows.push_back(MakeRow(ROW_TRAIN, 3, 301, 1, -1));
  rows.push_back(MakeRow(ROW_TRAIN, 3, 302, 0, -1));
  LayoutStripCells(rows, cells, kStripMax);
  nFailed += Check(cells[0].nRow == 1, "active unknown progress must beat waiting unknown");
  nFailed += Check(StripProgress(rows[1]) == 0, "active unknown progress must draw as zero");
  nFailed += Check(StripProgress(rows[0]) == -1, "waiting unknown progress must keep no bar");

  // Kind and id are both significant: a unit id cannot merge training, morphing and construction,
  // and equal numeric research/upgrade ids cannot merge those different kinds either.
  rows.clear();
  rows.push_back(MakeRow(ROW_TRAIN, 9, 401, 0, 10));
  rows.push_back(MakeRow(ROW_MORPH, 9, 402, 0, 20));
  rows.push_back(MakeRow(ROW_CONSTRUCT, 9, 403, 0, 30));
  rows.push_back(MakeRow(ROW_RESEARCH, 4, 404, 0, 40));
  rows.push_back(MakeRow(ROW_UPGRADE, 4, 405, 0, 50));
  rows.push_back(MakeRow(ROW_RESEARCH, 5, 406, 0, 60));
  nCells = LayoutStripCells(rows, cells, kStripMax);
  nFailed += Check(nCells == 6, "unrelated kinds and ids must remain separate cells");
  for (int i = 0; i < nCells; ++i)
    nFailed += Check(cells[i].nRow == i && cells[i].nCount == 1,
                     "separate kinds and ids must retain their own representative and count");

  if (nFailed == 0)
    puts("production strip checks passed");
  return nFailed != 0;
}
