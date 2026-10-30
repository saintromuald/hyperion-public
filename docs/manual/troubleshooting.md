# When something goes wrong

## Lede

Every start of Hyperion writes a log of each step it takes, and when a start fails, Hyperion
says so in a window. When Hyperion stops on an error, it saves a report with the build, the
system, what you were doing and the logs, and shows the same window. The window holds the
details as text to copy: pasted in a GitHub issue or on Discord with a few words, it is enough
to find what went wrong.

## How to use it

### If Hyperion doesn't start

Each start writes `hyperion_startup.log` in Hyperion's `Data` folder, one line for each step,
from reading its settings to the first frame on screen. The five starts before it are kept in
`Data\logs\`. When a start fails, a window says what failed and where the log is, with the
details to copy (below).

- **Windows: "SDL3.dll is missing".** Hyperion.exe needs `SDL3.dll` beside it. Unpack the whole
  zip into a folder and start Hyperion.exe from there, not from inside the zip.
- **Windows: Hyperion.exe is gone, or Defender warns.** Hyperion is not signed, so Windows
  Defender or SmartScreen may flag it as a false positive. Restore it from quarantine, or allow
  it.
- **Mac: "cannot be opened".** macOS stops an app that Apple has not signed. Open System
  Settings, Privacy & Security, and click **Open Anyway** beside Hyperion's name. Hyperion
  writes no log until macOS lets it run.
- **Linux: "Permission denied".** The program lost its mark as one: `chmod +x Hyperion.x86_64`
  (or `Hyperion.aarch64`). A message that names `GLIBC_2.xx` means the distribution is older than
  Hyperion needs.
- **Nothing comes on screen.** The log's last line is the step it stopped at. After 30 seconds
  with no progress, the log also says where Hyperion is waiting.
- **The game's files are not found.** The first start asks for the folder of each StarCraft
  you have. Settings, Data and mods, **Game files**, switches between them later.

When the `Data` folder cannot be written to, the log goes to the system's folder for temporary
files instead, in `Hyperion` there (`%TEMP%\Hyperion\` on Windows), and its first line says
why.

If Hyperion was closed from outside while it was starting, or its session ended with no word,
the next start says so once, and keeps that session's logs in `Data\crashes\`.

### If Hyperion stops on an error

Hyperion saves a report in a folder of its own in `Data\crashes\`, named by the time, and a
window says where it is, with the details to copy (below). The folder holds:

- `report.txt`: what happened and where, the Hyperion build, the system (Windows version, CPU,
  memory, graphics card and driver, displays), which StarCraft files it read, the game being
  played (map, players, mutators, mod) and the last lines Hyperion logged;
- `report.json`: the same, for a tool to read;
- `minidump.dmp` on Windows: the program's state at that moment, for a debugger;
- copies of the logs that session wrote.

`Data\hyperion_crash.log` is a copy of the last `report.txt`. The last ten reports are kept.

### What to send

The window has a box of text and a **Copy to clipboard** button. Press it, paste the text in a
GitHub issue or on Discord, and say what you were doing. The text is already in a code block
and fits in one Discord message. It names no folder, network address or name of yours:
Hyperion's own folders are written `<program>`, `<data>` and `<game>`, and other folders
`...\`. **Open the folder** shows the log or the report, if more is asked for.

On Linux, paste the text before you close the window: what is copied goes with it unless a
clipboard manager keeps it. The same text is on the standard error when Hyperion was started
from a terminal.

The logs and the report themselves name the folders Hyperion uses, with your user folder
written as `%USERPROFILE%` (or `~` on Linux and a Mac), and leave out your computer's name and
your own.

## Limits and known issues

- A few errors end a program before anything can report them: memory the program itself has
  damaged, and some failures inside Windows. Then the next start can only say that the last
  session ended with no word. Windows' own record of it is in Event Viewer, Windows Logs,
  Application.
- A report from a downloaded build gives Hyperion's places in its code as numbers. They are
  turned into names when the report is read, from that build's own symbols.
- In a web browser, an error goes to the browser's console, not to a file.

## In progress and planned

- Sending a report online from Hyperion itself, so it no longer has to be copied and pasted by
  hand.

## Technical notes

- The start-up log: `engine/src117/port/tools/port_startup.cpp`. The system facts it and the
  crash report share: `port_sysinfo.cpp`. The session's part of the report:
  `port_crashnotes.cpp`. The report: `port_crash.cpp`. The window: `port_errorwin.cpp`, and
  the text to copy: `port_share.cpp`.
- A crashed Hyperion starts a copy of itself, `Hyperion --crash-report <folder>`, to show the
  window, so nothing more runs in the process that crashed.
- On Windows the report covers a fault, `abort`, `std::terminate` (a C++ exception no one
  caught, on any thread), an invalid parameter to the C library and a pure virtual call. On
  Linux and a Mac it covers the fatal signals.
- `SDL3.dll` is a delay-loaded import, so Hyperion can say it is missing instead of Windows'
  loader error.
- Each Windows build's exe and PDB are kept by `engine/tools/package.py`, and
  `engine/tools/crash_symbols.py <report folder>` names a report's frames from them, or the
  frames of a text pasted from the window, saved to a file.
- On Linux and a Mac the report's `symbols:` line is the program's build id (the GNU build id,
  or the Mac's UUID). Packaging keeps each Linux program, which holds its debug info, and the
  Mac's with its `.dSYM`; `crash_symbols.py` finds the build by that id and names Linux frames
  with `addr2line` (through WSL on Windows) or `llvm-symbolizer`, and a Mac's with
  `llvm-symbolizer`.
- Module docs: `engine/docs/modules/platform.md` ("The start-up log") and
  `engine/docs/modules/tools.md` ("Crashes and traces", "The error window").
