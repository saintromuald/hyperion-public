"""An MCP server for Hyperion's live channel (src117/port/tools/port_control.cpp), so a
Claude session can change what the running game shows and look at the result.

Registered in the repository's .mcp.json as "hyperion". It connects to 127.0.0.1:27117 (or
HYPERION_CONTROL_PORT) when a tool is first used, and again after Hyperion restarts. The
channel is open while Hyperion runs with --control, while Debug Tools is the active mod,
and on the canvas (python engine/tools/play.py --renderer direct3d12 --canvas).

snapshot returns the frame as an image as well as its path. The frames show the player's
game art: they stay on this machine, in the game's data folder.
"""
import os
import sys

from mcp.server.fastmcp import FastMCP, Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hyperion_ctl import PORT, UNITS, Client, unit_id  # noqa: E402

mcp = FastMCP("hyperion")
_client = None


def _call(cmd, timeout=10.0, **args):
    """The game's answer to cmd, reconnecting once when the connection has gone."""
    global _client
    port = int(os.environ.get("HYPERION_CONTROL_PORT", PORT))
    for attempt in range(2):
        try:
            if _client is None:
                _client = Client(port)
            return _client.call(cmd, timeout=timeout, **args)
        except Exception as e:  # a closed socket, a refused connection, a timeout
            _client = None
            if attempt == 1:
                return {"ok": False, "error": f"Hyperion is not reachable on 127.0.0.1:{port}: {e}. Start it with "
                                              "--control, with Debug Tools active, or on the canvas."}


@mcp.tool()
def status() -> dict:
    """Where the game is: "front end", "menus" or "game"; the frame; paused; whether this is
    Debug Tools' canvas; whether a sandbox command changed it; the map's size in pixels."""
    return _call("status")


@mcp.tool()
def list_values() -> dict:
    """The values that can be changed live, each with its value, range, default and help."""
    return _call("list")


@mcp.tool()
def set_value(name: str, value: float) -> dict:
    """Sets a live value by name (list_values names them). Presentation only."""
    return _call("set", name=name, value=value)


@mcp.tool()
def snapshot(name: str = "") -> list:
    """Takes the next frame Hyperion shows and returns it as an image, with its path: the
    game at its own size, or the front end at the window's size."""
    answer = _call("snapshot", name=name or None)
    if not answer.get("ok"):
        return [str(answer)]
    return [f"{answer['path']} ({answer.get('width')}x{answer.get('height')})", Image(path=answer["path"])]


@mcp.tool()
def pause() -> dict:
    """Pauses a single-player game."""
    return _call("pause")


@mcp.tool()
def resume() -> dict:
    """Resumes a paused single-player game."""
    return _call("resume")


@mcp.tool()
def step(frames: int = 1) -> dict:
    """Runs a paused game on by a number of frames (1 to 10000) and pauses it again."""
    return _call("step", frames=frames, timeout=10.0 + frames / 10.0)


@mcp.tool()
def camera(x: int, y: int) -> dict:
    """Centres the view on a point of the map, in pixels (status gives the map's size)."""
    return _call("camera", x=x, y=y)


@mcp.tool()
def canvas(tileset: str = "jungle") -> dict:
    """From the front end's home screen, starts Debug Tools' canvas on a tileset: jungle (its
    grass, the default), badlands, platform, install, ashworld, desert, ice or twilight."""
    return _call("canvas", tileset=tileset)


SPAWN_HELP = ("On the canvas only: makes units at a point (map pixels) for a player (0 is the west camp, the "
              "player's; 1 the east camp, the computer's), and with attack_x/attack_y sends them to attack-move "
              "there. unit is a units.dat number or a name: " + ", ".join(sorted(UNITS)) + ". This changes the "
              "game, which is then marked as changed.")


@mcp.tool(description=SPAWN_HELP)
def spawn(unit: str, x: int, y: int, player: int = 0, count: int = 1, attack_x: int = -1, attack_y: int = -1) -> dict:
    try:
        n = unit_id(unit)
    except ValueError as e:
        return {"ok": False, "error": str(e)}
    attack = [attack_x, attack_y] if attack_x >= 0 and attack_y >= 0 else None
    return _call("spawn", unit=n, x=x, y=y, player=player, count=count, attack=attack)


