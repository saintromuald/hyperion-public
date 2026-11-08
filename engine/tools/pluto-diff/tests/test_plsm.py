from __future__ import annotations

import struct
import threading
import unittest
from unittest import mock

import pluto_diff.plsm as plsm
from pluto_diff.plsm import (
    CAPACITY,
    DECLARED_SIZE,
    PUBLICATION_MAX_ATTEMPTS,
    REQUEST_SERIAL_AT,
    RESPONSE_AT,
    RESPONSE_SERIAL_AT,
    PlsmCapture,
    PlsmError,
    inspect,
    pack_request,
    pack_response,
    request_size,
)


def image(n=2, request=1, response=0):
    raw = bytearray(DECLARED_SIZE + 0x80)
    raw[:4] = b"PLSM"
    struct.pack_into("<III", raw, 4, 4, DECLARED_SIZE, CAPACITY)
    struct.pack_into("<I", raw, 0x40, request)
    struct.pack_into("<I", raw, 0x80, response)
    struct.pack_into("<III", raw, 0xC0, n, 128, 128)
    # Every active byte is deterministic; reserved capacity has a different
    # pattern so tests can prove it is not included in canonical requests.
    for i in range(0x100, DECLARED_SIZE):
        raw[i] = (i * 37 + 11) & 0xFF
    struct.pack_into("<III", raw, 0xC0, n, 128, 128)
    struct.pack_into("<I", raw, 0x40, request)
    struct.pack_into("<I", raw, 0x80, response)
    return raw


