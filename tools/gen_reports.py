#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = []
# ///
# =====================================================================================
#  gen_reports.py  --  Single source of truth generator for MagicHID report tables
# =====================================================================================
#  Parses EACH device profile's HID report descriptor and emits, so the firmware and any
#  client never disagree on report layout (sizes AND relative/absolute):
#
#    ../magichid/mh_reports.h   per-profile C tables {id,in_len,out_len,feat_len,flags}
#    ../spec/reports.json       the same tables as JSON, keyed by profile
#
#  relative/absolute is derived straight from each descriptor (the HID Input item's
#  Relative bit), so NO per-report annotation is needed -- adding a backend "just works".
#  The 3 universal reports built from TinyUSB macros (Mouse/Keyboard/Consumer) are not
#  expanded in the descriptor, so their sizes AND relative-ness come from MACRO_SIZES (the
#  real TUD_HID_REPORT_DESC_* templates).
#
#  Run:  uv run tools/gen_reports.py
# =====================================================================================
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "magichid")              # Arduino sketch folder (firmware sources)
OUT_H = os.path.join(FW, "mh_reports.h")
OUT_JSON = os.path.join(ROOT, "spec", "reports.json")

# Report ID -> HUT Usage Page for the universal profile (informational; client convenience).
PAGE = {
    1: 0x01, 2: 0x02, 3: 0x03, 4: 0x04, 5: 0x05, 6: 0x06, 7: 0x07, 8: 0x08,
    9: 0x09, 10: 0x0A, 11: 0x0B, 12: 0x0C, 13: 0x0D, 14: 0x0E, 15: 0x0F, 16: 0x10,
    17: 0x11, 18: 0x12, 19: 0x14, 20: 0x20, 21: 0x40, 22: 0x41, 23: 0x59, 24: 0x80,
    25: 0x81, 26: 0x82, 27: 0x84, 28: 0x85, 29: 0x8C, 30: 0x8D, 31: 0x8E, 32: 0x90,
    33: 0x91, 34: 0x92, 35: 0xF1D0,
}

# universal reports built from TinyUSB macros (not expanded in the descriptor): sizes and
# relative-ness come from the real templates. The MOUSE moves X/Y/wheel/pan RELATIVELY.
MACRO_SIZES = {
    1:  {"in": 5, "out": 0, "feat": 0, "rel": True},   # MOUSE (Generic Desktop): rel X/Y/wheel/pan
    7:  {"in": 8, "out": 1, "feat": 0, "rel": False},  # KEYBOARD: absolute
    12: {"in": 2, "out": 0, "feat": 0, "rel": False},  # CONSUMER: absolute (usage array)
}

# ---- profiles (mirror backends.cpp): each has one HID descriptor array to parse ----------
PROFILES = [
    {  # profile 0
        "name": "universal", "src": "hid_descriptor.h", "array": "desc_hid_report",
        "use_enum": True, "macros": MACRO_SIZES,
        "table": "MH_REPORTS", "count": "MH_REPORT_COUNT",   # kept: used by mh_policy.h
    },
    {  # profile 1
        "name": "horipad", "src": "backend_horipad.cpp", "array": "horipad_hid_desc",
        "use_enum": False, "macros": {},
        "table": "MH_REPORTS_HORIPAD", "count": "MH_REPORTS_HORIPAD_COUNT",
    },
]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)   # block comments
    return re.sub(r"//.*", "", text)                    # line comments


def parse_enum(text):
    """Return {REPORT_ID_NAME: int} from the first enum block."""
    m = re.search(r"enum\s*\{(.*?)\}", text, re.S)
    if not m:
        sys.exit("enum block not found")
    names, val = {}, None
    for raw in m.group(1).splitlines():
        line = raw.strip().rstrip(",").strip()
        mm = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*(\d+))?$", line)
        if not mm:
            continue
        val = int(mm.group(2)) if mm.group(2) is not None else (0 if val is None else val + 1)
        names[mm.group(1)] = val
    return names


