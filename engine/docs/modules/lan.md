# lan: multiplayer on a network and in the browser

A room process that hosts matches, a relay a match that keeps the players in
step, seats that are installed Hyperions, streamed Hyperions on the host or Hyperion itself
running in a browser tab, a live feed of every match for watching, and the page the room serves.
Storm's network layer sits under all of it, so 1.17's own multiplayer code runs unchanged.

## Overview

A match is deterministic lockstep, as 1.17's always was: only commands cross the network, every
seat runs the whole game from the same commands, and nothing in the simulation knows the network
exists. The port replaced Storm's providers with one of its own (`port_stormlan.cpp`, listed
under 1.17's own LAN id `'UDPN'`), so `SNetCreateGame`, `SNetJoinGame`, `SNetSendTurn` and
`SNetReceiveTurns` reach the port's code and 1.17's lobby, countdown and game loop are untouched.

Above that provider sits the room. `Hyperion --room` is a process with no window and no game. It
holds the devices, the matches and their lobbies, a relay for each live match, the host's
settings, the evening's log, and the page browsers join from. A player's Hyperion is a client of
the room over one framed TCP connection, and a client of a match's relay over a second one.

Every part of this is presentation and transport, and the replay suite is the gate: a room's
match records a replay on every seat, and those replays must play back identically, a match by
the classic rules on the original as well. The rules are the host's choice: a match by
Hyperion's rules (since v0.6.8-beta) records Hyperion's replay on every seat, as a skirmish
does.

## Status

What works now:

- the transport: framed TCP, a UDP query and advert, and Storm's LAN provider over it (H0);
- the room (`--room`), its devices, lobbies and relays, and seats joining with `--join` (R1);
- the native screens: Multiplayer, the host's console with its Room, Devices and Settings tabs,
  and a match's lobby as a hosted skirmish set-up (R2);
- the live feed, watching a match a delay behind, and the room's own copy of each match, which
  feeds the console's Wall and tells watchers where the fighting is (R3);
- the page the host serves: join, the lobby with race, Ready and chat, and the live matches (R4);
- streamed seats over WebRTC, playing and watching, H.264 from NVENC or OpenH264 and Opus (R5);
- Hyperion as WebAssembly, a seat playing and watching in a browser tab, with the 1.17 files from
  the room or the player's own folder (W1 to W4).

Planned, in order: R6, the host serving each platform's install package and the 1.17 data (the
join page's install choice is greyed "Not available yet"); then tournaments (the
console's Tournament tab is greyed), and the gates and the record. Online play over the same
transport is parked.

## Processes and ports

| Process or role | How it starts | Ports |
|---|---|---|
| the room | `Hyperion --room [--room-key K] [--room-name N] [--room-password P] [--data D]`, by hand or from Host a Room (`FrontKit::Lan::ServerStart`, which starts this exe again) | listens TCP 6112, UDP 6111, TCP 7117 |
| the host's console | the host's own Hyperion, which joins its room at `127.0.0.1` with the room's key (`ServerPump`); let in as admin only from loopback and with the key | outgoing |
| an installed seat | `Hyperion --join <address>[:<port>]`, or Join a Room on the Multiplayer screen | outgoing, plus UDP for the query |
| a match's relay | one per live match, inside the room process (`Launch`); or inside the creator's Hyperion for a game made on 1.17's own screens, which then listens itself | TCP 6112 of whichever process holds it |
| the room's copy of a match | the room starts this exe again: `--data <data>/lan/copy<n> --join 127.0.0.1:<port> --lan-name "Host copy" --lan-copy <match>:<token>` (`StartCopy`) | outgoing |
| a streamed seat | the room starts this exe again: `--data <data>/lan/seat<n> --join 127.0.0.1:<port> --lan-name <player> --lan-stream <device>:<token>` (`StartStream`) | outgoing, plus WebRTC to the browser |
| a seat in a browser tab | the page runs Hyperion's WebAssembly build with `--join 127.0.0.1:<port> --lan-name <player> --lan-tab <device>:<token>` (`StartTab`, `web/lan/join.js`) | its TCP goes over the room's `/tcp/<port>` WebSocket |
| the page | any browser on the network, at the room's address on 7117 | TCP 7117 |

The game port is 6112 (`LanNet::kGamePort`), or `HYPERION_LAN_PORT`; the UDP query port is always
the game port less one. The page's port is 7117, or `HYPERION_LAN_WEB_PORT`. A second room on one
machine needs its own ports, and is then found by address rather than by the broadcast.

## Key files

