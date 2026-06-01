#!/usr/bin/env python3
"""HORIPAD (Nintendo Switch) backend demo -- self-contained operator-side driver.

操作PC(UART) -> ESP32 -> 被操作デバイス(Nintendo Switch) へゲームパッド入力を注入します。
profile 1 = backend_horipad を選んでおく必要があります（select_profile() でも切替可）:

    python tools/horipad_demo.py                # 自動検出
    python tools/horipad_demo.py COM8           # ESP32 の UART(操作)ポート
    python tools/horipad_demo.py COM8 --select  # 先に SET_IDENTITY で profile=1 に切替えて再起動

Wire contract (backend_horipad):
    MH_T_SEND_REPORT payload = 8 bytes = [buttons.lo, buttons.hi, dpad, LX, LY, RX, RY, vendor]
    （Report ID 無し。スティック中立 0x80、dpad 中立 0x0F）

⚠ ネイティブUSBを Nintendo Switch（または HID ホスト）に挿しておくこと。Switch のホーム画面 →
   「コントローラー」→「持ち方／順番を変える」画面なら、ボタン/スティックの反応が一目で分かります。
"""
import os
import sys
import time

# magichid_bridge (protocol framing only -- no per-report validation needed here)
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "reference-client"))
import serial                                   # noqa: E402
from magichid_bridge import protocol as P       # noqa: E402

BAUD = 1_000_000

# --- button bit positions inside the 16-bit buttons field (see horipad_proto.h) --------
Y, B, A, X, L, R, ZL, ZR, MINUS, PLUS, LSTICK, RSTICK, HOME, CAPTURE = range(14)
DPAD_UP, DPAD_RIGHT, DPAD_DOWN, DPAD_LEFT, DPAD_CENTER = 0, 2, 4, 6, 0x0F
CENTER = 0x80


def report(buttons=0, dpad=DPAD_CENTER, lx=CENTER, ly=CENTER, rx=CENTER, ry=CENTER):
    """Build the 8-byte HORIPAD report (little-endian buttons)."""
    return bytes([buttons & 0xFF, (buttons >> 8) & 0xFF, dpad & 0xFF, lx, ly, rx, ry, 0])


NEUTRAL = report()


class Link:
    """Tiny fire-and-forget framed UART link (PING/STATUS + SEND_REPORT)."""

    def __init__(self, port):
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self.deframer = P.Deframer()
        self.seq = 0
        self.flags = 0
        time.sleep(2.2)                         # allow possible auto-reset + boot
        self.ser.reset_input_buffer()

    def _send(self, mtype, payload=b""):
        self.seq = (self.seq % 255) + 1
        self.ser.write(P.build_frame(mtype, self.seq, payload))

    def _pump(self):
        for mtype, _seq, payload in self.deframer.feed(self.ser.read(self.ser.in_waiting or 1)):
            if mtype == P.T_STATUS and payload:
                self.flags = payload[0]

    def wait_ready(self, timeout=8.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self._send(P.T_PING)
            time.sleep(0.12)
            self._pump()
            if (self.flags & P.ST_MOUNTED) and (self.flags & P.ST_READY):
                return True
        return False

    def set_profile(self, profile, vid=0, pid=0, bcd=0):
        """SET_IDENTITY -> firmware reboots; vid/pid=0 keeps the backend's declared identity."""
        payload = bytes([vid & 0xFF, (vid >> 8) & 0xFF,
                         pid & 0xFF, (pid >> 8) & 0xFF,
                         bcd & 0xFF, (bcd >> 8) & 0xFF, profile & 0xFF])
        self._send(P.T_SET_IDENTITY, payload)

    def push(self, payload, hold=0.15):
        self._send(P.T_SEND_REPORT, payload)
        time.sleep(hold)

    def close(self):
        try:
            self._send(P.T_RELEASE_ALL)         # leave the pad neutral
        finally:
            self.ser.close()


def autodetect():
    from magichid_bridge.bridge import autodetect as ad
    return ad(baud=BAUD)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    do_select = "--select" in sys.argv
    port = args[0] if args else autodetect()
    print(f"[*] connecting {port}")
    link = Link(port)

    if do_select:
        print("[*] SET_IDENTITY profile=1 (horipad) -> rebooting; reconnecting ...")
        link.set_profile(1)
        link.close()
        time.sleep(3.0)
        link = Link(port)

    if not link.wait_ready():
        print(f"[!] not READY (flags=0x{link.flags:02X}). "
              f"Switch に native USB を挿したか / profile=1(horipad) で焼かれているか確認。")
        print("    profile を切り替えるなら: python tools/horipad_demo.py %s --select" % port)
        link.close()
        return
    print("[+] READY -- driving HORIPAD on the Switch\n")

    # 0) L + R together -- the "持ち方/順番を変える" screen registers the pad on this gesture
    print("    L+R (register on Change Grip/Order screen)")
    link.push(report(buttons=(1 << L) | (1 << R)), hold=0.5)
    link.push(NEUTRAL, hold=0.3)

    # 1) face buttons A/B/X/Y
    for name, bit in (("A", A), ("B", B), ("X", X), ("Y", Y)):
        print(f"    press {name}")
        link.push(report(buttons=1 << bit)); link.push(NEUTRAL)

    # 2) D-pad: up -> right -> down -> left
    print("    dpad circle")
    for d in (DPAD_UP, DPAD_RIGHT, DPAD_DOWN, DPAD_LEFT):
        link.push(report(dpad=d))
    link.push(NEUTRAL)

    # 3) left stick sweep left<->right
    print("    left stick sweep")
    for v in (0x00, 0x40, 0x80, 0xC0, 0xFF):
        link.push(report(lx=v))
    link.push(NEUTRAL)

    # 4) + / - then Home (HOME opens the Switch home menu -- handy to confirm)
    print("    minus / plus")
    link.push(report(buttons=1 << MINUS)); link.push(NEUTRAL)
    link.push(report(buttons=1 << PLUS));  link.push(NEUTRAL)

    link.close()
    print("\n[+] done (released to neutral)")


if __name__ == "__main__":
    main()
