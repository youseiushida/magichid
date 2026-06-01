// Host unit tests for horipad_proto.h -- the PURE Nintendo Switch HORIPAD report layout.
// Oracle = the descriptor in backend_horipad.cpp (14 buttons + hat + 4 axes, padded to 8
// bytes, no Report ID) and esp32beans/switch_ESP32. No hardware/Arduino. Built + run by
// tests/run.ps1 (g++ + doctest).
#include "doctest.h"
#include "horipad_proto.h"

TEST_CASE("report is exactly 8 bytes (matches the no-ID descriptor)") {
  CHECK(sizeof(horipad_report_t) == 8);
  CHECK(HORIPAD_REPORT_LEN == 8);
}

TEST_CASE("neutral = centered sticks, centered dpad, no buttons") {
  horipad_report_t r;
  horipad_set_neutral(&r);
  CHECK(horipad_is_neutral(&r));
  // (cast packed fields to prvalues -- doctest cannot bind a reference to a packed member)
  CHECK(int(r.buttons) == 0);
  CHECK(int(r.dpad) == 0x0F);
  CHECK(int(r.lx) == 0x80); CHECK(int(r.ly) == 0x80);
  CHECK(int(r.rx) == 0x80); CHECK(int(r.ry) == 0x80);
  CHECK(int(r.vendor) == 0);
}

TEST_CASE("any non-neutral field is detected (watchdog any_held basis)") {
  horipad_report_t r;
  horipad_set_neutral(&r); r.buttons = (uint16_t)1u << HORIPAD_BTN_A;
  CHECK_FALSE(horipad_is_neutral(&r));
  horipad_set_neutral(&r); r.dpad = HORIPAD_DPAD_UP;
  CHECK_FALSE(horipad_is_neutral(&r));
  horipad_set_neutral(&r); r.lx = 0x00;            // stick pushed off-center
  CHECK_FALSE(horipad_is_neutral(&r));
}

TEST_CASE("button bit order matches the Switch HORIPAD layout") {
  CHECK(HORIPAD_BTN_Y == 0);
  CHECK(HORIPAD_BTN_B == 1);
  CHECK(HORIPAD_BTN_A == 2);
  CHECK(HORIPAD_BTN_X == 3);
  CHECK(HORIPAD_BTN_HOME == 12);
  CHECK(HORIPAD_BTN_CAPTURE == 13);
}

TEST_CASE("buttons serialize little-endian into bytes [0],[1] (the wire contract)") {
  horipad_report_t r = horipad_make((uint16_t)1u << HORIPAD_BTN_A, HORIPAD_DPAD_CENTER,
                                    0x80, 0x80, 0x80, 0x80);
  const uint8_t *raw = (const uint8_t *)&r;
  CHECK(raw[0] == 0x04);   // buttons low  (bit2 = A)
  CHECK(raw[1] == 0x00);   // buttons high
  CHECK(raw[2] == 0x0F);   // dpad centered
  CHECK(raw[3] == 0x80);   // LX center
  CHECK(raw[7] == 0x00);   // vendor

  horipad_report_t r2 = horipad_make((uint16_t)1u << HORIPAD_BTN_HOME, HORIPAD_DPAD_CENTER,
                                     0x80, 0x80, 0x80, 0x80);
  const uint8_t *raw2 = (const uint8_t *)&r2;
  CHECK(raw2[0] == 0x00);  // HOME is bit12 -> high byte
  CHECK(raw2[1] == 0x10);  // 1<<(12-8)
}
