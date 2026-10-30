// imagetypes.h -- the images.dat row counts and image ids the game's C++ uses.
// Add an image id here, with the function it comes from, rather than in the
// file that needs it.
//
// It is not part of CImage.h: the generated files include CImage.h next
// to another definition of these names. CImage.cpp, its one user, includes it
// directly. The values are 1.17's; the two image ids' names say what each image
// is taken to be, which the code does not prove. The enum has no type name, so
// a fuller images.dat enum cannot collide with it.
#pragma once

enum {
  // GetNumImages (0x40F680) returns 999 with Brood War and 755 without.
  NUM_IMAGES          = 999,   // Brood War images.dat row count
  NUM_IMAGES_ORIGINAL = 755,   // vanilla StarCraft images.dat row count

  // GetJuiceFrame (0x40F670) reads sgpImgGroups[584], at 0x544018; the array
  // starts at 0x5436F8.
  IMG_PYLON_POWER_TOP = 584,

  // rle_draw_texture and rle_draw_texture_hflip (0x40E800, 0x40E880) composite
  // a frame of sgpImgGroups[210], at 0x543A40.
  IMG_P_WARP_TEXTURE  = 210,
};
