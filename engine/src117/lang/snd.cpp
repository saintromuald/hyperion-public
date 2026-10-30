// snd.cpp -- the DirectSound layer (oracle 0x49D520-0x49ED60): device and
// buffer setup, WAV parsing, the volume and options block, playback and the
// streamed-music toggles. Bodies are in oracle VA order.
//
// The options block is reached as &gOptions, as 1.17's `mov ecx, 6D3130h`
// passes it. The COM interfaces are snd.h's abstract classes, whose virtual
// calls are 1.17's. STREAMED's hFile was spelled as snd.h's SFileDdaGetPos
// spelled its handle:
//     Hyperion: both are `void *` now, as Storm's handles are pointers
//    .

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>                // memset, inlined by /Oi at these sizes
#include "../h/snd.h"
#include "../h/dlg_dlg.h"          // set_digital_volume
#include "../h/gluRdy.h"           // play_music / stop_music / snd_play_music
#include "../h/gamesnd.h"          // snd_get_duration / sub_49E780
#include "../h/glues.h"            // sub_49E740 and mem_free
#include "../h/snd_streamed.h"    // STREAMED, its list specialization
#include "../h/rtti.h"            // type_info::raw_name, ours, not <typeinfo>
#include "../h/CSprite.h"          // gamedata_free_data
#include "../h/CUnit.h"            // wType, for UNIT_TYPE
#include "../gen/globals117.h"
#include "../h/gamedata.h"  // Hyperion: array_data_t
#include "../port/front/port_front.h"  // Hyperion: PortFrontLaunchPending
#include "../port/data/port_sound.h"  // Hyperion: PortDirectSoundCreateAny, PortSoundLoad
#include "../h/typelimits.h"  // Hyperion: SoundTypeValid, a mod's sounds

// The streamed-music list node and its TSList specialization are in
// src117/h/snd_streamed.h.

// The options record, gOptions: snd_set_options takes its base in ECX. Only the
// two volumes this TU touches are named; the size is that of option_dlg_alloc's
// whole-object copy.
struct options_t {
  /*0x00*/ BYTE _unmapped_00[0x0C];
  /*0x0C*/ int nMusicVolume;
  /*0x10*/ int nSfxVolume;
  /*0x14*/ BYTE _unmapped_14[0x10];
};
static_assert(sizeof(options_t) == 0x24,
              "option_dlg_alloc copies the whole object as 16+16+4");
static_assert(offsetof(options_t, nMusicVolume) == 0x0C, "mov ecx,[esi+0Ch]");
static_assert(offsetof(options_t, nSfxVolume) == 0x10,
              "imul edx,[esi+10h],63h");

// The error record both device-setup bodies write: the HRESULT at +0x00 and, on
// failure, the API's name at +0x08. Its end is not known; 0x0C is the smallest
// size the two members admit.
struct snd_err_t {
  /*0x00*/ LONG hr;              // mov dword ptr [edi], eax / [esi], eax
  /*0x04*/ DWORD dwCode;         // InDirectSoundCreate writes 87h here and
                                 // snd_init 8Ah / 85h; nothing in this TU
                                 // reads it back
  /*0x08*/ const char *pszApi;   // mov dword ptr [edi+8h], 4EFDE0h
};
static_assert(offsetof(snd_err_t, hr) == 0x00, "the HRESULT store is [rec+0]");
static_assert(offsetof(snd_err_t, pszApi) == 0x08,
              "mov dword ptr [edi+8h], 4EFDE0h = \"SetCooperativeLevel\"");
OC_X86_LAYOUT(sizeof(snd_err_t) == 0x0C, "UNDERIVED -- smallest size the two "
                                         "witnessed members admit");

// WAVEFORMATEX, packed to its 18 bytes: snd_calc_duration passes 0x12 as
// GetFormat's buffer size, and snd_init_format's zero-init is a 16-byte copy
// with the last WORD folded into the store of wBitsPerSample.
#pragma pack(push, 1)
struct waveformatex_t {
  /*0x00*/ WORD wFormatTag;        // 0x20001 low half  = 1 (PCM)
  /*0x02*/ WORD nChannels;         // 0x20001 high half = 2
  /*0x04*/ DWORD nSamplesPerSec;   // 0x5622  = 22050
  /*0x08*/ DWORD nAvgBytesPerSec;  // 0x15888 = 88200, and the divisor in
                                   // snd_calc_duration (`mov ecx,[ebp-10h]`
                                   // = wfx + 8)
  /*0x0C*/ WORD nBlockAlign;       // mov word ptr [ebp-8h], ax  with eax = 4
  /*0x0E*/ WORD wBitsPerSample;    // low half of `mov dword [ebp-6h], 10h`
  /*0x10*/ WORD cbSize;            // high half of the same store = 0
};
#pragma pack(pop)
static_assert(offsetof(waveformatex_t, nSamplesPerSec) == 0x04,
              "mov [ebp-10h]");
static_assert(offsetof(waveformatex_t, nAvgBytesPerSec) == 0x08,
              "mov ecx, dword ptr [ebp-10h] off a base of ebp-18h");
static_assert(offsetof(waveformatex_t, nBlockAlign) == 0x0C,
              "mov [ebp-8h], ax");
static_assert(offsetof(waveformatex_t, wBitsPerSample) == 0x0E,
              "low half of mov dword ptr [ebp-6h], 10h");
static_assert(offsetof(waveformatex_t, cbSize) == 0x10,
              "high half of the same");
static_assert(sizeof(waveformatex_t) == 0x12,
              "push 12h -- GetFormat's cbSize");

// DSBUFFERDESC, 0x14 bytes, the size it states in its own dwSize.
struct dsbufferdesc_t {
  /*0x00*/ DWORD dwSize;         // mov dword ptr [ebp-14h], 14h
  /*0x04*/ DWORD dwFlags;        // mov dword ptr [ebp-10h], 1h
  /*0x08*/ DWORD dwBufferBytes;  // zeroed by the movdqu
  /*0x0C*/ DWORD dwReserved;     // zeroed by the movdqu
  /*0x10*/ const waveformatex_t *lpwfxFormat;  // mov dword ptr [ebp-4h], 0
};
static_assert(offsetof(dsbufferdesc_t, dwFlags) == 0x04, "mov [ebp-10h], 1");
static_assert(offsetof(dsbufferdesc_t, lpwfxFormat) == 0x10,
              "mov dword ptr [ebp-4h], 0 is the last dword of the object");
OC_X86_LAYOUT(sizeof(dsbufferdesc_t) == 0x14, "sub esp,14h AND the dwSize it "
                                              "writes into itself");

// The 16-byte sound record, one per sound id in sgehSnds: the byte count, the
// last play time, the duration and the buffer.
struct sound_t {
  /*0x00*/ DWORD cbData;         // cmp dword ptr [esi], 0 / imul eax,[esi],3E8h
  /*0x04*/ int nLastPlayed;      // sub_49E620: mov dword ptr [ebx+4h], edi with
                                 // edi == -GetTickCount(), and the throttle
                                 // reads it back as `add esi, edi / cmp esi,50h`
  /*0x08*/ int nDuration;        // sub_49E350: mov dword ptr [esi+8h], eax off
                                 // snd_calc_duration
  /*0x0C*/ IDirectSoundBuffer *pBuffer;  // mov ecx, dword ptr [esi+0Ch]
};
static_assert(offsetof(sound_t, nLastPlayed) == 0x04, "mov [ebx+4h], edi");
static_assert(offsetof(sound_t, nDuration) == 0x08, "mov [esi+8h], eax");
OC_X86_LAYOUT(offsetof(sound_t, pBuffer) == 0x0C, "mov ecx,[esi+0Ch] then "
                                                  "mov eax,[ecx] as a vtable");
OC_X86_LAYOUT(sizeof(sound_t) == 0x10, "sub_49E350 and snd_free_snd index the "
                                       "array with shl <idx>,4h");

