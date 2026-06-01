// =====================================================================================
//  horipad_proto.h  --  HORIPAD (Nintendo Switch) report layout: pure, host-testable.
// =====================================================================================
//  Shared report struct + button/dpad constants + neutral helpers for the Switch
//  gamepad backend (backend_horipad.cpp). NO Arduino/USB dependency, so the exact same
//  definitions also compile under host g++ for tests/test_horipad.cpp.
//
//  The HID report descriptor, the report byte layout, the button order, and the VID/PID
//  are ported from the reference library esp32beans/switch_ESP32 (MIT, (c) 2023
//  esp32beans@gmail.com): https://github.com/esp32beans/switch_ESP32 . That descriptor
//  presents a HORI "HORIPAD" wired pad (VID 0x0F0D / PID 0x00C1) which the Nintendo Switch
//  accepts as a controller with NO init handshake -- you just stream input reports.
//
//  Wire contract (operator -> this backend, via MH_T_SEND_REPORT):
//      payload = exactly 8 bytes = one horipad_report_t (full state, little-endian):
//        [0] buttons.lo  [1] buttons.hi  [2] dpad  [3] LX [4] LY [5] RX [6] RY  [7] vendor
//  The device descriptor carries NO Report ID, so the backend sends with report id 0.
// =====================================================================================
#ifndef MH_HORIPAD_PROTO_H_
#define MH_HORIPAD_PROTO_H_

#include <stdint.h>

// ---- USB identity (HORIPAD wired pad; accepted by Nintendo Switch) --------------------
#define HORIPAD_VID  0x0F0D
#define HORIPAD_PID  0x00C1
#define HORIPAD_BCD  0x0100

// ---- button bit positions inside horipad_report_t.buttons (bit index) -----------------
enum {
  HORIPAD_BTN_Y       = 0,
  HORIPAD_BTN_B       = 1,
  HORIPAD_BTN_A       = 2,
  HORIPAD_BTN_X       = 3,
  HORIPAD_BTN_L       = 4,
  HORIPAD_BTN_R       = 5,
  HORIPAD_BTN_ZL      = 6,
  HORIPAD_BTN_ZR      = 7,
  HORIPAD_BTN_MINUS   = 8,
  HORIPAD_BTN_PLUS    = 9,
  HORIPAD_BTN_LSTICK  = 10,
  HORIPAD_BTN_RSTICK  = 11,
  HORIPAD_BTN_HOME    = 12,
  HORIPAD_BTN_CAPTURE = 13,
};

// ---- D-pad (hat) values; 0x0F = centered (the descriptor's null state) ----------------
enum {
  HORIPAD_DPAD_UP         = 0,
  HORIPAD_DPAD_UP_RIGHT   = 1,
  HORIPAD_DPAD_RIGHT      = 2,
  HORIPAD_DPAD_DOWN_RIGHT = 3,
  HORIPAD_DPAD_DOWN       = 4,
  HORIPAD_DPAD_DOWN_LEFT  = 5,
  HORIPAD_DPAD_LEFT       = 6,
  HORIPAD_DPAD_UP_LEFT    = 7,
  HORIPAD_DPAD_CENTER     = 0x0F,
};

#define HORIPAD_AXIS_CENTER  0x80
#define HORIPAD_REPORT_LEN   8

// ---- the 8-byte input report (matches the descriptor field order; packed, LE) ---------
typedef struct __attribute__((packed)) {
  uint16_t buttons;   // bit0=Y .. bit13=Capture (bits 14,15 are const/unused)
  uint8_t  dpad;      // 0..7 direction, 0x0F centered
  uint8_t  lx, ly;    // left  stick (0x80 center)
  uint8_t  rx, ry;    // right stick (0x80 center)
  uint8_t  vendor;    // const/filler (0)
} horipad_report_t;

// ---- pure helpers (no Arduino) --------------------------------------------------------
static inline void horipad_set_neutral(horipad_report_t *r) {
  r->buttons = 0;
  r->dpad    = HORIPAD_DPAD_CENTER;
  r->lx = r->ly = r->rx = r->ry = HORIPAD_AXIS_CENTER;
  r->vendor  = 0;
}

// True when nothing is pressed and both sticks are centered (watchdog "any held?" basis).
static inline bool horipad_is_neutral(const horipad_report_t *r) {
  return r->buttons == 0 &&
         r->dpad == HORIPAD_DPAD_CENTER &&
         r->lx == HORIPAD_AXIS_CENTER && r->ly == HORIPAD_AXIS_CENTER &&
         r->rx == HORIPAD_AXIS_CENTER && r->ry == HORIPAD_AXIS_CENTER &&
         r->vendor == 0;
}

static inline horipad_report_t horipad_make(uint16_t buttons, uint8_t dpad,
                                            uint8_t lx, uint8_t ly,
                                            uint8_t rx, uint8_t ry) {
  horipad_report_t r;
  r.buttons = buttons; r.dpad = dpad;
  r.lx = lx; r.ly = ly; r.rx = rx; r.ry = ry;
  r.vendor = 0;
  return r;
}

#endif // MH_HORIPAD_PROTO_H_
