"""MagicHID UART wire protocol (mirror of ../../mh_protocol.h).

Frame on wire:  COBS( [TYPE][SEQ][PAYLOAD..][CRC16 LE] ) + 0x00
CRC16 is CCITT (poly 0x1021, init 0xFFFF) over TYPE..PAYLOAD.

Keep this file byte-compatible with mh_protocol.h; tools/test_protocol_parity.py
checks the C and Python implementations agree.
"""

DELIM = 0x00

# operator PC -> ESP32
T_SEND_REPORT = 0x01
T_PING = 0x02
T_RELEASE_ALL = 0x03
T_GET_CAPS = 0x04
T_SET_IDENTITY = 0x05
T_SET_FEATURE = 0x06

# ESP32 -> operator PC
T_STATUS = 0x81
T_ACK = 0x82
T_NACK = 0x83
T_HOST_EVENT = 0x84
T_LOG = 0x85
T_CAPS = 0x86

# STATUS flags
ST_MOUNTED = 0x01
ST_SUSPENDED = 0x02
ST_READY = 0x04
ST_WATCHDOG = 0x08

# NACK reasons
NACK_REASONS = {
    1: "BAD_CRC", 2: "BAD_LEN", 3: "UNKNOWN_ID", 4: "NOT_READY",
    5: "NOT_SENDABLE", 6: "BAD_FRAME", 7: "TOO_BIG",
}

# HID report types (from USB HID spec)
HID_INPUT, HID_OUTPUT, HID_FEATURE = 1, 2, 3

HID_MAX_PAYLOAD = 63   # CFG_TUD_HID_EP_BUFSIZE(64) - 1 report-id byte


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
