// game.cpp -- starting, restarting and tearing down a game session: the theme
// and slot setup that runs before the first frame, the allocation of everything
// the session owns, and the matching free. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/game.h"
#include "../port/gameplay/port_eud.h"  // Hyperion: EUD maps
#include "../port/gameplay/port_handicap.h"  // Hyperion: handicaps
#include "../port/gameplay/port_mutators.h"  // Hyperion: mutators
#include "../port/render/port_looks.h"  // Hyperion: PortLooksGameBegin
#include "../h/map_load.h"
#include "../h/minimap.h"
#include "../h/Teams.h"
#include "../h/filters.h"
#include "../h/gamesnd.h"
#include "../h/gametype.h"
#include "../h/glues.h"
#include "../h/snd.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/CFlingy.h"
#include "../h/CUnitProtoss.h"
#include "../h/net_error.h"
#include "../h/net_glue.h"
#include "../h/net_cmds.h"
#include "../h/net_player.h"
#include "../h/net_record.h"
#include "../h/net_time.h"
#include "../h/opt_dlgs.h"
#include "../h/placebox.h"
#include "../h/game_evt.h"
#include "../h/gds_palette.h"
#include "../h/event_Win.h"
#include "../h/maphdr.h"
#include "../h/replay.h"
#include "../h/net_rand.h"
#include "../h/net_mgr.h"
#include "../h/net_slot.h"
#include "../h/dlg_dlg.h"
#include "../h/init.h"
#include "../h/mapmsg.h"
#include "../h/menu_dlg.h"
#include "../h/pause.h"
#include "../h/CUnitSpells.h"
#include "../h/CThingy.h"
#include "../h/CBullet.h"
#include "../h/COrder.h"
#include "../h/CImage.h"
#include "../h/gluRdy.h"
#include "../h/gluScore.h"
#include "../h/victory.h"
#include "../h/Storm.h"
#include "../h/net_misc.h"
#include "../h/sai.h"
#include "../h/SAI_Main.h"
#include "../h/saveload.h"
#include "../h/status.h"
#include "../h/TrigAct.h"
#include "../h/uiSingle.h"
#include "../gen/globals117.h"
#include "../port/tools/port_bench.h"  // Hyperion: PortBenchReplayStarts
#include "../port/gameplay/port_watchgame.h"  // Hyperion: PortWatchReplayStarts
#include "../port/data/port_map.h"  // Hyperion: PortMapColorsApply
#include "../port/replays/port_observe.h"  // Hyperion: PortObserveSeed
#include "../port/render/port_teamcolor.h"  // Hyperion: PortTeamColorsBegin, PortTeamColorsApply
#include "../h/port_hxreplay.h"  // Hyperion: Hyperion's replays
#include "../h/port_hxsave.h"    // Hyperion: Hyperion's saves
#include "../h/hxrules.h"        // Hyperion: RulesForGame, HxRules
#include "../h/sai_path.h"       // Hyperion: SaiPathPoolForGame
#include "../port/gameplay/port_teams.h"   // Hyperion: TeamsSeat, TeamsAlliances
#include "../port/gameplay/port_ai.h"      // Hyperion: AiSeat
#include "../port/data/port_moddata.h"  // Hyperion: ModRacesForGame
#include "../port/gameplay/port_score.h"  // Hyperion: PortScoreGameStart
#include "../port/tools/port_crashnotes.h"  // Hyperion: PortCrashNotesGameStart
#include "../port/tools/port_crash.h"  // Hyperion: PortCrashTest
#include "../h/img.h"  // Hyperion: _img_list
#include "../h/playervision.h"  // Hyperion: the replay viewer past slot 7 (stage D)
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX, SELECTION_HUMANS

// The CRT entry points this TU reaches, spelled the way the rest of the tree
// spells them rather than by putting <string.h> on the include path.
extern "C" {
void *__cdecl memcpy(void *, const void *, size_t);
void *__cdecl memset(void *, int, size_t);
// rand and srand: OcRand and OcSrand (h/platform.h)
void __cdecl OcSrand(unsigned int nSeed);
__int64 __cdecl _time64(__int64 *pTime);
}

// Storm.dll ordinal 142, through the thunk at 0x4D008E: __stdcall, with the
// path, the game id and league_get_game_result's value.
// Hyperion: BOOL, as port_stormnet.cpp defines it
extern "C" BOOL STORMAPI SNetSendReplayPath(const char *pszPath, DWORD dwGameId,
                                            int nResult);

// ReadGroupFile is the loader callback dlg_load_theme takes in EDX, spelled as
// src117/lang/glues.cpp spells it.
struct GROUP;
GROUP *__fastcall ReadGroupFile(const char *pszFile, intptr_t nFlags,
                                const char *pszTag, int nLine);

// Globals this TU reaches, dual-spelled, one #ifdef per name. The names keep the
// spellings the rest of the tree uses (placebox.cpp's dword_526490, filters.cpp's
// gbMsgFilterMode).

