"""Writes src117/gen/snapranges.h: the parts of 1.17's two global blocks a trainer snapshot copies
(port/replays/port_snap.cpp).

The .data block (g_data117 from 0x501000) holds no pointers on x64 (every pointer-bearing global
has its storage in g_wide117), so a value restored that was not the game's cannot leave a
pointer dangling. It is copied whole but for the TUs that are the program's and not the game's
(windows, dialogs, sound, the glue screens, the network's transport) and the globals named in
DATA_KEEP, which hold the moment rather than the game: timers, the cursor, the palette, the
speed, the pause, the camera, the replay's mode flags.

The wide block holds pointers, so only the game's TUs are copied (WIDE_TUS), less WIDE_KEEP.

Usage: python tools/snapranges.py  (writes the header; --list prints what each block copies)

The build runs it with --if-stale before compiling: the header is written again only when a
global block's layout moved (a pool grown, a global added), and says so. A header behind the
layout copies the wrong bytes, and a rewind then puts back a game that crashes."""
import bisect
import json
import os
import re
import sys

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ENGINE, 'tools'))
import snapprobe  # noqa: E402  wide_rows

DATA_VA, DATA_END, RDATA = 0x4EB000, 0x6DF000, 0x501000

# .data: whole TUs that are the program's, not the game's
DATA_SKIP_TUS = {
    'BlizzardError.cpp', 'CodeSign.cpp', 'CtxtHelp.cpp', 'DebugHelp.cpp', 'Exception.cpp', 'League.cpp',
    'Logging.cpp', 'StatLB.cpp', 'SystemSurvey.cpp', 'VictDlg.cpp', 'cellcopy.asm', 'cellscrl.cpp',
    'chunk.cpp', 'compress.cpp', 'credits.cpp', 'cur.cpp', 'cursor.cpp', 'dlgFatal.cpp', 'error.cpp',
    'event_Win.cpp', 'fd.cpp', 'fd.h', 'fd_sessn.cpp', 'fd_user.cpp', 'filters.cpp', 'game_evt.cpp',
    'gamesnd.cpp', 'grid.cpp', 'help.cpp', 'idea.cpp', 'inspectorreport.cpp', 'init.cpp', 'main.cpp',
    'mapmsg.cpp', 'mask.cpp', 'message_box.cpp', 'minimap.cpp', 'music.cpp', 'okcancel.cpp',
    'opt_dlgs.cpp', 'pause.cpp', 'placebox.cpp', 'playflic.cpp', 'rle.asm', 'save_dlg.cpp', 'scroll.cpp',
    'sha.cpp', 'signature.cpp', 'snd.cpp', 'snd_dlg.cpp', 'spd_dlg.cpp', 'statbtn.cpp', 'statcmd.cpp',
    'statdata.cpp', 'statdraw.cpp', 'statfluf.cpp', 'statmisc.cpp', 'statport.cpp', 'statres.cpp',
    'statselect.cpp', 'status.cpp', 'statwire.cpp', 'target.cpp', 'textbox.cpp', 'tips.cpp', 'title.cpp',
    'uiSingle.cpp', 'usel.cpp', 'videodlg.cpp', 'hp_float.cpp', 'light.cpp', 'gametype.cpp',
    'net_ban.cpp', 'net_bnet_msg.cpp', 'net_chat_cmds.cpp', 'net_chrm.cpp', 'net_error.cpp',
    'net_glue.cpp', 'net_misc.cpp', 'net_strt.cpp', 'netdesc.cpp', 'netdlg.cpp', 'net_player.cpp',
    'net_slot.cpp', 'net_data.cpp', 'net_mgr.cpp', 'replay.cpp', 'gameloop.cpp',
    'saveload.cpp', 'gamedata.cpp', 'map_load.cpp', 'maphdr.cpp',
}
DATA_SKIP_PREFIXES = ('gds', 'dlg', 'glu', 'arr/')
# .data: the game's own in those TUs
DATA_TAKE = {
    # saveload.cpp names the tech and upgrade tables
    'gbTechAvailSC', 'gbTechResearchedSC', 'gOldTechInProgress', 'gbMaxUpgradeLevelSC', 'gbUpgradeLevelSC',
    'gOldUpgradeInProgress', 'gbTechAvailBW', 'gbTechResearchedBW', 'gbUpgradeLevelBW',
    'gStartLocations',        # gametype.cpp
    'dword_6663E4',           # gameloop.cpp: the fog's countdown
    'sgdwElapsed_g', 'dword_67501C', 'dword_675020',  # net_time.cpp: the game's elapsed time
    'sgbErrObstructions_g',
    # net_mgr.cpp: a replay's feed, the next recorded frame and its turns read ahead
    'dword_508D68', 'dword_672980', 'byte_672BE8', 'byte_672DE8', 'dword_672FE8',
    'dword_672658',  # net_mgr.cpp: the game's turns, which hotkey groups stamp
    'gTechInProgress', 'gUpgradeInProgress',  # statbtn.cpp: research under way, by player
    'gbVictoryState', 'sgdwPlayerStatus_g', 'sgdwLeaveReason_g',  # who won, lost or left
    'byte_5349F8',  # pause.cpp: each player's pauses
    'sgbNetworkTurnTimer',  # gameloop.cpp: the frames to the next network turn
    'sgbCmdQueue_g', 'sgdwBytesInCmdQueue',  # net_mgr.cpp: the local commands not yet sent
}
# .data: the moment, not the game, in the TUs copied
DATA_KEEP = {
    'gwGameMode', 'sgpCurrConsoleDlgGrp_g', 'gwScreenTileX', 'gwScreenTileY', 'gwLastScrollMtxY',
    'sgMinimapPalette_g', 'gbWasDemoMode_g', 'gdwGameId_g', 'sgnLastMusic_g', 'byte_527120',
    'sgSavedSelected_g', 'sgGamePalette_g', 'sgbIsMinimapPreview_g',
    'sgdwYScanBegin_g', 'sgdwYScanEnd_g', 'sgdwCurrentDrawColor',
    'dword_526520',  # the elapsed time a save stamps
}
# wide: the game's TUs
WIDE_TUS = {
    'CBullet.cpp', 'CFlingy.cpp', 'CImage.cpp', 'COrder.cpp', 'CSprite.cpp', 'CThingy.cpp',
    'CUnitBounds.cpp', 'CUnitFind.cpp', 'CUnitInit.cpp', 'CUnitOrder.cpp', 'CUnitProtoss.cpp',
    'CUnitSearches.cpp', 'CUnitSpells.cpp', 'Priority.cpp', 'SAI_Banker.cpp', 'SAI_Build.cpp',
    'SAI_Scripts.cpp', 'TAI_Captain.cpp', 'TAI_GuardPost.cpp', 'choke.cpp', 'game.cpp', 'net_cmds.cpp',
    'sai_LoadSave.cpp', 'sai_PathFindLocal.cpp', 'sai_TownBuilder.cpp', 'sai_path.cpp', 'tile2.cpp',
    'victory.cpp',
    'select.cpp',  # the local selection: a command of the local player's slot in a replay reads it
}
# wide: the moment, or a pointer that must stay the program's, in those TUs
WIDE_KEEP = {
    'sgpCurrConsoleDlgGrp_g', 'gnRClickX', 'gnRClickY', 'gBlackX',
    'gIMAGESArrayDataTbl_g', 'sgUpdateFuncs', 'sgDrawFuncs', 'gpszTilesetNames_g', 'light_g', 'img_grp',
    'sgpScripts', 'sgpImgGroups', 'gORDERSArrayDataTbl_g', 'gUnitsArrayDataTbl', 'gFLINGYArrayDataTbl_g',
    'gpAIScript_g', 'gpBWScript_g', 'sgActionFcns', 'sgConditionFcns',
    # the art's tables, which a mod's push may change during a game (port_moddata.cpp)
    'sgpOverlayGroups', 'sgpShieldOverlayGroups_g', 'sgHpbarGroups_g', 'sgPlacemaskGroups_g',
    # the build placement's preview, the program's
    'sgPlaceboxImages_g', 'sgPlacemaskImages_g',
}
# wide: the .dat tables, which a mod's push may change during a game
WIDE_KEEP_PREFIXES = ('gub', 'guw', 'gx', 'gul', 'gb', 'wpn_', 'order_', 'ord_', 'img_', 'sprite_')
# wide: the pools, whose storage holds the most any game takes (wide_globals.py, GROWN): a
# snapshot copies the slots of the game's count and the extra past it, by the
# pool's name in port_snap.cpp
GROWN_POOLS = {
    'sgUnitsMem': ('UNITS', 0), 'dword_541778': ('UNITS', 0), 'g_find_arr_flag': ('UNITS', 1),
    'g_find_arr_unit': ('UNITS', 1), 'g_pos_sort_x': ('FINDER', 0), 'g_pos_sort_y': ('FINDER', 0),
    'sgSprites': ('SPRITES', 0), 'sgSprPriorities_g': ('SPRITES', 1), 'sgSprPriorityIterator': ('SPRITES', 0),
    'sgImages': ('IMAGES', 0), 'sgBullets': ('BULLETS', 0), 'sgThingies1Arr_g': ('THINGIES', 0),
    'sgThingies2Arr_g': ('THINGIES', 0), 'sgOrders': ('ORDERS', 0),
}
POOL_NAMES = ('UNITS', 'FINDER', 'SPRITES', 'IMAGES', 'BULLETS', 'THINGIES', 'ORDERS')


