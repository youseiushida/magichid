#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyyaml"]
# ///
# =====================================================================================
#  gen_universal_reports.py
# =====================================================================================
#  Parses magichid/hid_descriptor.h and emits spec/universal_reports.yaml — a complete,
#  field-level definition of every report in the universal (profile 0) HID descriptor.
#
#  The output is designed so that a client (Python, JS, Rust, etc.) can be implemented
#  with ZERO guesswork.  Every byte offset, bit position, signedness, and HID Usage is
#  spelled out explicitly.
#
#  The three reports backed by TinyUSB macros (MOUSE, KEYBOARD, CONSUMER) are defined
#  manually below because the macros are not expanded in the descriptor array.
#
#  Run:  uv run tools/gen_universal_reports.py
# =====================================================================================
import os
import re
import sys
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FW = os.path.join(ROOT, "magichid")
DESCRIPTOR_H = os.path.join(FW, "hid_descriptor.h")
OUT_YAML = os.path.join(ROOT, "spec", "universal_reports.yaml")

# =====================================================================================
#  HUT Usage name lookup tables
# =====================================================================================
# Usage names from HID Usage Tables 1.7, keyed by (page, usage_id)
# Auto-populated from comments in hid_descriptor.h; this table fills gaps.
HUT_NAMES = {
    # Generic Desktop (0x01)
    (0x01, 0x30): "X",
    (0x01, 0x31): "Y",
    (0x01, 0x32): "Z",
    (0x01, 0x33): "Rx",
    (0x01, 0x34): "Ry",
    (0x01, 0x35): "Rz",
    (0x01, 0x38): "Wheel",
    (0x01, 0x39): "Hat Switch",
    # Consumer (0x0C) — 16-bit usages
    (0x0C, 0x0238): "AC Pan",
    # Simulation (0x02)
    (0x02, 0xB0): "Aileron",
    (0x02, 0xB8): "Elevator",
    (0x02, 0xBA): "Rudder",
    (0x02, 0xBB): "Throttle",
    (0x02, 0xC3): "Wing Flaps",
    (0x02, 0xC0): "Trigger",
    # VR (0x03)
    (0x03, 0x20): "Stereo Enable",
    (0x03, 0x21): "Display Enable",
    # Sport (0x04)
    (0x04, 0x33): "Stick Speed",
    (0x04, 0x34): "Stick Face Angle",
    (0x04, 0x35): "Stick Heel/Toe",
    (0x04, 0x37): "Stick Tempo",
    # Game (0x05)
    (0x05, 0x21): "Turn Right/Left",
    (0x05, 0x22): "Pitch Forward/Backward",
    (0x05, 0x23): "Roll Right/Left",
    (0x05, 0x24): "Move Right/Left",
    (0x05, 0x25): "Move Forward/Backward",
    (0x05, 0x26): "Move Up/Down",
    # Generic Device (0x06)
    (0x06, 0x20): "Battery Strength",
    (0x06, 0x21): "Wireless Channel",
    (0x06, 0x22): "Wireless ID",
    (0x06, 0x27): "Sequence ID",
    (0x06, 0x29): "RF Signal Strength",
    # Button (0x09)
    (0x09, 0x01): "Button 1",
    (0x09, 0x02): "Button 2",
    (0x09, 0x03): "Button 3",
    (0x09, 0x04): "Button 4",
    (0x09, 0x05): "Button 5",
    (0x09, 0x06): "Button 6",
    (0x09, 0x07): "Button 7",
    (0x09, 0x08): "Button 8",
    # Ordinal (0x0A)
    (0x0A, 0x01): "Instance 1",
    (0x0A, 0x02): "Instance 2",
    (0x0A, 0x03): "Instance 3",
    (0x0A, 0x04): "Instance 4",
    # Telephony (0x0B)
    (0x0B, 0x20): "Hook Switch",
    (0x0B, 0x21): "Flash",
    (0x0B, 0x23): "Hold",
    (0x0B, 0x2F): "Phone Mute",
    (0x0B, 0x31): "Send",
    # LED (0x08)
    (0x08, 0x01): "Num Lock",
    (0x08, 0x02): "Caps Lock",
    (0x08, 0x03): "Scroll Lock",
    (0x08, 0x04): "Compose",
    (0x08, 0x05): "Kana",
    (0x08, 0x09): "Mute",
    (0x08, 0x17): "Off-Hook",
    (0x08, 0x18): "Ring",
    (0x08, 0x19): "Message Waiting",
    (0x08, 0x4B): "Generic Indicator",
    (0x08, 0x52): "RGB LED",
    (0x08, 0x53): "Red LED Channel",
    (0x08, 0x54): "Blue LED Channel",
    (0x08, 0x55): "Green LED Channel",
    # Keyboard/Keypad (0x07) — modifier keys
    (0x07, 0xE0): "Left Control",
    (0x07, 0xE1): "Left Shift",
    (0x07, 0xE2): "Left Alt",
    (0x07, 0xE3): "Left GUI",
    (0x07, 0xE4): "Right Control",
    (0x07, 0xE5): "Right Shift",
    (0x07, 0xE6): "Right Alt",
    (0x07, 0xE7): "Right GUI",
    # Digitizer (0x0D)
    (0x0D, 0x22): "Finger",
    (0x0D, 0x30): "Tip Pressure",
    (0x0D, 0x32): "In Range",
    (0x0D, 0x42): "Tip Switch",
    (0x0D, 0x51): "Contact Identifier",
    (0x0D, 0x54): "Contact Count",
    (0x0D, 0x55): "Contact Count Maximum",
    # Haptics (0x0E)
    (0x0E, 0x10): "Waveform List",
    (0x0E, 0x11): "Duration List",
    (0x0E, 0x21): "Manual Trigger",
    (0x0E, 0x23): "Intensity",
    # PID (0x0F)
    (0x0F, 0x25): "Effect Type",
    (0x0F, 0x26): "ET Constant Force",
    (0x0F, 0x31): "ET Sine",
    (0x0F, 0x40): "ET Spring",
    (0x0F, 0x50): "Duration",
    (0x0F, 0x52): "Gain",
    # Unicode (0x10) — no named usages; Usage IDs ARE code points
    # Eye/Head Tracker (0x12)
    (0x12, 0x20): "Sensor Timestamp",
    (0x12, 0x21): "Position X",
    (0x12, 0x22): "Position Y",
    (0x12, 0x24): "Gaze Point",
    # Aux Display (0x14)
    (0x14, 0x2C): "Display Data",
    (0x14, 0x46): "Display Brightness",
    (0x14, 0x47): "Display Contrast",
    # SoC (0x11)
    (0x11, 0x03): "FirmwareFileId",
    (0x11, 0x04): "FileOffsetInBytes",
    (0x11, 0x06): "FilePayload",
    (0x11, 0x07): "FilePayloadSizeInBytes",
    (0x11, 0x08): "FilePayloadContainsLastBytes",
    # Sensor (0x20) — 16-bit usages
    (0x20, 0x0073): "Accelerometer 3D",
    (0x20, 0x0201): "Sensor State",
    (0x20, 0x030E): "Report Interval",
    (0x20, 0x0453): "Acceleration X",
    (0x20, 0x0454): "Acceleration Y",
    (0x20, 0x0455): "Acceleration Z",
    # Medical (0x40)
    (0x40, 0x20): "VCR/Acquisition",
    (0x40, 0x21): "Freeze/Thaw",
    (0x40, 0x40): "Cine",
    (0x40, 0x41): "Transmit Power",
    (0x40, 0x43): "Focus",
    (0x40, 0x44): "Depth",
    # Braille (0x41)
    (0x41, 0x03): "8 Dot Braille Cell",
    (0x41, 0x05): "Number of Braille Cells",
    # Lighting (0x59) — 16-bit usages
    (0x59, 0x0002): "LampArrayAttributesReport",
    (0x59, 0x0003): "LampCount",
    (0x59, 0x0004): "BoundingBoxWidth",
    (0x59, 0x0005): "BoundingBoxHeight",
    (0x59, 0x0006): "BoundingBoxDepth",
    (0x59, 0x0021): "LampId",
    (0x59, 0x0050): "LampMultiUpdateReport",
    (0x59, 0x0051): "RedUpdateChannel",
    (0x59, 0x0052): "GreenUpdateChannel",
    (0x59, 0x0053): "BlueUpdateChannel",
    (0x59, 0x0054): "IntensityUpdateChannel",
    # Monitor (0x80)
    (0x80, 0x02): "EDID Information",
    (0x80, 0x04): "VESA Version",
    # VESA VC (0x82)
    (0x82, 0x01): "Degauss",
    (0x82, 0x10): "Brightness",
    (0x82, 0x12): "Contrast",
    (0x82, 0x16): "Red Video Gain",
    (0x82, 0x18): "Green Video Gain",
    (0x82, 0x1A): "Blue Video Gain",
    # Power (0x84)
    (0x84, 0x30): "Voltage",
    (0x84, 0x31): "Current",
    (0x84, 0x32): "Frequency",
    (0x84, 0x35): "Percent Load",
    (0x84, 0x50): "Switch On Control",
    (0x84, 0x51): "Switch Off Control",
    # Battery (0x85)
    (0x85, 0x44): "Charging",
    (0x85, 0x45): "Discharging",
    (0x85, 0x64): "Relative State Of Charge",
    (0x85, 0x66): "Remaining Capacity",
    (0x85, 0x67): "Full Charge Capacity",
    (0x85, 0x68): "Run Time To Empty",
    (0x85, 0x6B): "Cycle Count",
    (0x85, 0xD0): "AC Present",
    # Barcode (0x8C)
    (0x8C, 0x30): "Aiming/Pointer Mode",
    (0x8C, 0x60): "Initiate Barcode Read",
    (0x8C, 0x61): "Trigger State",
    (0x8C, 0xFE): "Decoded Data",
    # Scale (0x8D)
    (0x8D, 0x40): "Data Weight",
    (0x8D, 0x41): "Data Scaling",
    (0x8D, 0x50): "Weight Unit",
    (0x8D, 0x80): "Zero Scale",
    # MSR (0x8E)
    (0x8E, 0x11): "Track 1 Length",
    (0x8E, 0x12): "Track 2 Length",
    (0x8E, 0x13): "Track 3 Length",
    (0x8E, 0x21): "Track 1 Data",
    (0x8E, 0x22): "Track 2 Data",
    (0x8E, 0x23): "Track 3 Data",
    # Camera (0x90)
    (0x90, 0x20): "Camera Auto-focus",
    (0x90, 0x21): "Camera Shutter",
    # Arcade (0x91)
    (0x91, 0x30): "GP Analog Input State",
    (0x91, 0x31): "GP Digital Input State",
    (0x91, 0x33): "GP Digital Output State",
    (0x91, 0x40): "Coin Door Lockout",
    # Monitor Enumerated (0x81)
    (0x81, 0x01): "Enum 1",
    (0x81, 0x02): "Enum 2",
    (0x81, 0x03): "Enum 3",
    (0x81, 0x04): "Enum 4",
    (0x81, 0x05): "Enum 5",
    (0x81, 0x06): "Enum 6",
    (0x81, 0x07): "Enum 7",
    (0x81, 0x08): "Enum 8",
    (0x81, 0x09): "Enum 9",
    (0x81, 0x0A): "Enum 10",
    (0x81, 0x0B): "Enum 11",
    (0x81, 0x0C): "Enum 12",
    (0x81, 0x0D): "Enum 13",
    (0x81, 0x0E): "Enum 14",
    (0x81, 0x0F): "Enum 15",
    (0x81, 0x10): "Enum 16",
    # Gaming Device (0x92) — GSA, no HUT names
    # FIDO (0xF1D0)
    (0xF1D0, 0x20): "Input Report Data",
    (0xF1D0, 0x21): "Output Report Data",
}