#ifdef OC_DLL_MODE
  #ifndef byte_5264A0
  #define byte_5264A0 (reinterpret_cast<BYTE *>(OC_VA(0x5264A0u)))
  #endif
  #ifndef byte_5264C2
  #define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
  #endif
  #ifndef dword_526490
  #define dword_526490 (*reinterpret_cast<DWORD *>(OC_VA(0x526490u)))
  #endif
  #ifndef gdwLocalBlackMask
  #define gdwLocalBlackMask (*reinterpret_cast<DWORD *>(OC_VA(0x67266Cu)))
  #endif
  #ifndef gbMsgFilterMode
  #define gbMsgFilterMode (*reinterpret_cast<BYTE *>(OC_VA(0x529040u)))
  #endif
#else
extern BYTE  byte_5264A0[];      // 0x5264A0
extern BYTE  byte_5264C2;        // 0x5264C2
extern DWORD dword_526490;       // 0x526490
extern DWORD gdwLocalBlackMask;  // 0x67266C
extern BYTE  gbMsgFilterMode;    // 0x529040
#endif

// byte_5264C3_g is uiSingle.cpp's spelling of 0x5264C3. 0x526546 and 0x526547
// both feed a race argument and may be one nine-byte array; the code walks eight
// bytes from 0x526547.

#ifdef OC_DLL_MODE
  #ifndef byte_5264C3_g
  #define byte_5264C3_g (*reinterpret_cast<BYTE *>(OC_VA(0x5264C3u)))
  #endif
  #ifndef byte_526546_g
  #define byte_526546_g (*reinterpret_cast<BYTE *>(OC_VA(0x526546u)))
  #endif
  #ifndef byte_526547_g
  #define byte_526547_g (reinterpret_cast<BYTE *>(OC_VA(0x526547u)))
  #endif
#else
extern BYTE  byte_5264C3_g;      // 0x5264C3
extern BYTE  byte_526546_g;      // 0x526546
extern BYTE  byte_526547_g[8];   // 0x526547
#endif

// byte_52701C_g is the map path sub_443C70 and sub_443970 copy from and new_game
// hands to LoadReplayData; it holds at least the 0x104 bytes they copy.
// sgbRestartGame_g is the restart request. gGameHeader is spelled as
// src117/lang/gametype.cpp spells it.

#ifdef OC_DLL_MODE
  #ifndef byte_52701C_g
  #define byte_52701C_g (reinterpret_cast<char *>(OC_VA(0x52701Cu)))
  #endif
  #ifndef sgbRestartGame_g
  #define sgbRestartGame_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD2Cu)))
  #endif
#else
extern char  byte_52701C_g[0x104] OC_UNALIGNED;  // 0x52701C
extern DWORD sgbRestartGame_g;      // 0x66FD2C
#endif
#ifdef OC_DLL_MODE
  #ifndef gGameHeader
  #define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
  #endif
#else
extern GameHeader gGameHeader;      // 0x6728D0
#endif

// The eight 36-byte player slots at 0x6726D0, src117/lang/gluChatSlot.cpp's
// definition unchanged, so the two spellings stay one type. sub_443C70 saves and
// restores the byte at +0x01, and sub_443970 tests +0x00 against 6.
struct netslot_t {
  /*0x00*/ BYTE bType;
  /*0x01*/ BYTE _unmapped01[1];
  /*0x02*/ BYTE bTeam;
  /*0x03*/ BYTE _unmapped03[0x24 - 0x03];
};
static_assert(sizeof(netslot_t) == 0x24, "netslot_t: GetNumTeamOccupiedSlots walks 0x6727F0-0x24 down to 0x6726D0 in 0x24 steps");
static_assert(offsetof(netslot_t, bType) == 0x00, "netslot_t::bType sub_443970: cmp byte ptr [0x6726D0], 6h");
static_assert(offsetof(netslot_t, bTeam) == 0x02, "netslot_t::bTeam sub_4517D0: mov cl, byte ptr [eax*4+6726D2h]");

#ifdef OC_DLL_MODE
  #ifndef sgNetSlots
  #define sgNetSlots (*reinterpret_cast<netslot_t(*)[8]>(OC_VA(0x6726D0u)))
  #endif
#else
extern netslot_t sgNetSlots[8];     // 0x6726D0
#endif

// game_init_theme's globals. byte_5264C2 indexes both four-byte campaign start
// tables. sgnLastMusic_g is signed (a -1 probe and a signed wrap test).
// word_526524_g is src117/lang/gluBrfAct.cpp's spelling.

#ifdef OC_DLL_MODE
  #ifndef sgCmpgnStarts_g
  #define sgCmpgnStarts_g (reinterpret_cast<BYTE *>(OC_VA(0x50334Cu)))
  #endif
  #ifndef sgXCmpgnStarts_g
  #define sgXCmpgnStarts_g (reinterpret_cast<BYTE *>(OC_VA(0x503350u)))
  #endif
  #ifndef sgnLastMusic_g
  #define sgnLastMusic_g (*reinterpret_cast<int *>(OC_VA(0x503354u)))
  #endif
  #ifndef word_5264BE_g
  #define word_5264BE_g (*reinterpret_cast<WORD *>(OC_VA(0x5264BEu)))
  #endif
  #ifndef word_526524_g
  #define word_526524_g (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
  #endif
  #ifndef sgpCurrConsoleDlgGrp_g
  #define sgpCurrConsoleDlgGrp_g (*reinterpret_cast<int *>(OC_VA(0x6662B4u)))
  #endif