| File | What it holds | Key functions |
|---|---|---|
| `src117/port/lan/port_lannet.cpp` | the transport: framed TCP, raw TCP for HTTP, the UDP query and advert, and the one place that touches a socket (Winsock, POSIX or Emscripten) | `Listen`, `ListenOn`, `Accept`, `Connect`, `Send`, `Receive`, `SendRaw`, `TakeRaw`, `Pump`, `Query`, `Answers`, `MarkBridge`, `LocalAddr`, `Wait` |
| `src117/port/lan/port_lanproto.h` | the frame kinds, the advert kinds, the feed kinds, the limits, and the `Writer` / `Reader` / `Quote` helpers every part writes its fields with | `Frame`, `FeedKind`, `Advert`, `kProtocol`, `kPlayers`, `kHashEvery` |
| `src117/port/lan/port_lanrelay.cpp` | one match's relay: the players' turns made into steps, their messages routed, their state hashes compared | `LanRelay::Admit`, `Pump`, `Steps`, `Compare`, `Remove`, `Seats`, `TakeHashes` |
| `src117/port/lan/port_lanroom.cpp` | the room: its state, devices, matches, lobbies, launches, the connections and the main loop | `PortLanRoomMain`, `Sit`, `NewMatch`, `Launch`, `EndMatch`, `PumpMatches`, `PumpDevices`, `Tonight` |
| `src117/port/lan/port_lanroom_proto.cpp` | what the room tells its devices and what they say: the JSON, the hello, the console's commands | `Hello`, `Said`, `Admin`, `Heard`, `RoomJson`, `MatchJson`, `Advertise` |
| `src117/port/lan/port_lanroom_host.cpp` | the host machine's work: the password and tokens, the settings, the files and fonts served, the load, the copies, the streamed seats and tabs | `SettingsLoad`, `ServeGameFiles`, `ReadLoad`, `StartCopy`, `StartStream`, `StartTab`, `PumpWatch`, `PumpStreams` |
| `src117/port/lan/port_lanroom_replay.cpp` | the feeds: each seat's record as it comes, the watchers' delay, the room's replay | `FeedHeard`, `PumpFeed`, `CheckWatchers`, `WriteReplay` |
| `src117/port/lan/port_lanroom_internal.h` | what the four share, in `namespace LanRoom`: `Device`, `Seat`, `Feed`, `Match`, `StreamSeat`, the room's state, the functions one calls in another | |
| `src117/port/lan/port_lanclient.cpp` | a Hyperion's side of the room: the connection, the hello, the room's state as last sent, the rooms the network answers with, and the launch | `LanClientRooms`, `LanClientJoin`, `LanClientPump`, `LanClientSay`, `LanClientSend`, `LanClientTakeLaunch`, `LanClientTakeFeed`, `LanFingerprint`, `LanPlayerName` |
| `src117/port/lan/port_lanlobby.cpp` | a launch played through 1.17's own lobby: the game made or joined, races, computers and Start Game, with nobody clicking, unseen under the start layer | `PortLaunchNetGame`, `PortLanLobbyTick`, `PortLanGameFrame`, `PortLanAfterGame`, `PortLanBackToFront`, `PortLanChatLeft`, `PortLanLobbyUnseen`, `PortLanChatWhy`, `PortLanTakeWhy` |
| `src117/port/lan/port_lanfeed.cpp` | the feed: a seat sending its growing replay record, and a watcher playing it the room's delay behind; the copy's pictures and heat | `LanFeedSeatFrame`, `LanFeedSeatEnd`, `SendRecord`, `LanWatchAsk`, `LanWatchAsCopy`, `LanWatchPump`, `LanWatchLaunch`, `PortLanWatchHold`, `PortLanWatchLive`, `PortLanWatchShown`, `PortLanWatchAhead`, `PortLanWatchHash`; the Live bug a layer over the game (`render.md`) |
| `src117/port/lan/port_lanstream.cpp` | a streamed seat: the WebRTC peer, H.264 by NVENC or OpenH264, Opus from the mixer, and the browser's mouse and keys back into the window's messages | `LanStreamSignal`, `LanStreamPump`, `LanStreamWants`, `LanStreamFrame`, `MakePeer`, `MakeEncoder`, `EncodeLoop`, `SoundTap`, `Input` |
| `src117/port/lan/port_lanweb.cpp` | the page's server: HTTP/1.1 GET and HEAD with Range, the WebSocket at `/ws`, the TCP bridge at `/tcp/<port>`, `/play/` and the files the room serves | `Start`, `Pump`, `Events`, `SendText`, `SendBinary`, `Serve`, `ServeFile`, `PlayOn`, `Get`, `WsRead` |
| `src117/port/lan/port_stormlan.cpp` | Storm's LAN provider: the SNet calls of a networked game, the creator's own relay, the step reader, and the state hash each 120 frames | `LanInitialize`, `LanCreateGame`, `LanEnumGames`, `LanJoinGame`, `LanSendTurn`, `LanReceiveTurns`, `LanGameFrame`, `LanSetLaunch`, `HashRecord`, `Enter`, `ReadStep`, `RelayLost` |
| `src117/port/data/port_stormerr.h` | the error codes the Storm network sets and the relay sends, named for what 1.17 makes of them | `NET_BAD_PROVIDER`, `NET_NO_PLAYER`, `NET_CANNOT_JOIN`, `NET_GAME_FULL` and the rest |
| `src117/port/lan/port_stormnet.cpp` | Storm's network layer: the loopback of one local player for single games and replays, and the switch to the LAN provider (`g_bLan`) | `SNetInitializeProvider`, `SNetEnumProviders`, `SNetSendTurn`, `SNetReceiveTurns`, `PortNetSnapSave`, `PortNetSnapRestore` |
| `src117/port/lan/port_front_lan.cpp` | Multiplayer, a room as a guest sees it, a match's lobby on either side, and the pieces the console shares | `ServerStart`, `ServerStop`, `TakeBackRoom`, `ServerPump`, `NewMatch`, `WatchMatch`, `WatchOff`, `ToConsole`, `ToLobby`, `Ask`, `Toast` |
| `src117/port/lan/port_front_lanhost.cpp`, `port_front_landevices.cpp` | the host's console: the header, the tabs, the Room tab as floor, rack or wall, the join card with its QR code, this machine's load, Settings, and the Devices tab with everyone in the room in one list | `ConsoleOpen`, `ConsoleScreen`, `SettingsJson`, `SettingsRead`, `SettingsSend`, `DevicesTab`, `DeviceCard`, `OpenDevice` |
| `src117/port/lan/port_qr.cpp` | a QR code for the join card's address, byte mode, level L, versions 1 to 5, the best-scoring mask | `QrMatrix` |
| `src117/port/lan/port_instance.cpp` | the single-instance lock, scoped to one data folder, so seats with their own `--data` run side by side | `PortCheckForOtherInstances` |
| `engine/web/lan/`, `engine/web/play/` | the page the room serves (its steps: join, choose, lobby, watch, play, spectate) and the page that runs the WebAssembly build (the files setup, the browser's storage, the canvas) | `connect`, `hello`, `heard`, `tabHeard`, `tabFollow`, `setRoute` in `join.js` |
| `engine/tools/fetch_webrtc.py`, `build_webrtc.py` | the streamed seat's libraries at pinned versions and checksums, and the static x64 Windows build of them | libdatachannel 0.24.6, Mbed TLS 3.6.7, Opus 1.5.2, OpenH264 2.6.0, nv-codec-headers 13.1.15.0 |

## How it works

**Finding a room.** `LanNet::Query` sends the four bytes `HXLQ` to the broadcast address, to
loopback and to each address in `HYPERION_LAN_HOSTS`, twice a second while the Multiplayer screen
is open. A room answers with `HXLA`, the protocol word, `ADVERT_ROOM` and its summary as JSON
(`Advertise`). `LanClientRooms` keeps what was heard in the last five seconds and marks each row
"Plays here", or why not, comparing the version, the simulation revision (`h/hxsim.h`), the active
mod and the game data's fingerprint. A game made on 1.17's own screens advertises `ADVERT_GAME`,
which `LanEnumGames` reads into 1.17's own game list.

The fingerprint (`LanFingerprint`) is a hash of the game's rules as Storm reads them: the eleven
`arr` DATs and the three scripts, from the active mod, 1.17's MPQs or a Remastered install.
It no longer hashes the archives' bytes, so a copy on 1.17's MPQs and one on
a Remastered install play in one room: the two sources give these files byte for byte. The
tilesets are left out, measured: Remastered's `cv5` differ from 1.17's only in groups 1.17 left
empty (91 on platform, 254 on desert, 227 on twilight; the rest equal), and its `vf4` are
1.17's with more appended, so a map 1.17's tiles can draw plays the same on both, and a map
that names the newer tiles is one a copy on 1.17's MPQs refuses.

**Joining.** `LanClientJoin` connects to the room and sends a `ROOM` frame holding
`{"t":"hello", ...}`: the name, machine, os, arch, kind, route, version, sim revision, data
fingerprint, mod and the room's password. `Hello` in the room checks the lot and answers
`welcome` or `refused` with a reason and a fix. Four hellos are special, each carrying a token the
room made: the console's (the `--room-key` key), a match's copy and a streamed seat, all three
taken from loopback only, and a tab seat, which arrives through the page's bridge. A tab seat's
version and sim revision must be the room's: the room serves it from `web/play`, which can be
another build than the room's own when that folder was not updated with it, so a mismatch is
refused there and the page is told (`tabrefused`), and says it over the tab's frame beside Play
Streamed Instead. A match whose relay refuses a player for another version (`LanRelay::Mismatch`)
ends at once, saying whose, rather than after the 90 seconds a start is given. Every
connection, framed or a page's WebSocket, counts as nobody until its hello is let in: before
that it is told only `PublicJson`, the room's name, version and who is in it, and nothing it
says counts. A second hello is ignored. A connection that sends no first frame in ten seconds
is closed (`kGreetingMs`, `LanNet::Greeted`).

**The lobby.** A match is a `Match` in the room with eight seats, each open, closed, a computer or
a player's device. The host's console makes one (`newmatch`), picks the map, type and speed
(`match`), and moves seats (`slot`); a player sits, stands, picks a race and a handicap, says
Ready and chats. The rules are the match's (`rules` in `match` and `newmatch`, `hyperion` or
`classic`), the host's choice on its set-up, where a handicap below 100% forces Hyperion's as in
a skirmish; a computer's handicap is the host's (`handicap` in `slot`), a person's their own
(`handicap`, from their set-up's pulldown or the page's select), and a seat started with
`--handicaps` says its first percent as it joins. The room keeps a device's on the device, so it
follows the person from match to match, and puts each seat's in `RoomJson`. The mutators are the
match's too (`mutators` in `match` and `newmatch`, an object of ten numbers, `MutatorsJson`),
the host's choice in the box its set-up shares with the Room box; a guest's set-up shows them
locked, and the page names them under the match's line.
The room sends its whole state as JSON (`RoomJson`) to every device when something changed and at
least once a second, so every screen and the page draw from one source. The lobby's map file goes
to each installed seat (`SendLobbyMaps`) so its set-up can show the picture. The page's picture
comes from the host, since the room reads no game data: once a map, the console's `HostSync`
draws its minimap (`MapPicturePng`, at most 256 pixels a side, as a PNG) and sends it with its
starts (`mappic`, the PNG in base64, each start the map's player and its place as fractions); the
room serves it at `/maps/<match>.png` and names it in the match's JSON (`pic`, with `?v=` changed
for each picture, and `picStarts`), and `join.js`'s `drawMap` draws it with each start's number in
its seat's colour.