// The eight playing-sound slots at 0x6AA990, 0x18 bytes apart, as snd_find_slot
// and stop_digital walk them. bOwned selects Release over SetCurrentPosition in
// stop_digital; +0x09 to +0x0B are never touched.
struct snd_slot_t {
  /*0x00*/ IDirectSoundBuffer *pBuffer;
  /*0x04*/ WORD wUnk04;
  /*0x06*/ BYTE bFlags;
  /*0x07*/ BYTE bPriority;
  /*0x08*/ BYTE bOwned;
  /*0x09*/ BYTE _pad09[3];
  /*0x0C*/ int nSoundId;
  /*0x10*/ int nUnk10;
  /*0x14*/ int nUnk14;
};
static_assert(offsetof(snd_slot_t, pBuffer) == 0x00, "buffer at +0");
OC_X86_LAYOUT(offsetof(snd_slot_t, wUnk04) == 0x04, "WORD at +4");
OC_X86_LAYOUT(offsetof(snd_slot_t, bFlags) == 0x06, "flags at +6");
OC_X86_LAYOUT(offsetof(snd_slot_t, bPriority) == 0x07, "priority at +7");
OC_X86_LAYOUT(offsetof(snd_slot_t, bOwned) == 0x08, "cmp byte ptr [esi+8h], 0h");
OC_X86_LAYOUT(offsetof(snd_slot_t, nSoundId) == 0x0C, "sound id at +0xC");
OC_X86_LAYOUT(offsetof(snd_slot_t, nUnk10) == 0x10, "dword at +0x10");
OC_X86_LAYOUT(offsetof(snd_slot_t, nUnk14) == 0x14, "dword at +0x14");
OC_X86_LAYOUT(sizeof(snd_slot_t) == 0x18, "stride 0x18 -- snd_find_slot's walk");

// The chunk header find_chunk walks in a WAV file: the four-character code and
// the size. It returns the chunk's body.
struct riff_chunk_t {
  /*0x00*/ DWORD dwFourCC;
  /*0x04*/ DWORD cbSize;
};
static_assert(offsetof(riff_chunk_t, dwFourCC) == 0x00, "fourcc at +0");
static_assert(offsetof(riff_chunk_t, cbSize) == 0x04, "size at +4");
static_assert(sizeof(riff_chunk_t) == 8, "an 8-byte chunk header");

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// gpDirectSound @0x6AA954, the IDirectSound device; is_sfx_enabled and
// snd_set_options also test it as "sound is up".
#ifdef OC_DLL_MODE
#define gpDirectSound (*reinterpret_cast<IDirectSound **>(OC_VA(0x6AA954u)))
#else
extern IDirectSound *gpDirectSound;
#endif

// gpPrimaryBuffer @0x6AA958, the primary buffer snd_create_buffer creates.
#ifdef OC_DLL_MODE
#define gpPrimaryBuffer (*reinterpret_cast<IDirectSoundBuffer **>(OC_VA(0x6AA958u)))
#else
extern IDirectSoundBuffer *gpPrimaryBuffer;
#endif

// gnSfxVolume @0x6D3140 and gnMusicVolume @0x6D313C, gOptions + 0x10 and
// + 0x0C, spelled as separate globals since the registry types gOptions as a
// single DWORD.
#ifdef OC_DLL_MODE
#define gnSfxVolume (*reinterpret_cast<int *>(OC_VA(0x6D3140u)))
#else
extern int gnSfxVolume;
#endif

#ifdef OC_DLL_MODE
#define gnMusicVolume (*reinterpret_cast<int *>(OC_VA(0x6D313Cu)))
#else
extern int gnMusicVolume;
#endif

// The two saved volumes, where snd_toggle_sound and snd_toggle_music park the
// live value while muted; snd_init_volumes' defaults, 50 and 25, pair each with
// its live slot.
#ifdef OC_DLL_MODE
#define gnSavedSfxVolume (*reinterpret_cast<int *>(OC_VA(0x6AAA50u)))
#else
extern int gnSavedSfxVolume;
#endif

#ifdef OC_DLL_MODE
#define gnSavedMusicVolume (*reinterpret_cast<int *>(OC_VA(0x6AA98Cu)))
#else
extern int gnSavedMusicVolume;
#endif

// sgSndVolTbl_g @0x4EFC00 .rdata: 100 signed attenuations, -4000 rising to -20.
// The music half of snd_set_options indexes it by v - 1. The index sub_49E780
// forms can reach 100, one past the end, as in 1.17.
#ifdef OC_DLL_MODE
#define sgSndVolTbl_g (reinterpret_cast<const LONG *>(OC_VA(0x4EFC00u)))
#else
extern const LONG sgSndVolTbl_g[100];
#endif

// sgdwSndVolDiff_g @0x6AA96C, the per-device bias subtracted from a table entry
// before either volume call.
#ifdef OC_DLL_MODE
#define sgdwSndVolDiff_g (*reinterpret_cast<LONG *>(OC_VA(0x6AA96Cu)))
#else
extern LONG sgdwSndVolDiff_g;
#endif

// sbPlayingMusic @0x6AAA5C, a BYTE only snd_set_options reads and writes, to
// gate start_song and stop_song. music.cpp's gbMusicOn is a different byte.
#ifdef OC_DLL_MODE
#define sbPlayingMusic (*reinterpret_cast<BYTE *>(OC_VA(0x6AAA5Cu)))
#else
extern BYTE sbPlayingMusic;
#endif

// sgpSfxTbl_g @0x6AA988, the loaded sfxdata.tbl block sfxdata_exit frees.
#ifdef OC_DLL_MODE
#define sgpSfxTbl_g (*reinterpret_cast<void **>(OC_VA(0x6AA988u)))
#else
extern void *sgpSfxTbl_g;
#endif

// gSFXDATAArrayDataTbl_g @0x5013C0, the gamedata descriptor for
// arr\sfxdata.dat; only its address is taken, so the extent is a guess.
#ifdef OC_DLL_MODE
#define gSFXDATAArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x5013C0u)))
#else
extern array_data_t gSFXDATAArrayDataTbl_g[6] OC_UNALIGNED;
#endif

// sfx_fileStr @0x51C270, the 1144 file-name pointers str_load_table_ex fills
// from the loaded .tbl block.
#ifdef OC_DLL_MODE
#define sfx_fileStr (reinterpret_cast<char **>(OC_VA(0x51C270u)))
#else
extern char *sfx_fileStr[1144];
#endif

// gbSoundPriority @0x51D450, a BYTE per sound id.
#ifdef OC_DLL_MODE
#define gbSoundPriority (reinterpret_cast<BYTE *>(OC_VA(0x51D450u)))
#else
extern BYTE gbSoundPriority[];
#endif

// sgbSndPlayFlags_g @0x51D8C8, a name of our own: a BYTE of flags per sound id,
// parallel to gbSoundPriority, which snd_purge tests and clears.
#ifdef OC_DLL_MODE
#define sgbSndPlayFlags_g (reinterpret_cast<BYTE *>(OC_VA(0x51D8C8u)))
#else
extern BYTE sgbSndPlayFlags_g[];
#endif

// sgpBtnClickSnd_g @0x6A61CC, a name of our own: the button-click sound's
// buffer.
#ifdef OC_DLL_MODE
#define sgpBtnClickSnd_g (*reinterpret_cast<IDirectSoundBuffer **>(OC_VA(0x6A61CCu)))
#else
extern IDirectSoundBuffer *sgpBtnClickSnd_g;
#endif

// sghSndEvent_g @0x6AA960, a name of our own: an event handle, probably the one
// the sound thread waits on, though only the SetEvent call is seen.
#ifdef OC_DLL_MODE
#define sghSndEvent_g (*reinterpret_cast<void **>(OC_VA(0x6AA960u)))
#else
extern void *sghSndEvent_g;
#endif

// dword_6AA95C @0x6AA95C, signed, compared with soundMemoryCache.
#ifdef OC_DLL_MODE
#define dword_6AA95C (*reinterpret_cast<int *>(OC_VA(0x6AA95Cu)))
#else
extern int dword_6AA95C;
#endif

// soundMemoryCache @0x5093C4, the signed budget dword_6AA95C is measured
// against.
#ifdef OC_DLL_MODE
#define soundMemoryCache (*reinterpret_cast<int *>(OC_VA(0x5093C4u)))
#else
extern int soundMemoryCache;
#endif

// sgSoundsPlaying @0x6AA990; see snd_slot_t above.
#ifdef OC_DLL_MODE
#define sgSoundsPlaying (*reinterpret_cast<snd_slot_t (*)[8]>(OC_VA(0x6AA990u)))
#else
extern snd_slot_t sgSoundsPlaying[8] OC_UNALIGNED;
#endif

// sgStreamedList @0x5093C8, the streamed-music list head, with the 12-byte
// TSList layout snd_end_streamed and snd_update_streams reach.
#ifdef OC_DLL_MODE
#define sgStreamedList (*reinterpret_cast<STREAMEDLIST *>(OC_VA(0x5093C8u)))
#else
extern STREAMEDLIST sgStreamedList;
#endif

// sgehSnds @0x6A61D0, the per-sound cache, 1144 16-byte rows; snd_get_duration
// reads a row's nDuration. This TU spelled that sgSndCache_g @0x6A61D8 until
// sound_t's layout changed at 64 bits.
#ifdef OC_DLL_MODE
  #ifndef sgehSnds
  #define sgehSnds (reinterpret_cast<sound_t *>(OC_VA(0x6A61D0u)))
  #endif
