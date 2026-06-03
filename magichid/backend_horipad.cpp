// =====================================================================================
//  backend_horipad.cpp  --  Nintendo Switch HORIPAD gamepad, as a DeviceBackend
// =====================================================================================
//  A minimal, handshake-free Switch controller: a single no-Report-ID HID gamepad whose
//  descriptor + 8-byte report + identity are ported from esp32beans/switch_ESP32 (MIT).
//  The Switch accepts this HORI pad (VID 0x0F0D / PID 0x00C1) as a wired controller and
//  reads input reports directly -- no 0x80/0x01 init protocol (that is the Pro Controller).
//
//  Policy in terms of the seam:
//    - operator MH_T_SEND_REPORT  -> 8 raw bytes = one horipad_report_t -> sendReport(id 0)
//    - MH_T_GET_CAPS              -> one entry [id=0, in=8, out=0, feat=0]
//    - release_all / any_held     -> watchdog auto-return to NEUTRAL (centered, no buttons)
//    - no host OUTPUT/FEATURE, no periodic streaming (send-on-change is enough)
//  It touches the core ONLY through CoreServices; it never sees usb_hid or the UART.
//
//  Descriptor/report/identity attribution: esp32beans/switch_ESP32, MIT License,
//  (c) 2023 esp32beans@gmail.com -- https://github.com/esp32beans/switch_ESP32
// =====================================================================================
#include <Arduino.h>
#include <string.h>

#include "device_backend.h"
#include "backend_horipad.h"
#include "mh_protocol.h"      // MH_T_CAPS, MH_NACK_BAD_LEN, MH_NACK_NOT_READY
#include "mh_caps.h"          // mh_emit_caps + the generated MH_REPORTS_HORIPAD table
#include "horipad_proto.h"    // horipad_report_t + button/dpad/neutral helpers

// ---- injected core services ----------------------------------------------------------
static const CoreServices *sys = nullptr;

// ---- current controller state (the report streamed to the Switch) --------------------
static horipad_report_t g_report;

// ---- HID report descriptor: single report, NO Report ID ------------------------------
//  Ported verbatim from esp32beans/switch_ESP32 (MIT). 14 buttons, 1 8-way hat,
//  2 analog sticks (X/Y/Z/Rz), padded to exactly 8 bytes.
static const uint8_t horipad_hid_desc[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
  0x09, 0x05,        // Usage (Game Pad)
  0xA1, 0x01,        // Collection (Application)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x35, 0x00,        //   Physical Minimum (0)
  0x45, 0x01,        //   Physical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x0E,        //   Report Count (14)
  0x05, 0x09,        //   Usage Page (Button)
  0x19, 0x01,        //   Usage Minimum (0x01)
  0x29, 0x0E,        //   Usage Maximum (0x0E)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x95, 0x02,        //   Report Count (2)
  0x81, 0x01,        //   Input (Const)            -- 2 padding bits -> buttons = 16 bits
  0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
  0x25, 0x07,        //   Logical Maximum (7)
  0x46, 0x3B, 0x01,  //   Physical Maximum (315)
  0x75, 0x04,        //   Report Size (4)
  0x95, 0x01,        //   Report Count (1)
  0x65, 0x14,        //   Unit (Eng Rot: degrees)
  0x09, 0x39,        //   Usage (Hat switch)
  0x81, 0x42,        //   Input (Data,Var,Abs,Null State)
  0x65, 0x00,        //   Unit (None)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x01,        //   Input (Const)            -- 4 padding bits -> dpad byte
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x46, 0xFF, 0x00,  //   Physical Maximum (255)
  0x09, 0x30,        //   Usage (X)   -> left  X
  0x09, 0x31,        //   Usage (Y)   -> left  Y
  0x09, 0x32,        //   Usage (Z)   -> right X
  0x09, 0x35,        //   Usage (Rz)  -> right Y
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x04,        //   Report Count (4)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x01,        //   Input (Const)            -- vendor/filler byte
  0xC0,              // End Collection
};

// =====================================================================================
//  helpers
// =====================================================================================
// Stream the current 8-byte state. No Report ID -> sendReport(0, ...).
static bool hp_send_current() {
  return sys->hid_send(0, (const uint8_t *)&g_report, (uint16_t)sizeof(g_report));
}

// =====================================================================================
//  DeviceBackend callbacks
// =====================================================================================
static void hp_begin(const CoreServices *s) {
  sys = s;
  horipad_set_neutral(&g_report);
}

// Operator pushes a full 8-byte controller state (absolute, so retransmit is idempotent).
static void hp_on_operator(uint8_t seq, const uint8_t *p, uint32_t plen) {
  if (plen != HORIPAD_REPORT_LEN) { sys->nack(seq, MH_NACK_BAD_LEN);   return; }
  if (!sys->hid_ready())          { sys->nack(seq, MH_NACK_NOT_READY); return; }
  memcpy(&g_report, p, HORIPAD_REPORT_LEN);
  if (hp_send_current()) sys->ack(seq);
  else                   sys->nack(seq, MH_NACK_NOT_READY);
}

// One report, id 0: [id=0][in=8][out=0][feat=0][flags=0] (absolute; from the generated table).
static void hp_get_caps(uint8_t seq) {
  mh_emit_caps(sys, seq, MH_REPORTS_HORIPAD, MH_REPORTS_HORIPAD_COUNT);
}

// Watchdog / unmount: return the pad to NEUTRAL (centered sticks, no buttons) and send it.
static void hp_release_all() {
  horipad_set_neutral(&g_report);
  if (sys->hid_ready()) hp_send_current();
}

static bool hp_any_held() {
  return !horipad_is_neutral(&g_report);
}

// ---- the backend (field order must match struct DeviceBackend) -----------------------
const DeviceBackend BACKEND_HORIPAD = {
  "horipad",                                // name
  HORIPAD_VID, HORIPAD_PID, HORIPAD_BCD,    // vid, pid, bcd  (HORI wired pad)
  horipad_hid_desc,                         // desc
  (uint16_t)sizeof(horipad_hid_desc),       // desc_len
  hp_begin,                                 // begin
  hp_on_operator,                           // on_operator
  nullptr,                                  // on_set_feature  (unused)
  hp_get_caps,                              // get_caps
  nullptr,                                  // on_host_out     (no host OUTPUT/FEATURE)
  nullptr,                                  // on_host_get     (no host FEATURE read)
  nullptr,                                  // task            (send-on-change; no streaming)
  hp_release_all,                           // release_all
  hp_any_held,                              // any_held
  nullptr,                                  // session_reset   (no per-session SEQ dedup state)
};
