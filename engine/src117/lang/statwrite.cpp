// statwrite.cpp -- the two game-creation log writers, split from SystemSurvey.
// Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/SystemSurvey.h"
#include "../h/statwrite.h"
#include "../gen/globals117.h"

// The same three CRT names SystemSurvey.cpp restates locally: main.cpp
// declares fclose with a void * and sees SystemSurvey.h.
extern "C" {
int __cdecl fclose(FILE *pFile);
char *__cdecl fgets(char *pszBuf, int cchBuf, FILE *pFile);
int __cdecl sscanf_s(const char *pszSrc, const char *pszFormat, ...);
}

// Globals this TU reaches, dual-spelled.

#ifndef OC_gpszConnectionName_g
#define OC_gpszConnectionName_g
#ifdef OC_DLL_MODE
#define gpszConnectionName_g (reinterpret_cast<const char **>(OC_VA(0x50DA58u)))
#else
extern const char *gpszConnectionName_g[6];
#endif
#endif

// 0x4A8B50 (627 B). Counts the logged games per provider, in total and for
// this month, and writes the counts to Protocol.json. The month test ignores
// the year, as 1.17's does.
bool write_game_creation_log()
{
  struct tm tmLine;
  char szPath[0x1000];
  struct tm tmStart;
  ULONG anTotal[6];
  ULONG anMonthly[6];
  FILE *pLog;
  FILE *pOut;
  int nProvider;
  int nFields;
  int nMonth;
  bool bOk;

  sprintf_s(szPath, sizeof(szPath), "%s%s%c%s%c%s", LocalAppDataDirectory(), GetSurveyProduct(),
            '\\', "Game", '\\', "GameCreationLog.bin");
  szPath[sizeof(szPath) - 1] = '\0';
  pLog = NULL;
  stat_open_file(&pLog, szPath, "r");
  memset(anTotal, 0, sizeof(anTotal));
  memset(anMonthly, 0, sizeof(anMonthly));
  GetStartTime(&tmStart);

  while (pLog && fgets(szPath, sizeof(szPath), pLog)) {
    nProvider = get_provider_index(szPath, sizeof(szPath));
    if (nProvider == 0)
      continue;
    // The month is read before the count is tested, and the test goes through
    // a bool, as in 1.17.
    nFields = sscanf_s(szPath, "%*s %4i-%2i-%2iT%2i:%2i:%2i%*s", &tmLine.tm_year, &tmLine.tm_mon,
                       &tmLine.tm_mday, &tmLine.tm_hour, &tmLine.tm_min, &tmLine.tm_sec);
    nMonth = tmLine.tm_mon - 1;
    bOk = nFields == 6;
    if (bOk)
      anTotal[nProvider]++;
    if (bOk && nMonth == tmStart.tm_mon)
      anMonthly[nProvider]++;
  }
  if (pLog)
    fclose(pLog);

  sprintf_s(szPath, sizeof(szPath), "%s%s%c%s%c%s", LocalAppDataDirectory(), GetSurveyProduct(),
            '\\', "Game", '\\', "Protocol.json");
  if (!CreateDirectory(szPath))
    return false;

  pOut = NULL;
  if (!stat_open_file(&pOut, szPath, "wb"))
    return false;

  fprintf(pOut, "{\n");
  fprintf(pOut, "\t\"totalBNETGames\": %lu,\n", anTotal[1]);
  fprintf(pOut, "\t\"monthlyBNETGames\": %lu,\n", anMonthly[1]);
  fprintf(pOut, "\t\"totalIPXGames\": %lu,\n", anTotal[2]);
  fprintf(pOut, "\t\"monthlyIPXGames\": %lu,\n", anMonthly[2]);
  fprintf(pOut, "\t\"totalMODEMGames\": %lu,\n", anTotal[3]);
  fprintf(pOut, "\t\"monthlyMODEMGames\": %lu,\n", anMonthly[3]);
  fprintf(pOut, "\t\"totalDIRECTCABLECONNECTIONGames\": %lu,\n", anTotal[4]);
  fprintf(pOut, "\t\"monthlyDIRECTCABLECONNECTIONGames\": %lu,\n", anMonthly[4]);
  fprintf(pOut, "\t\"totalUDPGames\": %lu,\n", anTotal[5]);
  fprintf(pOut, "\t\"monthlyUDPGames\": %lu,\n", anMonthly[5]);
  fprintf(pOut, "}\n");
  fclose(pOut);
  return true;
}

// 0x4A8DD0 (341 B). The header comment goes in only when the log did not
// exist yet, which the first, read-only open tells it.
bool sub_4A8DD0(DWORD dwProvider)
{
  char szPath[0x1000];
  struct tm tmNow;
  FILE *pFile;
  bool bExisted;
  int nProvider;

  sprintf_s(szPath, sizeof(szPath), "%s%s%c%s%c%s", LocalAppDataDirectory(), GetSurveyProduct(),
            '\\', "Game", '\\', "GameCreationLog.bin");
  szPath[sizeof(szPath) - 1] = '\0';
  CreateDirectory(szPath);

  pFile = NULL;
  bExisted = stat_open_file(&pFile, szPath, "r");
  if (bExisted)
    fclose(pFile);

  if (!stat_open_file(&pFile, szPath, "ab"))
    return false;

  if (!bExisted) {
    fprintf(pFile, "//Log of all the network games played in order to determine what protocols\n");
    fprintf(pFile, "//are no longer being used by the community.\n");
  }

  GetEndTime(&tmNow);
  switch (dwProvider) {
  case 'BNET':
    nProvider = 1;
    break;
  case 'IPXN':
    nProvider = 2;
    break;
  case 'MODM':
    nProvider = 3;
    break;
  case 'SCBL':
    nProvider = 4;
    break;
  case 'UDPN':
    nProvider = 5;
    break;
  default:
    nProvider = 0;
    break;
  }
  fprintf(pFile, "%s ", gpszConnectionName_g[nProvider]);
  WriteTimestamp(&pFile, &tmNow);
  fprintf(pFile, "\n");
  fclose(pFile);
  return true;
}