#else
extern sound_t sgehSnds[1144];
#endif

// ghDSound @0x6AA950, the dsound.dll module handle.
#ifdef OC_DLL_MODE
  #ifndef ghDSound
  #define ghDSound (*reinterpret_cast<void **>(OC_VA(0x6AA950u)))
  #endif
#else
extern void *ghDSound;
#endif

// sgnDSoundRetries_g @0x5093D4, the DSERR_ALLOCATED retry budget, 3 in 1.17's
// image, signed.
#ifdef OC_DLL_MODE
  #ifndef sgnDSoundRetries_g
  #define sgnDSoundRetries_g (*reinterpret_cast<int *>(OC_VA(0x5093D4u)))
  #endif
#else
extern int sgnDSoundRetries_g;
#endif

// KERNEL32 imports, __declspec(dllimport) for 1.17's indirect calls. SetEvent
// is in snd.h.
extern "C" __declspec(dllimport) void *__stdcall GetProcAddress(void *hModule,
                                                                const char *pszName);  // 0x4EB150
extern "C" __declspec(dllimport) DWORD __stdcall GetLastError(void);             // 0x4EB17C
extern "C" __declspec(dllimport) void  __stdcall Sleep(DWORD dwMilliseconds);    // 0x4EB2F8
extern "C" __declspec(dllimport) DWORD __stdcall GetTickCount(void);             // 0x4EB2FC

// sgpSndPreload_g @0x6AAA60, the 0-terminated WORD list of sound ids
// sub_49E3F0 walks.
#ifdef OC_DLL_MODE
  #ifndef sgpSndPreload_g
  #define sgpSndPreload_g (*reinterpret_cast<const WORD **>(OC_VA(0x6AAA60u)))
  #endif
#else
extern const WORD *sgpSndPreload_g;
#endif

// 0x6A61C8 gbSndInited, 0x6A61C9 sgbDdaInitialized_g and 0x6A61CA
// sgbSndVidInitialized_g, three BYTE flags.
#ifdef OC_DLL_MODE
  #ifndef gbSndInited
  #define gbSndInited (*reinterpret_cast<BYTE *>(OC_VA(0x6A61C8u)))
  #endif
#else
extern BYTE gbSndInited;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbDdaInitialized_g
  #define sgbDdaInitialized_g (*reinterpret_cast<BYTE *>(OC_VA(0x6A61C9u)))
  #endif
#else
extern BYTE sgbDdaInitialized_g;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbSndVidInitialized_g
  #define sgbSndVidInitialized_g (*reinterpret_cast<BYTE *>(OC_VA(0x6A61CAu)))
  #endif
#else
extern BYTE sgbSndVidInitialized_g;
#endif

// sghSndThread_g @0x6AA964, the sound thread's handle.
#ifdef OC_DLL_MODE
  #ifndef sghSndThread_g
  #define sghSndThread_g (*reinterpret_cast<void **>(OC_VA(0x6AA964u)))
  #endif
#else
extern void *sghSndThread_g;
#endif

// sgbDS @0x6AA968, written as a DWORD and never read in this TU.
#ifdef OC_DLL_MODE
  #ifndef sgbDS
  #define sgbDS (*reinterpret_cast<int *>(OC_VA(0x6AA968u)))
  #endif
#else
extern int sgbDS;
#endif

// sgdwLoadSndThreadId_g @0x6AAA54, the sound thread's id, which snd_close
// compares with its own.
#ifdef OC_DLL_MODE
  #ifndef sgdwLoadSndThreadId_g
  #define sgdwLoadSndThreadId_g (*reinterpret_cast<unsigned *>(OC_VA(0x6AAA54u)))
  #endif
#else
extern unsigned sgdwLoadSndThreadId_g;
#endif

extern "C" __declspec(dllimport) void *__stdcall LoadLibraryA(const char *pszFile);   // 0x4EB0F8
extern "C" __declspec(dllimport) DWORD __stdcall GetCurrentThreadId(void);            // 0x4EB0F4
extern "C" __declspec(dllimport) void *__stdcall CreateEventA(void *pAttrs,
                                                              BOOL bManual,
                                                              BOOL bInitial,
                                                              const char *pszName);   // 0x4EB204
extern "C" __declspec(dllimport) DWORD __stdcall WaitForSingleObject(void *hObject,
                                                                     DWORD dwMillis); // 0x4EB208
extern "C" __declspec(dllimport) BOOL  __stdcall CloseHandle(void *hObject);          // 0x4EB228

// SRegLoadValue (Storm ordinal 423), declared here as well as in spd_dlg.h,
// which this TU does not include.
extern "C" {
BOOL __stdcall SRegLoadValue(const char *pszKey, const char *pszValue,
                             DWORD dwFlags, DWORD *pdwValue);
}

// _beginthreadex, the CRT's own (0x4D360F), __cdecl.
extern "C" ULONG_PTR __cdecl _beginthreadex(void *pSecurity, unsigned nStack,
                                                ULONG(__stdcall *pfnStart)(void *),
                                                void *pvArg, unsigned nFlags,
                                                unsigned *pnThreadId);

// sgSndCrit_g @0x6AA970, the critical section snd_purge brackets its sweep
// with.
// Hyperion: spelled as a CRITICAL_SECTION's fields, which this TU cannot
// take from <windows.h> (its own import declarations), so that it is 0x28 bytes
// with its pointers at 64 bits and has storage of that size
//; 0x18 bytes on x86 as before.
struct snd_crit_t {
  void *pDebugInfo;
  LONG nLockCount;
  LONG nRecursionCount;
  void *hOwningThread;
  void *hLockSemaphore;
  uintptr_t nSpinCount;
};
static_assert(sizeof(snd_crit_t) == (sizeof(void *) == 8 ? 0x28 : 0x18), "snd_crit_t is CRITICAL_SECTION");
#ifdef OC_DLL_MODE
  #ifndef sgSndCrit_g
  #define sgSndCrit_g (*reinterpret_cast<snd_crit_t *>(OC_VA(0x6AA970u)))
  #endif
#else
extern snd_crit_t sgSndCrit_g;
#endif

// sgnSndCursor_g @0x6AAA58, the round-robin sweep position, wrapped at 0x477 (in Hyperion at
// the last sound loaded, a mod's among them).
#ifdef OC_DLL_MODE
  #ifndef sgnSndCursor_g
  #define sgnSndCursor_g (*reinterpret_cast<int *>(OC_VA(0x6AAA58u)))
  #endif
#else
extern int sgnSndCursor_g;
#endif

extern "C" __declspec(dllimport) void __stdcall EnterCriticalSection(void *pcs); // 0x4EB238
extern "C" __declspec(dllimport) void __stdcall LeaveCriticalSection(void *pcs); // 0x4EB234

// 1.17's RTTI type descriptor for `struct STREAMED` at 0x5109E4, whose raw name
// SMemFree takes as the owner tag; typeid would give a descriptor of our own.
#ifdef OC_DLL_MODE
  #ifndef gTypeInfo_STREAMED
  #define gTypeInfo_STREAMED (*reinterpret_cast<type_info *>(OC_VA(0x5109E4u)))
  #endif
#else
extern type_info gTypeInfo_STREAMED;
#endif

// Storm ordinal 401, spelled as src117/h/Storm.h spells it; that header is not
// included here because it undefines CRT macros this TU does not want.
extern "C" void *__stdcall SMemAlloc(DWORD size, const char *ownerTag,
                                     int ownerLine, DWORD flags);   // 0x4D001C
extern "C" int __stdcall SMemFree(const void *ptr, const char *ownerTag,
                                  int ownerLine, DWORD flags);      // 403

// DirectSoundCreate, reached only through GetProcAddress, __stdcall.
typedef LONG(__stdcall *PFNDIRECTSOUNDCREATE)(void *pGuid, IDirectSound **ppDS,
                                              void *pUnkOuter);

// OpenFile, as gamedata.h declares it.
BOOL __fastcall OpenFile(intptr_t nArg0, intptr_t nArg1, int nArg2);

// MemFileLoad, as gamedata.h declares it; that header would bring in types this
// TU does not use.
void *__fastcall MemFileLoad(const char *pszFile, DWORD_PTR dwArg2, DWORD *pdwSize,
                             int nFlags, int nScope, const char *pszOwnerFile,
                             int nOwnerLine);

// CUnit +0x64 is the unit type. The owner arrives as gamesnd.h's void *, so the
// field is reached by its offset; the load is `movzx eax, word ptr [r+64h]`.
#define UNIT_TYPE(p)   (*reinterpret_cast<const WORD *>(reinterpret_cast<const BYTE *>(p) + offsetof(CUnit, wType)))

