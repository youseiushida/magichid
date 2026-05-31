"""MagicHID operator-side bridge: session, framing, reliability.

Uniform interface only -- no per-device (Keyboard/Mouse) special-casing. You send a
Report ID + raw bytes (built per reports.json / the HUT layout). Optional reliable
delivery waits for the firmware ACK and retransmits on timeout.
"""
import threading
import time

from . import protocol as P
from .reports import BY_ID, resolve


class NackError(Exception):
    def __init__(self, reason):
        super().__init__(f"device NACK: {reason}")
        self.reason = reason


class HIDBridge:
    def __init__(self, port, baud=1_000_000, on_host_event=None, on_log=None):
        self.port = port
        self.baud = baud
        self.on_host_event = on_host_event      # callback(report_id, report_type, data)
        self.on_log = on_log                    # callback(str)
        self.flags = 0                          # latest STATUS flags
        self._ser = None
        self._running = False
        self._reader = None
        self._deframer = P.Deframer()
        self._wlock = threading.Lock()
        self._seq = 0
        self._pending = {}                      # seq -> [Event, result]
        self._status_ev = threading.Event()
        self._caps_ev = threading.Event()
        self._caps = None

    # ---- lifecycle ----
    def open(self):
        import serial                           # lazy: package importable without pyserial
        self._ser = serial.Serial(self.port, self.baud, timeout=0.05)
        self._running = True
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        return self

    def close(self):
        self._running = False
        if self._reader:
            self._reader.join(timeout=1.0)
        if self._ser:
            try:
                self.release_all(reliable=False)
            except Exception:
                pass
            self._ser.close()
            self._ser = None

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    # ---- internals ----
    def _next_seq(self):
        self._seq = (self._seq % 255) + 1       # 1..255, never 0
        return self._seq

    def _write(self, frame: bytes):
        with self._wlock:
            self._ser.write(frame)

    def _read_loop(self):
        while self._running:
            try:
                n = self._ser.in_waiting
                data = self._ser.read(n if n else 1)
            except Exception:
                break
            if not data:
                continue
            for mtype, seq, payload in self._deframer.feed(data):
                self._dispatch(mtype, seq, payload)

    def _dispatch(self, mtype, seq, payload):
        if mtype == P.T_ACK:
            p = self._pending.get(seq)
            if p:
                p[1] = ("ack", None); p[0].set()
        elif mtype == P.T_NACK:
            reason = P.NACK_REASONS.get(payload[0], payload[0]) if payload else "?"
            p = self._pending.get(seq)
            if p:
                p[1] = ("nack", reason); p[0].set()
        elif mtype == P.T_STATUS:
            self.flags = payload[0] if payload else 0
            self._status_ev.set()
        elif mtype == P.T_HOST_EVENT:
            if len(payload) >= 2 and self.on_host_event:
                self.on_host_event(payload[0], payload[1], bytes(payload[2:]))
        elif mtype == P.T_CAPS:
            self._caps = self._parse_caps(payload)
            self._caps_ev.set()
        elif mtype == P.T_LOG:
            msg = bytes(payload).decode("ascii", "replace")
            (self.on_log or (lambda s: print("[bridge]", s)))(msg)

    @staticmethod
    def _parse_caps(payload):
        caps = {}
        for i in range(0, len(payload) - 3, 4):
            rid, inb, outb, featb = payload[i], payload[i + 1], payload[i + 2], payload[i + 3]
            caps[rid] = {"in": inb, "out": outb, "feat": featb}
        return caps

    def _send_reliable(self, mtype, payload, retries, timeout):
        seq = self._next_seq()
        ev = threading.Event()
        self._pending[seq] = [ev, None]
        try:
            for _ in range(retries + 1):
                self._write(P.build_frame(mtype, seq, payload))
                if ev.wait(timeout):
                    kind, info = self._pending[seq][1]
                    if kind == "ack":
                        return True
                    raise NackError(info)
                ev.clear()                       # timeout -> retransmit (idempotent)
            raise TimeoutError("no ACK after retries")
        finally:
            self._pending.pop(seq, None)

    # ---- public API (uniform) ----
    def send_report(self, report, data=b"", reliable=True, retries=3, timeout=0.2):
        """Inject one INPUT report. `report` is an id or name; `data` is raw bytes."""
        rid = resolve(report)
        info = BY_ID.get(rid)
        if info is not None:
            if info.input_bytes == 0:
                raise ValueError(f"report {rid} has no INPUT (output/feature-only)")
            if len(data) > info.input_bytes:
                raise ValueError(f"report {rid} expects <= {info.input_bytes} bytes, got {len(data)}")
        payload = bytes([rid]) + bytes(data)
        if not reliable:
            self._write(P.build_frame(P.T_SEND_REPORT, self._next_seq(), payload))
            return True
        return self._send_reliable(P.T_SEND_REPORT, payload, retries, timeout)

    raw = send_report                           # alias

    def release_all(self, reliable=True):
        if reliable:
            return self._send_reliable(P.T_RELEASE_ALL, b"", 2, 0.2)
        self._write(P.build_frame(P.T_RELEASE_ALL, self._next_seq()))
        return True

    def set_feature(self, report, data, reliable=True):
        """Supply the FEATURE value the firmware serves on host GET_REPORT(Feature)."""
        rid = resolve(report)
        payload = bytes([rid]) + bytes(data)
        if reliable:
            return self._send_reliable(P.T_SET_FEATURE, payload, 2, 0.2)
        self._write(P.build_frame(P.T_SET_FEATURE, self._next_seq(), payload))
        return True

    def set_identity(self, vid, pid, bcd=0, profile=0):
        """Change VID/PID/profile. The bridge ACKs then REBOOTS to re-enumerate;
        reconnect the serial port afterwards."""
        payload = bytes([vid & 0xFF, (vid >> 8) & 0xFF,
                         pid & 0xFF, (pid >> 8) & 0xFF,
                         bcd & 0xFF, (bcd >> 8) & 0xFF, profile & 0xFF])
        self._write(P.build_frame(P.T_SET_IDENTITY, self._next_seq(), payload))

    def get_caps(self, timeout=1.0):
        self._caps_ev.clear()
        self._write(P.build_frame(P.T_GET_CAPS, self._next_seq()))
        if self._caps_ev.wait(timeout):
            return self._caps
        raise TimeoutError("no CAPS reply")

    def ping(self):
        self._write(P.build_frame(P.T_PING, self._next_seq()))

    def wait_ready(self, timeout=5.0):
        """Block until the target has mounted the device and the IN endpoint is ready."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.ping()
            self._status_ev.wait(0.3)
            self._status_ev.clear()
            if (self.flags & P.ST_MOUNTED) and (self.flags & P.ST_READY):
                return True
        return False

    @property
    def mounted(self):
        return bool(self.flags & P.ST_MOUNTED)

    @property
    def ready(self):
        return bool(self.flags & P.ST_READY)