class PlsmTests(unittest.TestCase):
    def test_layout_and_exact_active_request(self):
        raw = image(n=2, request=7)
        packed = pack_request(raw)
        self.assertEqual(len(packed), request_size(2))
        self.assertEqual(struct.unpack_from("<I", packed)[0], 7)
        self.assertEqual(packed[4:52], raw[0xC0:0xF0])
        cursor = 52
        for offset, size in ((0x100, 96), (0xC100, 872), (0x79100, 2), (0x79500, 2),
                             (0x79900, 0x14), (0x79940, 0xE9), (0x79A40, 0x490),
                             (0x79F00, 0x8000), (0x81F00, 0x4000), (0x85F00, 0x20)):
            self.assertEqual(packed[cursor:cursor + size], raw[offset:offset + size])
            cursor += size
        self.assertEqual(cursor, len(packed))
        info = inspect(raw)
        self.assertEqual(info["request_serial"], 7)
        self.assertEqual(info["response_serial"], 0)
        self.assertTrue(info["response_pending"])
        self.assertEqual(info["units"], 2)

    def test_response_is_serial_plus_exact_40_bytes(self):
        raw = image(response=9)
        packed = pack_response(raw)
        self.assertEqual(len(packed), 44)
        self.assertEqual(packed[:4], raw[0x80:0x84])
        self.assertEqual(packed[4:], raw[0x85F40:0x85F68])
        self.assertNotIn(raw[0x85F68:0x85F80], packed)

    def test_padding_and_reserved_capacity_do_not_change_request_hash(self):
        raw = image(n=1)
        before = pack_request(raw)
        # Outside every active range, including page-rounding tail bytes.
        for offset in (0x100 + 48, 0xC100 + 436, 0x79100 + 1, 0x79914,
                       0x79A29, 0x79ED0, 0x85F20, 0x85F40, 0x85F68, 0x85F80):
            raw[offset] ^= 0xFF
        self.assertEqual(pack_request(raw), before)

    def test_serial_not_payload_change_is_the_capture_clock(self):
        raw = image(request=1, response=1)
        capture = PlsmCapture()
        first = capture.request(raw)
        self.assertIsNotNone(first)
        self.assertIsNone(capture.request(raw))
        # Identical request bytes under a newly published serial are captured.
        struct.pack_into("<I", raw, 0x40, 2)
        second = capture.request(raw)
        self.assertIsNotNone(second)
        self.assertEqual(second.payload[4:], first.payload[4:])
        self.assertNotEqual(second.payload, first.payload)
        self.assertIsNotNone(capture.response(raw))
        self.assertIsNone(capture.response(raw))

    def test_backward_serial_requires_explicit_new_handshake(self):
        raw = image(request=2, response=2)
        capture = PlsmCapture()
        self.assertIsNotNone(capture.request(raw))
        self.assertIsNotNone(capture.response(raw))
        struct.pack_into("<I", raw, 0x40, 1)
        struct.pack_into("<I", raw, 0x80, 1)
        with self.assertRaises(PlsmError):
            capture.request(raw)
        with self.assertRaises(PlsmError):
            capture.response(raw)
        capture.reset()
        self.assertIsNotNone(capture.request(raw))
        self.assertIsNotNone(capture.response(raw))

    def test_zero_serial_is_not_a_publication(self):
        raw = image(request=0, response=0)
        capture = PlsmCapture()
        self.assertIsNone(capture.request(raw))
        self.assertIsNone(capture.response(raw))
        with self.assertRaises(PlsmError):
            pack_request(raw)
        with self.assertRaises(PlsmError):
            pack_response(raw)

    def test_live_memoryview_serial_payload_serial_read_retries_real_writer(self):
        for kind in ("request", "response"):
            with self.subTest(kind=kind):
                raw = image(request=7, response=9)
                view = memoryview(raw)
                serial_at = REQUEST_SERIAL_AT if kind == "request" else RESPONSE_SERIAL_AT
                payload_at = 0x100 if kind == "request" else RESPONSE_AT
                old_serial = 7 if kind == "request" else 9
                real_u32 = plsm._u32
                reads = 0
                mutate = threading.Event()
                mutated = threading.Event()

                def writer():
                    self.assertTrue(mutate.wait(2))
                    raw[payload_at] ^= 0xFF
                    struct.pack_into("<I", raw, serial_at, old_serial + 1)
                    mutated.set()

                def racing_u32(image_value, offset):
                    nonlocal reads
                    value = real_u32(image_value, offset)
                    if offset == serial_at:
                        reads += 1
                        if reads == 1:
                            # Preserve the first serial, then let a real writer
                            # mutate the same live buffer before payload copying.
                            mutate.set()
                            self.assertTrue(mutated.wait(2))
                    return value

                thread = threading.Thread(target=writer)
                thread.start()
                try:
                    with mock.patch.object(plsm, "_u32", new=racing_u32):
                        packed = (
                            pack_request(view)
                            if kind == "request"
                            else pack_response(view)
                        )
                finally:
                    mutate.set()
                    thread.join(2)
                self.assertFalse(thread.is_alive())
                self.assertEqual(reads, 4)  # two genuine serial/payload/serial attempts
                self.assertEqual(struct.unpack_from("<I", packed)[0], old_serial + 1)
                packed_payload_at = 52 if kind == "request" else 4
                self.assertEqual(packed[packed_payload_at], raw[payload_at])

    def test_live_publication_recheck_has_bounded_retries(self):
        raw = image(request=1)
        real_u32 = plsm._u32
        reads = 0

        def changing_u32(image_value, offset):
            nonlocal reads
            value = real_u32(image_value, offset)
            if offset == REQUEST_SERIAL_AT:
                reads += 1
                if reads % 2 == 0:
                    struct.pack_into("<I", raw, REQUEST_SERIAL_AT, value + 1)
                    return value + 1
            return value

        with mock.patch.object(plsm, "_u32", new=changing_u32):
            with self.assertRaisesRegex(PlsmError, "did not stabilize"):
                pack_request(memoryview(raw))
        self.assertEqual(reads, 2 * PUBLICATION_MAX_ATTEMPTS)

    def test_invalid_header_dimensions_and_truncation(self):
        for raw in (b"", b"NOPE" + bytes(DECLARED_SIZE)):
            with self.assertRaises(PlsmError):
                inspect(raw)
        raw = image()
        struct.pack_into("<I", raw, 4, 3)
        with self.assertRaises(PlsmError):
            inspect(raw)
        raw = image()
        struct.pack_into("<I", raw, 0xC0, CAPACITY + 1)
        with self.assertRaises(PlsmError):
            pack_request(raw)
        raw = image()
        struct.pack_into("<I", raw, 0xC4, 0)
        with self.assertRaises(PlsmError):
            pack_request(raw)
        with self.assertRaises(PlsmError):
            request_size(0)

    def test_capacity_boundaries_have_exact_sizes(self):
        for n in (1, 768, CAPACITY):
            raw = image(n=n)
            self.assertEqual(len(pack_request(raw)), request_size(n))


if __name__ == "__main__":
    unittest.main()