#else
extern BYTE  sgCmpgnStarts_g[4];      // 0x50334C
extern BYTE  sgXCmpgnStarts_g[4];     // 0x503350
extern int   sgnLastMusic_g;          // 0x503354
extern WORD  word_5264BE_g;           // 0x5264BE
extern WORD  word_526524_g;           // 0x526524
extern intptr_t   sgpCurrConsoleDlgGrp_g;  // 0x6662B4
#endif

// The .rdata strings game_init_theme passes, read in place in 1.17's data;
// 0x4EC3E0, "game.cpp", is this TU's owner tag.

#ifdef OC_DLL_MODE
  #ifndef sgszGameCpp
  #define sgszGameCpp (reinterpret_cast<const char *>(OC_VA(0x4EC3E0u)))
  #endif
  #ifndef sgszZergGrp
  #define sgszZergGrp (reinterpret_cast<const char *>(OC_VA(0x4EC3ECu)))
  #endif
  #ifndef sgszTerranGrp
  #define sgszTerranGrp (reinterpret_cast<const char *>(OC_VA(0x4EC3FCu)))
  #endif
  #ifndef sgszProtossGrp
  #define sgszProtossGrp (reinterpret_cast<const char *>(OC_VA(0x4EC40Cu)))
  #endif
#else
extern const char sgszGameCpp[1];     // 0x4EC3E0  "game.cpp"
extern const char sgszZergGrp[1];     // 0x4EC3EC  "dlgs\zerg.grp"
extern const char sgszTerranGrp[1];   // 0x4EC3FC  "dlgs	erran.grp"
extern const char sgszProtossGrp[1];  // 0x4EC40C  "dlgs\protoss.grp"
#endif

// free_game's globals. gbWasDemoMode_g is src117/lang/gluScore.cpp's spelling
// and type. The three 0x6D0B.. bytes beside byte_6D0BF5 are written zero here and
// nowhere else in reach. gszLeagueName_g is a string.

#ifdef OC_DLL_MODE
  #ifndef sgszLastReplay
  #define sgszLastReplay (reinterpret_cast<const char *>(OC_VA(0x4EC3D4u)))
  #endif
  #ifndef gbWasDemoMode_g
  #define gbWasDemoMode_g (*reinterpret_cast<int *>(OC_VA(0x66B414u)))
  #endif
  #ifndef gdwGameId_g
  #define gdwGameId_g (*reinterpret_cast<DWORD *>(OC_VA(0x66B418u)))
  #endif
  #ifndef gszLeagueName_g
  #define gszLeagueName_g (reinterpret_cast<char *>(OC_VA(0x66D0F8u)))
  #endif
#else
extern const char sgszLastReplay[1];  // 0x4EC3D4  "LastReplay"
extern int   gbWasDemoMode_g;         // 0x66B414
extern DWORD gdwGameId_g;             // 0x66B418
extern char  gszLeagueName_g[1];      // 0x66D0F8
#endif

// dword_66A278 is the pending-restart request, spelled as gluCmpgn.cpp spells
// it; byte_527120 is uiSingle.cpp's map name. dword_526524_g is a second, 32-bit
// view of word_526524_g's address: game_init_theme reads it as a WORD and
// sub_443970 as a DWORD.

#ifdef OC_DLL_MODE
  #ifndef dword_526524_g
  #define dword_526524_g (*reinterpret_cast<DWORD *>(OC_VA(0x526524u)))
  #endif
  #ifndef dword_66A278
  #define dword_66A278 (*reinterpret_cast<DWORD *>(OC_VA(0x66A278u)))
  #endif
  #ifndef byte_527120
  #define byte_527120 (reinterpret_cast<char *>(OC_VA(0x527120u)))
  #endif
#else
extern DWORD dword_526524_g;          // 0x526524, the 32-bit view
extern DWORD dword_66A278;            // 0x66A278
extern char  byte_527120[0x20];       // 0x527120
#endif

// new_game's globals, spelled as src117/lang/replay.cpp (oc_dwReplayFrame,
// sgpMap) and CUnitCloak.cpp (dword_5264FC) spell them. dword_526520 is the
// elapsed time a loaded game restores.

#ifdef OC_DLL_MODE
  #ifndef oc_dwReplayFrame
  #define oc_dwReplayFrame (*reinterpret_cast<unsigned int *>(OC_VA(0x52651Cu)))
  #endif
  #ifndef dword_526520
  #define dword_526520 (*reinterpret_cast<DWORD *>(OC_VA(0x526520u)))
  #endif
  #ifndef dword_5264FC
  #define dword_5264FC (reinterpret_cast<DWORD *>(OC_VA(0x5264FCu)))
  #endif
  #ifndef sgpMap
  #define sgpMap (*reinterpret_cast<void **>(OC_VA(0x67CC78u)))
  #endif
#else
extern unsigned int oc_dwReplayFrame;  // 0x52651C
extern DWORD dword_526520;             // 0x526520
extern DWORD dword_5264FC[8] OC_UNALIGNED;          // 0x5264FC
extern void *sgpMap;                   // 0x67CC78
#endif

