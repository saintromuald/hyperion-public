// port_pluto.h -- Pluto AI: the bridge client's boundary (docs/pluto-ai.md,
// docs/pluto-protocol.md).
//
// Pluto's brain runs in the bridge process (engine/tools/pluto-bridge): the real
// pluto.dll + pluto_infer.exe under Wine, fed one frame a game frame, answering
// with the bot's commands as the classic net commands. The game loop reaches the
// bridge only through these three, called by the AI boundary (port_ai.cpp):
//
//   PlutoGameStart()  as the game starts (AiForGame): spawn the bridge, once
//   PlutoFrame()     each game frame (AiFrame): the Pluto slots' steps
//   PlutoGameEnd()   as the game ends: close the bridge
//
// Each is a no-op when no slot plays Pluto or the bridge cannot start, and the
// slot then sits as a computer that never acts.
#pragma once

void PlutoGameStart();
void PlutoFrame();
void PlutoGameEnd();
