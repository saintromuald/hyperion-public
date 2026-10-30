// fog.hlsl -- the fog's light map, made on the GPU (port_gpu_scene.cpp; GpuSceneFogLight).
//
// 1.17 makes it on the processor at every pass (mask.cpp's Fog): each megatile of the view's
// ring takes the smoothed mask grid's shade at its corner and the three after it, and its four
// rows of light are four steps from the top pair to the bottom pair, each row's four bytes the
// ramp between that row's left and right shade (dword_670A80, 32 by 32 dwords). Zoomed out the
// ring is thousands of megatiles, and the light map half a megabyte to make and upload each
// pass. Here the grid and the ramp are uploaded, a few kilobytes, and each thread makes one
// dword of the light map in its place in the frame's bytes, by the same integer steps; the
// column and the rows past the ring, which 1.17 leaves as they were, are 0.
//
// Registers as SDL's compute: read-write buffers in space 1, uniforms in space 2.

RWByteAddressBuffer gLightFrame : register(u0, space1);  // the frame's store

cbuffer FogLightParams : register(b0, space2)
{
  uint gFlGridAt;   // the smoothed grid's first byte in the store
  uint gFlGrid;     // its bytes to a row: the ring's megatiles across, and 3
  uint gFlRampAt;   // the ramp's first byte, 32 by 32 dwords
  uint gFlLightAt;  // the light map's first byte, a multiple of 4
  uint gFlRingX;    // the ring's megatiles across and down
  uint gFlRingY;
  uint gFlPitch;    // the light map's bytes to a row, (gFlRingX + 1) * 4
  uint gFlUnused;
};

uint GridByte(uint n)
{
  uint nAt = gFlGridAt + n;
  return (gLightFrame.Load(nAt & ~3u) >> ((nAt & 3u) * 8u)) & 0xFFu;
}

// A thread a dword: column id.x of gFlRingX + 1, light row id.y of (gFlRingY + 1) * 4.
[numthreads(8, 8, 1)]
void CSFogLight(uint3 id : SV_DispatchThreadID)
{
  uint c = id.x;
  uint row = id.y;
  if (c > gFlRingX || row >= (gFlRingY + 1u) * 4u)
    return;
  uint v = 0u;
  if (c < gFlRingX && row < gFlRingY * 4u) {
    // the megatile's corner in the grid, past its border, and the row's step down
    uint k = row & 3u;
    uint n = ((row >> 2) + 1u) * gFlGrid + c + 1u;
    int a = int(GridByte(n)) << 8;
    int b = int(GridByte(n + 1u)) << 8;
    int da = ((int(GridByte(n + gFlGrid)) << 8) - a) / 4;
    int db = ((int(GridByte(n + gFlGrid + 1u)) << 8) - b) / 4;
    a += int(k) * da;
    b += int(k) * db;
    v = gLightFrame.Load(gFlRampAt + uint((a >> 8) * 32 + (b >> 8)) * 4u);
  }
  gLightFrame.Store(gFlLightAt + row * gFlPitch + c * 4u, v);
}