// The three init bodies' globals. The minimap palette is 1024 bytes, 0x100
// dwords. The scroll pair is game_evt.cpp's spelling. 0x5264B0 is written here
// as one dword of -1 and read elsewhere as the WORDs gwScreenTileX_g and
// gwScreenTileY_g, so this TU spells the 32-bit view.

#ifdef OC_DLL_MODE
  #ifndef gwLastScrollMtxX
  #define gwLastScrollMtxX (*reinterpret_cast<WORD *>(OC_VA(0x543208u)))
  #endif
  #ifndef glLastScrollPixelX
  #define glLastScrollPixelX (*reinterpret_cast<DWORD *>(OC_VA(0x543210u)))
  #endif
  #ifndef glLastScrollPixelY
  #define glLastScrollPixelY (*reinterpret_cast<DWORD *>(OC_VA(0x543214u)))
  #endif
  #ifndef dword_5264B0
  #define dword_5264B0 (*reinterpret_cast<DWORD *>(OC_VA(0x5264B0u)))
  #endif
  #ifndef sgMinimapPalette_g
  #define sgMinimapPalette_g (reinterpret_cast<BYTE *>(OC_VA(0x671A90u)))
  #endif
  #ifndef sgbErrObstructions_g
  #define sgbErrObstructions_g (*reinterpret_cast<DWORD *>(OC_VA(0x69B580u)))
  #endif
#else
extern WORD  gwLastScrollMtxX;         // 0x543208
extern DWORD glLastScrollPixelX;       // 0x543210
extern DWORD glLastScrollPixelY;       // 0x543214
extern DWORD dword_5264B0;             // 0x5264B0
extern BYTE  sgMinimapPalette_g[1024]; // 0x671A90
extern DWORD sgbErrObstructions_g;     // 0x69B580
#endif
#ifdef OC_DLL_MODE
  #ifndef gwLastScrollMtxY
  #define gwLastScrollMtxY (*reinterpret_cast<WORD *>(OC_VA(0x54320Cu)))
  #endif
#else
extern WORD  gwLastScrollMtxY;         // 0x54320C
#endif

// Ten sites below call 0x4BE950 with no arguments, which net_cmds.h declares as
// `void __fastcall voice_squelch(DWORD, BOOL)`: the call goes through a nullary
// pointer cast, so no argument setup is emitted.
#define OC_VOICE_SQUELCH() reinterpret_cast<void (__fastcall *)()>(&voice_squelch)()

// free_game passes free_map_strs a byte in CL, and new_game passes
// replay_get_selectable_races a second argument in EDX, which their declarations
// leave out; the same cast keeps that register setup.
#define OC_FREE_MAP_STRS(b) reinterpret_cast<void (__fastcall *)(BYTE)>(&free_map_strs)(b)
#define OC_SUB_480680(p, n) reinterpret_cast<void (__fastcall *)(void *, int)>(&replay_get_selectable_races)(p, n)

// The bodies, in oracle VA order.

// Load the console art for the local player's race and pick the track the
// session starts on. Race 3 keeps whatever theme index is already set and skips
// the stores. Outside a campaign the track advances from the last one played,
// seeded at random the first time; inside one it is the mission's offset from
// the campaign's first mission, modulo three.
void game_init_theme()
{
    gamesnd_reset(1);
    switch (byte_5264C2) {
    case 0:
        sgpCurrConsoleDlgGrp_g = dlg_load_theme(reinterpret_cast<intptr_t>(sgszZergGrp),reinterpret_cast<intptr_t>(ReadGroupFile),reinterpret_cast<intptr_t>(sgszGameCpp), 0xB0);
        word_5264BE_g = 1;
        break;
    case 1:
        sgpCurrConsoleDlgGrp_g = dlg_load_theme(reinterpret_cast<intptr_t>(sgszTerranGrp),reinterpret_cast<intptr_t>(ReadGroupFile),reinterpret_cast<intptr_t>(sgszGameCpp), 0xB5);
        word_5264BE_g = 4;
        break;
    case 2:
        sgpCurrConsoleDlgGrp_g = dlg_load_theme(reinterpret_cast<intptr_t>(sgszProtossGrp),reinterpret_cast<intptr_t>(ReadGroupFile),reinterpret_cast<intptr_t>(sgszGameCpp), 0xBA);
        word_5264BE_g = 7;
        break;
    }

    if (word_526524_g == 0) {
        int nMusic = sgnLastMusic_g;
        if (nMusic == -1)
            nMusic = OcRand() % 3;
        else
            OcRand();  // Hyperion: a session's later games draw as its first did, so their creep's tiles, drawn next, are those their replays draw in a process of their own
        nMusic++;
        if (nMusic >= 3)
            nMusic = 0;
        word_5264BE_g = word_5264BE_g + nMusic;
        sgnLastMusic_g = nMusic;
    } else {
        BYTE bStart;
        if (word_526524_g <= 0x1F)
            bStart = sgCmpgnStarts_g[byte_5264C2];
        else
            bStart = sgXCmpgnStarts_g[byte_5264C2];
        word_5264BE_g = word_5264BE_g + (word_526524_g - bStart) % 3;
    }
}

