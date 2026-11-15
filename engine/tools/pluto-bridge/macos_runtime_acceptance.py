#!/usr/bin/env python3
"""Run the real Pluto bridge/inference on macOS ARM64 using only synthetic frames.

The official archives are validation inputs, never outputs. The runtime, Wine prefix, logs, and
summary live below a fresh --work directory. No StarCraft installation or game asset is read.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import stat
import struct
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "pluto-diff"))

import genframes  # noqa: E402
from pluto_diff.hxp import parse_frame_stream, parse_reply_stream  # noqa: E402


@dataclass(frozen=True)
class PayloadSpec:
    name: str
    sha256: str
    size: int | None = None
    max_size: int = 512 << 20


@dataclass(frozen=True)
class ArchiveSpec:
    sha256: str
    size: int
    payloads: tuple[PayloadSpec, ...]


OFFICIAL_PLUTO = ArchiveSpec(
    sha256="17544fc3ab32e3e2823646887e021cf998362348d73740817e94d1a78aa6209d",
    size=309686477,
    payloads=(
        PayloadSpec("pluto.dll", "603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc"),
        PayloadSpec(
            "pluto/pluto_infer.exe",
            "3f2814a51034b9817b396e52b38d30341ede506a2baedc7cbc11cb6ebcece2cc",
        ),
        PayloadSpec(
            "pluto/pluto_weights.bin",
            "c53a559b4910777dc0e1c561c0d89d243091f465d8e9fc0f5f72e56597067d86",
            size=347935248,
        ),
    ),
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def extract_official_runtime(archive: Path, runtime: Path, spec: ArchiveSpec = OFFICIAL_PLUTO) -> dict:
    """Verify the outer archive and every payload, then extract only those pinned paths."""
    if not archive.is_file():
        raise RuntimeError(f"official Pluto ZIP is missing: {archive}")
    if archive.stat().st_size != spec.size:
        raise RuntimeError(f"official Pluto ZIP has size {archive.stat().st_size}, expected {spec.size}")
    outer_hash = sha256_file(archive)
    if outer_hash != spec.sha256:
        raise RuntimeError(f"official Pluto ZIP SHA-256 is {outer_hash}, expected {spec.sha256}")
    if runtime.exists():
        raise RuntimeError(f"refusing to reuse a possibly stale runtime directory: {runtime}")
    runtime.mkdir(parents=True)
    evidence = {"archive_sha256": outer_hash, "payloads": {}}
    try:
        with zipfile.ZipFile(archive) as package:
            entries = package.infolist()
            for payload in spec.payloads:
                matches = [entry for entry in entries if entry.filename == payload.name]
                if len(matches) != 1:
                    raise RuntimeError(f"official Pluto ZIP must contain exactly one {payload.name}")
                entry = matches[0]
                mode = entry.external_attr >> 16
                if entry.is_dir() or stat.S_ISLNK(mode) or entry.flag_bits & 1:
                    raise RuntimeError(f"official Pluto entry is not a plain unencrypted file: {payload.name}")
                if entry.file_size > payload.max_size or (payload.size is not None and entry.file_size != payload.size):
                    raise RuntimeError(f"official Pluto entry has an invalid size: {payload.name}")
                target = runtime / payload.name
                target.parent.mkdir(parents=True, exist_ok=True)
                digest = hashlib.sha256()
                size = 0
                with package.open(entry) as source, target.open("xb") as output:
                    for block in iter(lambda: source.read(1024 * 1024), b""):
                        size += len(block)
                        if size > payload.max_size:
                            raise RuntimeError(f"official Pluto entry exceeds its size limit: {payload.name}")
                        digest.update(block)
                        output.write(block)
                if size != entry.file_size or (payload.size is not None and size != payload.size):
                    raise RuntimeError(f"official Pluto entry was truncated: {payload.name}")
                actual_hash = digest.hexdigest()
                if actual_hash != payload.sha256:
                    raise RuntimeError(
                        f"official Pluto entry {payload.name} SHA-256 is {actual_hash}, expected {payload.sha256}"
                    )
                evidence["payloads"][payload.name] = {"sha256": actual_hash, "size": size}
    except Exception:
        shutil.rmtree(runtime)
        raise
    (runtime / "pluto" / "pluto_infer.exe").chmod(
        (runtime / "pluto" / "pluto_infer.exe").stat().st_mode | 0o111
    )
    return evidence


def pe_machine(path: Path) -> int:
    with path.open("rb") as source:
        dos = source.read(64)
        if len(dos) != 64 or dos[:2] != b"MZ":
            raise RuntimeError(f"not a PE image: {path}")
        offset = struct.unpack_from("<I", dos, 0x3C)[0]
        source.seek(offset)
        header = source.read(6)
    if len(header) != 6 or header[:4] != b"PE\0\0":
        raise RuntimeError(f"not a PE image: {path}")
    return struct.unpack_from("<H", header, 4)[0]


def read_log(runtime: Path, name: str) -> str:
    path = runtime / name
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def validate_evidence(
    *,
    returncode: int,
    timed_out: bool,
    stdout: bytes,
    stderr: bytes,
    runtime: Path,
    frame_count: int,
    remaining_inference_processes: list[str],
) -> dict:
    errors: list[str] = []
    if timed_out:
        errors.append("bridge/inference run timed out")
    if returncode != 0:
        errors.append(f"bridge exited with status {returncode}")
    try:
        replies = parse_reply_stream(stdout)
    except Exception as error:
        replies = []
        errors.append(f"HXRP output is not a strict reply stream: {error}")
    reply_frames = [reply.frame for reply in replies]
    expected_frames = list(range(frame_count + 1))  # one reply per frame plus the final flush
    if reply_frames != expected_frames:
        errors.append(f"HXRP frames {reply_frames} do not equal 0..{frame_count}")
    nonzero_win = [reply.frame for reply in replies if reply.win != 0]
    if nonzero_win:
        errors.append(f"in-game HXRP replies have nonzero win fields at frames {nonzero_win}")
    command_bytes = sum(len(reply.command_bytes) for reply in replies)
    strict_commands = sum(len(reply.commands) for reply in replies)
    if command_bytes == 0 or strict_commands == 0:
        errors.append("real Pluto produced no strictly validated classic commands")

    bridge_text = stderr.decode("utf-8", errors="replace")
    pluto_text = read_log(runtime, "pluto.log")
    infer_text = read_log(runtime, "pluto_infer.log")
    if "[bridge] done (clean EOF)" not in bridge_text:
        errors.append("bridge did not record clean EOF finalization")
    if "onStart" not in pluto_text or "inference server up" not in pluto_text:
        errors.append("official DLL did not record onStart and its inference server handshake")
    ready = re.search(
        r"\[pluto_infer\] ready: [^\n]*\bmodel v2\.0 md09b_1331000\b[^\n]*\bframes_per_step 6\b",
        infer_text,
    )
    observations = re.findall(r"\bobs F\d+ flushed F\d+ .*?infer [0-9]+(?:\.[0-9]+)? ms", infer_text)
    steps = re.findall(r"\[pluto_infer\] step \d+ N=\d+", infer_text)
    shutdown = re.search(r"\[pluto_infer\] shutdown after ([1-9]\d*) steps; exiting", infer_text)
    shutdown_steps = int(shutdown.group(1)) if shutdown is not None else 0
    if ready is None:
        errors.append("official inference engine did not record its expected ready handshake")
    if shutdown is None:
        errors.append("official inference engine did not record a nonzero step count and clean shutdown")
    if remaining_inference_processes:
        errors.append(f"pluto_infer.exe remained running after clean EOF: {remaining_inference_processes}")
    return {
        "valid": not errors,
        "errors": errors,
        "returncode": returncode,
        "timed_out": timed_out,
        "input_frames": frame_count,
        "reply_frames": reply_frames,
        "nonempty_replies": sum(bool(reply.command_bytes) for reply in replies),
        "nonzero_win_frames": nonzero_win,
        "command_bytes": command_bytes,
        "strict_commands": strict_commands,
        "inference_ready": ready is not None,
        "inference_observation_records": len(observations),
        "inference_step_records": len(steps),
        "inference_shutdown_steps": shutdown_steps,
        "bridge_clean_eof": "[bridge] done (clean EOF)" in bridge_text,
        "remaining_inference_processes": remaining_inference_processes,
        "stdout_sha256": hashlib.sha256(stdout).hexdigest(),
    }


@dataclass(frozen=True)
class ProcessRecord:
    pid: int
    pgid: int
    command: str


def list_processes() -> list[ProcessRecord]:
    result = subprocess.run(
        ["/bin/ps", "-ax", "-o", "pid=,pgid=,command="],
        text=True,
        capture_output=True,
        check=True,
        timeout=10,
    )
    records = []
    for line in result.stdout.splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) == 3:
            records.append(ProcessRecord(int(fields[0]), int(fields[1]), fields[2]))
    return records


class OwnedProcesses:
    """Only the acceptance child's process group or Wine processes using its isolated runtime."""

    def __init__(self, pgid: int, work: Path, *, lister=None, killpg=None, kill=None):
        self.pgid = pgid
        self.lister = lister or list_processes
        self.killpg = killpg or os.killpg
        self.kill = kill or os.kill
        self.windows_work = ("Z:" + str(work).replace("\\", "/")).casefold()

    def owned(self) -> list[ProcessRecord]:
        records = []
        for record in self.lister():
            if record.pid == os.getpid():
                continue
            command = record.command.replace("\\", "/").casefold()
            if record.pgid == self.pgid or self.windows_work in command:
                records.append(record)
        return records

    def survivors(self) -> list[ProcessRecord]:
        return [
            record
            for record in self.owned()
            if "pluto_infer.exe" in record.command or "pluto-bridge.exe" in record.command
        ]

    def wait_for_survivors(self, timeout=30) -> list[ProcessRecord]:
        deadline = time.monotonic() + timeout
        while True:
            remaining = self.survivors()
            if not remaining or time.monotonic() >= deadline:
                return remaining
            time.sleep(0.1)

    def kill_and_wait(self, timeout=5) -> list[ProcessRecord]:
        owned = self.owned()
        if any(record.pgid == self.pgid for record in owned):
            try:
                self.killpg(self.pgid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        # A descendant that escaped the group is still owned only when its Wine-visible command
        # names this acceptance runtime. No process outside that group/prefix is signalled. An empty
        # group is left alone rather than risking a later PGID-reuse race.
        for record in owned:
            if record.pgid != self.pgid:
                try:
                    self.kill(record.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        return self.wait_for_survivors(timeout)


def probe_wine_11_unified(wine: Path, env: dict[str, str]) -> str:
    if not wine.is_file() or not os.access(wine, os.X_OK):
        raise RuntimeError(f"Wine is not a regular executable file: {wine}")
    version = subprocess.run(
        [str(wine), "--version"], env=env, text=True, capture_output=True, timeout=30
    )
    reported = version.stdout.strip()
    if version.returncode != 0 or not reported.startswith("wine-11."):
        raise RuntimeError(
            f"resolved Wine must report wine-11.* --version; got status {version.returncode}, "
            f"stdout {reported!r}, stderr {version.stderr.strip()!r}"
        )
    wine_files = wine.parent.parent / "lib" / "wine"
    required = (
        wine_files / "i386-windows" / "ntdll.dll",
        wine_files / "x86_64-windows" / "ntdll.dll",
        wine_files / "x86_64-unix" / "wine",
    )
    if any(not path.is_file() for path in required) or not os.access(required[-1], os.X_OK):
        raise RuntimeError(f"resolved Wine 11 lacks its unified i386/x86-64 runtime files below {wine_files}")
    return reported


def probe_wine_11_fex(wine: Path, env: dict[str, str]) -> str:
    if not wine.is_file() or not os.access(wine, os.X_OK):
        raise RuntimeError(f"Wine/FEX is not a regular executable file: {wine}")
    version = subprocess.run(
        [str(wine), "--version"], env=env, text=True, capture_output=True, timeout=30
    )
    reported = version.stdout.strip()
    if version.returncode != 0 or not reported.startswith("wine-11.") or "MetalSharp Wine" not in reported:
        raise RuntimeError(
            "resolved Wine/FEX must report Wine 11 and MetalSharp Wine; "
            f"got status {version.returncode}, stdout {reported!r}, stderr {version.stderr.strip()!r}"
        )
    contents = wine.parent.parent
    wineserver = contents / "MacOS" / "wineserver"
    i386_ntdll = contents / "lib" / "wine" / "i386-windows" / "ntdll.dll"
    loader = contents / "lib" / "wine" / "aarch64-unix" / "wine"
    if (
        not i386_ntdll.is_file()
        or not wineserver.is_file()
        or not os.access(wineserver, os.X_OK)
        or not loader.is_file()
        or not os.access(loader, os.X_OK)
    ):
        raise RuntimeError(f"resolved MetalSharp Wine lacks its signed i386/ARM64EC runtime below {contents}")
    if not (contents / "embedded.provisionprofile").is_file():
        raise RuntimeError("resolved MetalSharp Wine lacks its embedded cross-architecture provisioning profile")
    return reported


def require_macos_runtime(backend: str) -> None:
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise RuntimeError("this acceptance run must execute on a macOS ARM64 host")
    version = subprocess.run(
        ["/usr/sbin/sysctl", "-n", "kern.osproductversion"], text=True, capture_output=True, timeout=20
    )
    match = re.match(r"^(\d+)(?:\.(\d+))?", version.stdout.strip())
    if version.returncode != 0 or match is None:
        raise RuntimeError(f"could not determine macOS version: {version.stdout.strip()!r}")
    parts = (int(match.group(1)), int(match.group(2) or 0))
    minimum = (26, 4) if backend == "fex" else (15, 0)
    if parts < minimum:
        raise RuntimeError(f"macOS {minimum[0]}.{minimum[1]}+ is required for the {backend} runtime")
    if backend == "fex":
        return
    env = os.environ.copy()
    env["ROSETTA_ADVERTISE_AVX"] = "1"
    probe = subprocess.run(
        ["/usr/bin/arch", "-x86_64", "/usr/bin/true"], capture_output=True, timeout=20, env=env
    )
    if probe.returncode != 0:
        raise RuntimeError("a working Rosetta 2 x86-64 translator is required for the rosetta runtime")


def process_descriptions(records: list[ProcessRecord]) -> list[str]:
    return [f"{record.pid} (pgid {record.pgid}) {record.command}" for record in records]


def cleanup_failed_evidence(owned: OwnedProcesses, summary: dict) -> None:
    remaining = owned.kill_and_wait()
    summary["cleanup_remaining_processes"] = process_descriptions(remaining)
    if remaining:
        summary["errors"].append("owned inference/bridge processes survived cleanup")


def run_acceptance(args: argparse.Namespace) -> dict:
    require_macos_runtime(args.mac_backend)
    wine_arg = Path(args.wine)
    if not wine_arg.is_absolute():
        raise RuntimeError("--wine must be the same explicit absolute path used by capability detection")
    wine = wine_arg.resolve(strict=True)
    bridge = Path(args.bridge).resolve(strict=True)
    if pe_machine(bridge) != 0x14C:
        raise RuntimeError("Hyperion's bridge is not a 32-bit i386 PE image")
    work = Path(args.work).resolve()
    if work.exists() and any(work.iterdir()):
        raise RuntimeError(f"--work must be new or empty: {work}")
    work.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.update(
        {
            "WINEPREFIX": str(work / "wine-prefix"),
            "WINEDEBUG": "-all",
            "WINEDLLOVERRIDES": "mscoree,mshtml=",
            "HYPERION_PLUTO_DIRECT_PIPES": "1",
            "ROSETTA_ADVERTISE_AVX": "1",
        }
    )
    for name in (
        "HYPERION_PLUTO_MAP_FILE",
        "HYPERION_PLUTO_MAP_NAME",
        "HYPERION_PLUTO_STARTS",
        "HYPERION_PLUTO_DUMP",
        "HX_TEST_SYNC_CLIENT",
        "HX_TEST_ASYNC_CLIENT",
    ):
        env.pop(name, None)
    version = probe_wine_11_fex(wine, env) if args.mac_backend == "fex" else probe_wine_11_unified(wine, env)

    runtime = work / "runtime"
    archive_evidence = extract_official_runtime(Path(args.pluto_zip).resolve(strict=True), runtime)
    if pe_machine(runtime / "pluto.dll") != 0x14C:
        raise RuntimeError("official pluto.dll is not the required 32-bit i386 image")
    if pe_machine(runtime / "pluto" / "pluto_infer.exe") != 0x8664:
        raise RuntimeError("official pluto_infer.exe is not the required x86-64 image")
    shutil.copy2(bridge, runtime / "pluto-bridge.exe")

    payload = genframes.stream(args.frames)
    frames = parse_frame_stream(payload)
    if len(frames) != args.frames or [frame.frame for frame in frames] != list(range(args.frames)):
        raise RuntimeError("synthetic HXPF input is incomplete or noncontiguous")
    if any(frame.protocol != "HXPF" for frame in frames):
        raise RuntimeError("synthetic input is not strictly HXPF")

    process = subprocess.Popen(
        [str(wine), "pluto-bridge.exe"],
        cwd=runtime,
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    owned = OwnedProcesses(process.pid, work)
    cleanup_completed = False
    try:
        timed_out = False
        try:
            stdout, stderr = process.communicate(payload, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            owned.kill_and_wait(timeout=0)
            stdout, stderr = process.communicate()
        finally:
            if process.poll() is None:
                owned.kill_and_wait(timeout=0)
                process.wait()

        (work / "bridge.stdout.bin").write_bytes(stdout)
        (work / "bridge.stderr.log").write_bytes(stderr)
        remaining = owned.wait_for_survivors()
        summary = validate_evidence(
            returncode=process.returncode,
            timed_out=timed_out,
            stdout=stdout,
            stderr=stderr,
            runtime=runtime,
            frame_count=args.frames,
            remaining_inference_processes=process_descriptions(remaining),
        )
        if not summary["valid"]:
            cleanup_failed_evidence(owned, summary)
            cleanup_completed = True
        summary.update(
            {
                "wine": str(wine),
                "wine_version": version,
                "mac_backend": args.mac_backend,
                "bridge_sha256": sha256_file(bridge),
                "input_sha256": hashlib.sha256(payload).hexdigest(),
                "official": archive_evidence,
            }
        )
        (work / "acceptance-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
        print(json.dumps(summary, indent=2, sort_keys=True))
        if not summary["valid"]:
            for name, text in (
                ("bridge stderr", stderr.decode("utf-8", errors="replace")),
                ("pluto.log", read_log(runtime, "pluto.log")),
                ("pluto_infer.log", read_log(runtime, "pluto_infer.log")),
            ):
                print(f"\n--- {name} (last 16000 characters) ---\n{text[-16000:]}", file=sys.stderr)
            raise RuntimeError("macOS Pluto runtime acceptance failed: " + "; ".join(summary["errors"]))
        return summary
    except Exception:
        # This includes validation errors and survivors discovered after the bridge's normal exit.
        if not cleanup_completed:
            try:
                owned.kill_and_wait()
            except Exception as cleanup_error:
                print(f"ERROR: owned-process cleanup also failed: {cleanup_error}", file=sys.stderr)
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wine", required=True, help="absolute path to the pinned official Wine executable")
    parser.add_argument(
        "--mac-backend", choices=("rosetta", "fex"), default="rosetta", help="macOS translation runtime"
    )
    parser.add_argument("--bridge", required=True, type=Path, help="Hyperion-owned 32-bit pluto-bridge.exe")
    parser.add_argument("--pluto-zip", required=True, type=Path, help="official pinned pluto-v2.0.zip")
    parser.add_argument("--work", required=True, type=Path, help="new/empty isolated runtime and evidence directory")
    parser.add_argument("--frames", type=int, default=54, help="synthetic HXPF frame count (default: 54)")
    parser.add_argument("--timeout", type=int, default=600, help="bridge/inference timeout in seconds (default: 600)")
    args = parser.parse_args()
    if not 12 <= args.frames <= 1000:
        parser.error("--frames must be in 12..1000")
    if not 1 <= args.timeout <= 3600:
        parser.error("--timeout must be in 1..3600 seconds")
    try:
        run_acceptance(args)
    except Exception as error:
        raise SystemExit(f"ERROR: {error}") from error


if __name__ == "__main__":
    main()
