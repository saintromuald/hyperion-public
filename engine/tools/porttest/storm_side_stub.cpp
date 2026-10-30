// storm_side_stub.cpp -- what the Storm files call beside the tests' own code, for storm_test and
// smk_test: the program's folder as port_paths.cpp finds it, logs in the working folder, no
// subtitles, no WebM videos (the tests play the Smacker files), and a black screen palette.
#include <windows.h>
#include <string.h>
#include "../../src117/h/gds_palette.h"

const char *PortProgramFolder(void)
{
  static char s_sz[MAX_PATH];
  if (s_sz[0] == '\0') {
    const DWORD n = GetModuleFileNameA(NULL, s_sz, MAX_PATH);
    s_sz[n < MAX_PATH ? n : 0] = '\0';
    char *pszEnd = s_sz;
    for (char *p = s_sz; *p != '\0'; ++p)
      if (*p == '\\' || *p == '/')
        pszEnd = p + 1;
    *pszEnd = '\0';
  }
  return s_sz;
}

const char *PortLogFile(const char *pszName) { return pszName; }

void PortPresentPaletteRGB(unsigned char *pRGB) { memset(pRGB, 0, 3 * 256); }

void PortSubtitlesBegin(const char *) {}
void PortSubtitlesAt(double) {}
void PortSubtitlesEnd(void) {}

// 0x6D2B88 in 1.17, the palette the screen shows (gds_palette.cpp); black here
extern "C" palentry_t _vid_want[256];
palentry_t _vid_want[256];

struct WebmVideo;
WebmVideo *WebmOpen(const void *, size_t) { return NULL; }
void WebmClose(WebmVideo *) {}
int WebmWidth(const WebmVideo *) { return 0; }
int WebmHeight(const WebmVideo *) { return 0; }
unsigned WebmFrames(const WebmVideo *) { return 0; }
unsigned WebmInterval(const WebmVideo *) { return 0; }
bool WebmKeyFrame(const WebmVideo *, unsigned) { return false; }
bool WebmFrame(WebmVideo *, unsigned, unsigned char *, int) { return false; }
const short *WebmSound(const WebmVideo *, size_t *, int *, int *) { return NULL; }
