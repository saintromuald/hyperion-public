// example.cpp: what a map editor does over Hyperion's live channel, from the command line. It is
// a sample for the author of an editor such as StarDraft 2: every call goes through hxlive.h, and
// the only JSON written by hand is the one-field commands.
//
//   example.exe [--port 27117] status
//   example.exe push <tileset> <size> [--fill <mtxm value>] [--scene <file.json>]
//   example.exe load <path to map> [--ums] [--scene <file.json>]
//   example.exe scene <file.json>
//   example.exe pushscene <tileset> <size> <file.json>
//
// <size> is 64, 96, 128, 192 or 256, or a pair such as 128x192. push with no --fill leaves the
// tiles out, so the ground is the tileset's own plain buildable group; with --fill it builds an
// array of that MTXM value and sends it as tiles_b64, which is the path an editor takes with a
// real map. Each map switch is followed by a wait for the new game and then reveal, to lift the
// fog a camera tour wants.

#include "hxlive.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// A whole file as text, for a cutscene's JSON.
static bool ReadFileText(const std::string &path, std::string *pText, std::string *pWhy) {
  FILE *pFile = nullptr;
  if (fopen_s(&pFile, path.c_str(), "rb") != 0 || pFile == nullptr) {
    *pWhy = "the file could not be opened: " + path;
    return false;
  }
  char buf[8192];
  size_t cbRead = 0;
  pText->clear();
  while ((cbRead = fread(buf, 1, sizeof buf, pFile)) != 0)
    pText->append(buf, cbRead);
  fclose(pFile);
  if (pText->empty()) {
    *pWhy = "the file is empty: " + path;
    return false;
  }
  return true;
}

// 128 or 128x192 as a width and a height.
static bool ParseSize(const std::string &text, int *pWidth, int *pHeight) {
  const size_t at = text.find_first_of("xX");
  *pWidth = atoi(text.c_str());
  *pHeight = at == std::string::npos ? *pWidth : atoi(text.c_str() + at + 1);
  return *pWidth > 0 && *pHeight > 0;
}

// What status says, in one line.
static bool PrintStatus(hxlive::Client &client, bool bWhole) {
  std::string answer, why;
  if (!client.Call(hxlive::StatusMessage(), &answer, &why)) {
    fprintf(stderr, "status: %s\n", why.c_str());
    return false;
  }
  if (!hxlive::AnswerOk(answer)) {
    fprintf(stderr, "status: %s\n", hxlive::AnswerString(answer, "error").c_str());
    return false;
  }
  printf("state %s, frame %s, map_swap %s", hxlive::AnswerString(answer, "state").c_str(),
         hxlive::AnswerString(answer, "frame").c_str(), hxlive::AnswerString(answer, "map_swap").c_str());
  const std::string error = hxlive::AnswerString(answer, "map_swap_error");
  if (!error.empty())
    printf(", map_swap_error \"%s\"", error.c_str());
  printf("\n");
  if (bWhole)
    printf("%s\n", answer.c_str());
  return true;
}

// One command, its answer printed; false when the channel or the command said no.
static bool Send(hxlive::Client &client, const std::string &message, const char *pszWhat) {
  std::string answer, why;
  if (!client.Call(message, &answer, &why)) {
    fprintf(stderr, "%s: %s\n", pszWhat, why.c_str());
    return false;
  }
  if (!hxlive::AnswerOk(answer)) {
    fprintf(stderr, "%s: %s\n", pszWhat, hxlive::AnswerString(answer, "error").c_str());
    return false;
  }
  printf("%s: %s\n", pszWhat, answer.c_str());
  return true;
}

// The wait after a map command, and then the fog off.
static bool Settle(hxlive::Client &client) {
  std::string why;
  if (!hxlive::WaitForSwitch(client, 90000, &why)) {
    fprintf(stderr, "the map did not come up: %s\n", why.c_str());
    return false;
  }
  printf("the new game is running\n");
  return Send(client, hxlive::CommandMessage("reveal"), "reveal");
}