// Build everything a session simulates: the map, the five object pools, the
// palette pens and the AI. A loaded game skips the counters, the map data and
// the AI seed, because the save file supplies all three.
BOOL game_init()
{
    RulesForGame();  // Hyperion: the rules this game plays by
    EudGameStart();  // Hyperion: an EUD map's state
    TeamsAlliances();  // Hyperion: a game with chosen teams allied by team (port_teams.h)
    ModRacesForGame();  // Hyperion: and the slots' races, a mod's
    AiForGame();  // Hyperion: and the computers' levels (port_ai.h)
    HandicapForGame();  // Hyperion: and the players' handicaps (port_handicap.h)
    MutatorsForGame();  // Hyperion: and its mutators (port_mutators.h)
    if (gpLoadGameFile == 0)
      gametype_mutator_resources();  // Hyperion: and their start resources, which game_ui_init set before them
    ReplayHxNoteGame();  // Hyperion: whether its replay is Hyperion's, for a room's feed as it goes
    PoolsForGame();  // Hyperion: the pools' sizes for this game
    SaiPathPoolForGame();  // Hyperion: and its paths (revision 15)
    map_msg_init();
    sai_init();
    gamemap_init();
    CImage::InitImages();
    CSprite::InitSprites();
    CThingy::InitThingies();
    CFlingy::InitFlingies();
    CBullet::InitBullets();
    COrder::InitOrders();
    init_color_cycles();
    init_pens(sgGamePalette_g, CPalette::find_closest_palette_entry);
    opts_init();
    if (gpLoadGameFile == 0)
        count_init();
    cunit_protoss_init();
    maphdr_data_init();
    CUnit::InitUnits();
    init_unit_threat_scores();
    CSprite::InitHash();
    if (gpLoadGameFile == 0 && !LoadMapData()) {
        if (sgbErrObstructions_g == 0)
            err_popup(0x61, 0, 0, 0, 1);
        return FALSE;
    }
    PortLooksGameBegin();  // Hyperion: the looks given by hand went with the last game
    MutatorsMapLoaded();  // Hyperion: the mutators' times, tech and fog over the map just loaded
    // Hyperion: the map's pixel size and box for the map just loaded, before the
    // starting units are placed (PlaceOverlord sides the overlord by them). In 1.17 the
    // map's preview had set them for the same map; a game Hyperion starts without one (the
    // command line, the live channel, the front end, a session's later games) found the
    // last game's or none, so its replay, played where the preview ran, went another way.
    cell_init();
    PlaceGameStart();
    if (gpLoadGameFile == 0)
        sai_main_init();
    return TRUE;
}

// game_init for the map-preview minimap: the same pools, no AI and no palette
// pens, with the game palette saved across minimap_init and put back after.
BOOL game_minimap_init()
{
    RulesForGame();  // Hyperion: the rules of the game being set up, so its map reads a mod's types as the game will
    memcpy(sgMinimapPalette_g, sgGamePalette_g, 0x400);
    PoolsForGame();  // Hyperion: the pools' sizes for this map
    cell_init();
    gamemap_init();
    CImage::InitImages();
    CSprite::InitSprites();
    CThingy::InitThingies();
    CFlingy::InitFlingies();
    CBullet::InitBullets();
    COrder::InitOrders();
    if (gpLoadGameFile == 0)
        count_init();
    cunit_protoss_init();
    maphdr_data_init();
    CUnit::InitUnits();
    CSprite::InitHash();
    if (gpLoadGameFile == 0 && !LoadMapData()) {
        if (sgbErrObstructions_g == 0)
            err_popup(0x61, 0, 0, 0, 1);
        return FALSE;
    }
    minimap_init();
    memcpy(sgGamePalette_g, sgMinimapPalette_g, 0x400);
    return TRUE;
}

// Bring up the selection, the console and the saved-game state the session
// starts from, then hand the local player's twelve selection slots to the save
// system and clear the whole 96-slot array.
BOOL sub_443900()
{
    select_init();
    update_unit_sel_colors();
    if (gpLoadGameFile != 0) {
        if (!saveload_read_save_data()) {
            err_popup(0x62, 0, 0, 0, 1);
            return FALSE;
        }
    }
    status_init();
    sub_445460();
    sub_445470();
    save_select(SelectedRow(g_LocalHumanID));  // Hyperion: selectlimits.h's (stage S)
    // Hyperion: a Hyperion game keeps the selections its save holds, which commands
    // act on; 1.17 clears them, and RestoreSelection sends the local player's again, which
    // the game seats in the reverse order (stage D, saveload.cpp)
    if (!HxRules())
      memset(SelectedRow(0), 0, SELECTION_HUMANS * SelectionMax() * sizeof(CUnit *));  // Hyperion: 0x180 in 1.17
    return TRUE;
}

