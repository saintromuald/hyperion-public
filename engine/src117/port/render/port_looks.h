// port_looks.h -- looks: what a unit's images are drawn with past their own
// colours (docs/modules/render.md, "Looks"). Presentation only: the game
// never reads any of it.
//
// A look has a name, the images it covers (all of a sprite's when it names none) and a colour
// rule with a ramp of six colours: the palette entries the rule takes (blues, as a crystal's,
// greens, as vespene's, or all) go onto the ramp by their tone. A unit has a look given by hand
// (PortLookUnitSet, the live channel's look_unit), by a provider (rich bases, port_richbases.h)
// or by its type, in that order. Each drawing pass gives every looked unit's sprite its look,
// and a fogged copy of a looked resource the same; CSprite::Draw then draws each image the look
// covers through the look's remap of 1.17's palette, which the software picture, SDL's
// renderer, the GPU's index map and full colour all follow. The minimap's dot of a looked unit
// takes the look's own pen.
//
// In full colour the GPU draws more of the look (scene.hlsl, LookColour and LookPrim): the
// look's colours by tone in place of the palette's nearest, relief from the art's own light,
// as a crystal or as gas; through a crystal the ground, and its shine and stars; a glow from
// within; a light on the ground under each looked unit, recorded before the sprites; and a
// plume from each vent of the images that have them, in place of the images it replaces
// (1.17's puffs for vespene). The live values looks and looks.* tune it.
//
// The built-in looks are "rich-gold" and "rich-violet". looks.json in the active mod's folder
// (its data folder, else its own, as hud.json), or with no mod in the data folder, adds looks
// and changes built-in ones by name; it is read as each game begins. Its format, and the live
// channel's looks, look, look_unit and looks_save, are in docs/modules/render.md, "Looks".
#ifndef PORT_LOOKS_H
#define PORT_LOOKS_H

class CSprite;
class CUnit;

// The looks by name, 0 for none. A look keeps its number for the session: the built-in ones
// are 1 and 2, and those a file or the channel adds come after them.
int PortLookFind(const char *pszName);
const char *PortLookName(int nLook);

// A unit's look given by hand, 0 to take it away and PORT_LOOK_NONE for none; it goes with the
// unit's slot until another unit is made in it.
enum { PORT_LOOK_NONE = 0xFF };
void PortLookUnitSet(int nUnit, int nLook);
// As a game begins (game.cpp): the hand-given looks cleared and looks.json read again.
void PortLooksGameBegin(void);
// A provider's look for a unit without one given by hand, asked each pass; 0 for none.
typedef int (*PFN_PORT_LOOK_OF_UNIT)(int nUnit, const CUnit *pUnit);
void PortLooksProvider(PFN_PORT_LOOK_OF_UNIT pfn);

// The pass (CSprite::DrawAll): each looked unit's sprite and its fogged copies; then each sprite
// drawn asks for its look.
void PortLooksPassBegin(void);
int PortLookOfSprite(const CSprite *pSprite);
// CSprite::Draw: an image of a looked sprite through the look's remap, when the look covers it
// (true, and PortLookImageEnd puts 1.17's table back after the image).
bool PortLookImageBegin(int nLook, int nImage);
void PortLookImageEnd(void);

// The minimap: a unit's dot (sub_46E5B0), and a fogged resource's at its place (sub_46D510),
// in its look's pen, or bColour.
unsigned char PortLookUnitPen(const CUnit *pUnit, unsigned char bColour);
unsigned char PortLookPlacePen(int x, int y, int nType, unsigned char bColour);

// The GPU's part. A primitive the scene records (port_scene.h, PortSceneRle) takes the look of
// the image drawn now, PORT_LOOK_REPLACED or'ed in where the plume replaces the image; 0 for
// none, and always with the live value looks off.
enum { PORT_LOOK_REPLACED = 0x100, PORT_LOOK_LIGHT = 1, PORT_LOOK_PLUME = 2, PORT_LOOK_HALO = 3, PORT_LOOK_GPU_BYTES = 416 };
unsigned int PortLookNow(void);
// How many pixels past a looked image's edge its halo reaches, for PortLookNow's nNow: a
// crystal's glow on what is behind it, which the scene draws as a look primitive over the image
// that reads the image's own frame; 0 for none (gas, the glow off, a replaced image, looks off).
int PortLookHalo(unsigned int nNow);
// CSprite::Draw, after a looked sprite's images: its plume, one look primitive a vent, swollen
// by the puff of 1.17's playing there, so the game's own puffs keep their timing.
void PortLookSpriteDrawn(const CSprite *pSprite, int nLook);
// Each look's data for scene.hlsl, PORT_LOOK_GPU_BYTES each in the looks' order from 1: the
// tone of each palette entry its rule takes (0 none, else 1 + the tone * 254), the ramp as six
// float4s, the clarity, shine, glow and sparkle, the plume's height, density and rise, the
// light's colour and strength, and whether it is gas and drawn in steps. NULL when there are
// none or looks is off.
const unsigned char *PortLooksGpu(unsigned int *pnBytes);

#endif
