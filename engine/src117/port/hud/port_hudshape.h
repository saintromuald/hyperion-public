// port_hudshape.h -- the shape layers' tables and geometry, as the design center's shader
// file has them (hudshape.js): the materials a style names, the surfaces, edge profiles,
// patterns and bolt heads by the shader's numbers, and a path's outline cut into the straight
// edges the shader reads. The shader itself is shaders/hudshape.hlsl, made from the same file.
#pragma once

#include <string>
#include <vector>
#include "../platform/port_json.h"

// The material library: each a set of layer values (hudshape.js's MATERIALS). NULL for a name
// that is not one.
const PortJson *HudMaterial(const std::string &name);
std::string HudMaterialNames(void);  // for a checker's note

// The shader's tables: an index for a name, -1 for none.
int HudSurfaceOf(const std::string &name);
std::string HudSurfaceNames(void);
bool HudSurfaceLive(const std::string &name);  // a surface that moves by itself
int HudProfileOf(const std::string &name);
std::string HudProfileNames(void);
int HudPatternOf(const std::string &name);
std::string HudPatternNames(void);
int HudBoltStyleOf(const std::string &name);

// The edges a path draws, most the shader takes.
enum { HUD_MAX_EDGES = 96 };

// A path's loops, each a list of points in its own units (M L H V Q C A Z, either case).
struct HudPoint {
  double x, y;
};
const std::vector<std::vector<HudPoint>> &HudFlatten(const std::string &path);

// The path's edges in a w x h box from the box's centre (a.x, a.y, b.x, b.y), placed as
// pathFit says (slice, stretch or fit) with pathSize, pathScale and pathSlice.
struct HudPathFit {
  int nFit = 0;  // 0 slice, 1 stretch, 2 fit
  bool bSize = false;
  double size[2] = {0, 0};
  double scale = 0;  // 0: the box's height over the frame's
  int nSlice = 0;    // 0 none, 2 or 4 numbers
  double slice[4] = {0, 0, 0, 0};
};
void HudPathEdges(const std::string &path, const HudPathFit &fit, double w, double h, std::vector<float> *pEdges);