**Starting a match.** `Launch` checks the seats, makes a game id and a token, creates the match's
`LanRelay`, and sends the first person's seat the map as a `ROOM_FILE` and a `launch` naming the
relay, the token, the game, the map, the type, the speed, the rules and who sits where, each
seat with its race and handicap. That seat is the
creator; the others get their `launch` once the relay reports the game made. Each Hyperion then
plays 1.17's own steps itself (`PortLaunchNetGame`): the creator does what Create Game's Ok does
(`PortCreateNetGame`), the others what Join's Ok does over the one game the provider lists, and in
1.17's chat room `PortLanLobbyTick` does what the players would, one lobby command a pass, until
every person is in, the computers are made with their races, the map download is complete and
Start Game goes. A browser's seat that is not in yet holds the launch for up to 30 seconds while
the room starts it (`m.qwSeating`). Every seat takes the launch's rules (`RulesChooseNext`),
its handicaps (`HandicapChooseNet`: people by name, computers in the launch's order, which is
the order the creator seats them) and its mutators (`MutatorsChooseNet`), so each one starts the
same game (`gameplay.md`, "Handicaps" and "Mutators"). The game's replay is noted Hyperion's or 1.17's as it starts (`ReplayHxNoteGame`
in `game_init`), so a seat's feed begins with Hyperion's sections and the room's copy and the
watchers play the match by its rules.