static void Usage() {
  fprintf(stderr,
          "usage: example [--port N] status\n"
          "       example [--port N] push <tileset> <size> [--fill V] [--scene file.json]\n"
          "       example [--port N] load <path to map> [--ums] [--scene file.json]\n"
          "       example [--port N] scene <file.json>\n"
          "       example [--port N] pushscene <tileset> <size> <file.json>\n");
}

int main(int argc, char **argv) {
  int nPort = hxlive::DEFAULT_PORT;
  int nFill = -1;
  bool bUms = false;
  std::string sceneFile;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc)
      nPort = atoi(argv[++i]);
    else if (arg == "--fill" && i + 1 < argc)
      nFill = atoi(argv[++i]);
    else if (arg == "--scene" && i + 1 < argc)
      sceneFile = argv[++i];
    else if (arg == "--ums")
      bUms = true;
    else
      args.push_back(arg);
  }
  if (args.empty()) {
    Usage();
    return 2;
  }
  const std::string what = args[0];

  // the scene, when one was named: a cutscene file is a JSON object of {"steps": [...]}, and it
  // rides along with a map command as its "scene" or goes on its own as cutscene_play
  std::string scene, why;
  if (what == "pushscene" && args.size() >= 4)
    sceneFile = args[3];
  if (what == "scene") {
    if (args.size() < 2) {
      Usage();
      return 2;
    }
    sceneFile = args[1];
  }
  if (!sceneFile.empty() && !ReadFileText(sceneFile, &scene, &why)) {
    fprintf(stderr, "%s\n", why.c_str());
    return 1;
  }

  hxlive::Client client;
  if (!client.Connect(nPort, &why)) {
    fprintf(stderr, "%s\n", why.c_str());
    fprintf(stderr, "start Hyperion.exe --control first (--control=%d for this port)\n", nPort);
    return 1;
  }
  printf("the channel is open on 127.0.0.1:%d\n", client.Port());
  if (!PrintStatus(client, what == "status"))
    return 1;
  if (what == "status")
    return 0;

  if (what == "push" || what == "pushscene") {
    if (args.size() < 3) {
      Usage();
      return 2;
    }
    int nWidth = 0, nHeight = 0;
    if (!ParseSize(args[2], &nWidth, &nHeight)) {
      fprintf(stderr, "a size is 64, 96, 128, 192 or 256, or a pair such as 128x192\n");
      return 2;
    }
    // with --fill, the editor's own array of MTXM values; one value for every cell here, since a
    // group that the tileset does not have is turned down, and only the editor knows its groups
    std::string tiles;
    if (nFill >= 0) {
      const std::vector<uint16_t> mtxm(static_cast<size_t>(nWidth) * static_cast<size_t>(nHeight),
                                       static_cast<uint16_t>(nFill));
      tiles = hxlive::TilesBase64(mtxm.data(), mtxm.size());
      printf("%d tiles of MTXM value %d, %u characters of base64\n", nWidth * nHeight, nFill,
             static_cast<unsigned>(tiles.size()));
    }
    if (!Send(client, hxlive::MapPushMessage(args[1], nWidth, nHeight, tiles, 1000, scene), "map_push"))
      return 1;
    return Settle(client) ? 0 : 1;
  }

  if (what == "load") {
    if (args.size() < 2) {
      Usage();
      return 2;
    }
    // the path goes in as it is written, backslashes and all: JsonQuote inside MapLoadMessage
    // escapes them
    if (!Send(client, hxlive::MapLoadMessage(args[1], bUms, 1000, scene), "map_load"))
      return 1;
    return Settle(client) ? 0 : 1;
  }

  if (what == "scene") {
    // the steps out of the file, as the text of the array. A reader of one top-level field is
    // enough for that; an editor with a JSON library of its own would use it here.
    const std::string steps = hxlive::AnswerValue(scene, "steps");
    if (steps.empty() || steps[0] != '[') {
      fprintf(stderr, "%s holds no \"steps\" array\n", sceneFile.c_str());
      return 1;
    }
    return Send(client, hxlive::CutscenePlayMessage(steps, true, true, true), "cutscene_play") ? 0 : 1;
  }

  Usage();
  return 2;
}
