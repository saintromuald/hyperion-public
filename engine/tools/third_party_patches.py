"""Hyperion's patches to a fetched library: tools/<name>-patches/*.patch, applied after the
download's checksum is checked, and a stamp that says which ones the copy has.

A fetch script calls apply(folder, patches) once its files are in place; a build calls
check(folder, patches) and stops when the copy is not patched with the patches the repository
has, so a machine with an older copy fetches again rather than building without a fix.

Each patch is a -p1 diff on the library's folder. A file a patch touches is kept as it was
fetched, as <file>.orig, and put back before the patches are applied, so applying again
gives the same text.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

STAMP = "hyperion-patches.txt"


def patches(patch_dir: Path) -> list:
    return sorted(Path(patch_dir).glob("*.patch"))


def stamp_text(patch_dir: Path) -> str:
    lines = []
    for p in patches(patch_dir):
        digest = hashlib.sha256(p.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        lines.append(f"{p.name} {digest}")
    return "\n".join(lines) + "\n"


def apply(folder: Path, patch_dir: Path) -> bool:
    folder, patch_dir = Path(folder), Path(patch_dir)
    want = stamp_text(patch_dir)
    stamp = folder / STAMP
    if stamp.exists() and stamp.read_text() == want:
        return True
    for p in patches(patch_dir):
        text = p.read_bytes().replace(b"\r\n", b"\n")
        targets = [line[6:].decode().strip() for line in text.split(b"\n") if line.startswith(b"+++ b/")]
        for t in targets:
            f = folder / t
            orig = f.with_name(f.name + ".orig")
            if not orig.exists():
                shutil.copy2(f, orig)
            shutil.copy2(orig, f)
        with tempfile.NamedTemporaryFile("wb", suffix=".patch", delete=False) as tmp:
            tmp.write(text)
        # git apply inside a repository takes paths from its root and skips the rest without a
        # word; the ceiling keeps it from finding this one, so it patches the folder as given
        env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(folder.parent))
        try:
            r = subprocess.run(["git", "apply", "--unsafe-paths", "-p1", tmp.name], cwd=folder, env=env)
        finally:
            Path(tmp.name).unlink()
        if r.returncode != 0:
            print(f"{p.name} does not apply to {folder}")
            return False
        print(f"  applied {p.name}")
    stamp.write_text(want)
    return True


def check(folder: Path, patch_dir: Path, fetch: str) -> None:
    """Stops the build when folder is not patched with patch_dir's patches."""
    stamp = Path(folder) / STAMP
    want = stamp_text(patch_dir)
    if want.strip() and (not stamp.exists() or stamp.read_text() != want):
        sys.exit(f"{folder} lacks Hyperion's patches ({Path(patch_dir).name}): run python tools/{fetch}")
