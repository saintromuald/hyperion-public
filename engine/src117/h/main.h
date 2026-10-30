// main.h -- declarations for src117/lang/main.cpp: the program's entry, the
// install-path set-up and the top-level game loop, 0x466F20 through 0x467E60.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_MAIN_H
#define OC117_MAIN_H

#include "platform.h"

// WinMain below restates the declaration <winbase.h> makes, so the SDK
// spellings and C linkage must be in scope or the two become an overload pair
// (C2365). They come through Storm.h, which defines NOGDI first; a plain
// <windows.h> here would bring GDI declarations that clash with the tree's own.
#include "Storm.h"

// Refuses a second instance. ECX is the window title fed to FindWindowA after
// CreateEventA reports ERROR_ALREADY_EXISTS; EAX at the ret is the indirect
// call's and no caller reads it.
void CheckForOtherInstances(const char *pszTitle);

// Copies one directory's files, recursing into subdirectories. ECX is the
// source and EDX the destination: GetFileAttributesA on ECX, then CopyFileA
// with both. Two consuming callers read the full EAX.
int CopyFilesRecursive(const char *pszSrc, const char *pszDst);

// Creates every directory along a path, tolerating ERROR_ALREADY_EXISTS and
// ERROR_ACCESS_DENIED. ECX is the path. Both callers discard EAX, but 1.17's
// body returns its success flag.
int CreateDirs(const char *pszPath);

// Whether a path sits under CSIDL_PROGRAM_FILES. ECX is the path; the body
// takes the shell folder with SHGetFolderPathA and compares. One consuming
// caller reads the full EAX.
int IsProgramFilesDir(const char *pszPath);

// An app_add_exit callback: files the survey and ends in a tail jump into
// RunSystemSurvey, whose AL nothing reads.
void sub_4671B0();

// Copies a directory tree. ECX and EDX are the two roots. Returns the OR of
// every recursive result, which CopyFilesRecursive returns in turn.
int CopyDirRecursive(const char *pszSrc, const char *pszDst);

// Moves the pre-Appdata install directories to their Appdata homes. ECX and
// EDX are the two roots; the body builds a seven-entry table of subdirectory
// names in locals. One consuming caller reads the full EAX.
int TransferLegacyDirsToAppdata(const char *pszOld, const char *pszNew);

// Establishes the Appdata paths. Nullary; EAX at the ret is
// SFileSetAppDataDir's and no caller reads it.
void InitPaths();

// Replaces the process DACL. Nullary; WinMain discards EAX, but the body
// returns a 0/1 flag.
int InitACL();

// Writes the locale files with the CRT's fopen, fputc and fclose. Nullary; no
// caller reads EAX.
void InitLocale();

// Sizes the memory pools. Nullary; EAX at the ret comes from SRegLoadValue and
// a Win32 call, and no caller reads it.
void InitMemory();

// The top level: it never returns, ending in app_exit.
void RunGame();

// One pass of the game. Nullary; EAX at the ret is free_game's and no caller
// reads it.
void main_game_run();

// Creates and shows the main window. Nullary; EAX at the ret is
// init_lang_imm's and no caller reads it.
void InitWindow();

// `ret 10h`, so __stdcall and four stack words, of which only the first is
// read; returns 0 or 1. The Win32 types are spelled so this agrees with the
// declaration <windows.h> makes rather than becoming a second overload.
extern "C" {
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine, int nShowCmd);
}  // extern "C"

#endif  // OC117_MAIN_H
