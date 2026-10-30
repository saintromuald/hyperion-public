// maplimits.h -- the largest map Hyperion plays.
//
// 1.17 stops at 256 tiles a side and sizes its per-map arrays for that. Hyperion plays
// maps up to MAP_SIDE_MAX tiles a side, wide or tall, from its own map format
// (../../../docs/hxm-format.md). The arrays below are sized for the largest map; a map
// within 1.17's limits uses them as 1.17 did, so it plays and saves as it always has.
// A Hyperion game keeps its positions in 32 bits (h/pos.h); what still holds
// them in 16, the screen and the net commands' coordinates, reaches 2,047 tiles.
#ifndef MAPLIMITS_H
#define MAPLIMITS_H

#define MAP_SIDE_MAX 1024                          // tiles a side
#define MAP_TILES_MAX (MAP_SIDE_MAX * MAP_SIDE_MAX)
#define MAP_SIDE_117 256                           // 1.17's
#define MAP_TILES_117 (MAP_SIDE_117 * MAP_SIDE_117)

// The VER of a map that holds what a classic map cannot: more than 256 tiles a side,
// players past 7, triggers for slots past 11 or a mod's units. No Blizzard version uses
// them, so a map site or an editor tells such a map from a classic one by its VER alone.
// They read as Remastered's 206 and 64 do (maphdr.cpp, MapVersions).
#define MAP_VERSION_HX 1206           // needs Brood War
#define MAP_VERSION_HX_ORIGINAL 1064  // does not

// Pathing (sai_PathCreate.cpp, ep_glob_t in sai_PathCreate.h). A map within 1.17's
// limits keeps 1.17's rows of 256 tiles and its caps, so its regions come out as 1.17
// made them and save in 1.17's layout. A larger map has rows of MAP_SIDE_MAX tiles and
// the caps below: region ids stay under the group codes, which start at 0x1FF9, and a
// split tile is 0x2000 plus its index in a WORD. The merge still stops under 2,500
// regions, the AI's tables, so a larger map ends with coarser regions.
#define EP_REGIONS_117 5000
#define EP_REGIONS_MAX 0x1FF0
#define EP_SPLITS_117 25000
#define EP_SPLITS_MAX 0xDFFF
#define EP_POOL_117 10000
#define EP_POOL_MAX (EP_POOL_117 * 8)

// Set from the map's size as its pathing is built or loaded (PathSetMapLimits).
extern int g_epStride;     // wMap's row, in tiles: 256, or MAP_SIDE_MAX
extern int g_epRegionCap;  // EP_REGIONS_117, or EP_REGIONS_MAX
// How much larger the boxes the regions are carved from may be, each way: 1, or 2 on a
// larger map, whose raw regions would otherwise pass the cap (BGH tiled 4 x 4 needs
// more than 8,176 boxes of 1.17's 8 x 7).
extern int g_epBoxScale;
#define EP_STRIDE g_epStride
#define EP_MROW (g_epStride * 4)  // the passability matrix's row, in bytes
void PathSetMapLimits();

// The searches' budgets: the nodes and queue of the search over regions
// (sai_PathFindArea.cpp), and the entries and queue of the search over the tiles near the
// unit (sai_PathFindLocal.cpp). A search that fills one settles for the closest place it
// has reached. A map within 1.17's limits keeps 1.17's; a larger map has many more
// regions between two points, and larger crowds at the end of a route, and gets the
// larger budgets, set with the limits above.
#define EP_AREA_NODES_117 350
#define EP_AREA_QUEUE_117 125
#define EP_AREA_NODES_MAX 4000
#define EP_AREA_QUEUE_MAX 2000
#define EP_LOCAL_NODES_117 250
#define EP_LOCAL_QUEUE_117 150
#define EP_LOCAL_NODES_MAX 1000
#define EP_LOCAL_QUEUE_MAX 600
extern int g_epAreaNodes;
extern int g_epAreaQueue;
extern int g_epLocalNodes;
extern int g_epLocalQueue;

#endif
