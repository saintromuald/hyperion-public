"""Phase 1 Pluto differential capture and comparison tools.

Only Python's standard library is used.  The package does not start, attach to,
or communicate with StarCraft, Pluto, or an inference process.
"""

SCHEMA_VERSION = "pluto-diff/v2"

from .commands import (  # noqa: E402
    CommandError,
    InvalidCommandError,
    TruncatedCommandError,
    UnknownOpcodeError,
    parse_commands,
)
from .hxp import HxpError, parse_frame, parse_frame_stream, parse_reply, parse_reply_stream  # noqa: E402
from .compare import compare_traces  # noqa: E402
from .worker_stalls import analyze_worker_stalls  # noqa: E402

__all__ = [
    "SCHEMA_VERSION",
    "CommandError",
    "InvalidCommandError",
    "TruncatedCommandError",
    "UnknownOpcodeError",
    "HxpError",
    "parse_commands",
    "parse_frame",
    "parse_frame_stream",
    "parse_reply",
    "parse_reply_stream",
    "compare_traces",
    "analyze_worker_stalls",
]
