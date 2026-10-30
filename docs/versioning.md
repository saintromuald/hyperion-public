# Hyperion's versions and release notes

Decided 2026-10-15 (the user): Hyperion has a version number and a log of every update, kept
the way a product's release notes are, and the game shows both. Since 2026-10-29 the log holds
public releases only, each written with the user (below, "How it is kept").

## The version

A version is MAJOR.MINOR.PATCH with a channel: v0.3.0-beta.

| part | when it goes up |
|---|---|
| MAJOR | 1 at the first release that is not a beta; until then 0 |
| MINOR | a milestone build that goes out to people (a package for StarDraft 2's team, a public preview); PATCH goes back to 0 |
| PATCH | an update between milestones that a player or an editor would notice: fixes, small features |
| channel | `beta` until 1.0 |

The build's number is beside the version, not part of it: the commits on the branch it was
built from (`git rev-list --count HEAD`), with the commit itself. Settings shows the three at
its foot, on the left: `Hyperion v0.3.0-beta · build 272 · 2bf0e85`, with a `+` after the
commit when the build had changes not yet committed. Patch Notes shows the same under its list.

The version lives in `engine/hyperion/version.h`, its numbers and each spelling written out
(the exe's version resource cannot build one from the numbers). The main menu of the original's
screens shows `HYPERION_VERSION_TEXT`, replays record it, and `package.py` names the zips by it.

## The release notes

`engine/notes/release-notes.json` is the log, newest entry first. The build compiles it into
Hyperion.exe (`tools/release_notes.py header`, from `build_engine_x64.py`), so every build
carries its own notes. The home screen's notifications are its first three entries; "Read the
notes" opens Patch Notes at that entry, and Menu, Patch Notes opens it at the newest. A
notification closed once stays closed (`notesDismissed` in settings.json); a new entry has a
new id, so it shows.

An entry is a release or an announcement:

| field | |
|---|---|
| `id` | unique and never reused; a release's is its version (`v0.3.0-beta`) |
| `kind` | `release` or `announcement` |
| `version` | a release's, without the v (`0.3.0-beta`) |
| `date` | YYYY-MM-DD |
| `title` | a few words |
| `summary` | two sentences at most, at the top of the entry |
| `line` | the notification's one line, 110 characters at most; the summary when absent |
| `media` | what plays in the entry's frame, in turn (below) |
| `new`, `improved`, `changed`, `fixed` | the release's items, each `{"title", "text"}`; shown as New features, Improvements, Changes and Bug fixes, and left out when empty |
| `body` | an announcement's paragraphs |

Media, in a 16:9 frame under the summary, shown only when an entry has some:

| kind | |
|---|---|
| `image` | a PNG `file` in `engine/notes/` (shipped in the package's `Data\notes\`), up to 768 by 384 or scaled to fit |
| `frames` | a PNG of `frames` frames side by side, played at `fps` |
| `clip` | a Smacker video by its name in the game's data, played from the player's own install |

Each has a `caption`. Pictures committed here may not show Blizzard's art.

Planned (the user, 2026-10-15): in place of drawn mock-ups, the frame shows the engine itself,
real units on a real map with no HUD, playing a short recorded scene of the feature. The same
HUD-less view is meant to be reused elsewhere in the menus. A recording is a replay or a scene
file, which holds commands and no art, so it can be committed and shipped; the player's own
install draws it.

## How it is kept

- The log holds public releases only: builds that go out to players. Its first entry is
  v0.6.29-beta, the first public build, as one initial release with a summary of what it has
  (the user, 2026-10-29: "Only have for last build made public, combine that to a simple
  initial release").
- Nothing is added at a merge. For each public release the user and the session agree the
  entry: the version, what is major and what is minor, and what goes into a line such as
  "Other bug fixes and improvements" as immaterial. Builds between public releases carry the
  last public release's notes.
- `python engine/tools/release_notes.py check` checks the log's form, and that its newest
  release is not past `version.h` (a build may be past it, between public releases);
  `package.py` runs it and will not package when it fails, and writes
  `docs/release-notes.md` from the log (`release_notes.py md`), which the package carries in
  `docs\`.
- The notes are written for the people who use Hyperion: what changed for them, in plain
  sentences, without take numbers or function names.
