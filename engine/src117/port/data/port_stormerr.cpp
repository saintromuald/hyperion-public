// port_stormerr.cpp -- Storm's last error and its error texts, the port's own
// (Err). With these the engine imports nothing from
// storm.dll.
//
// Storm keeps one last error for the whole process, apart from Win32's:
// SErrSetLastError sets both, SetLastError alone does not reach it, and
// SErrGetLastError reads Storm's. The port's Storm functions set their errors
// here as storm.dll's set them there. SErrGetErrorStr gives the name of a Storm
// error or of a DirectDraw or DirectSound one, from the tables below, or else
// the system's text, each ending in CR LF; one that does not fit, terminator and
// all, is an empty string and ERROR_INSUFFICIENT_BUFFER. What storm.dll does
// here was found by trying it; tools/porttest/err_test.cpp compares the two.
#include <windows.h>
#include <string.h>
#include "port_stormapi.h"

struct ErrName {
  DWORD dwCode;
  const char *pszName;
};

// Storm's own errors, facility 0x510, as storm.dll names them.
static const ErrName STORM_ERRORS[] = {
    {0x85100000u, "ASSERTION!"},
    {0x85100065u, "STORM_ERROR_BAD_ARGUMENT"},
    {0x85100066u, "STORM_ERROR_GAME_ALREADY_STARTED"},
    {0x85100067u, "STORM_ERROR_GAME_FULL"},
    {0x85100068u, "STORM_ERROR_GAME_NOT_FOUND"},
    {0x85100069u, "STORM_ERROR_GAME_TERMINATED"},
    {0x8510006Au, "STORM_ERROR_INVALID_PLAYER"},
    {0x8510006Bu, "STORM_ERROR_NO_MESSAGES_WAITING"},
    {0x8510006Cu, "STORM_ERROR_NOT_ARCHIVE"},
    {0x8510006Du, "STORM_ERROR_NOT_ENOUGH_ARGUMENTS"},
    {0x8510006Eu, "STORM_ERROR_NOT_IMPLEMENTED"},
    {0x8510006Fu, "STORM_ERROR_NOT_IN_ARCHIVE"},
    {0x85100070u, "STORM_ERROR_NOT_IN_GAME"},
    {0x85100071u, "STORM_ERROR_NOT_INITIALIZED"},
    {0x85100072u, "STORM_ERROR_NOT_PLAYING"},
    {0x85100073u, "STORM_ERROR_NOT_REGISTERED"},
    {0x85100074u, "STORM_ERROR_REQUIRES_CODEC"},
    {0x85100075u, "STORM_ERROR_REQUIRES_CODEC"},
    {0x85100076u, "STORM_ERROR_REQUIRES_CODEC"},
    {0x85100077u, "STORM_ERROR_REQUIRES_UPGRADE"},
    {0x85100078u, "STORM_ERROR_STILL_ACTIVE"},
    {0x85100079u, "STORM_ERROR_VERSION_MISMATCH"},
    {0x8510007Au, "Attempt to free a memory block which is not currently allocated."},
    {0x8510007Bu, "This memory block has been corrupted by an out-of-bounds memory write."},
    {0x8510007Cu, "Attempt to free an invalid memory block."},
    {0x8510007Du, "This function call is invalid because the memory manager is not currently initialized."},
    {0x8510007Eu, "A block of memory was allocated but never freed."},
    {0x8510007Fu, "A resource handle was obtained but never released."},
    {0x85100080u, "Attempt to access beyond the bounds of an array."},
    {0x85100081u, "Attempt to free a NULL pointer."},
    {0x85100082u, "The CD keys do not match."},
    {0x85100083u, "The file data is corrupt."},
    {0x85100084u, "FATAL ERROR!"},
    {0x85100085u, "The requested game type is temporarily unavailable."},
};