// Build the single-player game header a restart needs and hand it to the game
// creator. Outside a campaign the map comes from the last path played and every
// slot set to race 6 past the first is knocked down to 5; inside one the map is
// resolved from the mission number.
int sub_443970()
{
    char szPath[0x104];
    char szMap[0x20];
    GameHeader hdr;
    BYTE *pHdr = reinterpret_cast<BYTE *>(&hdr);
    TGAMEPARAMS *pParams;
    BYTE bOpt;
    int nRes;

    if (dword_66A278 == 0)
        return 1;
    dword_66A278 = 0;
    if (gLocalPlayerName[0] == 0)
        SStrCopy(gLocalPlayerName, error_get_str(0x48), 0x19);

    if (static_cast<WORD>(dword_526524_g) == 0) {
        SStrCopy(szPath, byte_52701C_g, 0x104);
        if (!PreloadMap(szPath, szMap, 0)) {
            err_popup(0x61, 0, 0, 0, 1);
            return 0;
        }
        int nRandom = 0;
        for (int i = 0; i < 8; i++) {
            if (sgNetSlots[i].bType == 6) {
                nRandom++;
                if (nRandom != 1)
                    sgNetSlots[i].bType = 5;
            }
        }
    } else if (!PreloadCampaignMap(static_cast<WORD>(dword_526524_g), szMap)) {
        err_popup(0x61, 0, 0, 0, 1);
        return 0;
    }

    memset(&hdr, 0, sizeof(hdr));
    SStrCopy(reinterpret_cast<char *>(pHdr + 4), gLocalPlayerName, 0x18);
    SStrCopy(reinterpret_cast<char *>(pHdr + 0x4D), byte_527120, 0x20);
    bOpt = static_cast<BYTE>(gOptions);
    pHdr[0x24] = 1;
    pHdr[0x25] = 1;
    pHdr[0x26] = bOpt;

    pParams = gametype_load_ums();
    if (pParams == NULL) {
        err_popup(0x66, 0, 0, 0, 1);
        return 0;
    }
    *reinterpret_cast<TGAMEPARAMS *>(pHdr + 0x6D) = *pParams;
    SMemFree(pParams, sgszGameCpp, 0x159, 0);

    if (!seat_custom_player()) {
        err_popup(0x61, 0, 0, 0, 1);
        return 0;
    }
    ResetGameCreator();
    nRes = CreateGame_SinglePlayer(reinterpret_cast<intptr_t>(&hdr));
    OC_VOICE_SQUELCH();
    return nRes;
}

// Seed the single-player race choices. Nothing happens in a multiplayer game or
// in a scenario, and slot value 7 means the slot has no race to seed.
BOOL SinglePlayerMeleeInitGame()
{
    if (gbMultiPlayerMode != 0)
        return FALSE;
    if (byte_5264C3_g != 0 && !IsScenarioGame(0)) {
        PlayersSeatsBegin();  // Hyperion: which computer is which row
        reopen_computer_slots();
        set_single_race(byte_526546_g);
        for (int i = 0; i < 8; i++) {
            BYTE bRace = byte_526547_g[i];
            if (bRace != 7)
                seat_computer_player(bRace);
        }
        // Hyperion: the computers past 7 the command line asked for
        for (int i = 0; g_hxPlayers && i < PlayersExtraComputers(); i++)
            seat_computer_player(PlayersExtraComputerRace(i));
        TeamsSeat();  // Hyperion: the chosen teams, a seated player each (port_teams.h)
        AiSeat();     // Hyperion: the chosen computer profiles, the same way (port_ai.h)
    }
    return TRUE;
}

// Publish the local player's identity into the active-player triple, build the
// vision masks from its nation id, and pick the message filter the session
// starts on. A replay takes its filter from the slot being watched; a live game
// takes the local player's, and the team-game query is only asked then.
void game_slots_init()
{
    get_num_active_players();

    g_ActiveHumanID = g_LocalHumanID;
    g_ActiveStormID = g_LocalStormID;
    DWORD dwNation = g_LocalNationID;
    DWORD dwMask = 1u << dwNation;
    gdwLocalMask = dwMask;
    g_ActiveNationID = dwNation;
    gdwLocalBlackMask = dwMask << 8;

    if (gpLoadGameFile == 0) {
        if (gPlayback != 0) {
            byte_5264C2 = 1;
        } else {
            byte_5264C2 = byte_5264A0[dword_526490];
            IsTeamGame();
        }
        update_message_filter();

        if (IsTVBGame()) {
            gbMsgFilterMode = 9;
            if (gPlayback == 0)
                return;
        }
        gbMsgFilterMode = 8;
    }
}

// Restart the current map. The local slot's race byte is saved across the reload
// because PreloadMap rewrites the slot table from the map file. The two failures
// are separate arms: one `||` puts the error block after the success block.
int sub_443C70()
{
    char szMap[0x104];
    char szSlot[0x20];

    if (sgbRestartGame_g == 0)
        return 1;

    BYTE bRace = sgNetSlots[g_LocalNationID]._unmapped01[0];
    sgbRestartGame_g = 0;
    SStrCopy(szMap, byte_52701C_g, 0x104);
    if (!PreloadMap(szMap, szSlot, 1)) {
        err_popup(0x61, 0, 0, 0, 1);
        return 0;
    } else if (!seat_custom_player()) {
        err_popup(0x61, 0, 0, 0, 1);
        return 0;
    }
    sgNetSlots[g_LocalNationID]._unmapped01[0] = bRace;
    ResetGameCreator();
    int nGame = CreateGame_SinglePlayer(reinterpret_cast<intptr_t>(&gGameHeader));
    OC_VOICE_SQUELCH();
    return nGame;
}

