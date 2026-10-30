// snd.h -- declarations for src117/lang/snd.cpp: the DirectSound layer (oracle
// 0x49D520-0x49ED60), device and buffer creation, WAV file parsing, the volume
// and options block, playback, and the streamed-music toggles.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default. Some of this TU's names are declared
// elsewhere and not restated here: snd_get_duration and sub_49E780
// (gamesnd.h), sub_49E740 (glues.h), snd_play_music (gluRdy.h) and
// snd_end_streamed (victory.h).
#pragma once

#include "platform.h"
#include "dlg_str.h"

// The two COM interfaces this TU drives.

// Virtual members, not function-pointer fields: 1.17 calls each method as
// `call dword ptr [reg+slot]`, which only virtual dispatch gives. The method
// names are DirectSound's published ones, and each interface runs as far as the
// slots snd.cpp calls.

struct IDirectSound {
  virtual LONG __stdcall QueryInterface(const void *riid, void **ppv) = 0;
  virtual ULONG __stdcall AddRef() = 0;
  virtual ULONG __stdcall Release() = 0;
  /*0x0C*/ virtual LONG __stdcall CreateSoundBuffer(const void *pDesc,
                                                    void **ppBuffer,
                                                    void *pUnkOuter) = 0;
  virtual LONG __stdcall GetCaps(void *pCaps) = 0;
  virtual LONG __stdcall DuplicateSoundBuffer(void *pSrc, void **ppDup) = 0;
  /*0x18*/ virtual LONG __stdcall SetCooperativeLevel(void *hWnd,
                                                      DWORD dwLevel) = 0;
};

struct IDirectSoundBuffer {
  virtual LONG __stdcall QueryInterface(const void *riid, void **ppv) = 0;
  virtual ULONG __stdcall AddRef() = 0;
  /*0x08*/ virtual ULONG __stdcall Release() = 0;
  virtual LONG __stdcall GetCaps(void *pCaps) = 0;
  virtual LONG __stdcall GetCurrentPosition(DWORD *pdwPlay,
                                            DWORD *pdwWrite) = 0;
  /*0x14*/ virtual LONG __stdcall GetFormat(void *pwfx, DWORD cbSize,
                                            DWORD *pcbWritten) = 0;
  virtual LONG __stdcall GetVolume(LONG *plVolume) = 0;
  virtual LONG __stdcall GetPan(LONG *plPan) = 0;
  virtual LONG __stdcall GetFrequency(DWORD *pdwFrequency) = 0;
  /*0x24*/ virtual LONG __stdcall GetStatus(DWORD *pdwStatus) = 0;
  virtual LONG __stdcall Initialize(void *pDS, const void *pDesc) = 0;
  virtual LONG __stdcall Lock(DWORD dwOffset, DWORD dwBytes, void **ppv1,
                              DWORD *pcb1, void **ppv2, DWORD *pcb2,
                              DWORD dwFlags) = 0;
  virtual LONG __stdcall Play(DWORD dwReserved1, DWORD dwPriority,
                              DWORD dwFlags) = 0;
  /*0x34*/ virtual LONG __stdcall SetCurrentPosition(DWORD dwNewPosition) = 0;
  /*0x38*/ virtual LONG __stdcall SetFormat(const void *pwfx) = 0;
  virtual LONG __stdcall SetVolume(LONG lVolume) = 0;
  virtual LONG __stdcall SetPan(LONG lPan) = 0;
  virtual LONG __stdcall SetFrequency(DWORD dwFrequency) = 0;
  /*0x48*/ virtual LONG __stdcall Stop() = 0;
  /*0x4C*/ virtual LONG __stdcall Unlock(void *pv1, DWORD cb1, void *pv2,
                                         DWORD cb2) = 0;
};

// The TU's bodies, in oracle VA order.

// 0x49D610 (69 B). Two parameters: the window, and in EDX the error record the
// body writes the HRESULT and the API's name into.
struct snd_err_t;   // TU-local to snd.cpp (hr @+0x00, pszApi @+0x08). Named here
                    // only so this declaration is spellable; nothing outside that
                    // TU needs the layout, so it does not get one.
BOOL SetCooperativeLevel(void *hWnd, snd_err_t *pErr);

// 0x49D660 (70 B). Nullary. Builds a WAVEFORMATEX on the stack and hands it to
// the primary buffer's SetFormat. The three `mov dword ptr` immediates
// (0x20001, 0x5622, 0x15888) are the packed format fields; see the .cpp.
void snd_init_format();