def data_ranges():
    g = json.load(open(os.path.join(ENGINE, 're', 'globals117.json'), encoding='utf-8'))['globals']
    rows = sorted((int(va, 16), v) for va, v in g.items() if int(va, 16) >= RDATA)
    out, listed = [], []
    for i, (va, v) in enumerate(rows):
        end = rows[i + 1][0] if i + 1 < len(rows) else DATA_END
        tu, name = v.get('tu') or '', v.get('name') or ''
        skip = tu in DATA_SKIP_TUS or tu.startswith(DATA_SKIP_PREFIXES)
        take = (not skip or name in DATA_TAKE) and name not in DATA_KEEP
        if not tu and not name and listed:
            take = listed[-1][4]  # an unnamed row is the rest of the global before it
        listed.append((va, end - va, name or '(unnamed)', tu, take))
        if take:
            out.append((va - DATA_VA, end - va))
    # the start of .data up to the first named global
    if rows and rows[0][0] > RDATA:
        out.insert(0, (RDATA - DATA_VA, rows[0][0] - RDATA))
    return merge(out), listed


def strides():
    """{offset: x64 stride} for each global in g_wide117, from gen/wide117.h's x64 table."""
    src = open(os.path.join(ENGINE, 'src117', 'gen', 'wide117.h'), encoding='utf-8').read()
    x64 = src[src.index('#if defined(_WIN64)'):]
    x64 = x64[x64.index('kWideGlobals[]'):]
    x64 = x64[:x64.index('};')]
    out = {}
    for m in re.finditer(r'\{0x[0-9A-F]+u, 0x[0-9A-F]+u, 0x([0-9A-F]+)u, 0x[0-9A-F]+u, \d+u, 0x[0-9A-F]+u, 0x([0-9A-F]+)u,', x64):
        out[int(m.group(1), 16)] = int(m.group(2), 16)
    return out