// DirectDraw's (facility 0x876) and DirectSound's (0x878) errors by their
// SDK names, the ones storm.dll names.
static const ErrName DIRECTX_ERRORS[] = {
    {0x88760005u, "DDERR_ALREADYINITIALIZED"},
    {0x8876000Au, "DDERR_CANNOTATTACHSURFACE"},
    {0x88760014u, "DDERR_CANNOTDETACHSURFACE"},
    {0x88760028u, "DDERR_CURRENTLYNOTAVAIL"},
    {0x88760037u, "DDERR_EXCEPTION"},
    {0x8876005Au, "DDERR_HEIGHTALIGN"},
    {0x8876005Fu, "DDERR_INCOMPATIBLEPRIMARY"},
    {0x88760064u, "DDERR_INVALIDCAPS"},
    {0x8876006Eu, "DDERR_INVALIDCLIPLIST"},
    {0x88760078u, "DDERR_INVALIDMODE"},
    {0x88760082u, "DDERR_INVALIDOBJECT"},
    {0x88760091u, "DDERR_INVALIDPIXELFORMAT"},
    {0x88760096u, "DDERR_INVALIDRECT"},
    {0x887600A0u, "DDERR_LOCKEDSURFACES"},
    {0x887600AAu, "DDERR_NO3D"},
    {0x887600B4u, "DDERR_NOALPHAHW"},
    {0x887600CDu, "DDERR_NOCLIPLIST"},
    {0x887600D2u, "DDERR_NOCOLORCONVHW"},
    {0x887600D4u, "DDERR_NOCOOPERATIVELEVELSET"},
    {0x887600D7u, "DDERR_NOCOLORKEY"},
    {0x887600DCu, "DDERR_NOCOLORKEYHW"},
    {0x887600DEu, "DDERR_NODIRECTDRAWSUPPORT"},
    {0x887600E1u, "DDERR_NOEXCLUSIVEMODE"},
    {0x887600E6u, "DDERR_NOFLIPHW"},
    {0x887600F0u, "DDERR_NOGDI"},
    {0x887600FAu, "DDERR_NOMIRRORHW"},
    {0x887600FFu, "DDERR_NOTFOUND"},
    {0x88760104u, "DDERR_NOOVERLAYHW"},
    {0x88760118u, "DDERR_NORASTEROPHW"},
    {0x88760122u, "DDERR_NOROTATIONHW"},
    {0x88760136u, "DDERR_NOSTRETCHHW"},
    {0x8876013Cu, "DDERR_NOT4BITCOLOR"},
    {0x8876013Du, "DDERR_NOT4BITCOLORINDEX"},
    {0x88760140u, "DDERR_NOT8BITCOLOR"},
    {0x8876014Au, "DDERR_NOTEXTUREHW"},
    {0x8876014Fu, "DDERR_NOVSYNCHW"},
    {0x88760154u, "DDERR_NOZBUFFERHW"},
    {0x8876015Eu, "DDERR_NOZOVERLAYHW"},
    {0x88760168u, "DDERR_OUTOFCAPS"},
    {0x8876017Cu, "DDERR_OUTOFVIDEOMEMORY"},
    {0x8876017Eu, "DDERR_OVERLAYCANTCLIP"},
    {0x88760180u, "DDERR_OVERLAYCOLORKEYONLYONEACTIVE"},
    {0x88760183u, "DDERR_PALETTEBUSY"},
    {0x88760190u, "DDERR_COLORKEYNOTSET"},
    {0x8876019Au, "DDERR_SURFACEALREADYATTACHED"},
    {0x887601A4u, "DDERR_SURFACEALREADYDEPENDENT"},
    {0x887601AEu, "DDERR_SURFACEBUSY"},
    {0x887601B3u, "DDERR_CANTLOCKSURFACE"},
    {0x887601B8u, "DDERR_SURFACEISOBSCURED"},
    {0x887601C2u, "DDERR_SURFACELOST"},
    {0x887601CCu, "DDERR_SURFACENOTATTACHED"},
    {0x887601D6u, "DDERR_TOOBIGHEIGHT"},
    {0x887601E0u, "DDERR_TOOBIGSIZE"},
    {0x887601EAu, "DDERR_TOOBIGWIDTH"},
    {0x887601FEu, "DDERR_UNSUPPORTEDFORMAT"},
    {0x88760208u, "DDERR_UNSUPPORTEDMASK"},
    {0x88760219u, "DDERR_VERTICALBLANKINPROGRESS"},
    {0x8876021Cu, "DDERR_WASSTILLDRAWING"},
    {0x88760230u, "DDERR_XALIGN"},
    {0x88760231u, "DDERR_INVALIDDIRECTDRAWGUID"},
    {0x88760232u, "DDERR_DIRECTDRAWALREADYCREATED"},
    {0x88760233u, "DDERR_NODIRECTDRAWHW"},
    {0x88760234u, "DDERR_PRIMARYSURFACEALREADYEXISTS"},
    {0x88760235u, "DDERR_NOEMULATION"},
    {0x88760236u, "DDERR_REGIONTOOSMALL"},
    {0x88760237u, "DDERR_CLIPPERISUSINGHWND"},
    {0x88760238u, "DDERR_NOCLIPPERATTACHED"},
    {0x88760239u, "DDERR_NOHWND"},
    {0x8876023Au, "DDERR_HWNDSUBCLASSED"},
    {0x8876023Bu, "DDERR_HWNDALREADYSET"},
    {0x8876023Cu, "DDERR_NOPALETTEATTACHED"},
    {0x8876023Du, "DDERR_NOPALETTEHW"},
    {0x8876023Eu, "DDERR_BLTFASTCANTCLIP"},
    {0x8876023Fu, "DDERR_NOBLTHW"},
    {0x88760240u, "DDERR_NODDROPSHW"},
    {0x88760241u, "DDERR_OVERLAYNOTVISIBLE"},
    {0x88760242u, "DDERR_NOOVERLAYDEST"},
    {0x88760243u, "DDERR_INVALIDPOSITION"},
    {0x88760244u, "DDERR_NOTAOVERLAYSURFACE"},
    {0x88760245u, "DDERR_EXCLUSIVEMODEALREADYSET"},
    {0x88760246u, "DDERR_NOTFLIPPABLE"},
    {0x88760247u, "DDERR_CANTDUPLICATE"},
    {0x88760248u, "DDERR_NOTLOCKED"},
    {0x88760249u, "DDERR_CANTCREATEDC"},
    {0x8876024Au, "DDERR_NODC"},
    {0x8876024Bu, "DDERR_WRONGMODE"},
    {0x8876024Cu, "DDERR_IMPLICITLYCREATED"},
    {0x8876024Du, "DDERR_NOTPALETTIZED"},
    {0x8876024Eu, "DDERR_UNSUPPORTEDMODE"},
    {0x8876024Fu, "DDERR_NOMIPMAPHW"},
    {0x88760250u, "DDERR_INVALIDSURFACETYPE"},
    {0x8876026Cu, "DDERR_DCALREADYCREATED"},
    {0x88760280u, "DDERR_CANTPAGELOCK"},
    {0x88760294u, "DDERR_CANTPAGEUNLOCK"},
    {0x887602A8u, "DDERR_NOTPAGELOCKED"},
    {0x8878000Au, "DSERR_ALLOCATED"},
    {0x8878001Eu, "DSERR_CONTROLUNAVAIL"},
    {0x88780032u, "DSERR_INVALIDCALL"},
    {0x88780046u, "DSERR_PRIOLEVELNEEDED"},
    {0x88780064u, "DSERR_BADFORMAT"},
    {0x88780078u, "DSERR_NODRIVER"},
    {0x88780082u, "DSERR_ALREADYINITIALIZED"},
    {0x88780096u, "DSERR_BUFFERLOST"},
    {0x887800A0u, "DSERR_OTHERAPPHASPRIO"},
    {0x887800AAu, "DSERR_UNINITIALIZED"},
};

