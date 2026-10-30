// port_rules.cpp -- the rules a game plays by (h/hxrules.h).
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "../../h/hxrules.h"
#include "../../h/maplimits.h"
#include "../../h/playerlimits.h"  // g_hxPlayers
#include "../platform/port_paths.h"  // PortCommandOptions
#include "../platform/port_settings.h"      // PortSettingString
#include "../data/port_moddata.h"       // g_hxModGame, ModDataId
#include "../gameplay/port_teams.h"         // TeamsThisGame
#include "../gameplay/port_ai.h"            // AiChosenThisGame
#include "../gameplay/port_handicap.h"      // HandicapChosenThisGame
#include "../gameplay/port_mutators.h"      // MutatorsChosenThisGame
#include "../gameplay/port_session.h"       // Session
#include "../tools/port_bench.h"            // PortBenchStressChosen
#include "../../gen/globals117.h"

bool g_hxRulesGame = false;
unsigned g_hxRuleOptions = 0;
// 1.17's types until a mod with data brings its own.
HxTypeCounts g_hxTypes = {UNIT_TYPES_117,   WEAPON_TYPES_117, FLINGY_TYPES_117, SPRITE_TYPES_117,
                          IMAGE_TYPES_117,  ORDER_TYPES_117,  TECH_TYPES_117,   UPGRADE_TYPES_117};
// And its sounds and portraits, a mod's from start-up.
int g_hxSoundTypes = SOUND_TYPES_117;
int g_hxPortraitTypes = PORTRAIT_TYPES_117;

namespace {

// The replay or save read last: its rules and options, for the game it starts.
bool g_recordedHyperion = false;
unsigned g_recordedOptions = 0;
bool g_recordedMod = false;

// The choice for a new game: the launch's (port_session.h), else --rules, else the "rules"
// setting; Hyperion's unless one says "classic" (or "1.17"), by the user's decision of
// 2026-10-22 (PRD). A game over the network plays the rules a room's launch names, the host's
// choice that every seat is told (port_lanlobby.cpp); one made on 1.17's own screens names
// none and plays the classic rules, as the other side may be the original.
bool ChosenHyperion() {
  if (gbMultiPlayerMode != 0)
    return Session().nRules == 1;
  if (Session().nRules >= 0)
    return Session().nRules == 1;
  return !RulesChosenClassic();
}

unsigned ChosenOptions() {
  const char *pszBudget = getenv("HYPERION_PATH_BUDGET");
  return (pszBudget != NULL && strcmp(pszBudget, "117") == 0 ? HX_OPT_PATH_BUDGET_117 : 0) |
         (TeamsThisGame() ? HX_OPT_TEAMS : 0) | (AiChosenThisGame() ? HX_OPT_AI_LEVELS : 0) |
         (PortBenchStressChosen() ? HX_OPT_STRESS : 0) | (HandicapChosenThisGame() ? HX_OPT_HANDICAP : 0) |
         (MutatorsChosenThisGame() ? HX_OPT_MUTATORS : 0) | HX_OPT_ORDER_QUEUE | HX_OPT_SELECTION;
}

}  // namespace

bool RulesChosenClassic() {
  const char *psz = PortCommandOptions()->szRules;
  char sz[32];
  if (psz[0] == '\0' && PortSettingString("hyperion", "rules", sz, sizeof sz))
    psz = sz;
  return lstrcmpiA(psz, "classic") == 0 || lstrcmpiA(psz, HX_RULE_CLASSIC) == 0;
}

bool HxLimits() {
  return g_hxPlayers || gwMapWidth > MAP_SIDE_117 || gwMapHeight > MAP_SIDE_117;
}

int RulesCampaignMission() {
  return *reinterpret_cast<const WORD *>(OC_VA(0x526524u));
}

void RulesForGame() {
  if (gPlayback || gpLoadGameFile != NULL) {
    g_hxRulesGame = g_recordedHyperion;
    g_hxRuleOptions = g_recordedOptions;
    g_hxModGame = g_recordedMod;
  } else {
    // A new game has the mod's types when a mod's data loaded, and plays by Hyperion's
    // rules with them; a campaign's mission never has them, and plays as the
    // campaign was made (the user, 2026-10-27)
    g_hxModGame = ModDataId()[0] != '\0' && RulesCampaignMission() == 0;
    // chosen teams are Hyperion's too (port_teams.h), computers at a level (port_ai.h), a
    // stress run (port_bench.h), a handicap (port_handicap.h) and a mutator (port_mutators.h)
    g_hxRulesGame = ChosenHyperion() || g_hxModGame || TeamsThisGame() || AiChosenThisGame() ||
                    PortBenchStressChosen() || HandicapChosenThisGame() || MutatorsChosenThisGame();
    g_hxRuleOptions = ChosenOptions();
  }
}

void RulesChooseNext(int nHyperion) {
  Session().nRules = nHyperion;
}

void RulesRecorded(bool bHyperion, unsigned dwOptions, bool bMod) {
  g_recordedHyperion = bHyperion;
  g_recordedOptions = dwOptions;
  g_recordedMod = bMod;
}

void RulesRecordedClassic() {
  RulesRecorded(false, 0, false);
}
