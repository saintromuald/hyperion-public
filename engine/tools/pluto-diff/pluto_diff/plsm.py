"""PLSM v4 shared-memory request/response capture for the official Pluto binaries.

The offsets and active lengths come from the official v2.0 DLL writer and
``pluto_infer.exe`` reader/writer, not from the size of reserved capacity.  A
canonical request blob contains the request serial followed by the 48-byte
request header and each active request range in wire order.  A response blob
contains the response serial followed by the exact 40-byte response record.
No action semantics are decoded here.  Publication copying uses the live view
and assumes the official writer does not rewrite active payload for one serial
without changing that serial; other writers need an external lock contract.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
import hashlib
import struct
from typing import Any


MAGIC = b"PLSM"
VERSION = 4
DECLARED_SIZE = 0x85F80
CAPACITY = 0x400
REQUEST_SERIAL_AT = 0x40
RESPONSE_SERIAL_AT = 0x80
REQUEST_HEADER_AT = 0xC0
REQUEST_HEADER_SIZE = 0x30
RESPONSE_AT = 0x85F40
RESPONSE_SIZE = 0x28
PUBLICATION_MAX_ATTEMPTS = 8


class PlsmError(ValueError):
    """The shared-memory snapshot is not a supported complete PLSM v4 image."""


def _u32(image: bytes | bytearray | memoryview, offset: int) -> int:
    return struct.unpack_from("<I", image, offset)[0]


def _validate_image(image: bytes | bytearray | memoryview) -> memoryview:
    """Return a byte view of the original mapping, never an immutable copy."""

    if not isinstance(image, (bytes, bytearray, memoryview)):
        raise TypeError("PLSM input must be bytes-like")
    try:
        raw = memoryview(image)
        if raw.format != "B" or raw.itemsize != 1 or raw.ndim != 1:
            raw = raw.cast("B")
    except (TypeError, ValueError) as exc:
        raise PlsmError("PLSM input must be a contiguous byte-addressable view") from exc
    if not raw.contiguous:
        raise PlsmError("PLSM input must be a contiguous byte-addressable view")
    if raw.nbytes < DECLARED_SIZE:
        raise PlsmError(
            f"truncated PLSM image: need {DECLARED_SIZE} bytes, have {raw.nbytes}"
        )
    if raw[:4].tobytes() != MAGIC:
        raise PlsmError(f"invalid PLSM magic {raw[:4].tobytes()!r}")
    version, declared, capacity = struct.unpack_from("<III", raw, 4)
    if version != VERSION:
        raise PlsmError(f"unsupported PLSM version {version}")
    if declared != DECLARED_SIZE:
        raise PlsmError(f"invalid PLSM declared size 0x{declared:x}, expected 0x{DECLARED_SIZE:x}")
    if capacity != CAPACITY:
        raise PlsmError(f"invalid PLSM unit capacity {capacity}, expected {CAPACITY}")
    return raw


def request_serial(image: bytes | bytearray | memoryview) -> int:
    return _u32(_validate_image(image), REQUEST_SERIAL_AT)


def response_serial(image: bytes | bytearray | memoryview) -> int:
    return _u32(_validate_image(image), RESPONSE_SERIAL_AT)


def _dimensions(image: bytes | bytearray | memoryview) -> tuple[int, int, int]:
    n, width, height = struct.unpack_from("<III", image, REQUEST_HEADER_AT)
    if not 1 <= n <= CAPACITY:
        raise PlsmError(f"invalid PLSM request unit count {n}")
    if width == 0 or height == 0:
        raise PlsmError(f"invalid PLSM map dimensions {width}x{height}")
    return n, width, height


def request_size(n: int) -> int:
    """Size of a canonical request blob, including its four-byte serial."""

    if not 1 <= n <= CAPACITY:
        raise PlsmError(f"invalid PLSM request unit count {n}")
    return 4 + REQUEST_HEADER_SIZE + 48 * n + 436 * n + n + n + 0x14 + 0xE9 + 0x490 + 0x8000 + 0x4000 + 0x20


def _published_copy(
    raw: memoryview,
    serial_at: int,
    kind: str,
    expected_serial: int | None,
    copy_payload: Callable[[int], bytes],
) -> bytes:
    """Read serial/payload/serial from the live view with bounded retries.

    The returned serial is the first value read in each attempt, not a third
    slice of memory that could belong to another publication.  A caller that
    requires one particular serial never silently accepts its replacement.
    """

    for _attempt in range(1, PUBLICATION_MAX_ATTEMPTS + 1):
        published = _u32(raw, serial_at)
        if published == 0:
            raise PlsmError(f"PLSM {kind} serial is zero (no published {kind})")
        if expected_serial is not None and published != expected_serial:
            raise PlsmError(
                f"PLSM {kind} changed during capture: expected serial "
                f"{expected_serial}, got {published}"
            )
        try:
            result = copy_payload(published)
        except PlsmError:
            # Dimensions can be transient while a producer is publishing.  They
            # are retryable only if the live serial actually changed.
            if _u32(raw, serial_at) != published:
                continue
            raise
        if _u32(raw, serial_at) == published:
            return result
    raise PlsmError(
        f"PLSM {kind} serial did not stabilize after "
        f"{PUBLICATION_MAX_ATTEMPTS} live payload reads"
    )


def pack_request(image: bytes | bytearray | memoryview, *, serial: int | None = None) -> bytes:
    """Copy only the currently meaningful request bytes in canonical wire order.

    Reserved capacity, alignment bytes, tail padding, and the unrelated response
    record are deliberately excluded.  The serial is included so two consecutive
    byte-identical published requests remain distinct capture events.  Mutable
    inputs are read through their original live view using a bounded
    serial/payload/serial publication check.
    """

    raw = _validate_image(image)

    def copy_payload(published: int) -> bytes:
        n, _, _ = _dimensions(raw)
        ranges = (
            (REQUEST_HEADER_AT, REQUEST_HEADER_SIZE),
            (0x100, 48 * n),
            (0xC100, 436 * n),
            (0x79100, n),
            (0x79500, n),
            (0x79900, 0x14),
            (0x79940, 0xE9),
            (0x79A40, 0x490),
            (0x79F00, 0x8000),
            (0x81F00, 0x4000),
            (0x85F00, 0x20),
        )
        result = struct.pack("<I", published) + b"".join(
            raw[offset : offset + size].tobytes() for offset, size in ranges
        )
        if len(result) != request_size(n):
            raise AssertionError("PLSM canonical request size arithmetic mismatch")
        return result

    return _published_copy(raw, REQUEST_SERIAL_AT, "request", serial, copy_payload)


def pack_response(image: bytes | bytearray | memoryview, *, serial: int | None = None) -> bytes:
    """Return the response serial and exact 40-byte pre-DLL inference response."""

    raw = _validate_image(image)

    def copy_payload(published: int) -> bytes:
        return struct.pack("<I", published) + raw[
            RESPONSE_AT : RESPONSE_AT + RESPONSE_SIZE
        ].tobytes()

    return _published_copy(raw, RESPONSE_SERIAL_AT, "response", serial, copy_payload)


@dataclass(frozen=True)
class PlsmEvent:
    """One newly published request or response, ready for a frame trace."""

    serial: int
    payload: bytes

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.payload).hexdigest()


class PlsmCapture:
    """Serial-based deduplication; payload changes alone are never used as clocks."""

    def __init__(self) -> None:
        self._last_request = 0
        self._last_response = 0

    def reset(self) -> None:
        """Reset only for a demonstrably new mapping/handshake instance."""

        self._last_request = 0
        self._last_response = 0

    @staticmethod
    def _new(serial: int, previous: int, kind: str) -> bool:
        if serial == 0 or serial == previous:
            return False
        if serial < previous:
            raise PlsmError(
                f"PLSM {kind} serial moved backwards ({previous} -> {serial}); reset requires a new handshake"
            )
        return True

    def request(self, image: bytes | bytearray | memoryview) -> PlsmEvent | None:
        raw = _validate_image(image)
        serial = _u32(raw, REQUEST_SERIAL_AT)
        if not self._new(serial, self._last_request, "request"):
            return None
        payload = pack_request(raw, serial=serial)
        self._last_request = serial
        return PlsmEvent(serial, payload)

    def response(self, image: bytes | bytearray | memoryview) -> PlsmEvent | None:
        raw = _validate_image(image)
        serial = _u32(raw, RESPONSE_SERIAL_AT)
        if not self._new(serial, self._last_response, "response"):
            return None
        payload = pack_response(raw, serial=serial)
        self._last_response = serial
        return PlsmEvent(serial, payload)


def inspect(image: bytes | bytearray | memoryview) -> dict[str, Any]:
    """Return numeric layout evidence without interpreting model action fields."""

    raw = _validate_image(image)
    n, width, height = _dimensions(raw)
    request = _u32(raw, REQUEST_SERIAL_AT)
    response = _u32(raw, RESPONSE_SERIAL_AT)
    answer: dict[str, Any] = {
        "magic": MAGIC.decode("ascii"),
        "version": VERSION,
        "declared_size": DECLARED_SIZE,
        "snapshot_size": len(raw),
        "capacity": CAPACITY,
        "request_serial": request,
        "response_serial": response,
        "response_pending": request != response,
        "units": n,
        "map_width": width,
        "map_height": height,
        "canonical_request_size": request_size(n),
        "canonical_response_size": 4 + RESPONSE_SIZE,
    }
    if request:
        payload = pack_request(raw, serial=request)
        answer["request_sha256"] = hashlib.sha256(payload).hexdigest()
    if response:
        payload = pack_response(raw, serial=response)
        answer["response_sha256"] = hashlib.sha256(payload).hexdigest()
        answer["response_hex"] = payload[4:].hex()
    return answer
