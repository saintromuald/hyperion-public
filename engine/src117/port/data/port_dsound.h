// port_dsound.h -- DirectSound, as much of it as the engine uses, played through SDL
//.
//
// 1.17 plays its sounds on DirectSound: the game's effects (lang/snd.cpp), Storm's music
// stream (port_stormfile.cpp) and the cinematics' tracks (port_smacker.cpp) each create
// buffers on a device, fill them and play them. Hyperion keeps that code and gives it a
// DirectSound of its own, port_dsound.cpp, which mixes the playing buffers into one SDL
// audio stream on every platform. The interfaces are COM's: each method sits in the slot
// DirectSound's own vtable gives it, so a pointer made here serves lang/snd.h's
// declaration of the same interfaces as well as this one.
//
// This header stands in for <dsound.h>: a TU includes one or the other, never both.
#pragma once
#include <windows.h>

struct IUnknown;
struct IDirectSoundBuffer;

typedef struct _DSBUFFERDESC {
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwBufferBytes;
  DWORD dwReserved;
  WAVEFORMATEX *lpwfxFormat;
  GUID guid3DAlgorithm;
} DSBUFFERDESC, *LPDSBUFFERDESC;
typedef const DSBUFFERDESC *LPCDSBUFFERDESC;

// DirectX 3's description, without the 3D algorithm.
typedef struct _DSBUFFERDESC1 {
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwBufferBytes;
  DWORD dwReserved;
  WAVEFORMATEX *lpwfxFormat;
} DSBUFFERDESC1;

typedef struct _DSBCAPS {
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwBufferBytes;
  DWORD dwUnlockTransferRate;
  DWORD dwPlayCpuOverhead;
} DSBCAPS, *LPDSBCAPS;

// IDirectSound, in DirectSound's slot order.
struct IDirectSound {
  virtual HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) = 0;
  virtual ULONG __stdcall AddRef() = 0;
  virtual ULONG __stdcall Release() = 0;
  virtual HRESULT __stdcall CreateSoundBuffer(LPCDSBUFFERDESC pDesc, IDirectSoundBuffer **ppBuffer,
                                              IUnknown *pOuter) = 0;
  virtual HRESULT __stdcall GetCaps(void *pCaps) = 0;
  virtual HRESULT __stdcall DuplicateSoundBuffer(IDirectSoundBuffer *pOriginal, IDirectSoundBuffer **ppDuplicate) = 0;
  virtual HRESULT __stdcall SetCooperativeLevel(HWND hWnd, DWORD dwLevel) = 0;
  virtual HRESULT __stdcall Compact() = 0;
  virtual HRESULT __stdcall GetSpeakerConfig(DWORD *pdwConfig) = 0;
  virtual HRESULT __stdcall SetSpeakerConfig(DWORD dwConfig) = 0;
  virtual HRESULT __stdcall Initialize(LPCGUID pGuid) = 0;
};

// IDirectSoundBuffer, in DirectSound's slot order.
struct IDirectSoundBuffer {
  virtual HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) = 0;
  virtual ULONG __stdcall AddRef() = 0;
  virtual ULONG __stdcall Release() = 0;
  virtual HRESULT __stdcall GetCaps(LPDSBCAPS pCaps) = 0;
  virtual HRESULT __stdcall GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite) = 0;
  virtual HRESULT __stdcall GetFormat(WAVEFORMATEX *pwfx, DWORD cbSize, DWORD *pcbWritten) = 0;
  virtual HRESULT __stdcall GetVolume(LONG *plVolume) = 0;
  virtual HRESULT __stdcall GetPan(LONG *plPan) = 0;
  virtual HRESULT __stdcall GetFrequency(DWORD *pdwFrequency) = 0;
  virtual HRESULT __stdcall GetStatus(DWORD *pdwStatus) = 0;
  virtual HRESULT __stdcall Initialize(IDirectSound *pDirectSound, LPCDSBUFFERDESC pDesc) = 0;
  virtual HRESULT __stdcall Lock(DWORD dwOffset, DWORD dwBytes, void **ppv1, DWORD *pcb1, void **ppv2, DWORD *pcb2,
                                 DWORD dwFlags) = 0;
  virtual HRESULT __stdcall Play(DWORD dwReserved, DWORD dwPriority, DWORD dwFlags) = 0;
  virtual HRESULT __stdcall SetCurrentPosition(DWORD dwPosition) = 0;
  virtual HRESULT __stdcall SetFormat(const WAVEFORMATEX *pwfx) = 0;
  virtual HRESULT __stdcall SetVolume(LONG lVolume) = 0;
  virtual HRESULT __stdcall SetPan(LONG lPan) = 0;
  virtual HRESULT __stdcall SetFrequency(DWORD dwFrequency) = 0;
  virtual HRESULT __stdcall Stop() = 0;
  virtual HRESULT __stdcall Unlock(void *pv1, DWORD cb1, void *pv2, DWORD cb2) = 0;
  virtual HRESULT __stdcall Restore() = 0;
};