// Rebuild the game from a save file. Nothing to do unless a save is pending.
int sub_443D20()
{
    if (gpLoadGameFile == 0)
        return 1;
    if (!sub_499C50() || !seat_custom_player()) {
        err_popup(0x61, 0, 0, 0, 1);
        return 0;
    }
    ResetGameCreator();
    int nGame = CreateGame_SinglePlayer(reinterpret_cast<intptr_t>(&gGameHeader));
    OC_VOICE_SQUELCH();
    return nGame;
}

// Release the per-session pools a restart rebuilds. The minimap survives, so
// this is free_game minus the network, sound and replay teardown.
BOOL free_game_minimap()
{
    gamemap_free();
    sai_path_free();
    CUnit::FreeUnits();
    OC_VOICE_SQUELCH();
    CFlingy::FreeFlingies();
    CSprite::FreeSprites();
    OC_VOICE_SQUELCH();
    free_juice_matrix();
    minimap_deactivate();
    return TRUE;
}

// Tear the session down. The network half goes first, then every pool the game
// owns, and last the replay: a playback session frees the map it was reading,
// a live one writes the replay out and reports the result to the league.
void free_game(int nWriteReplay)
{
    char szReplay[0x104];

    sub_47A7F0();
    if (IsBNetGame())
        net_end_game();
    leave_game(0x40000001);
    if (gwGameMode == 4 && glGluesMode == 0)
        net_init_ids();
    vid_init_cycles();
    modeless_dlg_shutdown(1);
    opts_free();
    byte_6D0BCC = 0;
    byte_6D0BE0 = 0;
    byte_6D0BF4 = 0;
    snd_end_streamed();
    victory_free();
    placebox_free();
    game_event_reset(1);
    status_free();
    gamemap_free();
    OC_VOICE_SQUELCH();
    sai_path_free();
    sai_main_free();
    OC_VOICE_SQUELCH();
    CUnit::FreeUnits();
    OC_VOICE_SQUELCH();
    CFlingy::FreeFlingies();
    CSprite::FreeSprites();
    OC_VOICE_SQUELCH();
    snd_stop();
    stop_digital();
    gamesnd_reset(0);
    init_events();
    free_juice_matrix();
    OC_FREE_MAP_STRS(0);
    ReplayHxNoteGame();  // Hyperion: whether its replay is Hyperion's, before the map closes
    close_map();
    gbWasDemoMode_g = gPlayback;
    if (gPlayback != 0) {
        net_record_stop_playback();
        map_free();
        gPlayback = 0;
    } else {
        if (nWriteReplay != 0) {
            write_replay(sgszLastReplay, 1);
            PortReplayKeep();  // Hyperion: a copy of every game's (Settings)
        }
        if (gszLeagueName_g[0] != 0) {
            league_write_replay(szReplay);
            SNetSendReplayPath(szReplay, gdwGameId_g, (int)league_get_game_result());
        }
    }
    gdwGameId_g = 0;
    // Hyperion: the oracle frees the console theme unconditionally, and
    // crashes when a failed new_game reaches free_game after the previous game's
    // free_game already cleared it (a restart during replay playback). A null
    // theme is skipped; the frames before are unchanged.
    if (sgpCurrConsoleDlgGrp_g != 0)
        dlg_grp_free(reinterpret_cast<dlgtheme_t *>(sgpCurrConsoleDlgGrp_g));
    sgpCurrConsoleDlgGrp_g = 0;
}