// psnprintf, as victory.h declares it.
int __cdecl psnprintf(char *pszBuf, size_t cchBuf, const char *pszFormat, ...);

// The bodies, in oracle VA order.

// Unlink one track from whatever list holds it. Inlined into Clear and Remove.
static void UnlinkStreamed(STREAMED *node)
{
  TSLink<STREAMED> *pl = &node->link;
  STREAMED *pprev = pl->prev;
  if (pprev != 0) {
    STREAMED *pnext = pl->next;
    TSLink<STREAMED> *pnl;
    if ((intptr_t)pnext <= 0) {
      pnl = reinterpret_cast<TSLink<STREAMED> *>(~(intptr_t)pnext);
    } else {
      intptr_t nOff = 0;
      if ((intptr_t)pnext > 0) {
        nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
      }
      pnl = reinterpret_cast<TSLink<STREAMED> *>((intptr_t)pnext + nOff);
    }
    pnl->prev = pprev;
    pl->prev->link.next = pl->next;
    pl->prev = 0;
    pl->next = 0;
  }
}

// "Stop and free every streamed track."
void STREAMEDLIST::Clear(void)
{
  for (;;) {
    STREAMED *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    SFileDdaEnd(p->hFile);
    CloseFile((intptr_t)p->hFile);
    p->hFile = 0;
    UnlinkStreamed(p);
    SMemFree(p, gTypeInfo_STREAMED.raw_name(), -2, 0);
  }
}

// "Stop and free one track."
STREAMED *STREAMEDLIST::Remove(STREAMED *pNode)
{
  TSLink<STREAMED> *pl = pNode ? &pNode->link : &link;
  STREAMED *pRet = pl->next;
  if ((intptr_t)pRet <= 0)
    pRet = 0;
  SFileDdaEnd(pNode->hFile);
  CloseFile((intptr_t)pNode->hFile);
  pNode->hFile = 0;
  UnlinkStreamed(pNode);
  SMemFree(pNode, gTypeInfo_STREAMED.raw_name(), -2, 0);
  return pRet;
}

// "Ask DirectSound for the cooperative level."
// Tries level 2, then level 1; the API's name is stored only when both fail.
BOOL SetCooperativeLevel(void *hWnd, snd_err_t *pErr)
{
  pErr->hr = gpDirectSound->SetCooperativeLevel(hWnd, 2);
  if (pErr->hr != 0) {
    pErr->hr = gpDirectSound->SetCooperativeLevel(hWnd, 1);
    if (pErr->hr != 0) {
      pErr->pszApi = "SetCooperativeLevel";
      return 0;
    }
  }
  return 1;
}

// "Set the primary buffer's format."
// The three `mov dword ptr` immediates are MSVC coalescing adjacent field
// writes, not three DWORD fields.
void snd_init_format()
{
  waveformatex_t wfx;

  memset(&wfx, 0, sizeof(wfx));
  wfx.wFormatTag = 1;
  wfx.nChannels = 2;
  wfx.nSamplesPerSec = 22050;
  wfx.nAvgBytesPerSec = 88200;
  wfx.nBlockAlign = 4;
  wfx.wBitsPerSample = 16;
  gpPrimaryBuffer->SetFormat(&wfx);
}

// "Create the DirectSound device."
// ECX is the error record. DSERR_ALLOCATED (0x8878000A) is retried against a
// global budget.
BOOL InDirectSoundCreate(snd_err_t *pErr)
{
  // Hyperion: port_dsound.cpp's DirectSoundCreate, on SDL audio
  PFNDIRECTSOUNDCREATE pfnCreate = reinterpret_cast<PFNDIRECTSOUNDCREATE>(&PortDirectSoundCreateAny);

  if (pfnCreate == 0) {
    pErr->hr = GetLastError();
    pErr->pszApi = "GetProcAddress";
    return 0;
  }
  pErr->hr = pfnCreate(0, &gpDirectSound, 0);
  while (pErr->hr == (LONG)0x8878000A && sgnDSoundRetries_g > 0) {
    Sleep(1000);
    --sgnDSoundRetries_g;
    pErr->hr = pfnCreate(0, &gpDirectSound, 0);
  }
  if (pErr->hr != 0) {
    pErr->pszApi = "DirectSoundCreate";
    if (pErr->hr == (LONG)0x8878000A)
      pErr->dwCode = 0x87;
    return 0;
  }
  return 1;
}

// "Create the primary sound buffer."
// dwFlags is DSBCAPS_PRIMARYBUFFER, and the buffer lands in gpPrimaryBuffer.
// Returns 1 on success.
BOOL snd_create_buffer(void *pOut)
{
  snd_err_t *pErr = (snd_err_t *)pOut;
  dsbufferdesc_t desc;

  memset(&desc, 0, sizeof(desc));
  desc.dwSize = sizeof(desc);
  desc.dwFlags = 1;
  pErr->hr = gpDirectSound->CreateSoundBuffer(&desc, (void **)&gpPrimaryBuffer,
                                                0);
  if (pErr->hr != 0) {
    pErr->pszApi = "CreateSoundBuffer";
    return 0;
  }
  return 1;
}

// "Find a RIFF chunk by FOURCC."
// The mutated void * parameter is returned, not a BYTE * local, which keeps EAX
// free as in 1.17, and *pcbRemaining is updated in memory each time.
void *find_chunk(void *pCursor, DWORD *pcbRemaining, DWORD dwFourCC,
                 DWORD *pcbChunk)
{
  while (*pcbRemaining >= 8) {
    riff_chunk_t *pHdr = (riff_chunk_t *)pCursor;
    DWORD cbChunk;

    *pcbRemaining -= 8;
    pCursor = (BYTE *)pCursor + 8;
    cbChunk = pHdr->cbSize;
    if (pHdr->dwFourCC == dwFourCC) {
      *pcbChunk = cbChunk;
      return pCursor;
    }
    if (*pcbRemaining < cbChunk)
      break;
    *pcbRemaining -= cbChunk;
    pCursor = (BYTE *)pCursor + cbChunk;
  }
  return 0;
}

// "Is the sound effects channel usable?"
// The zero exit comes last, as in 1.17: an early `return 0` lets MSVC shrink
// both compares against a zero register.
BOOL is_sfx_enabled()
{
  if (gpDirectSound != NULL && gnSfxVolume != 0)
    return 1;
  return 0;
}

// "Parse a RIFF/WAVE header."
//
// ECX is the loaded file image, EDX the remaining byte count (read AND
// written), and the one stack word the WAVEFORMATEX to fill. Returns the data
// chunk's body, with *pcbSize narrowed to that chunk's length.
void *read_header(const void *pFile, DWORD *pcbSize, waveformatex_t *pwfx)
{
  DWORD cbChunk = 0;

  if (*pcbSize < 0x0C)
    return 0;
  *pcbSize -= 0x0C;
  if (*(const DWORD *)pFile != 0x46464952)                      // "RIFF"
    return 0;
  if (*(const DWORD *)((const BYTE *)pFile + 8) != 0x45564157)  // "WAVE"
    return 0;

  BYTE *pChunk = (BYTE *)find_chunk((BYTE *)pFile + 0x0C, pcbSize, 0x20746D66,
                                    &cbChunk);
  if (pChunk == 0)
    return 0;
  if (cbChunk < 0x10)
    return 0;
  if (*pcbSize < cbChunk)
    return 0;
  // The cursor is advanced BEFORE the copy, which is what kills cbChunk early
  // and puts it in EAX; reading pChunk after the copy costs three bytes.
  const waveformatex_t *pFmt = (const waveformatex_t *)pChunk;
  pChunk += cbChunk;
  *pcbSize -= cbChunk;

  pwfx->wFormatTag = pFmt->wFormatTag;
  pwfx->nChannels = pFmt->nChannels;
  pwfx->nSamplesPerSec = pFmt->nSamplesPerSec;
  pwfx->nAvgBytesPerSec = pFmt->nAvgBytesPerSec;
  pwfx->nBlockAlign = pFmt->nBlockAlign;
  pwfx->wBitsPerSample = pFmt->wBitsPerSample;
  pwfx->cbSize = 0;

  void *pData = find_chunk(pChunk, pcbSize, 0x61746164, &cbChunk);
  if (pData == 0)
    return 0;
  if (*pcbSize < cbChunk)
    return 0;
  *pcbSize = cbChunk;
  return pData;
}

