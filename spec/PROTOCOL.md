# MagicHID UART Protocol — Wire Specification

This document is the **contract**. Anyone can build a client in any language from this
contract alone (this file plus the per-profile layout and the vectors).

- **Machine-readable source of truth:** [`protocol.yaml`](protocol.yaml) (constant values).
- **Conformance oracle:** [`protocol_vectors.txt`](protocol_vectors.txt) — your codec must
  reproduce every vector byte-for-byte.
- **Report layout depends on the active profile** (§5). The default `universal` profile's
  table is [`reports.json`](reports.json) (generated from the HID descriptor); the `horipad`
  profile's layout is [`horipad.md`](horipad.md). Either way, fetch the active profile's
  report table at runtime via `GET_CAPS`.
- Protocol version: **1** (reported in `STATUS`).

---

## 1. Transport

- Physical: the ESP32 **UART0** (`Serial0`), **1 000 000 baud, 8N1**, no flow control.
- The bridge is a USB **device** to the *target* (PC/phone/Switch) and a UART **slave**
  to the *operator PC*. This protocol is the operator↔bridge link only.

## 2. Framing

```
wire =  COBS( BODY )  ++  0x00
BODY =  TYPE(1)  SEQ(1)  PAYLOAD(0..N)  CRC16(2, little-endian)
```

- **CRC16** = CRC-16/CCITT-FALSE: width 16, poly `0x1021`, init `0xFFFF`, no reflection,
  xorout `0x0000`. Computed over `TYPE..PAYLOAD` (not the CRC bytes).
- **COBS** (Consistent Overhead Byte Stuffing) removes every `0x00` from the COBS output,
  so the single `0x00` delimiter is an unambiguous frame boundary. Decode is the inverse.
- Receivers resync on the next `0x00`. A frame failing COBS/CRC is **dropped silently**.
- `SEQ` is 1..255 for commands (operator→device); device→operator notifications use `SEQ=0`.

To implement the codec, copy CRC-16/CCITT and standard COBS, then verify against
`protocol_vectors.txt`. (Reference C implementation: `magichid/mh_protocol.h`.)

## 3. Messages

### 3.1 Operator PC → device

| Code | Name | Payload | Purpose |
|---|---|---|---|
| `0x01` | SEND_REPORT | `[report_id][hid data…]` | Inject one **INPUT** report into the target |
| `0x02` | PING | *(none)* | Ask for `STATUS` (readiness probe) |
| `0x03` | RELEASE_ALL | *(none)* | Zero every currently-held report (panic/safe release) |
| `0x04` | GET_CAPS | *(none)* | Request the report table (`CAPS` reply) |
| `0x05` | SET_IDENTITY | `[vid:2][pid:2][bcd:2][profile:1]` | Change USB identity/profile; device **reboots** |
| `0x06` | SET_FEATURE | `[report_id][data…]` | Supply the FEATURE value served on host `GET_REPORT` |

All multi-byte integers are **little-endian**.

### 3.2 Device → operator PC

| Code | Name | Payload | Purpose |
|---|---|---|---|
| `0x81` | STATUS | `[flags:1][proto_version:1]` | Sent on PING and on mount/unmount change |
| `0x82` | ACK | `[seq:1]` | A command with this `SEQ` was applied |
| `0x83` | NACK | `[seq:1][reason:1]` | A command was rejected |
| `0x84` | HOST_EVENT | `[report_id][report_type:1][data…]` | Relays a host→device OUTPUT/FEATURE write |
| `0x85` | LOG | `[ascii…]` | Human-readable diagnostic |
| `0x86` | CAPS | `N × [id][in_len][out_len][feat_len]` | The full report table |

### 3.3 STATUS flags (bitfield)

| Bit | Name | Meaning |
|---|---|---|
| `0x01` | MOUNTED | Enumerated by the target host |
| `0x02` | SUSPENDED | USB suspended |
| `0x04` | READY | HID IN endpoint ready to accept a report |
| `0x08` | WATCHDOG | A watchdog auto-release just happened |

### 3.4 NACK reasons

| Code | Name | Meaning |
|---|---|---|
| 1 | BAD_CRC | Frame failed CRC (note: `SEQ` is 0, unmatchable) |
| 2 | BAD_LEN | Payload length invalid for the message/report |
| 3 | UNKNOWN_ID | Report ID not in the descriptor |
| 4 | NOT_READY | USB not mounted / endpoint busy |
| 5 | NOT_SENDABLE | Report has no INPUT (output/feature-only) |
| 6 | BAD_FRAME | Unknown message type |
| 7 | TOO_BIG | Report exceeds the 63-byte HID payload limit |

### 3.5 HID report types (used in HOST_EVENT)

