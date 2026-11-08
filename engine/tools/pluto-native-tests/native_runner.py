"""Process control and numeric evidence for deterministic native engine tests."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

from native_fixtures import fixture_bytes


ENGINE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ENGINE_ROOT / "tools"))
from hxlive import ws_call  # noqa: E402  (standard-library RFC 6455 client)


class EngineError(RuntimeError):
    pass


class FirstMismatch(AssertionError):
    pass


class Evidence:
    def __init__(self, test_name, folder):
        self.test_name = test_name
        self.folder = folder
        self.path = folder / "evidence.jsonl"
        self.mismatch_path = folder / "first-mismatch.json"
        self.result_path = folder / "result.json"
        self.last = None
        self.watch_ids = set()
        self.frames = 0
        self.commands = 0

    @staticmethod
    def _write_json(path, value):
        text = json.dumps(value, sort_keys=True, indent=1, allow_nan=False) + "\n"
        with path.open("x", encoding="utf-8") as stream:
            stream.write(text)

    def _append(self, record):
        with self.path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(record, sort_keys=True, allow_nan=False) + "\n")
            stream.flush()

    def capture(self, session, label):
        listed = session.call("units", max=1000)
        self.watch_ids.update(row["id"] for row in listed["units"])
        detailed = session.call("units", ids=sorted(self.watch_ids))
        players = session.call("players")["players"]
        record = {
            "kind": "state",
            "test": self.test_name,
            "label": label,
            "frame": detailed["frame"],
            "units": detailed["units"],
            "players": players,
        }
        session.expect(detailed["frame"], session.frame, f"{label}: state and engine frames agree")
        self.last = record
        self.frames += 1
        self._append(record)
        return record

    def command(self, session, player, payload):
        record = {
            "kind": "command",
            "test": self.test_name,
            "frame": session.frame,
            "player": player,
            "bytes": list(payload),
        }
        self.commands += 1
        self._append(record)

    def mismatch(self, session, label, expected, actual):
        report = {
            "test": self.test_name,
            "frame": session.frame,
            "assertion": label,
            "expected": expected,
            "actual": actual,
            "last_state": self.last,
            "engine_log": "engine.log",
            "evidence": "evidence.jsonl",
        }
        if not self.mismatch_path.exists():
            self._write_json(self.mismatch_path, report)
        raise FirstMismatch(
            f"{self.test_name} frame {session.frame}: {label}: expected {expected!r}, got {actual!r} "
            f"({self.mismatch_path})"
        )

    def unhandled(self, session, error):
        if self.mismatch_path.exists():
            return
        self._write_json(self.mismatch_path, {
            "test": self.test_name,
            "frame": session.frame,
            "assertion": "scenario completed without an engine/protocol exception",
            "expected": "no exception",
            "actual": {"type": type(error).__name__, "message": str(error)},
            "last_state": self.last,
            "engine_log": "engine.log",
            "evidence": "evidence.jsonl",
        })

    def passed(self, session):
        self._write_json(self.result_path, {
            "test": self.test_name,
            "status": "passed",
            "final_frame": session.frame,
            "state_captures": self.frames,
            "turn_commands": self.commands,
            "evidence": "evidence.jsonl",
        })


class EngineSession:
    """One owned, hidden UMS Hyperion process with exact one-frame stepping."""

    def __init__(self, test_name, engine, data, output_root, seed=7001, startup_timeout=90):
        self.test_name = test_name
        self.engine = Path(engine).expanduser().resolve()
        self.data = Path(data).expanduser().resolve() if data else self.engine.parent
        self.output_root = Path(output_root)
        self.seed = seed
        self.startup_timeout = startup_timeout
        self.folder = self.output_root / test_name
        self.proc = None
        self.port = self._free_port()
        # hxlive reads the key from this parent process, while the engine gets
        # the same key through its child environment below.
        os.environ["HYPERION_CONTROL_KEY"] = f"{self.port:032x}"
        self.frame = 0
        self.evidence = None
        self._log = None

    @staticmethod
    def _free_port():
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            return sock.getsockname()[1]

    def expect(self, actual, expected, label):
        if actual != expected:
            self.evidence.mismatch(self, label, expected, actual)
        return actual

    def expect_true(self, condition, label, actual):
        if not condition:
            self.evidence.mismatch(self, label, True, actual)
        return actual

    def call(self, command, timeout=30, **arguments):
        request = {"id": 1, "cmd": command, **arguments}
        answer = ws_call(self.port, request, timeout=timeout)
        if not isinstance(answer, dict) or not answer.get("ok"):
            raise EngineError(f"{command} failed: {answer!r}")
        return answer

    def _launch(self):
        if not self.engine.is_file():
            raise EngineError(f"explicit engine does not exist: {self.engine}")
        if os.name != "nt" and not os.access(self.engine, os.X_OK):
            raise EngineError(f"explicit engine is not executable: {self.engine}")
        if not self.data.is_dir():
            raise EngineError(f"engine data folder does not exist: {self.data}")
        self.output_root.mkdir(parents=True, exist_ok=True)
        self.folder.mkdir(parents=False, exist_ok=False)
        map_path = self.folder / "fixture.hxm"
        map_path.write_bytes(fixture_bytes(self.test_name))
        self.evidence = Evidence(self.test_name, self.folder)
        environment = dict(os.environ)
        for key in list(environment):
            if key.startswith("BWRL_") or key.startswith("HYPERION_PLUTO_") or key in ("HX_DEBUG", "HX_TEST_MORPH"):
                environment.pop(key)
        environment.update({
            "HYPERION_WINDOW": "hidden",
            "HYPERION_CONTROL_KEY": f"{self.port:032x}",
        })
        command = [
            str(self.engine),
            f"--data={self.data}",
            f"--map={map_path}",
            "--ums",
            f"--seed={self.seed}",
            f"--control={self.port}",
        ]
        self._log = (self.folder / "engine.log").open("wb")
        install = os.environ.get("HYPERION_NATIVE_INSTALL")
        if install:
            command.append(f"--install-folder={install}")
        self.proc = subprocess.Popen(
            command,
            cwd=str(self.engine.parent),
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=self._log,
            stderr=subprocess.STDOUT,
        )
        (self.folder / "command.json").write_text(json.dumps({
            "argv": command,
            "cwd": str(self.engine.parent),
            "seed": self.seed,
            "port": self.port,
            "pluto_or_inference": False,
        }, indent=1) + "\n", encoding="utf-8")

    def _connect_and_pause(self):
        deadline = time.monotonic() + self.startup_timeout
        last_error = None
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise EngineError(f"engine exited during startup with code {self.proc.returncode}; see {self.folder / 'engine.log'}")
            try:
                status = self.call("status", timeout=1)
                if status.get("state") == "game":
                    break
            except (OSError, ConnectionError, TimeoutError, EngineError) as error:
                last_error = error
            time.sleep(0.05)
        else:
            raise EngineError(f"no game/channel within {self.startup_timeout}s: {last_error}")

        # Dismiss the UMS objectives pause, then pause immediately.  No wall-clock
        # sleep is added here: subsequent assertions use engine frame numbers only.
        self.call("resume")
        while time.monotonic() < deadline:
            status = self.call("status")
            if status.get("frame", 0) > 0:
                break
            if self.proc.poll() is not None:
                raise EngineError("engine exited while dismissing UMS objectives")
            time.sleep(0.05)
        else:
            raise EngineError("the UMS objectives pause did not arrive")
        self.call("resume")
        paused = self.call("pause")
        self.frame = paused["frame"]

        self.call("passive", player=1)
        # The fixture's one-shot resource trigger is applied on frame 2. All
        # scenarios begin with that real state rather than frame 1's empty bank.
        if self.frame < 2:
            self.step("fixture-resources")
        initial = self.evidence.capture(self, "initial")
        required = {"unitId", "orderState", "buildQueue", "visibleMask", "detectedMask"}
        if initial["units"]:
            self.expect_true(required <= set(initial["units"][0]), "extended units schema", sorted(set(initial["units"][0])))
        return initial

    def __enter__(self):
        try:
            self._launch()
            self._connect_and_pause()
            return self
        except Exception as error:
            if self.evidence is not None:
                self.evidence.unhandled(self, error)
            self.close()
            raise

    def __exit__(self, exc_type, exc, traceback):
        self.close()

    def close(self):
        if self._log is not None:
            self._log.flush()
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()  # only the process created by this session
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()     # likewise, never a name/PID pattern or another host
                self.proc.wait(timeout=5)
        if self._log is not None:
            self._log.close()
            self._log = None

    def capture(self, label):
        return self.evidence.capture(self, label)

    def turn(self, player, payload):
        self.evidence.command(self, player, payload)
        answer = self.call("turn", player=player, bytes=list(payload))
        self.expect(answer["frame"], self.frame, "turn does not advance the frame")
        self.expect(answer["player"], player, "turn player")
        self.expect(answer["bytes"], len(payload), "turn byte count")
        return answer

    def step(self, label=None):
        target = self.frame + 1
        answer = self.call("step", frames=1, fast=True, timeout=30)
        self.frame = answer["frame"]
        self.expect(self.frame, target, "exact one-frame step")
        return self.capture(label or f"frame-{target}")

    def step_to(self, target):
        if target < self.frame:
            raise ValueError(f"cannot step backwards from {self.frame} to {target}")
        # Long production timers need not generate thousands of duplicate full
        # states. The engine still advances the exact requested frame count;
        # command and transition boundaries use one-frame steps explicitly.
        while target - self.frame > 120:
            count = min(120, target - self.frame)
            answer = self.call("step", frames=count, fast=True, timeout=30)
            self.frame = answer["frame"]
            self.capture(f"frame-{self.frame}")
        while self.frame < target:
            self.step()
        return self.evidence.last
