// Host unit tests for mh_policy.h -- the PURE SEND_REPORT decision logic.
// Oracle = spec/PROTOCOL.md (3.4 NACK matrix, 4 Semantics); report-size anchors come
// from spec/reports.json. No hardware/Arduino. Built + run by tests/run.ps1 (g++ + doctest).
#include "doctest.h"
#include "mh_policy.h"

TEST_CASE("report table lookup (in_len anchored to spec/reports.json)") {
  CHECK(mh_find_info(200) == nullptr);            // absent id
  REQUIRE(mh_find_info(7));   CHECK(mh_find_info(7)->in_len  == 8);   // KEYBOARD
  REQUIRE(mh_find_info(8));   CHECK(mh_find_info(8)->in_len  == 0);   // LED (output-only)
  REQUIRE(mh_find_info(29));  CHECK(mh_find_info(29)->in_len == 33);  // BARCODE
  REQUIRE(mh_find_info(35));  CHECK(mh_find_info(35)->in_len == 64);  // FIDO
}

TEST_CASE("relative flag auto-derived from the descriptor (machine-enforceable rule)") {
  REQUIRE(mh_find_info(1));   // MOUSE (Generic Desktop): relative X/Y/wheel/pan
  CHECK((mh_find_info(1)->flags & MH_REPORT_FLAG_RELATIVE) != 0);
  REQUIRE(mh_find_info(7));   // KEYBOARD: absolute
  CHECK((mh_find_info(7)->flags & MH_REPORT_FLAG_RELATIVE) == 0);
}

TEST_CASE("SEND_REPORT classification (PROTOCOL.md 3.4)") {
  const mh_report_info_t *kbd  = mh_find_info(7);    // in_len 8
  const mh_report_info_t *led  = mh_find_info(8);    // in_len 0
  const mh_report_info_t *fido = mh_find_info(35);   // in_len 64

  SUBCASE("unknown id -> UNKNOWN_ID")      { CHECK(mh_classify_send(nullptr, 0) == MH_NACK_UNKNOWN_ID); }
  SUBCASE("output-only -> NOT_SENDABLE")   { CHECK(mh_classify_send(led, 0)     == MH_NACK_NOT_SENDABLE); }
  SUBCASE("in_len>63 -> TOO_BIG")          { CHECK(mh_classify_send(fido, 64)   == MH_NACK_TOO_BIG); }
  SUBCASE("dlen>in_len -> BAD_LEN")        { CHECK(mh_classify_send(kbd, 9)     == MH_NACK_BAD_LEN); }
  SUBCASE("exact size -> accept")          { CHECK(mh_classify_send(kbd, 8)     == 0); }
  SUBCASE("short -> accept (zero-padded)") { CHECK(mh_classify_send(kbd, 0)     == 0); }
}

TEST_CASE("hid_max_payload anchor (PROTOCOL.md 4)") {
  CHECK(MH_HID_MAX_PAYLOAD == 63);
}

TEST_CASE("held detection (PROTOCOL.md 4 Safety: held while non-zero)") {
  uint8_t zero[8] = {0};
  uint8_t mid[5]  = {0, 0, 1, 0, 0};
  uint8_t hi[1]   = {0x80};
  CHECK(mh_report_nonzero(zero, 8) == false);
  CHECK(mh_report_nonzero(mid,  5) == true);
  CHECK(mh_report_nonzero(hi,   1) == true);
  CHECK(mh_report_nonzero(zero, 0) == false);
}
