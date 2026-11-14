"""Command-line interface for offline capture, parsing, and comparison."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any

from .commands import CommandError, command_dicts
from .compare import compare_traces, human_report
from .hxp import (
    END_MAGIC,
    FRAME_MAGICS,
    REPLY_MAGIC,
    HxpError,
    parse_end,
    parse_frame_stream,
    parse_reply_stream,
)
from .plsm import PlsmError, inspect as inspect_plsm
from .trace import (
    IncompleteTraceError,
    TraceError,
    TraceWriter,
    canonical_json,
    load_trace,
    validate_trace,
)
from .worker_stalls import (
    WorkerStallError,
    analyze_worker_stall_files,
    human_report as worker_stall_human_report,
)


def _json_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    answer: dict[str, Any] = {}
    for key, value in pairs:
        if key in answer:
            raise ValueError(f"duplicate JSON key {key!r}")
        answer[key] = value
    return answer


def _read_json(path: str) -> Any:
    if path == "-":
        text = sys.stdin.read()
    else:
        text = Path(path).read_text(encoding="utf-8")
    return json.loads(
        text,
        object_pairs_hook=_json_pairs,
        parse_constant=lambda value: (_ for _ in ()).throw(
            ValueError(f"non-finite JSON constant {value}")
        ),
    )


def _read_binary(path: str) -> bytes:
    # Deliberately no text conversion, newline conversion, or NUL handling.
    return Path(path).read_bytes()


def _write_text(path: str, text: str) -> None:
    if path == "-":
        sys.stdout.write(text)
        return
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8")


def _required_spec(spec: dict[str, Any], key: str) -> Any:
    if key not in spec or spec[key] is None:
        raise TraceError(f"frame spec is missing {key}; use an empty object/array/bytes file when present")
    return spec[key]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="pluto-diff",
        description="Offline Phase 1 Pluto differential tooling (starts no game or inference process).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    init = sub.add_parser("init", help="create a canonical trace and hash its run configuration")
    init.add_argument("trace", help="new trace directory")
    init.add_argument("--config", required=True, help="run configuration JSON, or - for stdin")

    add = sub.add_parser("add", help="validate and append one complete per-frame record")
    add.add_argument("trace", help="existing unfinished trace directory")
    add.add_argument("--spec", required=True, help="frame numeric/timing JSON, or - for stdin")
    add.add_argument("--state-input", required=True, help="exact Hyperion-to-bridge HXP2/HXP3/HXP4/HXPF bytes")
    add.add_argument("--observation", required=True, help="opaque raw PLSM/BWRL_SHM inference-request bytes")
    add.add_argument("--model-response", required=True, help="opaque raw PLSM/BWRL_SHM inference-response bytes")
    add.add_argument("--bridge-reply", help="optional exact later HXRP classic-command reply bytes")
    add.add_argument("--emitted", required=True, help="DLL-emitted classic TurnBuffer bytes (may be empty)")
    add.add_argument("--received", required=True, help="received classic command bytes (may be empty)")
    add.add_argument("--accepted", required=True, help="accepted classic command bytes (may be empty)")

    finish = sub.add_parser("finish", help="append a complete or partial end record")
    finish.add_argument("trace")
    finish.add_argument("--status", choices=("complete", "partial"), default="complete")
    finish.add_argument("--message", default="")

    compare = sub.add_parser("compare", help="report the first divergence and its exact class")
    compare.add_argument("left")
    compare.add_argument("right")
    compare.add_argument("--json", dest="json_path", help="write result JSON here, or - for stdout")
    compare.add_argument(
        "--report",
        default="-",
        help="write human report here, or - for stdout (default: stdout)",
    )

    worker_stalls = sub.add_parser(
        "worker-stalls",
        help="analyze offline HXP frames and raw HXRP replies for worker-production stalls",
    )
    worker_stalls.add_argument("frames_bin", help="concatenated strict HXP2/HXP3/HXP4/HXPF frames")
    worker_stalls.add_argument(
        "replies_raw_bin", help="raw HXRP capture, possibly containing resynchronised/foreign bytes"
    )
    worker_stalls.add_argument(
        "--threshold", type=int, default=240, help="minimum no-completion frames (default: 240)"
    )
    worker_stalls.add_argument("--json", dest="json_path", help="write result JSON here, or - for stdout")
    worker_stalls.add_argument(
        "--report",
        default="-",
        help="write human report here, or - for stdout (default: stdout)",
    )

    parse_commands = sub.add_parser("parse-commands", help="strictly decode one classic command stream")
    parse_commands.add_argument("input", help="binary command payload")

    parse_hxp = sub.add_parser("parse-hxp", help="strictly decode an HXP/HXRP/HXPE stream")
    parse_hxp.add_argument("input", help="binary transport capture")
    parse_hxp.add_argument("--kind", choices=("auto", "frame", "reply", "end"), default="auto")

    plsm = sub.add_parser("inspect-plsm", help="inspect one official PLSM v4 shared-memory snapshot")
    plsm.add_argument("input", help="binary PLSM mapping snapshot")

    validate = sub.add_parser("validate", help="load a manifest and verify every referenced blob")
    validate.add_argument("trace")
    return parser


def _run(args: argparse.Namespace) -> int:
    if args.command == "init":
        writer = TraceWriter.create(args.trace, _read_json(args.config))
        root = writer.root
        writer.close()
        print(canonical_json({"trace": str(root), "status": "recording"}))
        return 0

    if args.command == "add":
        spec = _read_json(args.spec)
        if not isinstance(spec, dict):
            raise TraceError("frame spec must be a JSON object")
        writer = TraceWriter.open_append(args.trace)
        try:
            writer.append_frame(
                frame=_required_spec(spec, "frame"),
                state_input=_read_binary(args.state_input),
                observation=_read_binary(args.observation),
                game_data=_required_spec(spec, "game_data"),
                model_response=_read_binary(args.model_response),
                emitted_commands=_read_binary(args.emitted),
                emitted_frame=_required_spec(spec, "emitted_frame"),
                received_commands=_read_binary(args.received),
                received_frame=_required_spec(spec, "received_frame"),
                accepted_commands=_read_binary(args.accepted),
                accepted_frame=_required_spec(spec, "accepted_frame"),
                unit_state=_required_spec(spec, "unit_state"),
                observation_of_change=_required_spec(spec, "observation_of_change"),
                bridge_reply=_read_binary(args.bridge_reply) if args.bridge_reply else None,
            )
        finally:
            writer.close()
        print(canonical_json({"trace": args.trace, "appended_frame": spec["frame"]}))
        return 0

    if args.command == "finish":
        trace = load_trace(args.trace)
        if trace.end is not None:
            raise TraceError("trace is already finished")
        writer = TraceWriter.open_append(args.trace)
        writer.finish(status=args.status, message=args.message)
        print(canonical_json({"trace": args.trace, "status": args.status}))
        return 0

    if args.command == "compare":
        result = compare_traces(args.left, args.right)
        if args.json_path:
            _write_text(args.json_path, canonical_json(result) + "\n")
        if args.report:
            _write_text(args.report, human_report(result))
        return {"equivalent": 0, "diverged": 1, "incomplete": 2, "invalid": 3}[result["status"]]

    if args.command == "worker-stalls":
        result = analyze_worker_stall_files(
            args.frames_bin, args.replies_raw_bin, threshold=args.threshold
        )
        if args.json_path:
            _write_text(args.json_path, canonical_json(result) + "\n")
        if args.report:
            _write_text(args.report, worker_stall_human_report(result))
        return 0

    if args.command == "parse-commands":
        commands = command_dicts(_read_binary(args.input))
        print(canonical_json({"commands": commands, "count": len(commands)}))
        return 0

    if args.command == "parse-hxp":
        data = _read_binary(args.input)
        kind = args.kind
        if kind == "auto":
            magic = data[:4]
            if magic in FRAME_MAGICS:
                kind = "frame"
            elif magic == REPLY_MAGIC:
                kind = "reply"
            elif magic == END_MAGIC:
                kind = "end"
            else:
                raise HxpError(f"cannot infer HXP record kind from magic {magic!r}")
        if kind == "frame":
            value: Any = [frame.to_dict() for frame in parse_frame_stream(data)]
        elif kind == "reply":
            value = [reply.to_dict() for reply in parse_reply_stream(data)]
        else:
            end = parse_end(data)
            value = {"winner": end.winner, "reserved": end.reserved}
        print(canonical_json({"kind": kind, "records": value}))
        return 0

    if args.command == "inspect-plsm":
        print(canonical_json(inspect_plsm(_read_binary(args.input))))
        return 0

    if args.command == "validate":
        trace = load_trace(args.trace)
        try:
            blobs = validate_trace(trace)
        except IncompleteTraceError as exc:
            print(f"pluto-diff: incomplete trace: {exc}", file=sys.stderr)
            return 2
        print(
            canonical_json(
                {
                    "trace": str(trace.root),
                    "status": trace.status,
                    "frames": len(trace.frames),
                    "blob_references_verified": blobs,
                    "issues": trace.issues,
                }
            )
        )
        return 0 if trace.status == "complete" else 2

    raise AssertionError(args.command)


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return _run(args)
    except (TraceError, CommandError, HxpError, PlsmError, WorkerStallError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"pluto-diff: error: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