static DWORD g_dwLastError;
static BOOL g_bSuppress;  // kept for nothing: the port shows no error of Storm's own

typedef void(__fastcall *SERRBLIZZERRFUNC)(const char *pszMessage, const char *pszDetail,
                                           const char *apszFiles[], unsigned int nFiles,
                                           unsigned int nLine, bool bFatal);
static SERRBLIZZERRFUNC g_pfnBlizzardError;  // likewise

static const char *ErrLookup(const ErrName *pTable, size_t nCount, DWORD dwCode)
{
  for (size_t i = 0; i < nCount; ++i)
    if (pTable[i].dwCode == dwCode)
      return pTable[i].pszName;
  return NULL;
}

extern "C" {

// Ordinal 465.
void __stdcall SErrSetLastError(DWORD dwErrCode)
{
  g_dwLastError = dwErrCode;
  SetLastError(dwErrCode);
}

// Ordinal 463.
DWORD __stdcall SErrGetLastError()
{
  return g_dwLastError;
}

// Ordinal 468. Storm's error dialogs are held back while it is set; it answers
// with what it was given.
BOOL __stdcall SErrSuppressErrors(BOOL bSuppress)
{
  g_bSuppress = bSuppress;
  return bSuppress;
}

// Ordinal 462.
BOOL __stdcall SErrGetErrorStr(DWORD dwErrCode, char *pszBuffer, DWORD cchBuffer)
{
  if (!pszBuffer || !cchBuffer) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const char *pszName = ErrLookup(STORM_ERRORS, ARRAYSIZE(STORM_ERRORS), dwErrCode);
  if (!pszName)
    pszName = ErrLookup(DIRECTX_ERRORS, ARRAYSIZE(DIRECTX_ERRORS), dwErrCode);
  if (pszName) {
    const size_t cch = strlen(pszName);
    if (cch + 3 > cchBuffer) {
      pszBuffer[0] = '\0';
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    memcpy(pszBuffer, pszName, cch);
    memcpy(pszBuffer + cch, "\r\n", 3);
    return TRUE;
  }
  // Without FORMAT_MESSAGE_IGNORE_INSERTS, as Storm asks: a text with inserts
  // is not given.
  if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwErrCode, 0, pszBuffer, cchBuffer, NULL))
    return TRUE;
  pszBuffer[0] = '\0';
  return FALSE;
}

}  // extern "C"

// By name, C++ linkage and __fastcall: the error handler the game registers at
// start-up. Storm called it from its own error dialogs, which the port does not
// have.
void __fastcall SErrSetBlizzardErrorFunction(SERRBLIZZERRFUNC pfnFunc)
{
  g_pfnBlizzardError = pfnFunc;
}
