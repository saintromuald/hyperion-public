// music.h -- declarations for src117/lang/music.cpp, the background music
// player: the song table, the MPQ-backed DDA stream and the fade timers.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_MUSIC_H
#define OC117_MUSIC_H

#include "platform.h"
// No <windows.h> here: gen/oracle_thunks.cpp includes this after dlg_ctrl.h's
// `#undef _toupper`, and Storm.h would bring <ctype.h> and the macro back
// (saveload.h has the same note). The two types below are spelled as
// <windows.h> spells them; UINT_PTR comes from <basetsd.h> through platform.h.
struct HWND__;
typedef struct HWND__ *HWND;
typedef unsigned int   UINT;

// music_update, play_music, stop_music, music_enabled, start_song,
// set_music_volume and stop_song are declared in event_Win.h, gluRdy.h and
// snd.h and not restated here.

// One row of the song table at 0x508C58, 25 rows of 8 bytes.
struct song_t {
  // The MPQ path play_music_track opens.
  /*0x00*/ const char *pszFile;
  // The playback mode: music_update chases a follow-on track only when it is 2,
  // and play_music_track sets SFileDdaBeginEx's second flag word on 1.
  /*0x04*/ BYTE bMode;
  // Nonzero: play_music ramps the volume up rather than starting at the setting.
  /*0x05*/ BYTE bFadeIn;
  // The track music_update starts when this one runs out.
  /*0x06*/ BYTE bNext;
  // Never read.
  /*0x07*/ BYTE bUnused07;
};

static_assert(offsetof(song_t, pszFile)  == 0x00, "song_t::pszFile   play_music_track: push dword ptr [esi*8+508C58h]");
OC_X86_LAYOUT(offsetof(song_t, bMode)    == 0x04, "song_t::bMode     music_update: cmp byte ptr [eax*8+508C5Ch], 2h");
OC_X86_LAYOUT(offsetof(song_t, bFadeIn)  == 0x05, "song_t::bFadeIn   play_music: cmp byte ptr [ecx*8+508C5Dh], 0h");
OC_X86_LAYOUT(offsetof(song_t, bNext)    == 0x06, "song_t::bNext     music_update: movzx ecx, byte ptr [eax*8+508C5Eh]");
OC_X86_LAYOUT(offsetof(song_t, bUnused07) == 0x07, "song_t::bUnused07 never read; pinned so the 8-byte stride stays honest");
OC_X86_LAYOUT(sizeof(song_t)             == 8,    "song_t            every access scales the index by 8");

// ECX is the open Storm file handle. Returns 0 or 1.
int IsRemoteFile(void *hFile);

// Nullary. Announces the switch on the message line, then plays the track.
void music_PlayRadioFreeZerg();

// The WM_TIMER callback play_music installs on timer id 3, a TIMERPROC; it
// reads only the timer id, which it hands KillTimer.
void __stdcall music_rampup_timerproc(HWND hWnd, UINT uMsg, UINT_PTR idEvent,
                                     DWORD dwTime);

// Nullary; duck the music by one step and remember that it is ducked.
void sub_46F120();

// ECX is the song table index, a 32-bit int: music_update widens the byte at
// the call site.
void play_music_track(int nTrack);

// Nullary; the inverse of sub_46F120.
void music_volume_default();

#endif  // OC117_MUSIC_H
