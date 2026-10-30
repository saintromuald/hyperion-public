// vpacket.cpp -- the voice packet stubs. Bodies are in oracle VA order.

#include "../h/vpacket.h"
#include "../gen/globals117.h"

// The bodies, in oracle VA order.

// Returns 0: the voice message consumes nothing.
int voice_chat(int nPlayerId, char *pData, unsigned int nSize)
{
    return 0;
}

// Squelches a player's voice: nothing, in this build. /OPT:ICF folded every
// empty function onto this `ret`; the spelling is net_cmds.h's.
// Hyperion: the
// engine has it too, and the other spellings folded onto it are empty bodies
// in src117/port/gameplay/port_names.cpp.
void __fastcall voice_squelch(DWORD dwStormID, BOOL bSquelch)
{
}