def usage_name(page, uid):
    """Return a human-readable name for a (page, usage_id) pair."""
    if (page, uid) in HUT_NAMES:
        return HUT_NAMES[(page, uid)]
    # Generic Desktop names not in HUT_NAMES
    if page == 0x01:
        gd = {0x00: "Undefined", 0x01: "Pointer", 0x02: "Mouse", 0x04: "Joystick",
              0x05: "Gamepad", 0x06: "Keyboard", 0x07: "Keypad", 0x08: "Multi-axis Controller",
              0x30: "X", 0x31: "Y", 0x32: "Z", 0x33: "Rx", 0x34: "Ry", 0x35: "Rz",
              0x36: "Slider", 0x37: "Dial", 0x38: "Wheel", 0x39: "Hat Switch",
              0x40: "Vx", 0x41: "Vy", 0x42: "Vz", 0x43: "Vbrx", 0x44: "Vbry", 0x45: "Vbrz"}
        return gd.get(uid, f"Usage(0x{uid:02X})")
    if page == 0x0B:
        tel = {0xB0: "Phone Key 0", 0xB1: "Phone Key 1", 0xB2: "Phone Key 2",
               0xB3: "Phone Key 3", 0xB4: "Phone Key 4", 0xB5: "Phone Key 5",
               0xB6: "Phone Key 6", 0xB7: "Phone Key 7", 0xB8: "Phone Key 8",
               0xB9: "Phone Key 9", 0xBA: "Phone Key *", 0xBB: "Phone Key #"}
        return tel.get(uid, f"Usage(0x{uid:02X})")
    if page == 0x8D:
        sc = {0x51: "Milligram", 0x52: "Gram", 0x53: "Kilogram", 0x54: "Carat",
              0x55: "Taels", 0x56: "Troy Ounce", 0x57: "Ounce", 0x58: "Ton(UK)",
              0x59: "Ton(US)", 0x5A: "Ton(Metric)", 0x5B: "Pound", 0x5C: "Pound(US)"}
        return sc.get(uid, f"Usage(0x{uid:02X})")
    if page == 0x41:
        bra = {0x0201: "Braille Dot 1", 0x0202: "Braille Dot 2", 0x0203: "Braille Dot 3",
               0x0204: "Braille Dot 4", 0x0205: "Braille Dot 5", 0x0206: "Braille Dot 6",
               0x0207: "Braille Dot 7", 0x0208: "Braille Dot 8"}
        return bra.get(uid, f"Usage(0x{uid:04X})")
    return f"Usage(0x{uid:04X})"


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def parse_enum(text):
    """Return {REPORT_ID_NAME: int} from the first enum block."""
    m = re.search(r"enum\s*\{(.*?)\}", text, re.S)
    if not m:
        sys.exit("enum block not found")
    names, val = {}, None
    for raw in m.group(1).splitlines():
        line = raw.strip().rstrip(",").strip()
        mm = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*(\d+))?", line)
        if not mm:
            continue
        val = int(mm.group(2)) if mm.group(2) is not None else (0 if val is None else val + 1)
        names[mm.group(1)] = val
    return names


