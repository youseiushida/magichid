# MagicHID Profile 1 — HORIPAD (Nintendo Switch) Report Layout

This file is part of the **contract**: together with [`PROTOCOL.md`](PROTOCOL.md) it is enough
to build a client (any language) that drives the `horipad` backend. It defines the device
identity and the exact report bytes; the UART framing/messages live in `PROTOCOL.md`.

This profile impersonates a **HORI "HORIPAD" wired controller**, which the Nintendo Switch
accepts as a controller with **no init handshake** — you simply stream input reports.

> Attribution: the descriptor, byte layout, button order, and VID/PID are ported from
> [esp32beans/switch_ESP32](https://github.com/esp32beans/switch_ESP32) (MIT, © 2023
> esp32beans@gmail.com).

## Selecting this profile

Send `SET_IDENTITY [vid:2][pid:2][bcd:2][profile:1]` with `profile = 1` (vid/pid/bcd = 0 keeps
the declared HORIPAD identity below). The device **reboots and re-enumerates**; reconnect the
serial port, then `PING`/`STATUS` until `MOUNTED|READY`.

## USB identity

| Field | Value |
|---|---|
| VID | `0x0F0D` (HORI) |
| PID | `0x00C1` |
| bcdDevice | `0x0100` |

## Report

The HID descriptor carries **no Report ID**. The `SEND_REPORT` payload is therefore exactly
**8 bytes** of controller state (the device sends them with report id `0`). `GET_CAPS` returns a
single entry `[id=0, in_len=8, out_len=0, feat_len=0]`.

Always send the **complete** 8-byte state (full-state principle, `PROTOCOL.md` §4) — never
deltas. The watchdog and `RELEASE_ALL` return the pad to **neutral** (below).

### Byte layout (little-endian)

| Offset | Field | Meaning |
|---|---|---|
| 0 | `buttons` low | button bits 0–7 (see table) |
| 1 | `buttons` high | button bits 8–13 (bits 14–15 are constant `0`) |
| 2 | `dpad` | hat value in the low nibble; high nibble `0` |
| 3 | `LX` | left stick X (`0x00`–`0xFF`, `0x80` center) |
| 4 | `LY` | left stick Y (`0x00`–`0xFF`, `0x80` center) |
| 5 | `RX` | right stick X (`0x00`–`0xFF`, `0x80` center) |
| 6 | `RY` | right stick Y (`0x00`–`0xFF`, `0x80` center) |
| 7 | `vendor` | constant filler, send `0` |

### Buttons (16-bit field, bit index)

| Bit | Button | Bit | Button |
|---|---|---|---|
| 0 | Y | 7 | ZR |
| 1 | B | 8 | Minus (−) |
| 2 | A | 9 | Plus (+) |
| 3 | X | 10 | L-stick click |
| 4 | L | 11 | R-stick click |
| 5 | R | 12 | Home |
| 6 | ZL | 13 | Capture |

Bit `n` lives in byte `0` for `n < 8`, byte `1` for `n ≥ 8`. Bits 14–15 are unused (`0`).

### D-pad / hat (byte 2)

| Value | Direction | Value | Direction |
|---|---|---|---|
| `0` | Up | `5` | Down-Left |
| `1` | Up-Right | `6` | Left |
| `2` | Right | `7` | Up-Left |
| `3` | Down-Right | `0x0F` | **Centered (neutral)** |
| `4` | Down | | |

### Neutral state

Nothing pressed, both sticks centered, hat centered:

```
00 00 0F 80 80 80 80 00
```

## Example

Press **A** (bit 2), everything else neutral:

```
buttons = 1<<2 = 0x0004  ->  04 00 0F 80 80 80 80 00
```

Push the **left stick fully right** while holding **A**:

```
04 00 0F FF 80 80 80 00
```