def extract_tokens(text, array, enum):
    """Flatten <array>[] body into a list of ints. enum (REPORT_ID_* -> int) may be empty.
    `text` must be comment-stripped (so the non-greedy `};` match is unambiguous)."""
    m = re.search(re.escape(array) + r"\s*\[\s*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        sys.exit(f"{array}[] not found")
    body = "\n".join(l for l in m.group(1).splitlines() if "TUD_HID_REPORT_DESC_" not in l)
    if enum:
        body = re.sub(r"REPORT_ID_[A-Z_]+", lambda mm: str(enum[mm.group(0)]), body)
    toks = re.findall(r"0[xX][0-9A-Fa-f]+|\d+", body)
    return [int(t, 16) if t.lower().startswith("0x") else int(t) for t in toks]


def parse_sizes(tokens):
    """Walk HID short items; return {report_id: {'in','out','feat' (bits), 'rel'}}.
    Descriptors with no Report ID item accumulate under id 0."""
    i, n = 0, len(tokens)
    size = count = 0
    rid = 0                                            # default: no Report ID -> report 0
    acc = {}

    def slot(r):
        return acc.setdefault(r, {"in": 0, "out": 0, "feat": 0, "rel": False})

    while i < n:
        p = tokens[i]
        if p == 0xFE:                                  # long item: [0xFE][len][tag][data...]
            i += 3 + tokens[i + 1]
            continue
        nbytes = {0: 0, 1: 1, 2: 2, 3: 4}[p & 0x03]
        itype = (p >> 2) & 0x03
        tag = (p >> 4) & 0x0F
        val = 0
        for k in range(nbytes):
            val |= tokens[i + 1 + k] << (8 * k)
        if itype == 1:                                 # Global
            if tag == 7:
                size = val
            elif tag == 9:
                count = val
            elif tag == 8:                             # Report ID
                rid = val
                slot(rid)
        elif itype == 0:                               # Main
            if tag == 0x8:                             # Input
                slot(rid)["in"] += size * count
                if (val >> 2) & 1:                     # bit2 = Relative
                    slot(rid)["rel"] = True
            elif tag == 0x9:                           # Output
                slot(rid)["out"] += size * count
            elif tag == 0xB:                           # Feature
                slot(rid)["feat"] += size * count
        i += 1 + nbytes
    return acc


def build_profile(prof):
    """Parse one profile's descriptor -> list of report dicts (sizes in bytes + relative)."""
    text = strip_comments(open(os.path.join(FW, prof["src"]),
                                encoding="utf-8", errors="replace").read())
    enum = parse_enum(text) if prof["use_enum"] else {}
    id_to_name = {v: k for k, v in enum.items() if k != "REPORT_ID_COUNT"}
    bits = parse_sizes(extract_tokens(text, prof["array"], enum))
    ids = range(1, 36) if prof["use_enum"] else sorted(bits.keys())

    reports = []
    for rid in ids:
        if rid in prof["macros"]:
            s = prof["macros"][rid]
            inb, outb, featb, rel = s["in"], s["out"], s["feat"], s["rel"]
        else:
            b = bits.get(rid)
            if b is None:
                sys.exit(f"{prof['name']}: report id {rid} not found in descriptor")
            for k in ("in", "out", "feat"):
                if b[k] % 8:
                    sys.exit(f"{prof['name']} report {rid} {k} not byte-aligned: {b[k]} bits")
            inb, outb, featb, rel = b["in"] // 8, b["out"] // 8, b["feat"] // 8, b["rel"]
        reports.append({
            "id": rid,
            "name": id_to_name.get(rid, f"REPORT_{rid}") if prof["use_enum"]
                    else prof["name"].upper(),
            "page": PAGE.get(rid, 0) if prof["use_enum"] else 0x01,
            "input_bytes": inb, "output_bytes": outb, "feature_bytes": featb,
            "relative": rel,
        })
    return reports


def main():
    built = [(p, build_profile(p)) for p in PROFILES]

    # ---- emit spec/reports.json (keyed by profile) ----
    data = {"profiles": {p["name"]: {"reports": reports} for p, reports in built}}
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2)
        f.write("\n")

    # ---- emit magichid/mh_reports.h (one C table per profile) ----
    L = [
        "// AUTO-GENERATED by tools/gen_reports.py from each profile's HID descriptor.",
        "// Do not edit by hand -- re-run the generator after changing a descriptor.",
        "#ifndef MH_REPORTS_H_",
        "#define MH_REPORTS_H_",
        "#include <stdint.h>",
        '#include "mh_protocol_defs.h"   // MH_REPORT_FLAG_*',
        "",
        "typedef struct {",
        "  uint8_t id;        // Report ID (0 if the descriptor carries no Report ID)",
        "  uint8_t in_len;    // INPUT report size in bytes (0 = no input / cannot send)",
        "  uint8_t out_len;   // OUTPUT report size in bytes",
        "  uint8_t feat_len;  // FEATURE report size in bytes",
        "  uint8_t flags;     // bit0 MH_REPORT_FLAG_RELATIVE (INPUT contains a relative field)",
        "} mh_report_info_t;",
        "",
    ]
    for p, reports in built:
        L.append(f"#define {p['count']} {len(reports)}")
        L.append(f"static const mh_report_info_t {p['table']}[{p['count']}] = {{")
        for r in reports:
            flag = "MH_REPORT_FLAG_RELATIVE" if r["relative"] else "0"
            L.append(
                f"  {{ {r['id']:3d}, {r['input_bytes']:3d}, {r['output_bytes']:3d}, "
                f"{r['feature_bytes']:3d}, {flag} }},  // 0x{r['page']:02X} {r['name']}"
            )
        L += ["};", ""]
    L += ["#endif // MH_REPORTS_H_", ""]
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))

    # ---- console summary ----
    for p, reports in built:
        print(f"[{p['name']}]")
        for r in reports:
            print(f"  id {r['id']:>3}  in {r['input_bytes']:>3}  out {r['output_bytes']:>3}  "
                  f"feat {r['feature_bytes']:>3}  {'REL' if r['relative'] else '   '}  {r['name']}")
    print(f"\nWrote {OUT_H}\nWrote {OUT_JSON}")


if __name__ == "__main__":
    main()
