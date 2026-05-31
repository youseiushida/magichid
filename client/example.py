#!/usr/bin/env python3
"""MagicHID client example -- uniform interface, no per-device special-casing.

You build raw report bytes per reports.json (the layout generated from hid_descriptor.h)
and send them by Report ID. This demo types "hi", nudges the mouse, then shows how to
reach any of the other 30+ pages via the same send_report() call.

    python example.py COM5
"""
import sys
import time

from magichid_bridge import HIDBridge, BY_NAME, HID_OUTPUT


def on_host_event(report_id, rtype, data):
    # e.g. the target host pushed keyboard LED state (Num/Caps/Scroll) as an Output report
    kind = "OUTPUT" if rtype == HID_OUTPUT else "FEATURE/other"
    print(f"[host->device] report {report_id} ({kind}): {data.hex()}")


# Minimal USB HID keyboard usage codes (HUT Keyboard/Keypad page 0x07).
KEY = {c: 0x04 + i for i, c in enumerate("abcdefghijklmnopqrstuvwxyz")}
MOD_SHIFT = 0x02


def kbd_report(mod=0, *keys):
    """Build the 8-byte keyboard INPUT report: [mod][reserved][k1..k6]."""
    buf = bytearray(8)
    buf[0] = mod
    for i, k in enumerate(keys[:6]):
        buf[2 + i] = k
    return bytes(buf)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
    with HIDBridge(port, on_host_event=on_host_event) as b:
        print("waiting for target to enumerate the device...")
        if not b.wait_ready(timeout=10):
            print("target not ready (is the被操作 device connected?)")
            return

        caps = b.get_caps()
        print(f"device reports {len(caps)} report IDs available")

        KB = BY_NAME["KEYBOARD"].id
        MS = BY_NAME["GENERIC_DESKTOP"].id     # the TinyUSB mouse report

        # --- type "hi" (full-state: press then release each key) ---
        for ch in "hi":
            b.send_report(KB, kbd_report(0, KEY[ch]))   # press
            b.send_report(KB, kbd_report())             # release
            time.sleep(0.02)

        # --- nudge the mouse right: report = [buttons][x][y][wheel][pan] ---
        b.send_report(MS, bytes([0x00, 40, 0, 0, 0]))
        b.send_report(MS, bytes([0x00, 0, 0, 0, 0]))    # stop

        # --- any other page via the SAME call, e.g. Consumer "Volume Up" (0x00E9) ---
        VOL = BY_NAME["CONSUMER"].id
        b.send_report(VOL, bytes([0xE9, 0x00]))         # 16-bit usage, little-endian
        b.send_report(VOL, bytes([0x00, 0x00]))         # release

        print("done. (release_all runs automatically on close)")


if __name__ == "__main__":
    main()
