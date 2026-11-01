// watcher64.c -- a 64-bit observer: polls for BWRL_SHM while the rig plays, dumps the
// mapping once it appears (or reports it never did).
#include <windows.h>
#include <stdio.h>
int main(void) {
  HANDLE h = NULL;
  for (int i = 0; i < 600; ++i) {
    h = OpenFileMappingA(FILE_MAP_READ, FALSE, "BWRL_SHM");
    if (h) {
      void *v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
      if (v) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(v, &mbi, sizeof mbi) == sizeof mbi) {
          FILE *f = fopen("Z:\\home\\saintbellarmine\\Github\\pluto\\rig\\shm-dump.bin", "wb");
          if (f) {
            fwrite(v, 1, (size_t)mbi.RegionSize, f);
            fclose(f);
            printf("[watcher64] dumped %zu bytes at iteration %d\n", (size_t)mbi.RegionSize, i);
            return 0;
          }
        }
      }
    }
    Sleep(50);
  }
  printf("[watcher64] BWRL_SHM never appeared\n");
  return 1;
}
