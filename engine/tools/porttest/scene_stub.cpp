// scene_stub.cpp -- the GPU scene (port_scene.cpp) for the tests: it records nothing, so the caller draws.
int PortSceneBlit(const void *, int, const void *, int, int, int, const void *, int)
{
  return 0;
}
int PortSceneRle(unsigned int, int, int, const void *, const void *, const void *, unsigned int, unsigned int)
{
  return 0;
}
int PortSceneRleUncloaked(int, int, const void *, const void *, const void *, const void *, int, int)
{
  return 0;
}
int PortSceneGlyph(const void *, const void *, int, int, int, const void *)
{
  return 0;
}
int PortSceneStage(void) { return 0; }  // SCENE_OFF
int PortSceneGpuOnly(void) { return 0; }
void PortSceneRenderBegin(void) {}
void PortSceneRenderEnd(void) {}
int PortSceneRingFill(void) { return 0; }  // the GPU does not draw the map, so cell_paint draws the ring