@mcp.tool()
def select(x: int, y: int, radius: int = 96, player: int = 0) -> dict:
    """In a single-player game: selects a player's units in the square radius game pixels round a
    point (map pixels) by a drag box's rules (its own units without its buildings when there are
    units, else one unit), to see what a selection draws (health bars, circles) without the
    mouse."""
    return _call("select", x=x, y=y, radius=radius, player=player)


@mcp.tool()
def lights() -> dict:
    """The 2D lights' table: each image of images.dat that gives light (by its place), its
    name, colour, radius in game pixels, intensity and flicker; where the table came from;
    and how many lights the last map pass lit. The lights show with the values "effects"
    and "lights" on and the GPU scene running."""
    return _call("lights")


@mcp.tool()
def light(image: int, color: str = "", radius: float = -1, intensity: float = -1, flicker: float = -1,
          name: str = "", remove: bool = False) -> dict:
    """Changes one entry of the lights' table, or adds it: color as "#rrggbb", radius in
    game pixels (1 to 512), intensity (0 to 8), flicker (0 to 1); a field left out stays as
    it is. remove takes the image out of the table. Seen at once; lights_save keeps it."""
    args = {"image": image, "color": color or None, "name": name or None,
            "radius": radius if radius >= 0 else None, "intensity": intensity if intensity >= 0 else None,
            "flicker": flicker if flicker >= 0 else None, "remove": True if remove else None}
    return _call("light", **args)


@mcp.tool()
def dialogs() -> dict:
    """The open dialogs, each with its name when it is one of the console's or the
    screen edges' (minimap, statdata, statport, statcmd, textbox, statres, statlb), its text
    and its rectangle on the screen in game pixels (left, top, right, bottom)."""
    return _call("dialogs")


@mcp.tool()
def lights_save() -> dict:
    """Writes the lights' table to lights.json in the game's data folder, which Hyperion
    reads in place of its built-in table from then on."""
    return _call("lights_save")


@mcp.tool()
def looks() -> dict:
    """The looks: each one's name, the palette entries it takes (rule: blues, greens or all),
    what it is drawn as in full colour (material: crystal or gas), its ramp of six colours,
    the images it covers (none for all of a sprite's), the unit types that have it, its
    clarity, shine, glow and sparkle, its plume (height, density, rise, the images it
    replaces, its vents) and its light on the ground; where they came from, and the
    looks.json that looks_save writes. rich-gold and rich-violet are built in."""
    return _call("looks")


@mcp.tool()
def look(name: str, rule: str = "", material: str = "", ramp: list[str] | None = None,
         images: list[int] | None = None, units: list[int] | None = None, clarity: float = -1,
         shine: float = -1, glow: float = -1, sparkle: float = -1, plume: dict | None = None,
         light: dict | None = None) -> dict:
    """Changes the look of that name, or adds it (the gold one's ramp and values, rule "all",
    material "crystal", no images, so all of a sprite's, and no units):
    rule "blues", "greens" or "all"; material "crystal" or "gas"; ramp six "#rrggbb", dark to
    light; images and units as places in images.dat and units.dat; clarity, shine, glow and
    sparkle 0 to 1; plume {height, density, rise, replaces, vents: [{image, x, y, strength,
    width, frame}]}, x and y in the image's frame, frame left out for any; light {color,
    radius, strength, x, y}. A field
    left out stays as it is. Seen at once; looks_save keeps it."""
    args = {"name": name, "rule": rule or None, "material": material or None, "ramp": ramp, "images": images,
            "units": units, "clarity": clarity if clarity >= 0 else None, "shine": shine if shine >= 0 else None,
            "glow": glow if glow >= 0 else None, "sparkle": sparkle if sparkle >= 0 else None, "plume": plume,
            "light": light}
    return _call("look", **args)


@mcp.tool()
def look_unit(unit: int, look: str = "") -> dict:
    """Gives one unit a look by hand (unit is its id, as units gives it): a look's name,
    "none" for none, or "" to leave it to the game (rich bases, or its type's look). It goes
    with the unit until another unit takes its place, and ends with the game."""
    return _call("look_unit", unit=unit, look=look)


@mcp.tool()
def looks_save() -> dict:
    """Writes the looks to looks.json in the active mod's folder, or in the game's data folder
    when no mod is active, which Hyperion reads over its built-in looks as each game begins."""
    return _call("looks_save")


if __name__ == "__main__":
    mcp.run()
