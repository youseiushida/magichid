#!/usr/bin/env python3
"""Conformance test (Python): verify the reference-client codec against the golden vectors.

Reads spec/protocol_vectors.txt and checks build_frame / cobs_encode reproduce each
expected output (and that parse/decode round-trips). The C firmware codec is checked
against the SAME file by test_protocol_parity.c -- so both conform to one contract.

    PYTHONPATH=reference-client python tools/test_protocol_parity.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "reference-client"))
from magichid_bridge import protocol as P  # noqa: E402

VEC = os.path.join(ROOT, "spec", "protocol_vectors.txt")


def unhex(s):
    return b"" if s == "-" else bytes.fromhex(s)


def main():
    total = fails = 0
    for line in open(VEC, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = line.split()
        if f[0] == "FRAME":
            t, seq, payload, expect = int(f[1], 16), int(f[2]), unhex(f[3]), f[4]
            total += 1
            got = P.build_frame(t, seq, payload).hex()
            if got != expect:
                fails += 1
                print(f"FRAME mismatch t={f[1]} seq={f[2]}\n  exp {expect}\n  got {got}")
            tt, ss, pp = P.parse_frame(bytes.fromhex(expect)[:-1])
            if (tt, ss, bytes(pp)) != (t, seq, payload):
                fails += 1
                print(f"FRAME roundtrip mismatch t={f[1]}")
        elif f[0] == "COBS":
            inp, expect = unhex(f[1]), f[2]
            total += 1
            got = P.cobs_encode(inp).hex()
            if got != expect:
                fails += 1
                print(f"COBS mismatch\n  exp {expect}\n  got {got}")
            if P.cobs_decode(bytes.fromhex(expect)) != inp:
                fails += 1
                print("COBS roundtrip mismatch")
    print(f"Python conformance: {total - fails}/{total} vectors OK")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
