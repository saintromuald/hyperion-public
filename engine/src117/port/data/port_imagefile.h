// port_imagefile.h -- a picture file into RGBA pixels: PNG and TGA through stb_image, WebP
// through libwebp's decoder (port_imagefile.cpp). A mod's art sheets and
// icons come this way.
#ifndef PORT_IMAGEFILE_H
#define PORT_IMAGEFILE_H

#include <stddef.h>
#include <string>
#include <vector>

// The most pixels a side, and in all, a picture may have: larger is refused, as a mod's
// mistake rather than a picture to hold in memory (a sheet past it comes in parts).
enum { PORT_IMAGE_MAX_SIDE = 16384, PORT_IMAGE_MAX_PIXELS = 128 * 1024 * 1024 };

// The picture in p (n bytes) as RGBA, four bytes a pixel, rows from the top. PNG and WebP are
// known by their first bytes, TGA, which has none, by its name's ending. False, with the reason
// in *pError, when it is neither or does not decode.
bool PortImageDecode(const unsigned char *p, size_t n, const std::string &name, std::vector<unsigned char> *pRgba,
                     int *pnWidth, int *pnHeight, std::string *pError);

// The picture's size from its header alone, as PortImageDecode would give it; false, with the
// reason in *pError, when it is not one PortImageDecode takes or is past the sizes above.
bool PortImageInfo(const unsigned char *p, size_t n, const std::string &name, int *pnWidth, int *pnHeight,
                   std::string *pError);

// Whether a file of this name is one PortImageDecode takes, by its ending: .png, .webp, .tga.
bool PortImageFileName(const std::string &name);

#endif
