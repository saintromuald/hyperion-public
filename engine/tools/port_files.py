"""Where the port's files are: src117/port/<area>/, a folder an area of the engine as the module
docs divide it (docs/modules/overview.md): loop, render, data, front, gameplay, replays, lan,
tools, platform. The builds and the tools that read the sources find a file by its name here,
so a file can change area without them changing.

    port_path("port_gpu.cpp")   the file's path, wherever its area is
    tu_path("port", "port_gpu") a TU of src117/tus.txt, lang/<name>.cpp or a port file
    port_sources()              every port .cpp and .h, area by area
"""
import os

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src117")
PORT = os.path.join(SRC, "port")
SHADERS = os.path.join(PORT, "render", "shaders")

_where = None


def _files():
    global _where
    if _where is None:
        _where = {}
        for area in sorted(os.listdir(PORT)):
            d = os.path.join(PORT, area)
            if not os.path.isdir(d):
                continue
            for f in sorted(os.listdir(d)):
                p = os.path.join(d, f)
                if os.path.isfile(p):
                    if f in _where:
                        raise SystemExit("port_files: %s is in two areas" % f)
                    _where[f] = p
    return _where


def port_path(name):
    """The path of the port file name (port_gpu.cpp, port_util.h)."""
    p = _files().get(name)
    if p is None:
        raise SystemExit("port_files: no %s in any area of %s" % (name, PORT))
    return p


def tu_path(group, name):
    """The source of a TU src117/tus.txt lists: lang/<name>.cpp, or the port file <name>.cpp."""
    return port_path(name + ".cpp") if group == "port" else os.path.join(SRC, group, name + ".cpp")


def port_dirs():
    """Every area's folder, for a tool that compiles a copy of a port file's text away from it."""
    return sorted({os.path.dirname(p) for p in _files().values()})


def tu_rel(group, filename):
    """A TU's file (port_gpu.cpp, game.cpp) as a path under src117/, for a tool that compiles a
    copy of the tree: lang/game.cpp, port/render/port_gpu.cpp."""
    path = port_path(filename) if group == "port" else os.path.join(SRC, group, filename)
    return os.path.relpath(path, SRC)


def port_sources(exts=(".cpp", ".h")):
    """Every port file with one of exts, by area and then name."""
    return [p for f, p in _files().items() if f.endswith(exts)]