// "Open the WAV for a sound-table index."
// The null test comes before the buffer is used. The format string is 1.17's
// `sound\%s`, its backslash doubled in the source.
intptr_t open_wav_file_idx(int nIndex, intptr_t nArg)
{
  char szPath[0x104];
  char *pszName = sfx_fileStr[nIndex];

  if (pszName == 0)
    return 0;
  psnprintf(szPath, 0x104, "sound\\%s", pszName);
  return open_wav_file(szPath, nArg);
}

// "Load a WAV into a DirectSound buffer."
// EDX is the cache row the data size is reported into, or null.
intptr_t open_wav_file(const char *pszPath, intptr_t nArg)
{
  sound_t *pSound = (sound_t *)nArg;
  IDirectSoundBuffer *pBuffer = 0;
  DWORD cbData;   // deliberately not initialised: MemFileLoad fills it, and a
                  // `= 0` here is seven bytes the oracle does not carry
  void *pFile;

  // Hyperion: Settings' Unit voices, Remastered's recording when it has one, and a mod's
  // sounds in the formats they come in, as a WAV the buffers play (port_dsound.cpp).
  pFile = PortSoundLoad(pszPath, &cbData);
  if (pFile == 0)
    return (intptr_t)pBuffer;
  if (cbData != 0) {
    waveformatex_t wfx;
    BYTE *pData = (BYTE *)read_header(pFile, &cbData, &wfx);

    if (pData != 0 && cbData != 0) {
      dsbufferdesc_t desc;

      memset(&desc, 0, sizeof(desc));
      desc.dwSize = sizeof(desc);
      desc.dwFlags = 0xC2;
      desc.dwBufferBytes = cbData;
      desc.lpwfxFormat = &wfx;
      if (gpDirectSound->CreateSoundBuffer(&desc, (void **)&pBuffer, 0) != 0) {
        pBuffer = 0;
      } else {
        void *pvLock;
        DWORD cbLock;

        if (pBuffer->Lock(0, cbData, &pvLock, &cbLock, 0, 0, 0) != 0) {
          pBuffer->Release();
          pBuffer = 0;
        } else {
          memcpy(pvLock, pData, cbLock);
          pBuffer->Unlock(pvLock, cbLock, 0, 0);
          if (pSound != 0)
            pSound->cbData = cbData;
        }
      }
    }
  }
  mem_free(pFile, "snd.cpp", 0x139);
  return (intptr_t)pBuffer;
}

// "How many milliseconds is this sound?"
// cbData is read from the record twice, as 1.17 reads it, not held in a local.
int snd_calc_duration(void *pSound)
{
  sound_t *pSnd = (sound_t *)pSound;
  IDirectSoundBuffer *pBuf = pSnd->pBuffer;
  waveformatex_t wfx;
  DWORD cbWritten;

  if (pBuf != 0 && pSnd->cbData != 0 &&
      pBuf->GetFormat(&wfx, sizeof(wfx), &cbWritten) >= 0 &&
      wfx.nAvgBytesPerSec != 0)
    return pSnd->cbData * 1000 / wfx.nAvgBytesPerSec;
  return 0;
}

// "Load the sfxdata tables."
// str_load_table_ex takes six arguments, load_game_data among them as the
// per-entry loader.
void sfxdata_init()
{
  load_array_data("arr\\sfxdata.dat", gSFXDATAArrayDataTbl_g);
  sgpSfxTbl_g = str_load_table_ex("arr\\sfxdata.tbl", sfx_fileStr, 1144,
                                  load_game_data, "snd.cpp", 1711);
}

// "Release the sfxdata tables."
void sfxdata_exit()
{
  gamedata_free_data(gSFXDATAArrayDataTbl_g);
  mem_free(sgpSfxTbl_g, "snd.cpp", 1719);
  sgpSfxTbl_g = 0;
}

// "The cached duration of a sound."
int snd_get_duration(int nSoundId)
{
  return sgehSnds[nSoundId].nDuration;
}

// "Stop everything."
// Stops every sound, then the music.
void snd_stop()
{
  stop_digital();
  stop_music();
}

// "Stop every streamed track."
// Declared in victory.h.
void snd_end_streamed()
{
  sgStreamedList.Clear();
}

// "Pick a playback slot, evicting if needed."
//
// Two passes over the same eight slots: the first reclaims anything finished
// and is strength-reduced to a pointer walk (it contains calls), the second
// picks the lowest-priority victim and is fully unrolled (it does not).
int snd_find_slot(BYTE bPriority)
{
  DWORD dwStatus;
  BYTE bBest = bPriority;
  int nSlot = 8;
  int i;

  for (i = 0; i < 8; ++i) {
    snd_slot_t *pSlot = &sgSoundsPlaying[i];

    if (pSlot->bFlags & 8)
      continue;
    if (pSlot->pBuffer != 0) {
      pSlot->pBuffer->GetStatus(&dwStatus);
      if (dwStatus == 1)
        continue;
      if (pSlot->bOwned != 0)
        pSlot->pBuffer->Release();
      pSlot->pBuffer = 0;
    }
    nSlot = i;
  }

  if (nSlot >= 8) {
    for (i = 0; i < 8; ++i) {
      BYTE bSlotFlags = sgSoundsPlaying[i].bFlags;

      if ((bSlotFlags & 8) == 0 && sgSoundsPlaying[i].pBuffer != 0 &&
          (bSlotFlags & 0x20) == 0 &&
          sgSoundsPlaying[i].bPriority < bBest) {
        bBest = sgSoundsPlaying[i].bPriority;
        nSlot = i;
      }
    }
    if (nSlot >= 8)
      return -1;
    sgSoundsPlaying[nSlot].pBuffer->Stop();
    if (sgSoundsPlaying[nSlot].bOwned != 0)
      sgSoundsPlaying[nSlot].pBuffer->Release();
    else
      sgSoundsPlaying[nSlot].pBuffer->SetCurrentPosition(0);
    sgSoundsPlaying[nSlot].pBuffer = 0;
  }
  return nSlot;
}

// "Drop a cached sound's buffer."
// noinline, since 1.17 calls it from snd_purge rather than inlining it there.
__declspec(noinline) void snd_free_snd(int nSoundId)
{
  sound_t *pSound = &sgehSnds[nSoundId];
  IDirectSoundBuffer *pBuffer = pSound->pBuffer;
  int i;

  if (pBuffer == 0)
    return;
  for (i = 0; i < 8; ++i) {
    if (sgSoundsPlaying[i].pBuffer == pBuffer)
      sgSoundsPlaying[i].pBuffer = 0;
  }
  pBuffer->Release();
  pSound->pBuffer = 0;
  if ((sgbSndPlayFlags_g[nSoundId] & 1) == 0)
    dword_6AA95C -= pSound->cbData;
}

// "Bring the sound device up."
// Every failure arm is its own `snd_close(); return 0;`, which MSVC merges. The
// cache floor is clamped in a local and stored unconditionally, as 1.17's cmovb
// does.
int snd_init(void *hWnd, void *pvOut)
{
  snd_err_t *pErr = (snd_err_t *)pvOut;

  if (gpDirectSound == 0) {
    pErr->hr = 0;
    pErr->pszApi = 0;
    pErr->dwCode = 0x8A;
    memset(sgSoundsPlaying, 0, sizeof(sgSoundsPlaying));

    if (ghDSound == 0) {
      // Hyperion: no dsound.dll; DirectSound is port_dsound.cpp's, on SDL audio
      //, and the handle only says it is there.
      ghDSound = reinterpret_cast<void *>(&PortDirectSoundCreateAny);
      if (ghDSound == 0) {
        pErr->dwCode = 0x85;
        pErr->hr = GetLastError();
        pErr->pszApi = "LoadLibrary";
        snd_close();
        return 0;
      }
    }
    if (!InDirectSoundCreate(pErr)) {
      snd_close();
      return 0;
    }
    if (!SetCooperativeLevel(hWnd, pErr)) {
      snd_close();
      return 0;
    }
    if (!snd_create_buffer(pErr)) {
      snd_close();
      return 0;
    }
    snd_init_format();
    SFileDdaInitialize(gpDirectSound);
    sgbDdaInitialized_g = 1;
    SVidInitialize(gpDirectSound);
    sgbSndVidInitialized_g = 1;
    snd_load_btn_snds();
    snd_init_volumes();
    if (!SRegLoadValue("Starcraft", "Sound Memory Cache", 0,
                       (DWORD *)&soundMemoryCache))
      soundMemoryCache = 0x400000;
    else {
      int nCache = soundMemoryCache;
      if ((DWORD)nCache < 0x100000)
        nCache = 0x100000;
      soundMemoryCache = nCache;
    }
    dword_6AA95C = 0;
    sghSndEvent_g = CreateEventA(0, 0, 0, 0);
    if (sghSndEvent_g == 0) {
      snd_close();
      return 0;
    }
    sgbDS = 1;
    sghSndThread_g = (void *)_beginthreadex(0, 0, snd_loadPlayThread, 0, 0,
                                            &sgdwLoadSndThreadId_g);
    if (sghSndThread_g == 0) {
      snd_close();
      return 0;
    }
    gbSndInited = 1;
  }
  return 1;
}

