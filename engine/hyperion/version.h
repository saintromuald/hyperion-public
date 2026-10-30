// version.h -- Hyperion's version, in one place: the main menu shows the text
// (src117/lang/gluMain.cpp), Settings shows it with the build's number, and Hyperion.exe's
// version resource carries the file version (hyperion.rc).
//
// Versions are MAJOR.MINOR.PATCH with a channel, beta until 1.0 (docs/versioning.md): MINOR
// for a milestone build that goes out, PATCH for an update between. Each spelling below is
// written out, as hyperion.rc cannot build one from the numbers; tools/release_notes.py
// checks they agree, and that the newest release in engine/notes/release-notes.json is this
// version.
//
// The version record 1.17 hands to SNet keeps 1.17's own version, from the product
// version in hyperion.rc (docs/hyperion-exe.md, "The exe on disk").
#pragma once

#define HYPERION_VERSION_MAJOR 0
#define HYPERION_VERSION_MINOR 7
#define HYPERION_VERSION_PATCH 1
#define HYPERION_VERSION_CHANNEL "beta"
#define HYPERION_VERSION_TEXT "Hyperion-v0.7.1-beta"
#define HYPERION_FILE_VERSION 0,7,1,0
#define HYPERION_FILE_VERSION_STR "0.7.1-beta"
