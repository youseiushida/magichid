#!/usr/bin/env python3
# =====================================================================================
#  gen_reports.py  --  Single source of truth generator for MagicHID
# =====================================================================================
#  Parses ../hid_descriptor.h (the USB-facing report descriptor) and emits, so the
#  firmware and the operator-PC client never disagree on report layout:
#
#    ../mh_reports.h        C table {id, in_len, out_len, feat_len} for the firmware
#    ../client/reports.json same table as JSON for the Python client
#
#  The 3 reports built from TinyUSB macros (Mouse/Keyboard/Consumer) are not expanded
#  here, so their sizes are injected from MACRO_SIZES (computed from the real
#  TUD_HID_REPORT_DESC_* templates in Adafruit TinyUSB's hid_device.h).
#
#  Run:  python tools/gen_reports.py
# =====================================================================================
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HID_H = os.path.join(ROOT, "hid_descriptor.h")
OUT_H = os.path.join(ROOT, "mh_reports.h")
OUT_JSON = os.path.join(ROOT, "client", "reports.json")

# Report ID -> HUT Usage Page (informational; for client convenience / debugging).
PAGE = {
    1: 0x01, 2: 0x02, 3: 0x03, 4: 0x04, 5: 0x05, 6: 0x06, 7: 0x07, 8: 0x08,
    9: 0x09, 10: 0x0A, 11: 0x0B, 12: 0x0C, 13: 0x0D, 14: 0x0E, 15: 0x0F, 16: 0x10,
    17: 0x11, 18: 0x12, 19: 0x14, 20: 0x20, 21: 0x40, 22: 0x41, 23: 0x59, 24: 0x80,
    25: 0x81, 26: 0x82, 27: 0x84, 28: 0x85, 29: 0x8C, 30: 0x8D, 31: 0x8E, 32: 0x90,
    33: 0x91, 34: 0x92, 35: 0xF1D0,
}

# Sizes (bytes) of the macro-built reports, from the real TinyUSB templates:
#   MOUSE   : 5 btn + 3 pad (1B) + X + Y + Wheel + AC Pan (4B) = 5B input
#   KEYBOARD: 8 mod + 8 resv + 6 keys = 8B input ; 5 LED + 3 pad = 1B output
#   CONSUMER: one 16-bit array = 2B input
MACRO_SIZES = {
    1:  {"in": 5, "out": 0, "feat": 0},
    7:  {"in": 8, "out": 1, "feat": 0},
    12: {"in": 2, "out": 0, "feat": 0},
}


def parse_enum(text):
    """Return {REPORT_ID_NAME: int} from the first enum block."""
    m = re.search(r"enum\s*\{(.*?)\}", text, re.S)
    if not m:
        sys.exit("enum block not found")
    names, val = {}, None
    for raw in m.group(1).splitlines():
        line = re.sub(r"//.*", "", raw).strip().rstrip(",").strip()
        if not line:
            continue
        mm = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*(\d+))?$", line)
        if not mm:
            continue
        if mm.group(2) is not None:
            val = int(mm.group(2))
        else:
            val = 0 if val is None else val + 1
        names[mm.group(1)] = val
    return names


def extract_tokens(text, enum):
    """Flatten the desc_hid_report[] body into a list of ints (macros removed)."""
    m = re.search(r"desc_hid_report\s*\[\s*\]\s*=\s*\{(.*)\};", text, re.S)
    if not m:
        sys.exit("desc_hid_report[] not found")
    lines = [l for l in m.group(1).splitlines() if "TUD_HID_REPORT_DESC_" not in l]
    body = "\n".join(lines)
    body = re.sub(r"//.*", "", body)                       # strip line comments
    body = re.sub(r"REPORT_ID_[A-Z_]+",
                  lambda mm: str(enum[mm.group(0)]), body)  # enum -> int
    toks = re.findall(r"0[xX][0-9A-Fa-f]+|\d+", body)
    return [int(t, 16) if t.lower().startswith("0x") else int(t) for t in toks]