// "Initialise the volume options."
// -1 means never set, and 50 and 25 are the defaults: the saved slots are
// written unconditionally, the live ones only when unset.
void snd_init_volumes()
{
  int nSfx = gnSfxVolume;
  int nMusic;

  gnSavedSfxVolume = 50;
  gnSavedMusicVolume = 25;
  if (nSfx == -1)
    nSfx = 50;
  gnSfxVolume = nSfx;

  nMusic = gnMusicVolume;
  if (nMusic == -1)
    nMusic = 25;
  gnMusicVolume = nMusic;

  snd_set_options(&gOptions);
}

// "Is music enabled?"
// A plain `jmp music_enabled`.
BYTE snd_music_enabled()
{
  return music_enabled();
}

// "Queue a sound into a playing slot."
// The first parameter is a sound id below 1144, though the header spells it
// void *; both are one register here.
BOOL sub_49E000(void *pArg0, int nArg1, int nArg2, WORD wArg3)
{
  DWORD nSoundId = (DWORD)(DWORD_PTR)pArg0;
  int nSlot;

  if (!SoundTypeValid(nSoundId))  // Hyperion: 1.17's 0x478, and a mod's sounds
    return 0;
  nSlot = snd_find_slot(gbSoundPriority[nSoundId]);
  if (nSlot < 0)
    return 0;

  sgbSndPlayFlags_g[nSoundId] |= 0x0C;
  sgSoundsPlaying[nSlot].bFlags |= 8;
  sgSoundsPlaying[nSlot].nUnk14 = nArg2;
  sgSoundsPlaying[nSlot].nSoundId = nSoundId;
  sgSoundsPlaying[nSlot].nUnk10 = nArg1;
  sgSoundsPlaying[nSlot].wUnk04 = wArg3;
  SetEvent(sghSndEvent_g);

  if (dword_6AA95C > soundMemoryCache)
    snd_purge(dword_6AA95C - soundMemoryCache);
  return 1;
}

// "The sound-loading thread."
//
// Reached by address only: snd_init pushes 0x49E090 at _beginthreadex. Waits on
// the event, then loads every slot flagged 8 and, if the slot is still flagged
// when the load finishes, starts it playing.
ULONG __stdcall snd_loadPlayThread(void *pvArg)
{
  char szPath[0x104];
  waveformatex_t wfx;
  dsbufferdesc_t desc;
  void *pFile = 0;
  void *pvLock;
  DWORD cbLock;
  DWORD cbData;
  int nTotal;
  int i;

  WaitForSingleObject(sghSndEvent_g, 0xFFFFFFFF);
  while (sgbDS != 0) {
    nTotal = 0;
    for (i = 0; i < 8; ++i) {
      int nSoundId;
      sound_t *pSound;
      char *pszName;

      if ((sgSoundsPlaying[i].bFlags & 8) == 0)
        continue;
      nSoundId = sgSoundsPlaying[i].nSoundId;
      pSound = &sgehSnds[nSoundId];
      pSound->nLastPlayed = ~(GetTickCount() - 0x51) + 1;
      pszName = sfx_fileStr[nSoundId];
      if (pszName != 0) {
        psnprintf(szPath, 0x104, "sound\\%s", pszName);
        pFile = PortSoundLoad(szPath, &cbData);  // Hyperion: as open_wav_file loads it
        if (pFile != 0 && cbData != 0) {
          BYTE *pData = (BYTE *)read_header(pFile, &cbData, &wfx);

          if (pData != 0) {
            pSound->cbData = cbData;
            memset(&desc, 0, sizeof(desc));
            desc.dwSize = sizeof(desc);
            desc.dwFlags = 0xC2;
            desc.dwBufferBytes = cbData;
            desc.lpwfxFormat = &wfx;
            if (gpDirectSound->CreateSoundBuffer(
                    &desc, (void **)&pSound->pBuffer, 0) != 0) {
              pSound->pBuffer = 0;
            } else if (pSound->pBuffer->Lock(0, cbData, &pvLock, &cbLock, 0, 0,
                                             0) != 0) {
              pSound->pBuffer->Release();
              pSound->pBuffer = 0;
            } else {
              memcpy(pvLock, pData, cbLock);
              pSound->pBuffer->Unlock(pvLock, cbLock, 0, 0);
              if ((sgbSndPlayFlags_g[nSoundId] & 1) == 0)
                nTotal += cbData;
              pSound->nDuration = snd_calc_duration(pSound);
              if (sgSoundsPlaying[i].bFlags & 8) {
                pSound->pBuffer->SetVolume(sgSoundsPlaying[i].nUnk10 - sgdwSndVolDiff_g);
                pSound->pBuffer->SetPan(sgSoundsPlaying[i].nUnk14);
                pSound->pBuffer->Play(0, 0, 0);
                sgSoundsPlaying[i].pBuffer = pSound->pBuffer;
                sgSoundsPlaying[i].bOwned = 0;
                sgSoundsPlaying[i].bFlags = sgbSndPlayFlags_g[nSoundId];
                sgSoundsPlaying[i].bPriority = gbSoundPriority[nSoundId];
              }
            }
          }
        }
      }
      if (pFile != 0) {
        mem_free(pFile, "snd.cpp", 0x337);
        pFile = 0;
      }
      sgbSndPlayFlags_g[nSoundId] &= ~8;
      sgSoundsPlaying[i].bFlags &= ~8;
    }
    if (nTotal != 0) {
      EnterCriticalSection(&sgSndCrit_g);
      dword_6AA95C += nTotal;
      LeaveCriticalSection(&sgSndCrit_g);
    }
    WaitForSingleObject(sghSndEvent_g, 0xFFFFFFFF);
  }
  return 0;
}

// "Load the button-click sound."
// The button click is sfx_fileStr[15].
void snd_load_btn_snds()
{
  char szPath[0x104];
  char *pszName = sfx_fileStr[15];

  if (pszName == 0) {
    sgpBtnClickSnd_g = 0;
    return;
  }
  psnprintf(szPath, 0x104, "sound\\%s", pszName);
  sgpBtnClickSnd_g = (IDirectSoundBuffer *)open_wav_file(szPath, 0);
}

// "Cache a sound, loading it if needed."
// The `|= 4` is written in both arms, as 1.17 has two lowerings of it.
void sub_49E350(int nSoundId)
{
  sound_t *pSound;

  if (gpDirectSound == 0)
    return;
  if (gnSfxVolume == 0)
    return;
  if (!SoundTypeValid((DWORD)nSoundId))  // Hyperion: and a mod's sounds
    return;

  pSound = &sgehSnds[nSoundId];
  // `~x + 1`, not `-x`, for 1.17's not/inc pair.
  pSound->nLastPlayed = ~(GetTickCount() - 0x51) + 1;
  if (pSound->pBuffer == 0) {
    pSound->pBuffer =
        (IDirectSoundBuffer *)open_wav_file_idx(nSoundId, (intptr_t)pSound);
    if (pSound->pBuffer == 0)
      return;
    pSound->nDuration = snd_calc_duration(pSound);
    if ((sgbSndPlayFlags_g[nSoundId] & 1) == 0)
      dword_6AA95C += pSound->cbData;
    sgbSndPlayFlags_g[nSoundId] |= 4;
    if (dword_6AA95C > soundMemoryCache)
      snd_purge(dword_6AA95C - soundMemoryCache);
  } else {
    sgbSndPlayFlags_g[nSoundId] |= 4;
  }
}

// "Preload or release a sound-id list."
// The stored list is the parameter, not the cursor, so a null argument clears
// the global and still walks the remembered list. Declared in gamesnd.h.
void sub_49E3F0(intptr_t nArg0)
{
  const WORD *pList = (const WORD *)nArg0;
  const WORD *p;
  DWORD dwId;   // wider than the element, which spares a movzx at the subscript

  if (gpDirectSound == 0)
    return;
  p = pList;
  if (p == 0) {
    p = sgpSndPreload_g;
    if (p == 0)
      return;
    sgpSndPreload_g = pList;
  } else {
    sgpSndPreload_g = p;
  }

  dwId = *p++;
  while (dwId != 0) {
    if (SoundTypeValid(dwId)) {  // Hyperion: and a mod's sounds
      if (sgpSndPreload_g != 0) {
        if (gnSfxVolume != 0 && (sgbSndPlayFlags_g[dwId] & 1) != 0)
          sub_49E350(dwId);
      } else {
        snd_free_snd(dwId);
      }
    }
    dwId = *p++;
  }
}

