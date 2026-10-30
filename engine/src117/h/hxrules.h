// hxrules.h -- the rules a game plays by.
//
// HxLimits(): is the game past 1.17's limits, players past 7 (the map's HXPL) or a map past
// 256 tiles a side? Then its pools, positions, formats and slots are Hyperion's, and so are
// its rules. HxRules(): does it play by Hyperion's rules? Every game past the limits does; a
// new game 1.17 could play does unless started by classic rules (--rules classic, the "rules"
// setting "classic", the skirmish set-up's Rules) or over the network, and with a mod's data
// (port_moddata.h) always; its replay or save says which. A game by classic rules plays as
// 1.17 does, to the frame.
//
// The rules and the options a game records (HX_OPT_*) are decided once, as it starts
// (RulesForGame, game_init). Hyperion's replay and save name them in their rule record
// (port_hxreplay.cpp), and playing one back or loading it sets them again.
#ifndef HXRULES_H
#define HXRULES_H

// The searches keep 1.17's budgets on a map past 256 tiles a side, for
// pathbench's comparison: HYPERION_PATH_BUDGET=117 as a game starts.
#define HX_OPT_PATH_BUDGET_117 0x1u
// The game's teams were chosen (the skirmish set-up, --teams): its players are allied by
// their nTeam as it starts (port_teams.h).
#define HX_OPT_TEAMS 0x2u
// A computer of the game plays at a level other than Classic (port_ai.h): the
// replay and the save hold each slot's level and style, and the game plays with them.
#define HX_OPT_AI_LEVELS 0x4u
// A benchmark's stress run (--bench-army; --bench-pools; port_bench.h): the replay's
// STRS holds the armies each computer is given at the first frame and the pools' scale.
#define HX_OPT_STRESS 0x8u
// An EUD map (port_eud.h): its triggers read and write 1.16.1's memory through the
// death counts, as StarCraft: Remastered plays them. A new game on such a map takes it.
#define HX_OPT_EUD 0x10u
// StarCraft: Remastered's order fixes (CUnit::SetOrder_Target_type_interrupt): an
// order the game gives a unit is dropped when its queue holds 8 or the order pool is at 90%,
// and an SCV putting up a building still comes down from the air. Every new game by
// Hyperion's rules takes it; a replay or save without it plays as it was recorded.
#define HX_OPT_ORDER_QUEUE 0x20u
// Hyperion's selection (stage S, selectlimits.h): selections and groups of up to 255
// units, sent in chunks (0x8C, net_cmds.h). Every new game by Hyperion's rules takes it but one
// on an EUD map, whose triggers read 1.17's arrays; a replay or save without it selects 12.
#define HX_OPT_SELECTION 0x40u
// A player has a handicap below 100% (handicap.h): a share of the maximum life and
// shields of its units, as StarCraft II's lobby gives it. The replay and the save hold each
// slot's (port_handicap.h).
#define HX_OPT_HANDICAP 0x80u
// The game has mutators (mutators.h): what each mineral field and geyser holds, the
// start resources and workers, the supply cap and the supply each supplier gives. The replay
// and the save hold them (port_mutators.h).
#define HX_OPT_MUTATORS 0x100u
// The options this build plays back and loads.
#define HX_OPT_KNOWN (HX_OPT_PATH_BUDGET_117 | HX_OPT_TEAMS | HX_OPT_AI_LEVELS | HX_OPT_STRESS | HX_OPT_EUD | \
                      HX_OPT_ORDER_QUEUE | HX_OPT_SELECTION | HX_OPT_HANDICAP | HX_OPT_MUTATORS)

// The rule versions' names, as the rule record holds them.
#define HX_RULE_CLASSIC "1.17"
#define HX_RULE_HYPERION "hyperion"

extern bool g_hxRulesGame;         // the game's rules are Hyperion's, decided as it starts
extern unsigned g_hxRuleOptions;   // the game's options, HX_OPT_*

bool HxLimits();
static inline bool HxRules() { return g_hxRulesGame || HxLimits(); }

// As a game starts (game_init, first): the rules and options of the replay being played
// back or the save being loaded, else of the choice (--rules, the "rules" setting) and
// the environment.
void RulesForGame();
// The campaign's mission being played (1.17's word at 0x526524, which a map that is no mission
// clears as it loads), 0 for none. A mission has no mod's data or tools (PRD 2026-10-27).
int RulesCampaignMission();
// Whether --rules, else the "rules" setting, names the classic rules ("classic" or "1.17");
// a new game plays by Hyperion's otherwise.
bool RulesChosenClassic();
// A launch's choice, ahead of --rules and the setting, which the session keeps
// (port_session.h): 1 Hyperion's rules, 0 classic, -1 none.
void RulesChooseNext(int nHyperion);
// A replay or save read: its rule record's rules, options and whether it played with the
// mod's data that loaded (the record's refusal checks it is that mod's), for the game it
// starts. A 1.17 replay or save has none and plays by classic rules (RulesRecordedClassic).
void RulesRecorded(bool bHyperion, unsigned dwOptions, bool bMod);
void RulesRecordedClassic();

#endif