`INPUT=1, OUTPUT=2, FEATURE=3` (USB HID standard).

## 4. Semantics

- **Sizes.** `max_payload = 192` (logical payload). `hid_max_payload = 63`
  (`CFG_TUD_HID_EP_BUFSIZE` 64 − 1 report-id byte). A SEND_REPORT whose report has
  `in_len > 63` is rejected `TOO_BIG`. Payload shorter than the report's `in_len` is
  **zero-padded** by the device to the declared size.
- **Handshake / readiness (order-independent).** The operator may connect before or after
  the target. To learn readiness, send `PING` and read `STATUS`; proceed when
  `MOUNTED|READY` are set. The device also emits `STATUS` on every mount/unmount change.
- **Reliability.** For guaranteed delivery: assign a `SEQ`, send, wait for `ACK[seq]`;
  on timeout **retransmit the same SEQ**. The device is **idempotent per SEQ** — a
  retransmit of an already-applied SEND_REPORT is re-ACKed but **not re-applied** (so a
  lost ACK never double-applies a *relative* report such as a mouse move). For
  fire-and-forget, ignore ACKs; rely on **full-state sending** (below) for self-healing.
- **Full-state principle.** HID is stateful. Always send the *complete current* report
  (e.g. the whole keyboard modifier+keys), not deltas, so a dropped frame self-heals on
  the next send. Hold a key by re-sending its full state periodically (< watchdog timeout).
- **Safety.** If no valid frame arrives for the watchdog timeout (~500 ms) while any report
  is held non-zero, the device auto-releases them (sets `WATCHDOG`). `RELEASE_ALL` forces
  the same. On unmount, all held state is dropped.
- **Bidirectional.** The target host may write OUTPUT (e.g. keyboard LEDs) or FEATURE
  reports; the device relays each as `HOST_EVENT`. For host `GET_REPORT(FEATURE)` the
  device answers instantly from a cache the operator fills with `SET_FEATURE` (so the
  operator is the source of truth; the device just caches + serves).
- **Identity/profiles.** `SET_IDENTITY` persists VID/PID/version/profile and **reboots**
  the bridge for a clean re-enumeration. The operator must reconnect the serial port.

## 5. Profiles (device backends)

The bridge ships several **device backends**; exactly one is active per boot. `SET_IDENTITY`'s
`profile` byte selects one and the device **reboots** to re-enumerate as that device. Each
backend defines its own USB identity, HID report descriptor, and report layout, so the report
table and field layout depend on the active profile (query it at runtime with `GET_CAPS`).

| profile | name | identity | reports | layout |
|---|---|---|---|---|
| `0` | universal | core / `SET_IDENTITY` default | 35 reports, IDs 1..35 | [`reports.json`](reports.json) + HUT 1.7 |
| `1` | horipad | VID `0x0F0D` / PID `0x00C1` | one report, **no Report ID**, 8 bytes | [`horipad.md`](horipad.md) |

- **universal** — the 35-page chimera relay. `SEND_REPORT` payload = `[report_id][hid data…]`;
  sizes come from `reports.json` / `GET_CAPS`, field layout follows the HID descriptor / HUT
  (see the §6 examples).
- **horipad** — a Nintendo Switch wired gamepad. The descriptor carries **no Report ID**, so the
  `SEND_REPORT` payload is the bare 8-byte controller state and the device sends with report
  id `0`. The full byte/bit layout is the contract in [`horipad.md`](horipad.md).

`GET_CAPS` always returns the **active** profile's table: universal → 35 entries; horipad → a
single `[id=0, in_len=8, out_len=0, feat_len=0]`.

## 6. Recommended client session

```
open serial (1 Mbps)
loop: send PING; read STATUS  -> until (MOUNTED and READY)        # handshake
optionally: send GET_CAPS -> read CAPS                            # discover report table
to act:  send SEND_REPORT[report_id, full-state bytes]
         (reliable) wait ACK[seq]; on timeout resend same SEQ
on host LED/feature interest: handle HOST_EVENT; push SET_FEATURE
on exit: send RELEASE_ALL
```

Report payload **layout** (which byte is which field) follows the HID descriptor /
HUT (e.g. keyboard = `[modifier][reserved][k1..k6]`, the TinyUSB mouse =
`[buttons][x][y][wheel][pan]`). Report **sizes** come from `reports.json` or `CAPS`.

## 7. Conformance

Your codec is correct iff, for every line of `protocol_vectors.txt`:
`build_frame(type, seq, payload)` equals the `FRAME` hex, and `cobs_encode(input)` equals
the `COBS` hex (and the inverses round-trip). `tests/run.ps1` (doctest: C codec + policy +
horipad layout) is the reference checker.
