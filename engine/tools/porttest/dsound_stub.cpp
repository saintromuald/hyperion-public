// dsound_stub.cpp -- port_dsound.cpp's DirectSound on SDL audio: the system's, as Smacker once had.
#include <windows.h>
#include <dsound.h>

HRESULT __stdcall PortDirectSoundCreate(LPCGUID pGuid, LPDIRECTSOUND *ppDS, LPUNKNOWN pOuter)
{
  return DirectSoundCreate(pGuid, ppDS, pOuter);
}
