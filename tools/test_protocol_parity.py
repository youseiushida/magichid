"""Parity test: emit the same frames/COBS as test_protocol_parity.c (Python side).
Run with the client package on PYTHONPATH:  PYTHONPATH=client python tools/test_protocol_parity.py
"""
from magichid_bridge import protocol as P


def main():
    print("FRAME", P.build_frame(0x01, 5, bytes([7, 0, 0, 4, 0, 0, 0, 0, 0])).hex())
    print("FRAME", P.build_frame(0x86, 1, bytes((i * 7 + 1) & 0xFF for i in range(140))).hex())
    print("FRAME", P.build_frame(0x02, 0, b"").hex())
    print("FRAME", P.build_frame(0x84, 0, bytes([8, 2, 0, 0, 0, 0])).hex())

    big = bytes((1 + (i % 200)) for i in range(300))
    enc = P.cobs_encode(big)
    print("COBS", len(enc), enc.hex())
    dec = P.cobs_decode(enc)
    print("DEC", len(dec), dec.hex())


if __name__ == "__main__":
    main()