typedef IDirectSound *LPDIRECTSOUND;
typedef IDirectSoundBuffer *LPDIRECTSOUNDBUFFER;
typedef IUnknown *LPUNKNOWN;

#define DSBCAPS_PRIMARYBUFFER 0x00000001
#define DSBCAPS_STATIC 0x00000002
#define DSBCAPS_LOCHARDWARE 0x00000004
#define DSBCAPS_LOCSOFTWARE 0x00000008
#define DSBCAPS_CTRLFREQUENCY 0x00000020
#define DSBCAPS_CTRLPAN 0x00000040
#define DSBCAPS_CTRLVOLUME 0x00000080
#define DSBCAPS_STICKYFOCUS 0x00004000
#define DSBCAPS_GLOBALFOCUS 0x00008000
#define DSBCAPS_GETCURRENTPOSITION2 0x00010000
#define DSBPLAY_LOOPING 0x00000001
#define DSBSTATUS_PLAYING 0x00000001
#define DSBSTATUS_BUFFERLOST 0x00000002
#define DSBSTATUS_LOOPING 0x00000004
#define DSBLOCK_FROMWRITECURSOR 0x00000001
#define DSBLOCK_ENTIREBUFFER 0x00000002
#define DSSCL_NORMAL 0x00000001
#define DSSCL_PRIORITY 0x00000002
#define DSSCL_EXCLUSIVE 0x00000003
#define DSSCL_WRITEPRIMARY 0x00000004
#define DSBVOLUME_MIN (-10000)
#define DSBVOLUME_MAX 0
#define DSBPAN_LEFT (-10000)
#define DSBPAN_CENTER 0
#define DSBPAN_RIGHT 10000
#define DSBFREQUENCY_ORIGINAL 0
#define DSBFREQUENCY_MIN 100
#define DSBFREQUENCY_MAX 200000

#define MAKE_DSHRESULT(code) ((HRESULT)(0x88780000u | (code)))
#define DS_OK S_OK
#define DSERR_ALLOCATED MAKE_DSHRESULT(10)
#define DSERR_CONTROLUNAVAIL MAKE_DSHRESULT(30)
#define DSERR_INVALIDPARAM E_INVALIDARG
#define DSERR_INVALIDCALL MAKE_DSHRESULT(50)
#define DSERR_GENERIC E_FAIL
#define DSERR_PRIOLEVELNEEDED MAKE_DSHRESULT(70)
#define DSERR_OUTOFMEMORY E_OUTOFMEMORY
#define DSERR_BADFORMAT MAKE_DSHRESULT(100)
#define DSERR_UNSUPPORTED E_NOTIMPL
#define DSERR_NODRIVER MAKE_DSHRESULT(120)
#define DSERR_ALREADYINITIALIZED MAKE_DSHRESULT(130)
#define DSERR_NOAGGREGATION ((HRESULT)0x80040110L)
#define DSERR_BUFFERLOST MAKE_DSHRESULT(150)
#define DSERR_OTHERAPPHASPRIO MAKE_DSHRESULT(160)
#define DSERR_UNINITIALIZED MAKE_DSHRESULT(170)
#define DSERR_NOINTERFACE ((HRESULT)0x80004002L)
#define DSERR_ACCESSDENIED ((HRESULT)0x80070005L)

// The device: DirectSoundCreate. Any number may be made; they share one SDL stream.
HRESULT __stdcall PortDirectSoundCreate(LPCGUID pGuid, LPDIRECTSOUND *ppDS, LPUNKNOWN pOuter);
