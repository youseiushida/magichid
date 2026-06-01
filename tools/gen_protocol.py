#!/usr/bin/env python3
# =====================================================================================
#  gen_protocol.py  --  derive protocol constants + golden vectors from spec/protocol.yaml
# =====================================================================================
#  Single source: spec/protocol.yaml. This generates:
#    - mh_protocol_defs.h                            C constants (firmware)
#    - spec/protocol_vectors.txt                     golden conformance vectors
#    - reference-client/magichid_bridge/_defs.py     Python constants -- ONLY if such a
#                                                    client dir exists alongside; skipped
#                                                    otherwise (the contract is self-sufficient)
#
#  The framing ALGORITHM (CRC16/COBS) is hand-written in mh_protocol.h (C); a self-contained
#  copy lives here only to MINT the golden vectors. tests/test_parity.cpp verifies the C codec
#  against them; any external client checks itself against the same spec/protocol_vectors.txt.
#
#  Run:  python tools/gen_protocol.py
# =====================================================================================
import os
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "spec", "protocol.yaml")
OUT_DEFS_H = os.path.join(ROOT, "mh_protocol_defs.h")
OUT_DEFS_PY = os.path.join(ROOT, "reference-client", "magichid_bridge", "_defs.py")
OUT_VECTORS = os.path.join(ROOT, "spec", "protocol_vectors.txt")


# ---- self-contained reference codec (used ONLY to mint golden vectors) ----------------
def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def cobs_encode(data):
    dst = bytearray([0]); code_i = 0; code = 1
    for b in data:
        if b == 0:
            dst[code_i] = code; code_i = len(dst); dst.append(0); code = 1
        else:
            dst.append(b); code += 1
            if code == 0xFF:
                dst[code_i] = code; code_i = len(dst); dst.append(0); code = 1
    dst[code_i] = code
    return bytes(dst)


def build_frame(mtype, seq, payload):
    body = bytes([mtype, seq]) + bytes(payload)
    crc = crc16(body)
    body += bytes([crc & 0xFF, (crc >> 8) & 0xFF])
    return cobs_encode(body) + b"\x00"


# ---- golden vector inputs (chosen to exercise zeros, the COBS 0xFF path, empty) -------
FRAME_VECS = [
    (0x01, 5, [7, 0, 0, 4, 0, 0, 0, 0, 0]),                  # SEND_REPORT keyboard 'a'
    (0x81, 0, [0x05, 0x01]),                                 # STATUS MOUNTED|READY, ver 1
    (0x84, 0, [7, 2, 1]),                                    # HOST_EVENT kbd LED
    (0x02, 1, []),                                           # PING (empty)
    (0x86, 1, [(i * 7 + 1) & 0xFF for i in range(140)]),     # CAPS-sized
]
COBS_VECS = [
    [1 + (i % 200) for i in range(300)],                     # > 254 nonzero -> 0xFF path
    [0, 0, 0],
    [1, 2, 0, 3, 0, 0, 4],
]


def load():
    with open(SRC, encoding="utf-8") as f:
        return yaml.safe_load(f)


def emit_defs_h(spec):
    L = ["// AUTO-GENERATED from spec/protocol.yaml by tools/gen_protocol.py. Do not edit.",
         "#ifndef MH_PROTOCOL_DEFS_H_", "#define MH_PROTOCOL_DEFS_H_", "",
         f"#define MH_PROTO_VERSION    {spec['meta']['proto_version']}",
         f"#define MH_DELIM            0x{spec['framing']['delimiter']:02X}",
         f"#define MH_MAX_PAYLOAD      {spec['limits']['max_payload']}",
         f"#define MH_HID_MAX_PAYLOAD  {spec['limits']['hid_max_payload']}", "",
         "// message types"]
    for m in spec["messages"]:
        L.append(f"#define MH_T_{m['name']:<14} 0x{m['code']:02X}")
    L += ["", "// STATUS flag bits"]
    for k, v in spec["status_flags"].items():
        L.append(f"#define MH_ST_{k:<12} 0x{v:02X}")
    L += ["", "// NACK reasons"]
    for k, v in spec["nack_reasons"].items():
        L.append(f"#define MH_NACK_{k:<13} {v}")
    L += ["", "#endif // MH_PROTOCOL_DEFS_H_", ""]
    open(OUT_DEFS_H, "w", encoding="utf-8").write("\n".join(L))
    return OUT_DEFS_H


def emit_defs_py(spec):
    if not os.path.isdir(os.path.dirname(OUT_DEFS_PY)):
        return None                                # reference client deleted -> skip
    L = ['"""AUTO-GENERATED from spec/protocol.yaml by tools/gen_protocol.py. Do not edit."""',
         f"PROTO_VERSION = {spec['meta']['proto_version']}",
         f"DELIM = 0x{spec['framing']['delimiter']:02X}",
         f"MAX_PAYLOAD = {spec['limits']['max_payload']}",
         f"HID_MAX_PAYLOAD = {spec['limits']['hid_max_payload']}", "",
         "# message types"]
    for m in spec["messages"]:
        L.append(f"T_{m['name']} = 0x{m['code']:02X}")
    L += ["", "# STATUS flag bits"]
    for k, v in spec["status_flags"].items():
        L.append(f"ST_{k} = 0x{v:02X}")
    L += ["", "# NACK reasons (code -> name)", "NACK_REASONS = {"]
    for k, v in spec["nack_reasons"].items():
        L.append(f"    {v}: {k!r},")
    L += ["}", "", "# HID report types"]
    for k, v in spec["hid_report_types"].items():
        L.append(f"HID_{k} = {v}")
    L.append("")
    open(OUT_DEFS_PY, "w", encoding="utf-8").write("\n".join(L))
    return OUT_DEFS_PY


def emit_vectors():
    L = ["# MagicHID protocol golden vectors -- generated by tools/gen_protocol.py.",
         "# Verify ANY implementation's codec against these:",
         "#   FRAME <type:hex> <seq:dec> <payload:hex> <frame:hex>   (frame includes 0x00 delimiter)",
         "#   COBS  <input:hex> <encoded:hex>                        (COBS, no delimiter)"]
    for t, s, p in FRAME_VECS:
        L.append(f"FRAME {t:02x} {s} {bytes(p).hex() or '-'} {build_frame(t, s, p).hex()}")
    for v in COBS_VECS:
        L.append(f"COBS {bytes(v).hex()} {cobs_encode(bytes(v)).hex()}")
    L.append("")
    open(OUT_VECTORS, "w", encoding="utf-8").write("\n".join(L))
    return OUT_VECTORS


def main():
    spec = load()
    wrote = [emit_defs_h(spec), emit_vectors()]
    py = emit_defs_py(spec)
    if py:
        wrote.append(py)
    for w in wrote:
        print("Wrote", w)
    if py is None:
        print("(reference-client absent -> skipped _defs.py; contract is self-sufficient)")


if __name__ == "__main__":
    main()