// 0x49D760 (90 B). Creates the primary buffer into gpPrimaryBuffer; ECX is the
// error record. Returns 1 on success.
BOOL snd_create_buffer(void *pOut);

// 0x49D7C0 (73 B). `ret 8h` -- two stack words on top of ECX and EDX. Walks
// RIFF chunks: ECX is the cursor, EDX points at the remaining byte count (it is
// both read and written -- `mov dword ptr [edi], ecx`), the first stack word is
// the FOURCC being searched for, the second an out-parameter for the chunk size.
void *find_chunk(void *pCursor, DWORD *pcbRemaining, DWORD dwFourCC,
                 DWORD *pcbChunk);

// 0x49D900 (72 B). Opens the WAV named by entry nIndex of the .rdata table at
// 0x51C270, forwarding nArg to open_wav_file; 0 when the slot is null.
intptr_t open_wav_file_idx(int nIndex, intptr_t nArg);

// 0x49DA70 (70 B). A sound's duration in milliseconds: its byte count times
// 1000 over its format's nAvgBytesPerSec, an unsigned divide.
int snd_calc_duration(void *pSound);

// 0x49DAC0 (56 B). Nullary.
void sfxdata_init();

// 0x49DB00 (42 B). Nullary.
void sfxdata_exit();

// 0x49DB40 (10 B). Nullary. `call stop_digital / jmp stop_music`.
void snd_stop();

// 0x49DFA0 (72 B). Seeds the options block's two volume slots from their
// defaults when they read -1, then tail-jumps snd_set_options with &gOptions.
void snd_init_volumes();

// 0x49DFF0 (5 B). A plain `jmp music_enabled`, so it returns that function's
// BYTE; a wider return would make the jmp a call plus a movzx.
BYTE snd_music_enabled();

// 0x49E000 (139 B). Four arguments, the fourth a WORD; what the first points at
// is not known.
BOOL sub_49E000(void *pArg0, int nArg1, int nArg2, WORD wArg3);

// 0x49E300 (75 B). Nullary. Opens the button-click WAVs through open_wav_file.
void snd_load_btn_snds();

// 0x49E460 (127 B). Nullary.
void snd_play_btn_click();

// 0x49E7C0 (65 B). ECX is a sound id. The return is an int although both
// callers discard it: as a void, MSVC merges the two epilogues 1.17 keeps apart.
intptr_t snd_play_snd(int nSoundId);

// 0x49EA10 (68 B). Nullary; both callers discard EAX.
void snd_update_streams();

// 0x49EA60 (122 B). Takes the address of the options block, as opt_dlgs.cpp
// passes it.
void snd_set_options(void *pOptions);

// 0x49EC00 (54 B). Nullary. Saves the music volume, zeroes it, and re-enters
// snd_set_options -- or restores it. The saved slot is a separate global.
void snd_toggle_music();

// 0x49EC40 (94 B). Nullary. snd_toggle_music's twin over the sfx volume, plus
// a stop_digital on the way down and the volume-table lookup on the way up.
void snd_toggle_sound();

// 0x49ECA0 (122 B). Nullary. Clears the streamed list, then walks the eight
// sound buffers asking each for its status.
void stop_digital();

// 0x49D810 (27 B). Nullary; option_dlg_cmd tests the result as a truth value.
BOOL is_sfx_enabled();

// The rest of this TU, in oracle VA order; sub_49E3F0 is in gamesnd.h. snd_init
// and snd_close keep the spellings init.cpp gave them, minus the __fastcall.

struct sound_t;          // TU-local to snd.cpp: the 16-byte per-sound cache row
                         // at sgehSnds (0x6A61D0). Named here only so the two
                         // declarations below are spellable.
struct waveformatex_t;   // TU-local to snd.cpp, 18 bytes and packed.

// 0x49D6B0 (163 B). ECX is the error record. Retries DSERR_ALLOCATED against
// the budget at 0x5093D4.
BOOL InDirectSoundCreate(snd_err_t *pErr);

// 0x49D830 (202 B). `ret 4h`. ECX the file image, EDX the remaining byte count
// (read and written), the stack word the WAVEFORMATEX to fill. Returns the data
// chunk body with *pcbSize narrowed to it.
void *read_header(const void *pFile, DWORD *pcbSize, waveformatex_t *pwfx);

// 0x49DD70 (181 B). ECX is a sound id. Both callers discard EAX and the oracle
// writes EAX on no path, so `void`.
void snd_free_snd(int nSoundId);

