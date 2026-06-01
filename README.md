# MagicHID

[![ci](https://github.com/youseiushida/magichid/actions/workflows/ci.yml/badge.svg)](https://github.com/youseiushida/magichid/actions/workflows/ci.yml)

A transparent USB-HID bridge for the **ESP32-S3**. Drive a target device (PC, phone,
Nintendo Switch, …) as **any HID** from your computer over UART — the ESP32 just relays.

```
operator PC + client ──UART (framed)──▶ ESP32-S3 ──USB HID──▶ target device
       (the brain)                       (dumb relay)          (PC / phone / Switch)
```

## How it works

- The ESP32 is a **dumb, safe relay**: it knows no report layouts, only forwards them, and
  auto-releases held inputs if the operator goes silent (watchdog). All logic lives in the client.
- **`spec/` is the contract** — it fully describes the UART wire protocol and the report
  layouts, so a client can be written in any language from it alone.
- Two device **profiles**, selected at runtime via `SET_IDENTITY`:
  - **universal** — one HID interface exposing all 35 HID Usage Tables pages (Report IDs 1–35).
  - **horipad** — a Nintendo Switch wired gamepad.

## Layout

| Path | What |
|---|---|
| `magichid/` | firmware (Arduino sketch) |
| `spec/` | the wire contract — start at [`PROTOCOL.md`](spec/PROTOCOL.md) |
| `tools/` | code generators (single source → firmware/spec; run with `uv`) |
| `tests/` | host unit tests (no hardware) |

## Build & flash

1. Arduino IDE → **USB Stack: Adafruit TinyUSB**, **USB CDC On Boot: Enabled** (required).
2. Open `magichid/magichid.ino` and upload.
3. Wiring: operator PC → **UART0** (1 Mbps); target device → the **native USB** port.

> Tested on ESP32-S3 with arduino-esp32 **3.3.8** + the Adafruit TinyUSB library.

## Write a client

Everything you need is in [`spec/`](spec): the wire protocol ([`PROTOCOL.md`](spec/PROTOCOL.md)),
the report tables ([`reports.json`](spec/reports.json) and [`horipad.md`](spec/horipad.md)), and
golden vectors ([`protocol_vectors.txt`](spec/protocol_vectors.txt)) to verify your codec
byte-for-byte. A session looks like:

```
open UART (1 Mbps) → PING → wait for STATUS(MOUNTED|READY)
→ SEND_REPORT [report_id, full-state bytes]    # always send the complete report
→ RELEASE_ALL on exit
```

## Develop

```
uv run tools/gen_protocol.py     # spec/protocol.yaml      → magichid/mh_protocol_defs.h + vectors
uv run tools/gen_reports.py      # magichid/hid_descriptor.h → magichid/mh_reports.h + reports.json
pwsh tests/run.ps1               # host tests (g++ + doctest)
```

CI enforces that the generated files are in sync and the host tests pass.

## Notes

- This is a powerful HID-injection tool — use it only for legitimate purposes (automation,
  testing, accessibility, remote control).
- One HID report carries **≤ 63 data bytes** (`CFG_TUD_HID_EP_BUFSIZE` 64 − 1 Report-ID byte).
- Per-page caveats (Power/Battery binding the host's power UI, the Windows POS Barcode driver,
  FIDO needing its own interface) are documented inline in `magichid/hid_descriptor.h`.

## Credits & license

The `horipad` profile's descriptor and report layout are ported from
[esp32beans/switch_ESP32](https://github.com/esp32beans/switch_ESP32) (MIT).
This project is MIT licensed — see [LICENSE](LICENSE).