def parse_comments_from_source(text):
    """Extract per-line comments from the source, keyed by approximate token position.
    Returns a list of (token_index, comment_text) pairs."""
    comments = []
    lines = text.splitlines()
    token_idx = 0
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith("/*"):
            # pure comment line — attach to last token?
            continue
        m = re.search(r"//\s*(.*)", line)
        if m:
            comments.append((token_idx, m.group(1).strip()))
        # Count tokens on this line
        toks = re.findall(r"0[xX][0-9A-Fa-f]+|\d+", line)
        token_idx += len(toks)
    return comments


def extract_tokens(text, enums):
    """Flatten desc_hid_report[] body into a list of ints."""
    m = re.search(r"desc_hid_report\s*\[\s*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        sys.exit("desc_hid_report[] not found")
    body = "\n".join(l for l in m.group(1).splitlines() if "TUD_HID_REPORT_DESC_" not in l)
    # Expand enum REPOFT_ID_* to integers
    if enums:
        body = re.sub(r"REPORT_ID_[A-Z_]+", lambda mm: str(enums.get(mm.group(0), mm.group(0))), body)
    toks = re.findall(r"0[xX][0-9A-Fa-f]+|\d+", body)
    return [int(t, 16) if t.lower().startswith("0x") else int(t) for t in toks]


def parse_item_prefix(p):
    """Return (tag, type, size_bytes)."""
    nbytes = {0: 0, 1: 1, 2: 2, 3: 4}[p & 0x03]
    itype = (p >> 2) & 0x03
    tag = (p >> 4) & 0x0F
    return tag, itype, nbytes


# HID item tags (extracted from prefix byte via (prefix >> 4) & 0x0F)
# Global items (itype=1)
TAG_USAGE_PAGE = 0       # Usage Page
TAG_LOGICAL_MIN = 1      # Logical Minimum
TAG_LOGICAL_MAX = 2      # Logical Maximum
TAG_PHYSICAL_MIN = 3     # Physical Minimum
TAG_PHYSICAL_MAX = 4     # Physical Maximum
TAG_UNIT_EXP = 5         # Unit Exponent
TAG_UNIT = 6             # Unit
TAG_REPORT_SIZE = 7      # Report Size
TAG_REPORT_ID = 8        # Report ID
TAG_REPORT_COUNT = 9     # Report Count
TAG_PUSH = 10            # Push
TAG_POP = 11             # Pop

# Local items (itype=2)
TAG_USAGE = 0            # Usage
TAG_USAGE_MIN = 1        # Usage Minimum
TAG_USAGE_MAX = 2        # Usage Maximum

# Main items (itype=0)
TAG_INPUT = 8            # Input
TAG_OUTPUT = 9           # Output
TAG_FEATURE = 11         # Feature
TAG_COLLECTION = 10      # Collection
TAG_END_COLLECTION = 12  # End Collection

# Long item marker
LONG_ITEM = 0xFE


def sign_extend(val, nbytes):
    """Sign-extend a value based on the number of data bytes in the HID item."""
    if nbytes == 1 and val & 0x80:
        return val - 0x100
    elif nbytes == 2 and val & 0x8000:
        return val - 0x10000
    elif nbytes >= 3 and val & 0x80000000:
        return val - 0x100000000
    return val


def parse_descriptor(tokens):
    """Parse HID descriptor tokens into structured report definitions.
    Returns a list of report dicts, each with field-level detail."""

    i, n = 0, len(tokens)
    reports = []
    current_report = None

    # Global state
    usage_page = 0
    logical_min = 0
    logical_max = 0
    report_size = 0
    report_count = 0

    # Local state (accumulated usages)
    usages = []
    usage_min = None
    usage_max = None

    # Stack for Push/Pop
    state_stack = []

    def push_state():
        state_stack.append({
            'usage_page': usage_page,
            'logical_min': logical_min,
            'logical_max': logical_max,
            'report_size': report_size,
            'report_count': report_count,
        })

    def pop_state():
        nonlocal usage_page, logical_min, logical_max, report_size, report_count
        if state_stack:
            s = state_stack.pop()
            usage_page = s['usage_page']
            logical_min = s['logical_min']
            logical_max = s['logical_max']
            report_size = s['report_size']
            report_count = s['report_count']

    # Per-section-type offset tracking
    section_offsets = {'input': 0, 'output': 0, 'feature': 0}

    def start_report(rid):
        r = {
            'id': rid,
            'input': {'size': 0, 'relative': False, 'fields': []},
            'output': {'size': 0, 'relative': False, 'fields': []},
            'feature': {'size': 0, 'relative': False, 'fields': []},
        }
        reports.append(r)
        for k in section_offsets:
            section_offsets[k] = 0
        return r

    def flush_fields(report_type, main_flags=0):
        """Create field entries from accumulated state and add to current report."""
        if not current_report:
            return
        if report_size == 0 or report_count == 0:
            return

        rt = report_type  # 'input', 'output', or 'feature'
        section = current_report[rt]
        off = section_offsets[rt]

        size_bits = report_size
        total_bytes_for_section = (size_bits * report_count) // 8

        is_const = bool(main_flags & 1)       # Const (padding)
        is_array_mode = not bool(main_flags & 2)  # Array (vs Var)
        has_usage_range = usage_min is not None and usage_max is not None
        is_padding = is_const or (not usages and not has_usage_range)

        if is_padding:
            # Padding field (Const)
            pad_bits = size_bits * report_count
            f = {
                'offset': off // 8,
                'name': '_pad',
                'size_bits': pad_bits,
                'type': 'padding',
            }
            if off % 8 != 0:
                f['bit_offset'] = off % 8
            section['fields'].append(f)
            off += pad_bits
        elif has_usage_range and is_array_mode:
            # Array field: Usage Min..Max, single selector value
            f = {
                'offset': off // 8,
                'name': field_name_from_usages(usages, usage_page, usage_min, usage_max),
                'size_bits': size_bits,
                'count': report_count,
                'usage_page': usage_page,
                'usage_min': usage_min,
                'usage_max': usage_max,
                'logical_min': logical_min,
                'logical_max': logical_max,
                'type': format_type(logical_min, logical_max, size_bits, is_array=True),
            }
            if off % 8 != 0:
                f['bit_offset'] = off % 8
            section['fields'].append(f)
            off += size_bits * report_count
        elif has_usage_range and not is_array_mode:
            # Var mode with Usage Min/Max: individual fields per usage
            for idx in range(report_count):
                uid = usage_min + idx
                if uid > usage_max:
                    uid = usage_max  # clamp
                f = {
                    'offset': off // 8,
                    'name': usage_name(usage_page, uid).lower().replace(" ", "_").replace("/", "_").replace("-", "_"),
                    'size_bits': size_bits,
                    'usage_page': usage_page,
                    'usage': uid,
                    'logical_min': logical_min,
                    'logical_max': logical_max,
                    'type': format_type(logical_min, logical_max, size_bits),
                }
                if off % 8 != 0:
                    f['bit_offset'] = off % 8
                section['fields'].append(f)
                off += size_bits
        elif usages:
            n_usages = len(usages)
            # If single usage with count>1, emit one field with count=N
            if n_usages == 1 and report_count > 1:
                f = {
                    'offset': off // 8,
                    'name': usage_name(usage_page, usages[0]).lower().replace(" ", "_").replace("/", "_").replace("-", "_"),
                    'size_bits': size_bits,
                    'count': report_count,
                    'usage_page': usage_page,
                    'usage': usages[0],
                    'logical_min': logical_min,
                    'logical_max': logical_max,
                    'type': format_type(logical_min, logical_max, size_bits),
                }
                if off % 8 != 0:
                    f['bit_offset'] = off % 8
                section['fields'].append(f)
                off += size_bits * report_count
            else:
                for idx in range(report_count):
                    uid = usages[idx % n_usages]
                    f = {
                        'offset': off // 8,
                        'name': usage_name(usage_page, uid).lower().replace(" ", "_").replace("/", "_").replace("-", "_"),
                        'size_bits': size_bits,
                        'usage_page': usage_page,
                        'usage': uid,
                        'logical_min': logical_min,
                        'logical_max': logical_max,
                        'type': format_type(logical_min, logical_max, size_bits),
                    }
                    if off % 8 != 0:
                        f['bit_offset'] = off % 8
                    section['fields'].append(f)
                    off += size_bits
        else:
            # No usage info — raw bytes
            f = {
                'offset': off // 8,
                'name': f'data',
                'size_bits': total_bytes_for_section * 8,
                'logical_min': logical_min,
                'logical_max': logical_max,
                'type': format_type(logical_min, logical_max, total_bytes_for_section * 8),
            }
            section['fields'].append(f)
            off += size_bits * report_count

        section_offsets[rt] = off
        section['size'] = (off + 7) // 8

    def set_main_flags(report_type, flags_byte):
        if report_type == 'input':
            is_rel = bool((flags_byte >> 2) & 1)
            current_report['input']['relative'] = is_rel

    # ---- Main parse loop ----
    while i < n:
        p = tokens[i]
        if p == LONG_ITEM:
            i += 3 + tokens[i + 1]
            continue

        tag, itype, nbytes = parse_item_prefix(p)
        val = 0
        for k in range(nbytes):
            val |= tokens[i + 1 + k] << (8 * k)

        if itype == 1:  # Global
            if tag == TAG_USAGE_PAGE:
                usage_page = val
            elif tag == TAG_LOGICAL_MIN:
                logical_min = sign_extend(val, nbytes)
            elif tag == TAG_LOGICAL_MAX:
                logical_max = sign_extend(val, nbytes)
            elif tag == TAG_REPORT_SIZE:
                report_size = val
            elif tag == TAG_REPORT_COUNT:
                report_count = val
            elif tag == TAG_REPORT_ID:
                current_report = start_report(val)
                usages = []
                usage_min = None
                usage_max = None
            elif tag == TAG_PUSH:
                push_state()
            elif tag == TAG_POP:
                pop_state()

        elif itype == 2:  # Local
            if tag == TAG_USAGE:
                usages.append(val)
            elif tag == TAG_USAGE_MIN:
                usages = []
                usage_min = val
            elif tag == TAG_USAGE_MAX:
                usage_max = val

        elif itype == 0:  # Main
            if tag == TAG_INPUT:
                set_main_flags('input', val)
                flush_fields('input', val)
                usages = []
                usage_min = None
                usage_max = None
            elif tag == TAG_OUTPUT:
                flush_fields('output', val)
                usages = []
                usage_min = None
                usage_max = None
            elif tag == TAG_FEATURE:
                flush_fields('feature', val)
                usages = []
                usage_min = None
                usage_max = None
            elif tag == TAG_COLLECTION:
                # Usages before a collection label the collection itself,
                # not data fields. Clear them so they don't pollute field names.
                usages = []
            elif tag == TAG_END_COLLECTION:
                usages = []

        i += 1 + nbytes

    return reports


def field_name_from_usages(usages, usage_page, usage_min, usage_max):
    """Derive a field name from accumulated usages or min/max."""
    if usages:
        names = [usage_name(usage_page, u) for u in usages]
        return "_".join(names).lower().replace(" ", "_").replace("/", "_")
    elif usage_min is not None and usage_max is not None:
        if usage_page == 0x09:
            return "buttons"
        return f"usage_{usage_min:04X}_to_{usage_max:04X}"
    return "data"


def format_type(logical_min, logical_max, size_bits, is_array=False):
    """Return a compact type string for a field."""
    if is_array and logical_min == 0 and logical_max < 256 and size_bits == 8:
        return "u8_array"
    if is_array and logical_min == 0 and size_bits == 16:
        return "u16_le_array"
    signed = logical_min < 0
    if size_bits == 1:
        return "u1"
    elif size_bits == 8:
        return "s8" if signed else "u8"
    elif size_bits == 16:
        return "s16_le" if signed else "u16_le"
    elif size_bits == 32:
        return "s32_le" if signed else "u32_le"
    else:
        s = "s" if signed else "u"
        return f"{s}{size_bits}"


def resolve_page_name(page):
    """Return (page_hex, page_name) for a HUT usage page."""
    NAMES = {
        0x01: "Generic Desktop", 0x02: "Simulation Controls", 0x03: "VR Controls",
        0x04: "Sport Controls", 0x05: "Game Controls", 0x06: "Generic Device Controls",
        0x07: "Keyboard/Keypad", 0x08: "LED", 0x09: "Button", 0x0A: "Ordinal",
        0x0B: "Telephony Device", 0x0C: "Consumer", 0x0D: "Digitizers",
        0x0E: "Haptics", 0x0F: "Physical Input Device (PID)", 0x10: "Unicode",
        0x11: "SoC", 0x12: "Eye and Head Trackers", 0x14: "Auxiliary Display",
        0x20: "Sensors", 0x40: "Medical Instrument", 0x41: "Braille Display",
        0x59: "Lighting And Illumination (LampArray)", 0x80: "Monitor",
        0x81: "Monitor Enumerated", 0x82: "VESA Virtual Controls",
        0x84: "Power", 0x85: "Battery System", 0x8C: "Barcode Scanner",
        0x8D: "Scales", 0x8E: "Magnetic Stripe Reader", 0x90: "Camera Control",
        0x91: "Arcade", 0x92: "Gaming Device", 0xF1D0: "FIDO Alliance",
    }
    return f"0x{page:04X}", NAMES.get(page, "Unknown")


# Report ID -> name mapping (from hid_descriptor.h enum)
REPORT_NAMES = {
    1: "REPORT_ID_GENERIC_DESKTOP", 2: "REPORT_ID_SIMULATION", 3: "REPORT_ID_VR",
    4: "REPORT_ID_SPORT", 5: "REPORT_ID_GAME", 6: "REPORT_ID_GENERIC_DEVICE",
    7: "REPORT_ID_KEYBOARD", 8: "REPORT_ID_LED", 9: "REPORT_ID_BUTTON",
    10: "REPORT_ID_ORDINAL", 11: "REPORT_ID_TELEPHONY", 12: "REPORT_ID_CONSUMER",
    13: "REPORT_ID_DIGITIZER", 14: "REPORT_ID_HAPTICS", 15: "REPORT_ID_PID",
    16: "REPORT_ID_UNICODE", 17: "REPORT_ID_SOC", 18: "REPORT_ID_EYE_HEAD_TRACKER",
    19: "REPORT_ID_AUX_DISPLAY", 20: "REPORT_ID_SENSOR", 21: "REPORT_ID_MEDICAL",
    22: "REPORT_ID_BRAILLE", 23: "REPORT_ID_LIGHTING", 24: "REPORT_ID_MONITOR",
    25: "REPORT_ID_MONITOR_ENUM", 26: "REPORT_ID_VESA_VC", 27: "REPORT_ID_POWER",
    28: "REPORT_ID_BATTERY", 29: "REPORT_ID_BARCODE", 30: "REPORT_ID_SCALE",
    31: "REPORT_ID_MSR", 32: "REPORT_ID_CAMERA", 33: "REPORT_ID_ARCADE",
    34: "REPORT_ID_GAMING_DEVICE", 35: "REPORT_ID_FIDO",
}


def fmt_section(section):
    """Format an input/output/feature section for YAML, dropping empty ones."""
    if section['size'] == 0 and not section['fields']:
        return None
    d = {'size': section['size']}
    if section.get('relative'):
        d['relative'] = True
    # Clean up fields: drop internal markers
    clean_fields = []
    for f in section['fields']:
        cf = {k: v for k, v in f.items() if v is not None and v != ''}
        if cf.get('count') == 1:
            cf.pop('count')  # count=1 is implicit
        clean_fields.append(cf)
    d['fields'] = clean_fields
    return d


def build_yaml(reports):
    """Build the final YAML structure."""
    entries = []
    for r in reports:
        page_hex, page_name = resolve_page_name(r.get('page', 0))
        entry = {
            'id': r['id'],
            'page': page_hex,
            'page_name': page_name,
            'device_usage': r.get('device_usage', ''),
        }
        # Add sections
        for section_name in ('input', 'output', 'feature'):
            s = fmt_section(r.get(section_name, {}))
            if s:
                entry[section_name] = s

        # Special notes (collected from descriptor comments)
        notes = r.get('notes', '')
        notes_list = r.get('notes_list', [])
        if notes_list:
            notes = notes + ('; ' if notes else '') + '; '.join(notes_list)
        if notes:
            entry['notes'] = notes

        entries.append(entry)
    return {
        'meta': {
            'version': 1,
            'source': 'magichid/hid_descriptor.h',
            'description': 'Field-level report layout definitions for the MagicHID universal profile (profile 0). '
                           'Each report maps a HID Usage Page to a concrete byte layout. '
                           'Together with protocol.yaml this is sufficient to implement a client driver for every report '
                           'with zero guesswork.',
            'generated_by': 'tools/gen_universal_reports.py',
        },
        'profiles': {
            'universal': {
                'reports': entries,
            }
        }
    }


def add_report_metadata(reports):
    """Add per-report metadata (page, device_usage, notes) from known sources."""
    # Map of report id -> {page, device_usage, notes}
    META = {
        1:  {"page": 0x01, "device_usage": "Mouse"},
        2:  {"page": 0x02, "device_usage": "Flight Simulation Device"},
        3:  {"page": 0x03, "device_usage": "Head Mounted Display"},
        4:  {"page": 0x04, "device_usage": "Golf Club"},
        5:  {"page": 0x05, "device_usage": "3D Game Controller"},
        6:  {"page": 0x06, "device_usage": "Background Nonuser Controls"},
        7:  {"page": 0x07, "device_usage": "Keyboard"},
        8:  {"page": 0x08, "device_usage": "Generic Indicator"},
        9:  {"page": 0x09, "device_usage": "Button 1"},
        10: {"page": 0x0A, "device_usage": "Instance 1"},
        11: {"page": 0x0B, "device_usage": "Phone"},
        12: {"page": 0x0C, "device_usage": "Consumer Control"},
        13: {"page": 0x0D, "device_usage": "Touch Screen"},
        14: {"page": 0x0E, "device_usage": "Simple Haptic Controller"},
        15: {"page": 0x0F, "device_usage": "Physical Input Device"},
        16: {"page": 0x10, "device_usage": "Unicode (U+0041 'A')"},
        17: {"page": 0x11, "device_usage": "SocControl"},
        18: {"page": 0x12, "device_usage": "Eye Tracker"},
        19: {"page": 0x14, "device_usage": "Alphanumeric Display"},
        20: {"page": 0x20, "device_usage": "Accelerometer 3D"},
        21: {"page": 0x40, "device_usage": "Medical Ultrasound"},
        22: {"page": 0x41, "device_usage": "Braille Display"},
        23: {"page": 0x59, "device_usage": "LampArray"},
        24: {"page": 0x80, "device_usage": "Monitor Control"},
        25: {"page": 0x81, "device_usage": "Monitor Control (Enumerated)"},
        26: {"page": 0x82, "device_usage": "Monitor Control (VESA VC)"},
        27: {"page": 0x84, "device_usage": "UPS"},
        28: {"page": 0x85, "device_usage": "Battery System"},
        29: {"page": 0x8C, "device_usage": "Barcode Scanner"},
        30: {"page": 0x8D, "device_usage": "Scales"},
        31: {"page": 0x8E, "device_usage": "MSR Device Read-Only"},
        32: {"page": 0x90, "device_usage": "Camera Auto-focus"},
        33: {"page": 0x91, "device_usage": "General Purpose IO Card"},
        34: {"page": 0x92, "device_usage": "GSA-defined 0x01"},
        35: {"page": 0xF1D0, "device_usage": "U2F Authenticator Device"},
    }

    SPECIAL_NOTES = {
        24: ["OVERSIZE: 130B EDID Feature exceeds the default 64B HID buffer. Won't transmit until buffer is enlarged."],
        27: ["OS POWER-BINDING WARNING: A host (esp. Windows) may bind this as a system UPS. Send safe values (100% / AC present) or the OS may pop a battery icon and move toward sleep/shutdown."],
        28: ["OS POWER-BINDING WARNING: Same as Report 27 (Power). Report a healthy charge state or stay silent during bring-up."],
        31: ["OVERSIZE: 229B Input report exceeds the default 64B HID buffer. Won't transmit until buffer is enlarged."],
        35: ["OVERSIZE: 64B data + 1B Report ID = 65B exceeds 63B usable HID payload. Won't transmit as-is.", "FIDO COMPAT: CTAPHID forbids a Report ID and needs exact 64-byte frames. This report enumerated here will not work as a real FIDO authenticator. For real FIDO use a dedicated HID interface."],
    }

    for r in reports:
        rid = r['id']
        if rid in META:
            r['page'] = META[rid]['page']
            r['device_usage'] = META[rid]['device_usage']
        if rid in SPECIAL_NOTES:
            r['notes_list'] = SPECIAL_NOTES[rid]


def merge_bit_fields(fields):
    """Merge adjacent bit fields that share the same byte offset.
    Bit fields from the descriptor parser may be split across byte boundaries
    due to how the HID parser handles bit-level fields."""
    if not fields:
        return fields

    # For now, just ensure byte-aligned offsets are correct
    result = []
    for f in fields:
        # Ensure offsets are byte-aligned for non-bit fields
        if 'bit_offset' not in f and f['size_bits'] >= 8:
            pass  # Already byte-aligned
        result.append(f)
    return result


def main():
    # Read source
    with open(DESCRIPTOR_H, "r", encoding="utf-8", errors="replace") as fh:
        raw_text = fh.read()

    # Parse enum
    enum = parse_enum(raw_text)
    id_to_name = {v: k for k, v in enum.items() if k != "REPORT_ID_COUNT"}

    # Strip comments and parse descriptor
    clean = strip_comments(raw_text)
    tokens = extract_tokens(clean, enum)
    parsed = parse_descriptor(tokens)

    # Add names
    for r in parsed:
        r['name'] = id_to_name.get(r['id'], f"REPORT_{r['id']}")

    # Sort by ID and add metadata
    reports = sorted(parsed, key=lambda r: r['id'])
    add_report_metadata(reports)

    # Build YAML
    data = build_yaml(reports)

    # Write
    os.makedirs(os.path.dirname(OUT_YAML), exist_ok=True)
    with open(OUT_YAML, "w", encoding="utf-8", newline="\n") as f:
        yaml.dump(data, f, default_flow_style=False, allow_unicode=True,
                  sort_keys=False, width=120)

    # Summary
    print(f"Wrote {OUT_YAML}")
    for r in reports:
        inp = r.get('input', {}).get('size', 0)
        out = r.get('output', {}).get('size', 0)
        feat = r.get('feature', {}).get('size', 0)
        rel = " REL" if r.get('input', {}).get('relative') else ""
        print(f"  id {r['id']:>3}  in={inp:>3}  out={out:>3}  feat={feat:>3}{rel}  {r['name']}")


if __name__ == "__main__":
    main()
