"""Compile the GPU presenter's shaders on Linux, with dxc.exe under Wine.

    python tools/build_shaders_linux.py

Upstream compiles the shaders as part of the Windows build (build_engine_x64.py, whose
SHADER_ENTRIES this script shares). A Linux machine has no dxc of its own, so this script
runs the same released dxc.exe (tools/fetch_dxc.py) under Wine and writes the same headers
into src117/build/x64/shaders/: each entry's .dxil.h and .spv.h, which the Linux build
(build_engine_linux.py) then includes. The Mac's MSL headers are not made here: the Mac
build makes them on the Mac (shader_msl.py).

    python tools/fetch_dxc.py    # once, before this
"""

import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shader_layout  # noqa: E402
from port_files import SHADERS as SHADER_SRC  # noqa: E402

ENGINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADERS = os.path.join(ENGINE, "src117", "build", "x64", "shaders")
DXC = os.path.join(ENGINE, "third_party", "dxc-1.9.2607", "bin", "x64", "dxc.exe")

# build_engine_x64.py's SHADER_ENTRIES, kept here because importing that module runs
# Windows-only code at import time (ctypes.WinDLL). Keep the two lists in step.
SHADER_ENTRIES = [("present.hlsl", "VSMain", "vs_6_0"), ("present.hlsl", "PSResolve", "ps_6_0"),
                  ("present.hlsl", "PSCompose", "ps_6_0"), ("present.hlsl", "PSScale", "ps_6_0"),
                  ("scene.hlsl", "CSScene", "cs_6_0"), ("scene.hlsl", "CSDistort", "cs_6_0"),
                  ("scene.hlsl", "CSShow", "cs_6_0"), ("scene.hlsl", "CSSceneFull", "cs_6_0"),
                  ("scene.hlsl", "CSDistortFull", "cs_6_0"), ("scene.hlsl", "CSShowFull", "cs_6_0"),
                  ("present.hlsl", "PSComposeFull", "ps_6_0"), ("present.hlsl", "PSGround", "ps_6_0"), ("scene.hlsl", "CSHud", "cs_6_0"),
                  ("ui.hlsl", "VSUi", "vs_6_0"), ("ui.hlsl", "PSUi", "ps_6_0"),
                  ("ui.hlsl", "VSFull", "vs_6_0"), ("ui.hlsl", "PSPlanet", "ps_6_0"),
                  ("effects.hlsl", "PSBright", "ps_6_0"), ("effects.hlsl", "PSDown", "ps_6_0"),
                  ("effects.hlsl", "PSUp", "ps_6_0"), ("effects.hlsl", "PSPost", "ps_6_0"),
                  ("effects.hlsl", "PSLights", "ps_6_0"),
                  ("hd.hlsl", "VSHdQuad", "vs_6_0"), ("hd.hlsl", "PSHdSprite", "ps_6_0"),
                  ("hd.hlsl", "PSHdBase", "ps_6_0"), ("hd.hlsl", "VSHdFull", "vs_6_0"),
                  ("hd.hlsl", "PSHdBlend", "ps_6_0"), ("hd.hlsl", "PSHdConsole", "ps_6_0"),
                  ("hd.hlsl", "PSHdUi", "ps_6_0"), ("hd.hlsl", "VSHdBatch", "vs_6_0"),
                  ("hd.hlsl", "PSHdBatch", "ps_6_0"), ("fog.hlsl", "CSFogLight", "cs_6_0"),
                  ("hud.hlsl", "VSHudQuad", "vs_6_0"), ("hud.hlsl", "VSHudPlace", "vs_6_0"),
                  ("hud.hlsl", "PSHudPlaced", "ps_6_0"), ("hud.hlsl", "PSHudFx", "ps_6_0"),
                  ("hud.hlsl", "PSHudBlur", "ps_6_0"), ("hud.hlsl", "PSHudDown", "ps_6_0"),
                  ("hud.hlsl", "PSHudUp", "ps_6_0"), ("hudshape.hlsl", "PSHudShape", "ps_6_0"),
                  ("hudshape.hlsl", "PSHudShadow", "ps_6_0"), ("video.hlsl", "PSVideoColour", "ps_6_0"),
                  ("video.hlsl", "PSVideoScale", "ps_6_0")]


def wine_path(path):
    """A POSIX path as Wine's Z: form, backslashed as Windows programs write paths."""
    return "Z:" + os.path.abspath(path).replace("/", "\\")


def main():
    if sys.platform == "win32":
        sys.exit("use build_engine_x64.py on Windows")
    if not os.path.exists(DXC):
        sys.exit("no %s: run python tools/fetch_dxc.py" % DXC)
    wine = shutil.which("wine")
    if wine is None:
        sys.exit("no wine: install it (dxc.exe is a Windows program)")
    # the C++ fills each uniform block as Direct3D lays it out; SPIR-V's must agree
    layout = shader_layout.check(SHADER_SRC)
    if layout:
        sys.exit("uniform blocks laid out differently for SPIR-V:\n  " + "\n  ".join(layout))
    os.makedirs(SHADERS, exist_ok=True)
    env = dict(os.environ, WINEDEBUG="-all")
    for src, entry, profile in SHADER_ENTRIES:
        hlsl = wine_path(os.path.join(SHADER_SRC, src))
        for ext, name, extra in ((".dxil.h", "k%sDxil", []),
                                 (".spv.h", "k%sSpv", ["-spirv", "-fspv-target-env=vulkan1.0"])):
            out = wine_path(os.path.join(SHADERS, entry + ext))
            cmd = [wine, wine_path(DXC), "-nologo", "-O3", "-Wno-ignored-attributes",
                   "-T", profile, "-E", entry] + extra + [hlsl, "-Fh", out, "-Vn", name % entry]
            print("[build_shaders_linux] dxc %s %s %s" % (src, entry, ext))
            r = subprocess.run(cmd, env=env)
            if r.returncode != 0 or not os.path.exists(os.path.join(SHADERS, entry + ext)):
                sys.exit("[build_shaders_linux] dxc %s %s %s FAILED" % (src, entry, ext))
    print("[build_shaders_linux] OK -> %s" % os.path.relpath(SHADERS, ENGINE))


if __name__ == "__main__":
    main()