**The start a player sees.** None of that lobby is seen on a seat with the GPU presenter: a match
starts as a skirmish does. The menus fade to black as for Start Game, and from the launch until
the game's first frame the window stays black under the motion's cover (`PortMotionCover`), with
the map's name and who plays over it and the player's clicks and keys held (the start layer in
`port_lanlobby.cpp`). 1.17's chat room, its countdown and the loading run beneath; the countdown
does not beep, and a popup the chat room would show as it closes (a kick, the host gone) becomes
a toast on the home screen instead (`PortLanChatWhy`). The game then comes in with Hyperion's
countdown (`port_intro.cpp`), which beeps 1.17's `glue\countdown.wav` as each digit comes up.
`run_game` drains the network until every player is in, so the seats begin to hold within a round
trip of each other and let go together; the network's messages go on while they hold. A browser's
seat has no GPU presenter, so it shows 1.17's chat room as before, and holds its first frame for
the countdown's length unseen, which keeps it with the others. A match the room ends before it
starts, in the lobby or in its countdown, is left with the chat room's Cancel
(`PortLanLobbyTick`), and the seat is back on the room's page.

**Lockstep through the relay.** Every seat, the creator included, is a connection to the relay;
no player lives inside it. `Admit` reads `HELLO` or `CREATE`, refuses a protocol, version, sim
revision or password that is not the relay's own, gives the creator id 0 (1.17's lobby takes slots,
forces and kicks from player 0 only) and a joiner the lowest free id, and answers `WELCOME` with
the game's data and the step number it starts at. `LanSendTurn` sends one `TURN`; `Steps` builds
the next step once a turn of every player in it is held and sends `STEP` to everyone, naming the
step's players, their turns, and who left before it, which `LanReceiveTurns` hands 1.17 a step at
a time. Twice a second `STATUS` says which players a step waits for and each player's round trip,
which `PING` and `PONG` measure. If the relay goes, `RelayLost` turns the turns in transit into
steps of this player alone and the game plays on. `DROP` is player 0's for anyone, as 1.17's
kick is, and the others' only for a player the steps are waiting on. A seat whose turns run
`kTurnsQueuedMost` ahead of the steps, or that the steps of a game under way have waited on for
two minutes (`kStalledMs`, the host console's "A player drops"), is out as a lost connection.
A player the relay of a room's live match loses (`TakeLost`) whose game is still in the room is
told to leave the match (`end`, with `why` "lost"), as Quit Mission leaves it, and its page why:
otherwise both sides, each playing on without the other, saw the other leave and both won (seen
2026-10-20, a browser's seat on a Linux laptop). How a browser's bridge (`/tcp`) ended is a line
of the room's log (`BridgeNote`: the browser's close and its code, its connection gone without
one, or the game port's end, after how long and how many bytes), and the page reports its own
side (`note`: the WebSocket's close code, whether the tab was hidden or the browser offline, and
the tab hidden or shown during a game).

**1.17's own sync check.** Besides Hyperion's state hash, each turn carries 1.17's sync
command (0x37, `net_sync.cpp`): the units, the resources and the random sequence, the minimap's
counts (`SaveMinimapCounts`) and a sight hash of the sprites' visibility, which a receiver checks
against its own record; a player that fails it is dropped by 1.17 itself (`sub_47A630`, reason
0x40000006, the same as a lost connection), so with two players each side drops the other and
each wins. `DoNetworkTurn` arms the sight hash and 1.17 takes it at the next draw
(`PriorityAddRange`), which 1.17 always makes at the end of that frame; Hyperion may run more
frames before it draws, so `gameloop_update` takes it itself at the end of the frame (since
2026-10-20, after a browser's seat on a slow laptop was dropped each game as its workers began
to move).

**The state check.** Every 120 frames (`kHashEvery`) `LanGameFrame` hashes the game's state
(`port_synchash.cpp`, through `HashRecord`) and sends it as `HASH`. The relay compares each
frame's hashes once every player in the steps has sent one (`Compare`), counts agreements and
differences, and writes the first difference of each player into `hyperion_lan.log` with the parts
that differ (random numbers, units, objects, players, map, sprites, ai: `HashPartsDiffer` in
`port_lanproto.h`, the names from `SyncDomainName`). The room holds those hashes and checks a
watcher's and the copy's against them (`CheckWatchers`).

**The feed and watching.** Each seat sends the room its replay record as it grows
(`port_lanfeed.cpp`): `FEED_START` with the magic, header, map and Hyperion's sections, then
`FEED_RECORD` every eight frames with the blocks since and the frame they are complete to, then
`FEED_END`. A seat's `FEED_START` is taken once a match and only with a replay header of
`kReplayHeader` bytes, the size `WriteReplay` reads. The room keeps every seat's feed, says once in the log if two differ, passes one on to
who watches (another seat's if that player leaves, `FeedOf`), and writes the match's replay into
`maps\replays\<room name>\` when every feed has ended (`WriteReplay`). A watcher turns the
beginning into a replay with no commands, starts it as Replays' Watch does, and appends the blocks
as they come. It plays no further than the room's delay (5 s, 10 s, 30 s or 1 min) behind the
players, holds there (`PortLanWatchHold`), runs to the edge at once with nothing drawn when it
starts or after L for Go Live (`PortLanWatchCatching`), and draws the Live bug, how far behind it
is, and Match Over (its layer over the game, `render.md`). As a match goes live the room starts its own copy, a
watcher with no delay: its hashes are the match's "copy" checks, it sends a 192 by 108 picture of
each player's army in turn for the console's Wall, and it reports where a fight is (`FEED_HEAT`),
which the room passes to watchers so their director can cut to it a couple of seconds before it
reaches their picture (`PortLanWatchAhead`).

**Streamed seats over WebRTC.** For a browser player on route `stream` the room starts a Hyperion
on the host with no window and the player's name. It plays that seat exactly as an installed copy
plays its own, and offers the browser a video track (H.264), a sound track (Opus) and a data
channel, libdatachannel doing the peer and the room passing the two descriptions between them as
`rtc` messages. Gathering finishes before the offer goes and the answer comes complete, so a LAN
needs no trickle and no STUN. The picture is the frame as `port_present.cpp` shows it, made I420
on the game's thread and encoded on a thread of its own at up to 30 frames a second: NVENC where
NVIDIA's driver has it, OpenH264 otherwise (`HYPERION_STREAM_ENCODER=openh264` forces the
latter). Sound is the mixer's, taken after its clamp and encoded in 20 ms frames. The browser's
input comes back as short text lines in game pixels and becomes the window's own messages
(`WinMsgRemoteMouse`, `WinMsgRemoteKey`). Every two seconds the seat tells the room its encoder,
size, frames and bits a second, which the console and the page show.

**Hyperion in the browser as WebAssembly.** Since 2026-10-17 this is the main browser route and
the stream is the fallback. The room serves the WebAssembly build at `/play/` and, while Settings
allow it, its own 1.17 files at `/files/`. On a PC whose browser validates a 64-bit WebAssembly
module, the join page offers Play in This Tab: the page runs `/play/` in a frame behind itself and
starts it with the token the room gave the page, the files coming from the host or from the
player's own folder into the browser's storage. A browser has no UDP and no raw TCP, so
`LanNet::Query` returns at once there and the seat's TCP goes over a WebSocket: `play.js` rewrites
Emscripten's socket URL to the room's bridge at `/tcp/<game port>`, the room joins that to its own
game port (and only that port) and calls `LanNet::MarkBridge` so the connection counts as the
browser's address. The frame comes over the page while the player plays or watches. Your Seat's
How You Play switches a browser between the tab and the stream between matches, where the room
offers both (`status` with its `route`; the room moves the seat). While the player is in a match
the page says, as it starts, what leaving costs, and the browser asks before a refresh, a close
or another address (`beforeunload`): in the tab the game goes with the page, which the match
counts as the seat's forfeit; streamed, the seat plays on unplayed until the player joins again
by the same name. A seat that comes back to a running match is not built (it needs the steps
kept and played through, the open item in the PRD).

## Protocols

- **A framed connection** carries a 32-bit length, a kind byte and the bytes
  (`port_lannet.cpp`), up to 64 MB a frame, which a launch's map needs. A connection accepted
  is held to 64 KB a frame until it is let in (`LanNet::Trust`: the room after a hello it takes,
  the relay after `Admit`, the lab after the worker's token); a raw one, the room's web port, to
  1 MB unread, and `Fill` reads no more until it is taken. A turn past 16 KB (`kTurnMost`) drops
  its seat at the relay, and the turns handed to the game have 2 KB of zeros after them, as
  the classic walker reads a cut-off last command whole. Fields are written by
  `LanProto::Writer` (byte, little-endian word and dword, zero-terminated string, length-prefixed
  bytes) and read by `Reader`, which sets `bBad` rather than reading past the end.
- **The query** is the four bytes `HXLQ` over UDP; the answer is `HXLA`, the protocol word
  (`kProtocol` is 2), an `Advert` kind and its body. `ADVERT_GAME` carries the game id, port,
  players, max players, mode, name, stat string, version and sim revision; `ADVERT_ROOM` carries
  the room's summary as JSON.
- **Relay frames** (`Frame` in `port_lanproto.h`): to the relay `CREATE`, `HELLO`, `TURN`,
  `MESSAGE`, `LEAVE`, `DROP`, `HASH`, `PONG`; from it `WELCOME`, `REFUSE`, `JOINED`, `STEP`,
  `MESSAGE`, `STATUS`, `MODE`, `PING`. A refusal carries Storm's error (`0x85100078` cannot join,
  `0x85100079` full) and a line of why.
- **Room messages** are JSON in a `ROOM` frame, or a WebSocket text frame for a page. From a
  device: `hello`, `race`, `handicap`, `ready`, `chat`, `sit`, `stand`, `status`, `watch`, `unwatch`, `leave`,
  `pong`, `rtc`, `streaming`. From the console as well: `newmatch`, `match`, `slot`, `start`,
  `end`, `drop`, `remove`, `password`, `settings`, `stop`, `run`. From the room: `room` (the whole
  state), `welcome`, `refused`, `launch`, `follow`, `tab`, `made`, `end`, `removed`, `error`,
  `closed`, `ping`, `rtc`. A hello's name is held to 24 bytes with no control characters, quotes
  or backslashes. A page's `welcome` carries `files`, the address of the host's game files for
  it alone (`/files/<token>/`, `LanWeb::AllowFiles`, revoked when it goes), and `back`, a token
  the page keeps in its storage and sends in its next hello: a streamed seat goes back to a
  page that shows it, not to a name. The room's web port refuses a WebSocket or `/tcp/`
  upgrade whose `Origin` is not the address it was asked by (`OriginOk`).
- **`ROOM_FILE`** is a file name and its bytes: the launch's map, and a lobby's map for the set-up
  to show.
- **`ROOM_FEED`** is a `FeedKind` and the match id, then its fields: `FEED_START`, `FEED_RECORD`,
  `FEED_END`, `FEED_HASH` (a watcher's hash at a frame), `FEED_PICTURE` (a copy's army picture,
  three bytes a pixel) and `FEED_HEAT` (where the fighting is, in map pixels, with the heat in
  hundredths).
- **A streamed seat's input channel** takes `m x y` a move; `d b x y`, `u b x y` and `D b x y` a
  button down, up and double click; `k vk 1 c` and `k vk 0` a key down with the character it types
  and up; `q kbps` the quality (once a second at most); `l` the player has left the stream. A
  page's `want` makes a new peer once in two seconds at most, and 4,096 unread lines are the
  most the seat keeps.

## Settings and environment variables

| Name | What it does |
|---|---|
| `HYPERION_LAN_PORT` | the game port in place of 6112; the UDP query port is always one less |
| `HYPERION_LAN_BIND` | bind the room's listeners to one address (`127.0.0.1` for a test on one machine) |
| `HYPERION_LAN_HOSTS` | addresses to query besides the broadcast, separated by commas, semicolons or spaces |
| `HYPERION_LAN_WEB_PORT` | the page's port in place of 7117 |
| `HYPERION_LAN_WEB_ROOT` | where the page's files are, in place of `web/lan` (in the package's `Data\`, or beside the exe) |
| `HYPERION_LAN_PLAY_ROOT` | where the browser build is, in place of `web/play` (in the package's `Data\`, or beside the exe) |
| `HYPERION_LAN_KEY` | the key the console gives the room it starts, so a check without a person can read the room as a second console |
| `HYPERION_LAN_TRACE` | write every turn sent and every step read to `hyperion_lan_steps.log` |
| `HYPERION_STREAM_ENCODER` | `openh264` to skip NVENC |
| `HYPERION_GAME` | the game's folder, which the room's `/files/` read from |
| `HYPERION_WINDOW`, `SDL_AUDIO_DRIVER` | set to `hidden` and `dummy` for the room's own child processes (`ForChild`) |

The host's own settings live in `lanroom.json` in the room's data folder, written whole by the
console: `name`, `password` (on or off), `dir` (the Room tab as floor, rack or wall), `delay`
(the watching delay), `watch` (who may watch), `browserFiles` (whether the host offers its 1.17
files), `copies` (`Off` starts no copy of a match) and `fp` (the host's data fingerprint, which
joiners are checked against). `lan_room.json` beside it is the running room's note, its process
id and console key, so a console restarted in the same data folder takes its room back
(`TakeBackRoom`) instead of starting a second one that could not have the port.

## Testing it

Two or three Hyperions on one machine, each with its own data folder, is the usual check. The
instance lock is per data folder (`port_instance.cpp`), so copies with their own `--data` run side
by side.

```
set HYPERION_LAN_BIND=127.0.0.1    # the broadcast cannot then reach another machine
Hyperion.exe --room --room-key testkey --room-password lan --data C:\hx\room
Hyperion.exe --join 127.0.0.1 --room-password lan --lan-name Bravo --race terran --lan-ready --data C:\hx\bravo
Hyperion.exe --join 127.0.0.1 --room-password lan --lan-name Delta --race zerg --lan-ready --data C:\hx\delta
```

`--lan-watch` has a copy watch each match as it goes live, and `HYPERION_LAN_KEY` lets a script
read the room as a second console and drive it with the room's own JSON. Across machines, leave
`HYPERION_LAN_BIND` unset; `HYPERION_LAN_HOSTS` reaches a host the broadcast does not. A browser
check runs headless Chrome against `http://<room>:7117/`; `?step=`, `?device=` and the play
page's `?args=`, `?env=`, `?offer=` and `?files=` drive it without a person.

Every seat's replay is the gate: play the match, then run the replay suite over what each seat
recorded (`../replay-test.md`). The room's own replay, in `maps\replays\<room name>\`, should
hold the same commands as each seat's.

## Integration points

- `port_stormnet.cpp` hands every SNet call to `port_stormlan.cpp` while `g_bLan` is set;
  `SNetEnumProviders` lists the LAN provider through `LanListProvider`.
- `port_hxreplay.cpp` calls `LanGameFrame` each frame, which sends the hash and calls
  `PortLanGameFrame` in `port_lanlobby.cpp` (the room told the frame, and the match left when the
  host ends it).
- `gameloop.cpp` holds a watched replay at the delay (`PortLanWatchHold`), keeps running without
  focus (`PortLanWatchLive`) and runs flat out while catching up (`PortLanWatchCatching`);
  its layer over the game (`port_overlay.h`) draws the Live bug and takes L; `net_record.cpp` hands
  the last frame over as Hyperion's games do; `port_tv.cpp` reads the copy's fight reports.
- `gluChat.cpp` calls `PortLanLobbyTick` from the chat room's poll, and ends the poll's pass at a
  Cancel the tick pressed, as 1.17 stops polling at its own Cancel; it calls `PortLanChatLeft`
  when the player leaves the chat room, and while the lobby is unseen its countdown does not beep
  and its closing popup goes to `PortLanChatWhy`; `main.cpp` calls `PortLanAfterGame` after the
  game.
- `port_intro.cpp` holds a room's match for its countdown as it holds a skirmish, and takes the
  cover off as it starts.
- `port_present.cpp` pumps the room's client each frame, hands a streamed seat its picture
  (`LanStreamWants`, `LanStreamFrame`) and pumps a tab seat's following; `port_dsound.cpp` gives
  the stream the mixer's samples through `PortSoundTap`; `port_winmsg.cpp` turns the browser's
  input into window messages.
- `port_front.cpp` and `port_front_skirmish.cpp` start a watched match, bring a seat home after
  one, and read the room's state for a hosted lobby; `hyperion/hyperion.cpp` runs
  `PortLanRoomMain` for `--room` before any game data is loaded.
- `tools/build_engine_x64.py` links what `build_webrtc.py` makes, and builds it when missing;
  `tools/play.py` copies `web/lan` beside the program; the packages put it in `Data\web\lan`,
  and the room looks there first (`PortPackagedFile`).

## Gotchas

- The room is polled from one thread. Nothing in `port_lannet.cpp` blocks except `Connect`, which
  waits at most its timeout, and `Wait`, which waits for a socket or a write. Anything added here
  must stay polled.
- A connection is nobody until its hello is let in. Earlier builds counted a WebSocket, and then
  a framed connection, as joined from its first frame, which let it read the room and take a seat
  without the password. `Device::state` starts empty and empty means not joined; keep it that way.
- Everything a connection sends is read as untrusted: a `Reader`'s `bBad` is checked before a
  size is used, a count or a length is bounded before it is kept, and an offset into a feed is
  checked against what the feed holds. The transport holds at most 512 connections and keeps a
  socket past `FD_SETSIZE` out of `select` on Linux.
- The console, a copy and a streamed seat are admitted by token and only from loopback. A tab seat
  comes through the bridge and carries the browser's address instead, which is why `MarkBridge`
  exists.
- A watched match is a replay, so 1.17's record reader needs the last frame handed over, and the
  replay file must hold every block so far because `new_game` may read it again.
- The relay's limits are Storm's: eight players, one creator as player 0, a join refused once the
  game is under way or full.
- A browser has no UDP, no raw TCP and no threads. `Query` is a no-op there, sockets go over the
  bridge, Hyperion's threads are Asyncify fibers, and `select` cannot wait, so the loops ask again
  between the page's turns. Hyperion's own menus need the GPU presenter, which has no browser
  backend, so a tab seat waits for the room's match with no screen of its own and the room's page
  stands in for the menus.
- A streamed seat's score screen is played in the stream. With nobody there to press Continue the
  seat presses it itself after two minutes, so it is back in the room for the next match.
- The room's children inherit its environment, so `ForChild` clears the front end's check
  variables and `HYPERION_LAN_KEY` before starting one; a child that kept them would open a
  console and try to start a room of its own.

## Open items

- R6: the host serving each platform's install package and the 1.17 data; the join page's install
  choice is greyed until then, and R8 is to put the third-party notices (MPL-2.0, BSD, Apache-2.0)
  in the packages. R7: tournaments, with the console's Tournament tab greyed meanwhile.
- A dropped player: the relay waits two minutes, then the seat is lost; the console's other
  choices (lost at once, pause for everyone) and save and rejoin are not built yet. Online play on the same
  transport, and SpacetimeDB as a service, are parked.
- The first H0 game parted once on 1.17's own sync check and has not since; if it comes back,
  `hyperion_lan.log` names the frame and the parts, and `HYPERION_LAN_TRACE=1` writes every step
  each machine read.
- A streamed seat's picture size against the browser's window, and how a browser player's screen
  maps to the view, are still open.

## Further reading

- `../hyperion-exe.md`: every switch and environment variable, the room's own section, and "In a
  browser" for the WebAssembly build.
- `../replay-test.md`: the gate every change here is held to. `front-end.md`: the kit the LAN
  screens draw with. `../../src117/port/lan/port_lanproto.h`: the protocol, in one short header.
