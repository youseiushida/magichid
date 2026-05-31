"""MagicHID operator-side client library.

Example:
    from magichid_bridge import HIDBridge, BY_NAME
    with HIDBridge("COM5") as b:
        b.wait_ready()
        b.send_report("KEYBOARD", bytes([0x00, 0x00, 0x04, 0,0,0,0,0]))  # press 'a'
        b.send_report("KEYBOARD", bytes(8))                              # release
"""
from . import protocol
from .protocol import (
    build_frame, parse_frame, Deframer, crc16, cobs_encode, cobs_decode,
    ST_MOUNTED, ST_READY, ST_SUSPENDED, ST_WATCHDOG,
    HID_INPUT, HID_OUTPUT, HID_FEATURE, HID_MAX_PAYLOAD,
)
from .reports import BY_ID, BY_NAME, ReportInfo, resolve, load_reports
from .bridge import HIDBridge, NackError, autodetect, find_ports

__all__ = [
    "HIDBridge", "NackError", "autodetect", "find_ports", "protocol",
    "BY_ID", "BY_NAME", "ReportInfo", "resolve", "load_reports",
    "build_frame", "parse_frame", "Deframer", "crc16", "cobs_encode", "cobs_decode",
    "ST_MOUNTED", "ST_READY", "ST_SUSPENDED", "ST_WATCHDOG",
    "HID_INPUT", "HID_OUTPUT", "HID_FEATURE", "HID_MAX_PAYLOAD",
]
