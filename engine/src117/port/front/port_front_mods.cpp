// port_front_mods.cpp -- the Mods screen (docs/front-end.md): the mods found, the one a page
// shows, and making one the active mod. Presentation only.
#include <windows.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_frontkit.h"
#include "../platform/port_paths.h"  // PortExplorer
#include "../data/port_mods.h"
#include "../data/port_moddata.h"  // ModDataId, ModDataName: the data this run loaded
#include "port_ui.h"
#include "port_world.h"
#include "port_front_internal.h"

using namespace FrontKit;

namespace Front {

// ---------------------------------------------------------------------------
// The Mods screen: the mod library, in the Settings dialog's frame. The mods
// down the left, the built-in ones first; the chosen one's page in the well, with what it
// changes and whether it changes the rules; Activate makes it the active mod at once,
// kept in the settings file, and the home screen offers what it brings. A mod's data is read
// as Hyperion starts (ModDataStartUp), so a mod whose data differs from the run's offers a
// restart into it, as Dawn of War's mod manager restarts into a mod (the user, 2026-10-27).

// The data a mod's games have: its id when it, or a mod it builds on, brings data; "" when
// none does, or a mod it builds on is missing, which *pWhy then names.
std::string ModsDataOf(int nMod, std::string *pWhy)
{
  const std::vector<PortMod> &mods = PortMods();
  std::vector<int> chain;
  if (!PortModChain(nMod, &chain, pWhy))
    return "";
  for (int k : chain)
    if (!mods[k].data.empty())
      return mods[nMod].id;
  return "";
}

// Why a mod cannot be made active ("" when it can): a mod it builds on is missing or too old.
std::string ModsMissing(int nMod)
{
  std::string why;
  ModsDataOf(nMod, &why);
  return why;
}

std::string ModsRestartText(void)
{
  std::string why;
  if (!ModsDataOf(g_nMod, &why).empty())
    return PortTextF("\xE2\x80\x9C{0}\xE2\x80\x9D brings its own units, weapons and rules, which Hyperion "
                     "reads as it starts.",
                     {ActiveMod().name});
  return PortTextF("Games are played without \xE2\x80\x9C{0}\xE2\x80\x9D once Hyperion starts again.", {ModDataName()});
}

void ModsOpen(void)
{
  PortModsRescan();
  g_nMod = PortModActive();
  g_nModPick = g_nMod;
  g_bMods = true;
  g_bMenuOpen = false;
  WorldSetView("mods", false);
}

void ModsClose(void)
{
  g_bMods = false;
  WorldSetView("home", false);
}

// The restart the active mod's data needs, if it needs one.
static void ModsAskRestart(void)
{
  std::string why;
  if (ModsDataOf(g_nMod, &why) != ModDataId())
    g_nConfirm = CONFIRM_MOD_RESTART;
}

void ModsActivate(void)
{
  g_nMod = g_nModPick;
  PortModSetActive(g_nMod);
  WorldSetPlanet(ActiveMod().world.c_str(), false);
  ModsClose();
  // a tool mod asks first whether it may open the live channel, once for each mod
  if (ActiveMod().bChannel && !PortModChannelAllowed(g_nMod))
    g_nConfirm = CONFIRM_MOD_CHANNEL;
  else
    ModsAskRestart();
}

std::string ModsChannelText(void)
{
  return PortTextF("\xE2\x80\x9C{0}\xE2\x80\x9D opens Hyperion's live channel while it is active. Programs on this "
                   "computer that you run can then read and change your games through it. Allow it only for a mod "
                   "you trust.",
                   {ActiveMod().name});
}

void ModsChannelAnswered(bool bAllow)
{
  g_nConfirm = CONFIRM_NONE;
  if (bAllow)
    PortModAllowChannel(g_nMod);
  ModsAskRestart();
}

// Explorer on the mods folder, made first if it is not there yet.
void ModsOpenFolder(void)
{
  const std::string folder = PortModsFolder();
  CreateDirectoryA(folder.c_str(), NULL);
  std::string cmd = "\"" + std::string(PortExplorer()) + "\" \"" + folder + "\"";
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  SDL_zero(si);
  si.cb = sizeof si;
  if (CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

// One line of the page: a label in the settings' label colour, then its text.
void ModsLabel(const char *pszLabel)
{
  Decl l;
  l.d.layout.padding = Pad(0, 0, Fs(26), Fs(8));
  Open(l);
  Text(pszLabel, UI_FACE_BOLD, Fs(16), 0.08f, kScInkLabel);
  Close();
}

void ModsPage(const PortMod &m, const std::string &missing)
{
  Decl page = Floating(NULL, 0, Sc(40), Sc(30), 63, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  page.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  page.d.layout.sizing.width = Fixed(Sc(975 - 80));
  page.d.floating.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH;
  Open(page);
  Text(m.name.c_str(), UI_FACE_WIDE, Fs(40), 0.02f, kScInkHead);
  const std::string meta = m.bBuiltIn ? PortTextF("Built in \xC2\xB7 by {0}", {m.by})
                                      : PortTextF("Installed \xC2\xB7 version {0} \xC2\xB7 by {1}", {m.version, m.by});
  Decl mt;
  mt.d.layout.padding = Pad(0, 0, Fs(8), Fs(22));
  Open(mt);
  Text(Keep(meta), UI_FACE_TEXT, Fs(17), 0.0f, kScInkMsg);
  Close();
  Decl line;
  line.d.layout.sizing.width = Grow();
  Open(line);
  Text(PortText(m.line.c_str()), UI_FACE_TEXT, Fs(21), 0.0f, kScInkButton);
  Close();

  ModsLabel(PortText("WHAT IT CHANGES"));
  Decl chips;
  chips.d.layout.childGap = Gap(Fs(10));
  Open(chips);
  for (const std::string &h : m.holds) {
    Decl c;
    c.d.layout.padding = Pad(Fs(12), Fs(12), Fs(5), Fs(5));
    c.d.border = Border(kScDdRim, 1, 1, 1, 1);
    c.d.backgroundColor = Col(kScCountFill);
    Open(c);
    Text(Keep(h), UI_FACE_TEXT, Fs(16), 0.02f, kScInkMini);
    Close();
  }
  Close();
  Decl rules;
  rules.d.layout.padding = Pad(0, 0, Fs(14), 0);
  rules.d.layout.sizing.width = Grow();
  Open(rules);
  if (m.rules == "changed")
    Text(PortText("It changes the rules: games on it play differently from the original, and their replays are its own."),
         UI_FACE_TEXT, Fs(18), 0.0f, kScInkAccept);
  else
    Text(PortText("It keeps the classic rules: its games play as the original plays them."), UI_FACE_TEXT, Fs(18), 0.0f,
         kScInkMsg);
  Close();

  // what a tool mod opens beyond its own files
  if (m.bChannel) {
    ModsLabel(PortText("WHAT IT OPENS"));
    Decl ch;
    ch.d.layout.sizing.width = Grow();
    Open(ch);
    Text(PortModChannelAllowed(g_nModPick)
             ? PortText("Hyperion's live channel, while it is active: programs on this computer can read and change "
                        "your games through it. You allowed it.")
             : PortText("Hyperion's live channel, while it is active: programs on this computer can read and change "
                        "your games through it. It asks before it opens the channel the first time."),
         UI_FACE_TEXT, Fs(18), 0.0f, kScInkAccept);
    Close();
  }

  // the mods it builds on, loaded before it, and what keeps it from being made active
  if (!m.depends.empty()) {
    ModsLabel(PortText("BUILT ON"));
    std::string on;
    for (const PortModDependency &d : m.depends) {
      std::string name = d.id;
      for (const PortMod &o : PortMods())
        if (o.id == d.id)
          name = o.name;
      // {0} the mod's name, {1} the version it needs
      on += (on.empty() ? "" : ", ") + (d.version.empty() ? name : PortTextF("{0} {1} or later", {name, d.version}));
    }
    Decl b;
    b.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    b.d.layout.sizing.width = Grow();
    b.d.layout.childGap = Gap(Fs(6));
    Open(b);
    Text(Keep(on), UI_FACE_TEXT, Fs(18), 0.0f, kScInkButton);
    if (!missing.empty())
      Text(Keep(missing + "."), UI_FACE_TEXT, Fs(18), 0.0f, kScInkAccept);
    Close();
  }

  if (!m.button.empty()) {
    ModsLabel(PortText("ON THE HOME SCREEN"));
    // {0} the button's name on the home screen, {1} the map's tileset
    const std::string offer =
        m.mode == "campaign" ? PortTextF("{0}, and every episode of its campaigns.", {m.button})
        : m.mode == "canvas" ? PortTextF("{0}: a map made on the spot ({1}), started at once.", {m.button, m.tileset})
        : m.mode == "bench"
            ? PortTextF("{0}: up to 64 computers on a map up to 1,024 by 1,024 tiles, watched "
                        "whole and timed, to measure how far this build can be pushed; the "
                        "runs logged to compare builds by. Still a work in progress.",
                        {m.button})
        : m.mode == "trainer"
            ? PortTextF("{0}: a game against computers you can go back in, to any moment, and "
                        "try again; the tries worth keeping saved as sessions. A proof of "
                        "concept, still a work in progress, for trying out timelines and "
                        "their paths.",
                        {m.button})
        : m.mode == "lab"
            ? PortTextF("{0}: games of computers played in the background, many at once, shown as "
                        "a wall of maps with their units moving, any one of them watched.",
                        {m.button})
            : m.button;
    Decl o;
    o.d.layout.sizing.width = Grow();
    Open(o);
    Text(Keep(offer), UI_FACE_TEXT, Fs(18), 0.0f, kScInkButton);
    Close();
  }
  if (!m.folder.empty()) {
    ModsLabel(PortText("FOLDER"));
    Text(Keep(ElideFront(m.folder.c_str(), Sc(975 - 80))), UI_FACE_WIDE, Fs(16), 0.0f, kScInkMini);
  }
  Close();
}

void ModsDialog(void)
{
  const std::vector<PortMod> &mods = PortMods();
  if (g_nModPick >= static_cast<int>(mods.size()))
    g_nModPick = 0;
  Decl black;
  black.d.id = Id("modblack");
  black.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  black.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  black.d.backgroundColor = Col(kScBlackout);
  black.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  black.d.floating.zIndex = 60;
  Open(black);
  Close();

  Decl dlg;
  dlg.d.id = Id("moddlg");
  dlg.d.layout.sizing.width = Fixed(Sc(1400));
  dlg.d.layout.sizing.height = Fixed(Sc(1170));
  dlg.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  dlg.d.floating.attachPoints.element = CLAY_ATTACH_POINT_CENTER_TOP;
  dlg.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_CENTER_TOP;
  dlg.d.floating.offset.y = Sc(15);
  dlg.d.floating.zIndex = 61;
  static UiCustom frame = {0.0f, DrawScFrame, const_cast<UiColor *>(&kScFillSolid)};
  dlg.d.custom.customData = &frame;
  Open(dlg);

  Decl title = Floating(NULL, 0, 0, Sc(35), 62, CLAY_ATTACH_POINT_CENTER_TOP, CLAY_ATTACH_POINT_CENTER_TOP);
  Open(title);
  Text(PortText("MODS"), UI_FACE_WIDE, Fs(24), 0.04f, kScInkHi);
  Close();

  // the library: one raised row a mod, its name and where it comes from
  for (int i = 0; i < static_cast<int>(mods.size()); ++i) {
    const PortMod &m = mods[i];
    Decl b = Floating("modrow", i, Sc(35), Sc(87 + i * 96), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
    const bool bHere = i == g_nModPick;
    const bool bHot = Over(b.d.id);
    if (Clicked(b.d.id))
      g_nModPick = i;
    b.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    b.d.layout.sizing.width = Fixed(Sc(341));
    b.d.layout.sizing.height = Fixed(Sc(82));
    b.d.layout.padding = Pad(Fs(24), Fs(20), 0, 0);
    b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    b.d.layout.childGap = Gap(Fs(4));
    b.d.custom.customData = Custom(DrawScRaised, bHere ? kScTabOn : bHot ? kScTabHi : kScTab);
    Open(b);
    Text(m.name.c_str(), UI_FACE_WIDE, Fs(18), 0.02f, bHere || bHot ? kScInkHi : kScInkButton);
    const std::string sub = i == g_nMod ? std::string(PortText("Active"))
                            : m.bBuiltIn ? std::string(PortText("Built in"))
                                         : PortTextF("Version {0}", {m.version});
    Text(Keep(sub), UI_FACE_TEXT, Fs(15), 0.02f, i == g_nMod ? kScInkAccept : kScInkMenu);
    Close();
  }
  Decl folder = Floating("modfolder", 0, Sc(35), Sc(1000), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  folder.d.layout.sizing.width = Fixed(Sc(341));
  folder.d.layout.sizing.height = Fixed(Sc(42));
  if (ScMini(folder, PortText("Open mods folder"), 'O'))
    ModsOpenFolder();

  // the chosen mod's page
  Decl well = Floating("modwell", 0, Sc(386), Sc(78), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  well.d.layout.sizing.width = Fixed(Sc(975));
  well.d.layout.sizing.height = Fixed(Sc(975));
  well.d.backgroundColor = Col(kScWell);
  well.d.border = Border(kScWellRim, Fs(2), Fs(2), Fs(2), Fs(2));
  Open(well);
  const std::string missing = ModsMissing(g_nModPick);
  ModsPage(mods[g_nModPick], missing);
  Close();

  // Activate in gold, Close beside it, as Settings has Accept and Cancel
  const bool bActive = g_nModPick == g_nMod;
  Decl act = Floating("modact", 0, Sc(382), Sc(1064), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  act.d.layout.sizing.width = Fixed(Sc(315));
  act.d.layout.sizing.height = Fixed(Sc(52));
  const bool bActivate =
      ScButton(act, bActive ? PortText("Active") : PortText("Activate"), 'A', true, !bActive && missing.empty());
  Decl close = Floating("modclose", 0, Sc(703), Sc(1064), 62, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  close.d.layout.sizing.width = Fixed(Sc(315));
  close.d.layout.sizing.height = Fixed(Sc(52));
  const bool bClose = ScButton(close, PortText("Close"), 'C', false);
  Close();

  if (bActivate)
    ModsActivate();
  else if (bClose)
    ModsClose();
}

// The Mods screen's keys: A, C or Escape, O, and the arrows to move down the library.
void ModsKeys(void)
{
  const int nMods = static_cast<int>(PortMods().size());
  if (Key(VK_ESCAPE) || Key('C'))
    ModsClose();
  else if (Key('A') && g_nModPick != g_nMod && ModsMissing(g_nModPick).empty())
    ModsActivate();
  else if (Key('O'))
    ModsOpenFolder();
  else if (Key(VK_DOWN))
    g_nModPick = (g_nModPick + 1) % nMods;
  else if (Key(VK_UP))
    g_nModPick = (g_nModPick + nMods - 1) % nMods;
}

}  // namespace Front
