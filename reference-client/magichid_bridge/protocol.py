"""MagicHID UART wire protocol -- framing ALGORITHM (COBS + CRC16).

Constants come from _defs.py, which is GENERATED from spec/protocol.yaml by
tools/gen_protocol.py -- so message types/flags/sizes never drift from the firmware.
The algorithm below is hand-written (same as mh_protocol.h in C);
spec/protocol_vectors.txt + tools/test_protocol_parity.py prove the two agree byte-for-byte.

Frame on wire:  COBS( [TYPE][SEQ][PAYLOAD..][CRC16 LE] ) + 0x00
CRC16 = CCITT (poly 0x1021, init 0xFFFF) over TYPE..PAYLOAD.
"""
from ._defs import *          # noqa: F401,F403  (T_*, ST_*, NACK_REASONS, HID_*, DELIM, sizes)


def crc16(data: bytes) -> int:
    """CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection)."""
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def cobs_encode(data: bytes) -> bytes:
    dst = bytearray([0])          # reserve first code byte
    code_i = 0
    code = 1
    for b in data:
        if b == 0:
            dst[code_i] = code
            code_i = len(dst); dst.append(0)
            code = 1
        else:
            dst.append(b)
            code += 1
            if code == 0xFF:
                dst[code_i] = code
                code_i = len(dst); dst.append(0)
                code = 1
    dst[code_i] = code
    return bytes(dst)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        code = data[i]; i += 1
        if code == 0:
            raise ValueError("zero byte inside COBS block")
        for _ in range(1, code):
            if i >= n:
                raise ValueError("COBS overrun")
            out.append(data[i]); i += 1
        if code != 0xFF and i < n:
            out.append(0)
    return bytes(out)


def build_frame(mtype: int, seq: int, payload: bytes = b"") -> bytes:
    body = bytes([mtype, seq]) + payload
    crc = crc16(body)
    body += bytes([crc & 0xFF, (crc >> 8) & 0xFF])
    return cobs_encode(body) + bytes([DELIM])


def parse_frame(cobs_chunk: bytes):
    """Decode one de-framed COBS chunk (without the 0x00). Returns (type, seq, payload)."""
    decoded = cobs_decode(cobs_chunk)
    if len(decoded) < 4:
        raise ValueError("frame too short")
    want = crc16(decoded[:-2])
    got = decoded[-2] | (decoded[-1] << 8)
    if want != got:
        raise ValueError("CRC mismatch")
    return decoded[0], decoded[1], decoded[2:-2]


class Deframer:
    """Streaming de-framer: feed raw serial bytes, get back complete (type,seq,payload)."""

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        frames = []
        for b in data:
            if b == DELIM:
                if self._buf:
                    try:
                        frames.append(parse_frame(bytes(self._buf)))
                    except ValueError:
                        pass            # corrupt frame -> drop, resync on next delimiter
                    self._buf = bytearray()
            else:
                self._buf.append(b)
        return frames