// 0x49DE30 (365 B). ECX is the window, EDX the error record. init.cpp:810
// passes a 12-byte array there, which is a second reading of sizeof(snd_err_t).
int snd_init(void *hWnd, void *pvOut);

// 0x49E090 (623 B). The sound-loading thread proc snd_init starts with
// _beginthreadex; its one stack word is never read.
ULONG __stdcall snd_loadPlayThread(void *pvArg);

// 0x49E350 (152 B). ECX is a sound id. Tail-jumps snd_purge, so `void`.
void sub_49E350(int nSoundId);

// 0x49E620 (281 B). `ret 18h` -- six stack words on top of ECX (the cache row)
// and EDX (the volume). The widths are contracts.py's: arg2 is read 2-byte and
// args 3, 4 and 5 1-byte.
BOOL sub_49E620(sound_t *pSound, LONG nVolume, int nPan, int nSoundId,
                WORD wUnitType, BYTE bFlags, BYTE bOwned, BYTE bPriority);

// 0x49EAE0 (268 B).
void snd_close();

// External callees no other header declares. play_music and stop_music are in
// gluRdy.h, set_digital_volume in dlg_dlg.h and psnprintf in victory.h.

// 0x46F110 (6 B), music.cpp. Returns a BYTE, as `mov al, byte ptr [0x67258C]`
// does; a BOOL would add a movzx.
BYTE music_enabled();

// 0x46F160 (11 B), music.cpp. Nullary; a tail jump into play_music.
void start_song();

// 0x46F170 (55 B), music.cpp. The volume is a signed hundredth-decibel
// attenuation: snd_set_options passes -10000.
void set_music_volume(LONG nVolume);

// 0x46F290 (44 B), TU music.cpp. Nullary; all three callers discard EAX.
void stop_song();

// 0x49D950 (281 B), this TU's: ECX the path open_wav_file_idx formats, EDX the
// value it forwards.
intptr_t open_wav_file(const char *pszPath, intptr_t nArg);

// 0x49DB60 (521 B), this TU's. Picks a free playback slot by a BYTE priority
// and returns a signed slot index, -1 on failure.
int snd_find_slot(BYTE bPriority);

// 0x49E810 (283 B), TU snd.cpp.
intptr_t play_digital(const char *pszPath);

// 0x49E930 (217 B), TU snd.cpp.
int snd_purge(int cbToFree);

// 0x49E4E0 (314 B), this TU's: ECX, EDX and two stack words. sub_49E740
// forwards its own ECX untouched.
int sub_49E4E0(int nSoundId, LONG nVolume, int nPan, void *pUnit);

// KERNEL32!SetEvent, __declspec(dllimport) for 1.17's indirect call.
extern "C" __declspec(dllimport) int __stdcall SetEvent(void *hEvent);

// The gamedata loaders sfxdata_init and sfxdata_exit reach, with the types the
// .cpp files that first declared them derived, minus the __fastcall.

// 0x4442B0 (233 B), gamedata.cpp. The void return is a placeholder: nothing
// tells a status code from void here.
void load_array_data(const char *pszFile, void *pArrayData);

// 0x444890 (156 B), gamedata.cpp, spelled as glues.cpp and gametype.cpp spell
// it. sfxdata_init passes it to str_load_table_ex as the per-entry loader.
void *load_game_data(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine);

// The Storm entry points this TU reaches, by ordinal, all __stdcall with C
// linkage, which is what storm_imports.def resolves.
// Hyperion: a file handle is a pointer, as Storm's own, `int` in 1.17; the
// x64 build would cut it to 32 bits.
extern "C" {
int  __stdcall SFileDdaBeginEx(void *hFile, DWORD dwFlags, DWORD dwMask,
                               DWORD dwOffset, LONG lVolume, LONG lPan,
                               int nReserved);                          // 255
BOOL __stdcall SFileDdaDestroy(void);                                   // 256
int  __stdcall SFileDdaEnd(void *hFile);                                // 257
BOOL __stdcall SFileDdaGetPos(void *hFile, DWORD *pdwPos, DWORD *pdwLen); // 258
// Hyperion: BOOL, as Storm returns and port_storm*.cpp define them; a WebAssembly
// link holds every call to its definition's signature
BOOL __stdcall SFileDdaInitialize(IDirectSound *pDirectSound);          // 260
BOOL __stdcall SVidDestroy(void);                                       // 451
BOOL __stdcall SVidInitialize(IDirectSound *pDirectSound);              // 453
}
