# addons: what a player calls up over a game

Add-ons are the second kind of extension beside mods. A mod changes the game itself and is
fingerprinted, so players with it play only each other (`data.md`, "Mods"). An add-on changes
what one player sees or hears: several at once, in almost any game, classic ones included, seeing
only what that player could see, never what would be unfair, such as where an enemy's units are.

Hyperion has no add-ons at present. The music player, the first, is out of the program until
add-ons have a system of their own: a way to find and load them, an interface that hands them
only the local player's view, and a way for a player to turn each on.

What the engine keeps for one:

- **A voice in the mix.** `PortSoundSource` (`port_sound.h`; `data.md`, "Sound"): a function the
  mixer's thread calls each time it mixes, which adds its own frames before Master volume.
- **Layers over the game.** `render.md`, "The layers over the game": a layer's Draw, Mouse, Key,
  Outside and Wheel, and the flags a window that belongs with the console needs
  (`PORT_OVERLAY_CONSOLE`, `PORT_OVERLAY_SYSKEYS`).
- **A skin from the HUD.** A style's `musicPlayer` block is read and checked, and its plates are
  drawn by the HUD for the boxes a music add-on's windows give it (`hud.md`, "The music player's
  skin"; `../hud-format.md`, "The music player").
- **The audio decoders.** MP3, Ogg Vorbis, Ogg Opus, FLAC and WAV (`port/data/port_audiodecode.cpp`),
  which a mod's sounds and music use (`data.md`).