// "Play the UI click."
// Plays the click unless it is already sounding. sgpBtnClickSnd_g is re-read
// across the calls, as in 1.17. The volume index is gnSfxVolume * 99 / 100,
// signed.
void snd_play_btn_click()
{
  DWORD dwStatus;

  if (gpDirectSound == 0)
    return;
  if (gnSfxVolume == 0)
    return;
  if (sgpBtnClickSnd_g == 0)
    return;
  if (sgpBtnClickSnd_g->GetStatus(&dwStatus) == 0 && dwStatus == 1)
    return;

  sgpBtnClickSnd_g->SetVolume(sgSndVolTbl_g[gnSfxVolume * 99 / 100] -
                              sgdwSndVolDiff_g);
  sgpBtnClickSnd_g->Play(0, 0, 0);
}

// "Play a sound with de-duplication."
//
// The eight-slot scan runs only when this sound is unique-per-id (flag 0x10) or
// unique-per-unit-type (flag 0x02 with a unit). `i` is dead inside the body, so
// MSVC strength-reduces it to a pointer walk and the bound compares signed.
int sub_49E4E0(int nSoundId, LONG nVolume, int nPan, void *pUnit)
{
  CUnit *pOwner = (CUnit *)pUnit;

  if (gpDirectSound != 0 && gnSfxVolume != 0) {
    BYTE bFlags = sgbSndPlayFlags_g[nSoundId];
    BYTE bPerId = (BYTE)(bFlags & 0x10);
    sound_t *pSound;
    int i;

    if ((pOwner != 0 && (bFlags & 2) != 0) || bPerId != 0) {
      for (i = 0; i < 8; ++i) {
        snd_slot_t *pSlot = &sgSoundsPlaying[i];
        IDirectSoundBuffer *pBuffer = pSlot->pBuffer;
        DWORD dwStatus;

        if (pBuffer == 0)
          continue;
        if (bPerId != 0) {
          if (pSlot->nSoundId != nSoundId)
            continue;
        } else {
          if (pSlot->wUnk04 != UNIT_TYPE(pOwner))
            continue;
          if ((pSlot->bFlags & 2) == 0)
            continue;
        }
        if (pBuffer->GetStatus(&dwStatus) != 0)
          continue;
        if (dwStatus == 1)
          return 0;
        if (pSlot->bOwned != 0)
          pSlot->pBuffer->Release();
        pSlot->pBuffer = 0;
      }
    }

    bFlags = sgbSndPlayFlags_g[nSoundId];
    pSound = &sgehSnds[nSoundId];
    if (bFlags & 8)
      return 0;
    if (pSound->pBuffer == 0) {
      sub_49E000((void *)(intptr_t)nSoundId, nVolume, nPan,
                 pOwner != 0 ? UNIT_TYPE(pOwner) : -1);
      return 1;
    }
    sgbSndPlayFlags_g[nSoundId] = (BYTE)(bFlags | 4);
    return sub_49E620(pSound, nVolume, nPan, nSoundId,
                      pOwner != 0 ? UNIT_TYPE(pOwner) : -1,
                      (BYTE)(bFlags | 4), 0, gbSoundPriority[nSoundId]);
  }
  return 0;
}

// "Start one sound on a free slot."
//
// The 80 ms throttle is `(DWORD)(pSound->nLastPlayed + now) < 0x50` with
// nLastPlayed holding the NEGATED tick count, which is what makes the sum the
// elapsed time and the compare unsigned.
BOOL sub_49E620(sound_t *pSound, LONG nVolume, int nPan, int nSoundId,
                WORD wUnitType, BYTE bFlags, BYTE bOwned, BYTE bPriority)
{
  IDirectSoundBuffer *pBuffer = pSound->pBuffer;
  DWORD dwNow = GetTickCount();
  DWORD dwStatus;
  int nSlot;

  // The guards nest around the success path: a shared `return 0` that is not
  // textually last is laid out inline and costs 12 B in jump widths.
  if ((DWORD)(pSound->nLastPlayed + dwNow) >= 0x50) {
    nSlot = snd_find_slot(bPriority);
    if (nSlot >= 0) {
      if (pBuffer->GetStatus(&dwStatus) == 0) {
        if (dwStatus == 1) {
          if (gpDirectSound->DuplicateSoundBuffer(pBuffer,
                                                  (void **)&pBuffer) != 0)
            return 0;
          bOwned = 1;
        }
        pBuffer->SetVolume(nVolume - sgdwSndVolDiff_g);
        pBuffer->SetPan(nPan);
        if (pBuffer->Play(0, 0, 0) == 0) {
          pSound->nLastPlayed = ~dwNow + 1;
          sgSoundsPlaying[nSlot].pBuffer = pBuffer;
          sgSoundsPlaying[nSlot].wUnk04 = wUnitType;
          sgSoundsPlaying[nSlot].bFlags = bFlags;
          sgSoundsPlaying[nSlot].bPriority = bPriority;
          sgSoundsPlaying[nSlot].bOwned = bOwned;
          sgSoundsPlaying[nSlot].nSoundId = nSoundId;
          sgSoundsPlaying[nSlot].nUnk10 = nVolume;
          return 1;
        }
      }
    }
  }
  return 0;
}

// "Play a sound at the current sfx volume."
// Returns an int; the zero on the guarded path is a value. The `return 0` comes
// last and the table load is hoisted into a local, which keeps the parameter in
// ECX as 1.17 does.
int sub_49E740(int nId)
{
  if (gpDirectSound != 0) {
    int nVol = gnSfxVolume;
    if (nVol != 0) {
      LONG nVolume = sgSndVolTbl_g[nVol * 99 / 100];
      return sub_49E4E0(nId, nVolume, 0, 0);
    }
  }
  return 0;
}

// "Play a sound at a scaled volume."
// The sound id in ECX is only forwarded. The transformed volume is assigned to
// its own parameter and the guards nest around the call, which keeps 1.17's
// tail jump.
int sub_49E780(int nSoundId, int nVolume, int nPan, CUnit *pUnit)
{
  int nSfx;

  if (gpDirectSound != 0) {
    nSfx = gnSfxVolume;
    if (nSfx != 0) {
      nVolume = sgSndVolTbl_g[nSfx * nVolume / 100];
      return sub_49E4E0(nSoundId, nVolume, nPan, pUnit);
    }
  }
  return 0;
}

// "Play a sound file by table index."
// open_wav_file_idx's twin with play_digital as the tail callee. The return is
// an int: as a void, MSVC merges the two epilogues 1.17 keeps apart.
intptr_t snd_play_snd(int nSoundId)
{
  char szPath[0x104];
  char *pszName = sfx_fileStr[nSoundId];

  if (pszName == 0)
    return 0;
  psnprintf(szPath, 0x104, "sound\\%s", pszName);
  return play_digital(szPath);
}

// "Stream a music/voice file."
// The list insertion is inlined, as in 1.17, in TSList's runtime-link-offset
// form, with the offset term kept apart. When SMemAlloc fails, 1.17 still
// writes through the null node; that is kept.
intptr_t play_digital(const char *pszPath)
{
  void *hFile;  // Hyperion: `int` in 1.17

  if (gpDirectSound == 0)
    return 0;
  if (gnSfxVolume == 0)
    return 0;
  if (!OpenFile((intptr_t)pszPath, (intptr_t)&hFile, 1))
    return 0;
  if (SFileDdaBeginEx(hFile, 0x40000, 0, 0,
                      sgSndVolTbl_g[gnSfxVolume * 99 / 100], 0, 0) == 0) {
    CloseFile((intptr_t)hFile);
    return 0;
  }
  {
    void *pv = SMemAlloc(sizeof(STREAMED), gTypeInfo_STREAMED.raw_name(), -2,
                         8);
    STREAMED *pNode;
    TSLink<STREAMED> *pl;
    TSLink<STREAMED> *pnl;
    STREAMED *pprev;
    STREAMED *pnext;

    if (pv != 0) {
      pNode = (STREAMED *)pv;
      pNode->link.prev = 0;
      pNode->link.next = 0;
      pl = &pNode->link;
    } else {
      pNode = 0;
      pl = &sgStreamedList.link;
    }

    pprev = pl->prev;
    if (pprev != 0) {
      pnext = pl->next;
      if ((intptr_t)pnext <= 0) {
        pnl = reinterpret_cast<TSLink<STREAMED> *>(~(intptr_t)pnext);
      } else {
        intptr_t nOff = 0;
        if ((intptr_t)pnext > 0)
          nOff = (intptr_t)pl - (intptr_t)pprev->link.next;
        pnl = reinterpret_cast<TSLink<STREAMED> *>((intptr_t)pnext + nOff);
      }
      pnl->prev = pprev;
      pl->prev->link.next = pl->next;
      pl->prev = 0;
      pl->next = 0;
    }

    pl->prev = reinterpret_cast<STREAMED *>(&sgStreamedList.link);
    pl->next = sgStreamedList.link.next;
    pnext = sgStreamedList.link.next;
    {
      intptr_t nOff = (int)sgStreamedList._unmapped_00;
      if ((intptr_t)pnext <= 0) {
        pnl = reinterpret_cast<TSLink<STREAMED> *>(~(intptr_t)pnext);
      } else {
        if (nOff < 0)
          nOff = (intptr_t)&sgStreamedList.link -
                 (intptr_t)sgStreamedList.link.prev->link.next;
        pnl = reinterpret_cast<TSLink<STREAMED> *>((intptr_t)pnext + nOff);
      }
      pnl->prev = reinterpret_cast<STREAMED *>(pl);
    }
    sgStreamedList.link.next = pNode;
    pNode->hFile = hFile;
  }
  return (intptr_t)hFile;
}

