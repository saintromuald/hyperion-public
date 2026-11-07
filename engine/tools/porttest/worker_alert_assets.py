#!/usr/bin/env python3
"""Validate Hyperion's three worker-alert recordings with ffmpeg and optionally Whisper.

    python engine/tools/porttest/worker_alert_assets.py
    python engine/tools/porttest/worker_alert_assets.py \
      --whisper-cli whisper-cli --whisper-model /path/to/ggml-large-v3-turbo.bin

The basic check needs ffmpeg/ffprobe. The optional check independently transcribes every
file and requires the exact sentence, not merely audio that happens to resemble speech.
"""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FILES = ROOT / "huds" / "files"
EXPECTED = "our workers are getting killed"


def run(*args):
    return subprocess.run(args, check=True, text=True, capture_output=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--whisper-cli")
    ap.add_argument("--whisper-model")
    args = ap.parse_args()
    if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
        ap.error("ffmpeg and ffprobe are required")

    for race in ("terran", "protoss", "zerg"):
        path = FILES / f"worker-{race}.ogg"
        with path.open("rb") as f:
            assert f.read(4) == b"OggS", f"{path}: not Ogg"
        probe = json.loads(run("ffprobe", "-v", "error", "-show_entries",
                               "format=duration", "-of", "json", str(path)).stdout)
        duration = float(probe["format"]["duration"])
        assert 0.8 < duration < 5.0, f"{path}: implausible duration {duration}"
        decoded = run("ffmpeg", "-hide_banner", "-i", str(path),
                      "-af", "volumedetect", "-f", "null", "-")
        text = decoded.stderr
        mean = float(re.search(r"mean_volume: (-?[0-9.]+) dB", text).group(1))
        peak = float(re.search(r"max_volume: (-?[0-9.]+) dB", text).group(1))
        assert mean > -45.0 and peak > -30.0, f"{path}: silent or nearly silent"
        print(f"{race}: {duration:.3f}s, mean {mean:.1f} dB, peak {peak:.1f} dB")

        if args.whisper_model:
            assert args.whisper_cli, "--whisper-model also needs --whisper-cli"
            heard = run(args.whisper_cli, "-m", args.whisper_model, "-t", "4",
                        "-nt", "-np", str(path)).stdout
            heard = re.sub(r"[^a-z ]", "", heard.lower()).strip()
            heard = re.sub(r"\s+", " ", heard)
            assert heard == EXPECTED, f"{path}: transcribed {heard!r}, expected {EXPECTED!r}"
            print(f"{race}: exact transcript: {EXPECTED}")
    print("worker alert asset checks passed")


if __name__ == "__main__":
    main()
