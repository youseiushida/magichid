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


# USB-serial bridge VIDs, used to rank auto-detect candidates (lower = probe first).
# 0x1A86 = WCH (CH34x), 0x10C4 = SiLabs (CP210x), 0x0403 = FTDI, 0x303A = Espressif native CDC.
_BRIDGE_VID_RANK = {0x1A86: 0, 0x10C4: 1, 0x0403: 1, 0x303A: 3}


def _probe_port(port, baud, timeout):
    """Open `port` and return True if a running MagicHID answers PING with STATUS."""
    import serial
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except Exception:
        return False
    try:
        time.sleep(2.2)                          # allow possible auto-reset + boot to reach baud
        ser.reset_input_buffer()
        deframer = P.Deframer()
        deadline = time.time() + timeout
        while time.time() < deadline:
            ser.write(P.build_frame(P.T_PING, 1))
            time.sleep(0.12)
            data = ser.read(ser.in_waiting or 1)
            for mtype, _seq, _payload in deframer.feed(data):
                if mtype == P.T_STATUS:
                    return True
        return False
    finally:
        ser.close()


def find_ports(baud=1_000_000, timeout=2.0, all_matches=False):
    """Probe serial ports for a running MagicHID bridge (PING -> STATUS confirms identity).

    Candidates are USB-serial ports (skips Bluetooth virtual COMs), ranked by likely
    bridge VID so the real device is usually probed first. Returns the first matching
    port name, or (all_matches=True) a list of all matches.
    """
    import serial.tools.list_ports as list_ports
    cands = [p for p in list_ports.comports() if p.vid is not None]
    cands.sort(key=lambda p: _BRIDGE_VID_RANK.get(p.vid, 2))
    found = []
    for p in cands:
        if _probe_port(p.device, baud, timeout):
            if not all_matches:
                return p.device
            found.append(p.device)
    return found if all_matches else None


def autodetect(baud=1_000_000, timeout=2.0):
    """Return the COM port of a connected MagicHID device, or raise RuntimeError."""
    port = find_ports(baud=baud, timeout=timeout)
    if port is None:
        raise RuntimeError("MagicHID device not found on any serial port "
                           "(flashed? UART cable connected?)")
    return port


class HIDBridge:
    def __init__(self, port=None, baud=1_000_000, on_host_event=None, on_log=None):
        self.port = port                        # None -> autodetect() on open()
        self.baud = baud
        self.on_host_event = on_host_event      # callback(report_id, report_type, data)
        self.on_log = on_log                    # callback(str)
        self.flags = 0                          # latest STATUS flags
        self.proto_version = None                # device protocol version (from STATUS)
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
        if self.port is None:
            self.port = autodetect(self.baud)   # probe serial ports for a responding MagicHID
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
            self.proto_version = payload[1] if len(payload) > 1 else None
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