// "Free cached sounds until the budget is met."
//
// A round-robin sweep that restarts once from 0 if one pass was not enough.
// The `bit 4 set` arm only clears the bit: a sound that was touched since the
// last sweep gets one more round before it is dropped.
int snd_purge(int cbToFree)
{
  BYTE bRestarted = 0;
  int nFreed = 0;
  int nEnd;
  DWORD dwStatus;

  EnterCriticalSection(&sgSndCrit_g);
  for (;;) {
    // Hyperion: to the last sound loaded, 1.17's 0x477 or a mod's
    nEnd = (sgnSndCursor_g == 0) ? g_hxSoundTypes - 1 : sgnSndCursor_g - 1;
    while (sgnSndCursor_g != nEnd) {
      sound_t *pSound = &sgehSnds[sgnSndCursor_g];

      if (pSound->pBuffer != 0) {
        BYTE bFlags = sgbSndPlayFlags_g[sgnSndCursor_g];

        if ((bFlags & 9) == 0) {
          if (bFlags & 4) {
            sgbSndPlayFlags_g[sgnSndCursor_g] = (BYTE)(bFlags & ~4);
          } else if (pSound->pBuffer->GetStatus(&dwStatus) == 0 &&
                     dwStatus != 1) {
            snd_free_snd(sgnSndCursor_g);
            cbToFree -= pSound->cbData;
            nFreed += pSound->cbData;
            if (cbToFree <= 0) {
              LeaveCriticalSection(&sgSndCrit_g);
              return nFreed;
            }
          }
        }
      }
      sgnSndCursor_g = (sgnSndCursor_g >= g_hxSoundTypes - 1) ? 0 : sgnSndCursor_g + 1;
    }
    if (bRestarted != 0)
      break;
    bRestarted = 1;
    sgnSndCursor_g = 0;
  }
  LeaveCriticalSection(&sgSndCrit_g);
  return nFreed;
}

// A TSList walk: the terminator test is the signed `(int)p > 0`, since the last
// node's next holds the complement of the list's head-slot address.
void snd_update_streams()
{
  DWORD dwPos;
  DWORD dwLen;
  STREAMED *p;

  for (p = sgStreamedList.link.next; (intptr_t)p > 0;) {
    SFileDdaGetPos(p->hFile, &dwPos, &dwLen);
    if (dwPos >= dwLen)
      p = sgStreamedList.Remove(p);
    else
      p = p->link.next;
  }
}

// "Apply the options block's volumes."
// The two halves differ: the sfx volume is scaled by 99/100 before the table
// lookup, the music volume indexes it by v - 1. The zero-music arm is the else
// of `if (nMusic != 0)`, as 1.17 lays it out.
void snd_set_options(void *pOptions)
{
  options_t *pOpt = (options_t *)pOptions;
  int nMusic;

  if (gpDirectSound == 0)
    return;

  set_digital_volume(sgSndVolTbl_g[pOpt->nSfxVolume * 99 / 100] -
                     sgdwSndVolDiff_g);

  nMusic = pOpt->nMusicVolume;
  if (nMusic != 0) {
    set_music_volume(sgSndVolTbl_g[nMusic - 1] - sgdwSndVolDiff_g);
    if (sbPlayingMusic == 0) {
      sbPlayingMusic = 1;
      start_song();
    }
  } else {
    set_music_volume(-10000);
    stop_song();
    sbPlayingMusic = 0;
  }
}

// "Tear the sound device down."
// The early exit comes before gbSndInited is cleared, as in 1.17.
void snd_close()
{
  if (gpDirectSound == 0)
    return;

  sgbDS = 0;
  if (sghSndThread_g != 0) {
    SetEvent(sghSndEvent_g);
    if (sgdwLoadSndThreadId_g != GetCurrentThreadId())
      WaitForSingleObject(sghSndThread_g, 0xFFFFFFFF);
    CloseHandle(sghSndThread_g);
    sghSndThread_g = 0;
  }
  if (sghSndEvent_g != 0) {
    CloseHandle(sghSndEvent_g);
    sghSndEvent_g = 0;
  }
  if (sgpBtnClickSnd_g != 0) {
    sgpBtnClickSnd_g->Release();
    sgpBtnClickSnd_g = 0;
  }
  stop_digital();
  stop_music();
  if (sgbDdaInitialized_g != 0) {
    sgbDdaInitialized_g = 0;
    SFileDdaDestroy();
  }
  if (sgbSndVidInitialized_g != 0) {
    sgbSndVidInitialized_g = 0;
    SVidDestroy();
  }
  sgdwSndVolDiff_g = 0;
  if (gpPrimaryBuffer != 0) {
    IDirectSoundBuffer *pBuffer = gpPrimaryBuffer;
    gpPrimaryBuffer = 0;
    pBuffer->Release();
  }
  if (gpDirectSound != 0) {
    IDirectSound *pDS = gpDirectSound;
    gpDirectSound = 0;
    pDS->Release();
  }
  gbSndInited = 0;
}

// "Start a music track."
// A plain `jmp play_music`.
void snd_play_music(int nTrack)
{
  // Hyperion: the menus' track (0x16) stays quiet while the live channel's map switch
  // passes through them on its way to the next game (port_front.cpp)
  if (nTrack == 0x16 && PortFrontLaunchPending())
    return;
  play_music(nTrack);
}

// "Mute or unmute the music."
// The call is written once, after the join, which lets MSVC duplicate the
// `mov ecx, K / jmp` tail into each arm as 1.17 has it.
void snd_toggle_music()
{
  int nVolume = gnMusicVolume;

  if (nVolume != 0)
  {
    gnSavedMusicVolume = nVolume;
    gnMusicVolume = 0;
  }
  else
  {
    gnMusicVolume = gnSavedMusicVolume;
  }

  snd_set_options(&gOptions);
}

// "Mute or unmute the sound effects."
// snd_toggle_music's twin over the sfx volume, with the device guard first and
// stop_digital on the way down; gnSfxVolume is read once after the if/else.
void snd_toggle_sound()
{
  int nVolume;

  if (gpDirectSound == NULL)
    return;

  nVolume = gnSfxVolume;
  if (nVolume != 0)
  {
    gnSavedSfxVolume = nVolume;
    gnSfxVolume = 0;
    stop_digital();
  }
  else
  {
    gnSfxVolume = gnSavedSfxVolume;
  }

  set_digital_volume(sgSndVolTbl_g[gnSfxVolume * 99 / 100] - sgdwSndVolDiff_g);
}

// Clears the stream list, then stops each playing slot that is not loading,
// releasing or rewinding its buffer. pBuffer is re-read after each call.
void stop_digital()
{
  snd_slot_t *pEnt;
  int i;

  sgStreamedList.Clear();

  pEnt = &sgSoundsPlaying[0];
  i = 8;
  do {
    BYTE bFlags;

    --i;
    bFlags = pEnt->bFlags;
    if (bFlags & 8) {
      pEnt->bFlags = (BYTE)(bFlags & ~8);
    } else if (pEnt->pBuffer != 0) {
      DWORD dwStatus;
      if (pEnt->pBuffer->GetStatus(&dwStatus) >= 0 && (dwStatus & 1) != 0) {
        pEnt->pBuffer->Stop();
        if (pEnt->bOwned != 0)
          pEnt->pBuffer->Release();
        else
          pEnt->pBuffer->SetCurrentPosition(0);
        pEnt->pBuffer = 0;
      }
    }
    ++pEnt;
  } while (i != 0);
}
