// port_imagefile.cpp -- a picture file into RGBA pixels (port_imagefile.h). stb_image and
// libwebp's decoder are built into the image library beside the engine (tools/fetch_stb.py,
// tools/fetch_libwebp.py), stb_image for PNG and TGA alone.
#include "port_imagefile.h"

#include <ctype.h>
#include <string.h>

#include <webp/decode.h>

extern "C" {
unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *channels_in_file,
                                     int desired_channels);
int stbi_info_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp);
void stbi_image_free(void *retval_from_stbi_load);
const char *stbi_failure_reason(void);
}

namespace {

bool EndsWith(const std::string &name, const char *pszEnd) {
  const size_t n = strlen(pszEnd);
  if (name.size() < n)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (tolower(static_cast<unsigned char>(name[name.size() - n + i])) != pszEnd[i])
      return false;
  return true;
}

bool Fits(int nWidth, int nHeight, std::string *pError) {
  if (nWidth <= 0 || nHeight <= 0 || nWidth > PORT_IMAGE_MAX_SIDE || nHeight > PORT_IMAGE_MAX_SIDE ||
      static_cast<long long>(nWidth) * nHeight > PORT_IMAGE_MAX_PIXELS) {
    *pError = "is " + std::to_string(nWidth) + " x " + std::to_string(nHeight) + ", past the " +
              std::to_string(PORT_IMAGE_MAX_SIDE) + " pixels a side or " + std::to_string(PORT_IMAGE_MAX_PIXELS >> 20) +
              " million in all a picture may have (a sheet past it comes in parts)";
    return false;
  }
  return true;
}

}  // namespace

namespace {

// What the first bytes say: PNG, WebP, or neither (a TGA has no mark of its own).
enum Kind { KIND_NONE, KIND_PNG, KIND_WEBP, KIND_TGA };
Kind KindOf(const unsigned char *p, size_t n, const std::string &name, std::string *pError) {
  static const unsigned char kPng[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (n > 0x7FFFFFFF) {
    *pError = "is past 2 GB";
    return KIND_NONE;
  }
  if (n >= 8 && memcmp(p, kPng, 8) == 0)
    return KIND_PNG;
  if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0)
    return KIND_WEBP;
  if (EndsWith(name, ".tga"))
    return KIND_TGA;
  *pError = "is not PNG, WebP or TGA";
  return KIND_NONE;
}

}  // namespace

bool PortImageInfo(const unsigned char *p, size_t n, const std::string &name, int *pnWidth, int *pnHeight,
                   std::string *pError) {
  const Kind kind = KindOf(p, n, name, pError);
  int nChannels = 0;
  if (kind == KIND_NONE)
    return false;
  if (kind == KIND_WEBP ? !WebPGetInfo(p, n, pnWidth, pnHeight)
                        : !stbi_info_from_memory(p, static_cast<int>(n), pnWidth, pnHeight, &nChannels)) {
    *pError = kind == KIND_WEBP ? "is not a WebP picture libwebp reads"
                                : std::string("is not a ") + (kind == KIND_PNG ? "PNG" : "TGA") + " picture";
    return false;
  }
  return Fits(*pnWidth, *pnHeight, pError);
}

bool PortImageFileName(const std::string &name) {
  return EndsWith(name, ".png") || EndsWith(name, ".webp") || EndsWith(name, ".tga");
}

bool PortImageDecode(const unsigned char *p, size_t n, const std::string &name, std::vector<unsigned char> *pRgba,
                     int *pnWidth, int *pnHeight, std::string *pError) {
  const Kind kind = KindOf(p, n, name, pError);
  if (kind == KIND_NONE)
    return false;
  if (kind == KIND_WEBP) {
    int w = 0, h = 0;
    if (!WebPGetInfo(p, n, &w, &h)) {
      *pError = "is not a WebP picture libwebp reads";
      return false;
    }
    if (!Fits(w, h, pError))
      return false;
    uint8_t *pOut = WebPDecodeRGBA(p, n, &w, &h);
    if (pOut == NULL) {
      *pError = "does not decode as WebP";
      return false;
    }
    pRgba->assign(pOut, pOut + static_cast<size_t>(w) * h * 4);
    WebPFree(pOut);
    *pnWidth = w;
    *pnHeight = h;
    return true;
  }
  int w = 0, h = 0, nChannels = 0;
  if (!PortImageInfo(p, n, name, &w, &h, pError))
    return false;
  unsigned char *pOut = stbi_load_from_memory(p, static_cast<int>(n), &w, &h, &nChannels, 4);
  if (pOut == NULL) {
    *pError = std::string("does not decode as ") + (kind == KIND_PNG ? "PNG" : "TGA") + " (" + stbi_failure_reason() + ")";
    return false;
  }
  const bool bFits = Fits(w, h, pError);
  if (bFits)
    pRgba->assign(pOut, pOut + static_cast<size_t>(w) * h * 4);
  stbi_image_free(pOut);
  *pnWidth = w;
  *pnHeight = h;
  return bFits;
}
