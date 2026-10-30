// port_mods.h -- the mods Hyperion knows:
// StarCraft and Brood War, built in, then each folder under mods\ in the player data
// folder that holds a mod.json. One is active at a time, kept in the settings file
// ("hyperion", "mod", its id); the front end's Mods screen chooses it and its home screen
// starts what the mod offers.
#ifndef PORT_MODS_H
#define PORT_MODS_H

#include <string>
#include <vector>

// A mod another builds on ("depends"): its id, and the least version it needs, "" for any.
struct PortModDependency {
  std::string id;
  std::string version;
};

struct PortMod {
  std::string id;
  std::string name;
  std::string by;
  std::string version;
  std::string line;               // what it is, in a sentence
  std::vector<std::string> holds; // what it changes or brings: "campaign", "maps", "tools"...
  std::string rules;              // "standard", or "changed" when its games play otherwise
  std::string world;              // the planet behind the home screen (port_world.h)
  std::string folder;             // its folder, ending in a backslash; empty when built in
  std::string data;               // its data's folder in it, ending in a backslash; empty for none (port_moddata.h)
  std::vector<PortModDependency> depends;  // the mods whose data loads before its own
  bool bBuiltIn = false;
  bool bExpansion = false;        // a built-in game: Brood War rather than StarCraft
  bool bChannel = false;          // a tool mod: the live channel opens while it is active (port_control.h)
  // What the home screen's main button starts: "campaign" for the built-in games, or a
  // mode of the mod's own ("canvas"), with the button's label and the mode's values.
  std::string mode;
  std::string button;
  std::string tileset;
};

// Every mod, the built-in ones first, then the installed ones in their folders' order.
// Read on first use; PortModsRescan reads mods\ again.
const std::vector<PortMod> &PortMods(void);
void PortModsRescan(void);

// The active mod's place in PortMods: the one --mod names, else the settings'; and making
// another active, which the settings keep and which outranks --mod from then on.
int PortModActive(void);
void PortModSetActive(int i);

// Whether a tool mod (bChannel) may open the live channel: the player allowed it on the Mods
// screen, for that mod in that folder, or named it with --mod. PortModAllowChannel keeps the
// player's yes in the settings.
bool PortModChannelAllowed(int i);
void PortModAllowChannel(int i);

// The folder installed mods are read from, ending in a backslash.
std::string PortModsFolder(void);
// The folders in it that were left out, each with why (--mod-check).
const std::vector<std::string> &PortModsSkipped(void);

// The mods whose data a mod's loads with, as places in PortMods, in load order: each mod it
// depends on before the mods that depend on it, and the mod itself last. False, with why, when
// one is not installed, is older than the version named, or depends on itself.
bool PortModChain(int nMod, std::vector<int> *pChain, std::string *pWhy);

#endif