def parse_sizes(tokens):
    """Walk HID short items; return {report_id: {'in','out','feat'} in bits}."""
    i, n = 0, len(tokens)
    size = count = rid = None
    acc = {}

    def add(k, bits):
        acc.setdefault(rid, {"in": 0, "out": 0, "feat": 0})[k] += bits

    while i < n:
        p = tokens[i]
        if p == 0xFE:                      # long item: [0xFE][len][tag][data...]
            i += 3 + tokens[i + 1]
            continue
        nbytes = {0: 0, 1: 1, 2: 2, 3: 4}[p & 0x03]
        itype = (p >> 2) & 0x03
        tag = (p >> 4) & 0x0F
        val = 0
        for k in range(nbytes):
            val |= tokens[i + 1 + k] << (8 * k)
        if itype == 1:                     # Global
            if tag == 7:
                size = val
            elif tag == 9:
                count = val
            elif tag == 8:                 # Report ID
                rid = val
                acc.setdefault(rid, {"in": 0, "out": 0, "feat": 0})
        elif itype == 0:                   # Main
            if tag == 0x8:
                add("in", (size or 0) * (count or 0))
            elif tag == 0x9:
                add("out", (size or 0) * (count or 0))
            elif tag == 0xB:
                add("feat", (size or 0) * (count or 0))
        i += 1 + nbytes
    return acc


def main():
    text = open(HID_H, encoding="utf-8", errors="replace").read()
    enum = parse_enum(text)
    id_to_name = {v: k for k, v in enum.items() if k != "REPORT_ID_COUNT"}

    bits = parse_sizes(extract_tokens(text, enum))

    reports = []
    for rid in range(1, 36):
        if rid in MACRO_SIZES:
            s = MACRO_SIZES[rid]
            inb, outb, featb = s["in"], s["out"], s["feat"]
        else:
            b = bits.get(rid)
            if b is None:
                sys.exit(f"report id {rid} not found in descriptor")
            for k in ("in", "out", "feat"):
                if b[k] % 8:
                    sys.exit(f"report {rid} {k} not byte-aligned: {b[k]} bits")
            inb, outb, featb = b["in"] // 8, b["out"] // 8, b["feat"] // 8
        reports.append({
            "id": rid,
            "name": id_to_name.get(rid, f"REPORT_{rid}"),
            "page": PAGE.get(rid, 0),
            "input_bytes": inb,
            "output_bytes": outb,
            "feature_bytes": featb,
        })

    # ---- emit reports.json ----
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump({"reports": reports}, f, indent=2)
        f.write("\n")

    # ---- emit mh_reports.h ----
    lines = [
        "// AUTO-GENERATED by tools/gen_reports.py from hid_descriptor.h.",
        "// Do not edit by hand -- re-run the generator after changing the descriptor.",
        "#ifndef MH_REPORTS_H_",
        "#define MH_REPORTS_H_",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "  uint8_t id;        // Report ID",
        "  uint8_t in_len;    // INPUT report size in bytes (0 = no input / cannot send)",
        "  uint8_t out_len;   // OUTPUT report size in bytes",
        "  uint8_t feat_len;  // FEATURE report size in bytes",
        "} mh_report_info_t;",
        "",
        f"#define MH_REPORT_COUNT {len(reports)}",
        "",
        "static const mh_report_info_t MH_REPORTS[MH_REPORT_COUNT] = {",
    ]
    for r in reports:
        lines.append(
            f"  {{ {r['id']:3d}, {r['input_bytes']:3d}, {r['output_bytes']:3d}, "
            f"{r['feature_bytes']:3d} }},  // 0x{r['page']:02X} {r['name']}"
        )
    lines += ["};", "", "#endif // MH_REPORTS_H_", ""]
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    # ---- console summary ----
    print(f"{'ID':>3} {'page':>6} {'in':>4} {'out':>4} {'feat':>4}  name")
    for r in reports:
        print(f"{r['id']:>3} 0x{r['page']:04X} {r['input_bytes']:>4} "
              f"{r['output_bytes']:>4} {r['feature_bytes']:>4}  {r['name']}")
    print(f"\nWrote {OUT_H}\nWrote {OUT_JSON}")


if __name__ == "__main__":
    main()