// Start a session. The replay path restores its own map and slot records, a
// live single-player game rebuilds them, and the seed is the clock unless a
// replay is dictating it. Everything after the seeding is the fixed bring-up
// order the first frame needs.
BOOL new_game()
{
    DWORD dwBlob[2];

    stop_music();
    DWORD dwPlayback = gPlayback;
    if (dwPlayback != 0) {
        if (sgpMap == 0) {
            LoadReplayData(byte_52701C_g, 0);
            dwPlayback = gPlayback;
        }
        if (dwPlayback != 0) {
            OC_SUB_480680(dwBlob, 8);
            set_selectable_races(dwBlob, 8);
        }
    }
    if (gpLoadGameFile == 0)
        oc_dwReplayFrame = 0;
    if (gbMultiPlayerMode == 0) {
        if (!sub_443970())
            return FALSE;
        if (!sub_443D20())
            return FALSE;
        if (!sub_443C70())
            return FALSE;
        if (!SinglePlayerMeleeInitGame())
            return FALSE;
        if (gPlayback == 0) {
            int nSeed = static_cast<int>(_time64(NULL));
            // Hyperion: the replay harness may pin the seed, so that two
            // runs of a loaded game agree.
            nSeed = PortObserveSeed(nSeed);
            net_set_fixed_seed(nSeed);
            replay_set_seed(nSeed);
        } else {
            net_set_fixed_seed(replay_get_seed());
        }
    }
    dlg_mgr_init();
    snd_set_btn_click(sub_443F20);
    font_init_game();
    OC_VOICE_SQUELCH();
    net_srand(net_get_fixed_seed());
    OcSrand(net_get_fixed_seed());
    sai_path_init();
    if (gpLoadGameFile == 0)
        initPlayerColorMap();
    AssignRandomRaces();
    if (gPlayback != 0)
        replay_assign_player_slots(gPlayerData.a);
    ReplayHxAssignSlots();  // Hyperion: the slots past 12 a Hyperion replay recorded
    PreparePlayerSlots();
    if (gPlayback == 0) {
        if (gpLoadGameFile == 0)
            AssignGameSlots();
        else
            SeatLoadedGameSlots();
    }
    game_slots_init();
    AssignAINames();
    net_srand(net_get_fixed_seed());
    OcSrand(net_get_fixed_seed());
    game_init_theme();
    game_ui_init();
    if (!game_init())
        return FALSE;
    if (gPlayback != 0)
        get_replay_player_colors(dword_5264FC);
    if (gpLoadGameFile == 0) {
        permute_colors_by_slot_map();
        // Hyperion: the colours the map sets in CRGB, over the ones each
        // slot now has (port_map.cpp). Presentation only.
        PortTeamColorsBegin();  // Hyperion: full RGB colours (stage G)
        PortMapColorsApply();
        PlayersColorsApply();  // Hyperion: the slots past 7
        ReplayHxColors();      // Hyperion: as a Hyperion replay recorded them
        PlayersChosenColoursApply();  // Hyperion: the skirmish screen's
        PortTeamColorsApply(); // Hyperion: the command line's --colour (stage G)
    } else {
        // Hyperion: a loaded game's ramps are the save's, and so are its RGB
        // colours (HXRC); the command line's over them (stage G). Presentation only.
        PortTeamColorsBegin();
        HxSaveColorsApply();
        PortTeamColorsApply();
        PlayersChosenColoursApply();  // Hyperion: none, but the set-up's notes go (F10)
    }
    ReplayHxGameStart();  // Hyperion: the state's checks start again
    if (gPlayback == 0 && !sub_469DB0(0, 0) && IsScenarioGame(0)) {
        if (gpLoadGameFile != 0)
            err_popup(0x63, reinterpret_cast<intptr_t>(GetCurrentMap(1)), 0, 0, 1);
        else
            err_popup(0x61, 0, 0, 0, 1);
        return FALSE;
    }
    glCheatBits &= 0x28000200;
    if (!sub_443900())
        return FALSE;
    HxSaveLoaded();  // Hyperion: a loaded Hyperion game's random state and check
    if (!IsALoadedGame() && !gametype_load_triggers()) {
        err_popup(0x5D, 0, 0, 0, 1);
        return FALSE;
    }
    if (!IsALoadedGame())
        clear_game_results();
    sub_461E00();
    PortScoreGameStart();  // Hyperion: the score screen's game, from its start
    PortCrashNotesGameStart();  // Hyperion: the game, for a crash report
    PortCrashTest("game");  // Hyperion: a crash made for a test (HYPERION_TEST_CRASH)
    if (gbMultiPlayerMode == 0)
        net_time_init();
    net_set_elapsed(IsALoadedGame() ? dword_526520 : 0);
    gluscore_set_loaded(IsALoadedGame());
    replay_init_speed();
    if (gPlayback != 0) {
        replay_assign_gamedata(&gGameHeader, gPlayerData.a);
        netmgr_replay_init();
        net_record_start_playback();
        gdwReplayVisionMask = 0xFF;
        gdwLocalMask = 0xFF;
        gdwLocalBlackMask = 0xFF00;
        sgbShowAll_g = 0;
    }
    PortBenchReplayStarts();  // Hyperion: a benchmark's viewer sees the whole map
    PortWatchReplayStarts();  // Hyperion: and a watched skirmish's, at its speed
    ViewHxReset(gPlayback != 0);  // Hyperion: the replay viewer's slots past 7 (stage D)
    if (gPlayback == 0)
        net_record_init();
    return TRUE;
}

// A dialog button handler: only sound 2 clicks. The switch is 1.17's shape; a
// subtraction from dwSound compiles differently.
void __stdcall sub_443F20(dlg_dword_t dwSound)
{
    switch (dwSound) {
    case 2:
        snd_play_btn_click();
        break;
    }
}

// Bring up the in-game interface: the view, the scroll state, and every dialog
// and subsystem the console draws. The tail is the status strings, which every
// path reaches.
void game_ui_init()
{
    cell_init();
    dword_5264B0 = 0xFFFFFFFF;
    gwLastScrollMtxY = 0xFFFF;
    gwLastScrollMtxX = 0xFFFF;
    glLastScrollPixelY = 0xFFFFFFFF;
    glLastScrollPixelX = 0xFFFFFFFF;
    if (gpLoadGameFile == 0) {
        gametype_init_resources();
        group_un_init();
        alliance_init();
        pause_init();
    }
    target_init();
    placebox_init();
    game_event_reset(1);
    victory_init();
    dlg_level_init();
    cunitspells_init();
    CBullet::InitBulletTracking();
    status_load_strings();
}