def wide_ranges():
    g = json.load(open(os.path.join(ENGINE, 're', 'globals117.json'), encoding='utf-8'))['globals']
    named = {int(va, 16): v for va, v in g.items()}
    stride = strides()
    out, grown, listed = [], [], []
    for off, size, va, name in snapprobe.wide_rows():
        tu = (named.get(va) or {}).get('tu') or ''
        take = tu in WIDE_TUS and name not in WIDE_KEEP and not name.startswith(WIDE_KEEP_PREFIXES)
        listed.append((off, size, name, tu, take))
        if take and name in GROWN_POOLS:
            grown.append((off, size, stride[off], GROWN_POOLS[name][0], GROWN_POOLS[name][1], name))
        elif take:
            out.append((off, size))
    return merge(out), grown, listed


def merge(ranges):
    ranges.sort()
    out = []
    for off, size in ranges:
        if out and off <= out[-1][0] + out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], off + size - out[-1][0]))
        else:
            out.append((off, size))
    return out


def main():
    data, data_list = data_ranges()
    wide, grown, wide_list = wide_ranges()
    if '--list' in sys.argv:
        for va, size, name, tu, take in data_list:
            print('data 0x%06X %7d %-40s %-24s %s' % (va, size, name, tu, 'copy' if take else 'keep'))
        for off, size, name, tu, take in wide_list:
            print('wide +0x%07X %8d %-40s %-24s %s' % (off, size, name, tu, 'copy' if take else 'keep'))
        return
    lines = [
        '// snapranges.h -- written by tools/snapranges.py; do not edit. The parts of 1.17\'s two',
        '// global blocks a trainer snapshot copies (port/replays/port_snap.cpp): offsets and sizes',
        '// in g_data117 (from 0x4EB000) and in g_wide117 (x64 layout).',
        '#pragma once',
        '',
        'struct SnapRange {',
        '  unsigned int off, size;',
        '};',
        '// a pool in g_wide117: its storage, its slot\'s bytes, the pool whose count of slots a',
        '// snapshot copies, and the slots past the count it copies too',
        'enum { %s };' % ', '.join('SNAP_POOL_%s = %d' % (n, i) for i, n in enumerate(POOL_NAMES)),
        'struct SnapGrown {',
        '  unsigned int off, size, stride, pool, extra;',
        '};',
        '',
        'static const SnapRange kSnapData[] = {',
    ]
    lines += ['  {0x%06Xu, 0x%Xu},' % r for r in data]
    lines += ['};', '', '#if defined(_WIN64) || defined(__LP64__)', 'static const SnapRange kSnapWide[] = {']
    lines += ['  {0x%07Xu, 0x%Xu},' % r for r in wide]
    lines += ['};', 'static const SnapGrown kSnapGrown[] = {']
    lines += ['  {0x%07Xu, 0x%Xu, 0x%Xu, SNAP_POOL_%s, %du},  // %s' % r for r in grown]
    lines += ['};', '#endif', '']
    path = os.path.join(ENGINE, 'src117', 'gen', 'snapranges.h')
    text = '\r\n'.join(lines).encode('utf-8')
    if '--if-stale' in sys.argv:
        if os.path.exists(path) and open(path, 'rb').read() == text:
            return
        print('[snapranges] the global blocks\' layout moved: gen/snapranges.h written again')
    open(path, 'wb').write(text)
    print('%s: data %d ranges, %d bytes; wide %d ranges, %d bytes' % (
        path, len(data), sum(r[1] for r in data), len(wide), sum(r[1] for r in wide)))


if __name__ == '__main__':
    main()
