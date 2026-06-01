// =====================================================================================
//  mh_policy.h  --  PURE, hardware-free decision logic (host-testable; no Arduino deps)
// =====================================================================================
//  These functions encode the SEND_REPORT contract documented in spec/PROTOCOL.md
//  (§3.4 NACK reasons, §4 Semantics). They take plain inputs and return plain results --
//  no usb_hid, no UART, no FreeRTOS, no globals -- so they compile and unit-test on the
//  host with plain gcc/clang (see tests/test_policy.cpp). The oracle is the SPEC, not the
//  current code: if a regenerated table or a code change diverges from PROTOCOL.md, the
//  host test fails before anything is flashed.
//
//  What stays OUT of here (not pure): idempotent-SEQ dedup (stateful), USB readiness
//  (hardware), the actual sendReport()/cache effects. Backends call these for the
//  decision, then perform the effect.
// =====================================================================================
#ifndef MH_POLICY_H_
#define MH_POLICY_H_

#include <stdint.h>
#include <stdbool.h>
#include "mh_protocol_defs.h"   // MH_NACK_*, MH_HID_MAX_PAYLOAD
#include "mh_reports.h"         // mh_report_info_t, MH_REPORTS, MH_REPORT_COUNT

// Look up a report by id in the active table. Pure. Returns NULL if absent.
static inline const mh_report_info_t *mh_find_info(uint8_t id) {
  for (uint8_t i = 0; i < MH_REPORT_COUNT; i++)
    if (MH_REPORTS[i].id == id) return &MH_REPORTS[i];
  return 0;
}

// Classify a SEND_REPORT given the looked-up report info and the data length (bytes after
// the report-id byte). Returns 0 to ACCEPT, else a NACK reason per spec/PROTOCOL.md §3.4:
//   - info == NULL             -> UNKNOWN_ID   (id not in descriptor)
//   - in_len == 0              -> NOT_SENDABLE (output/feature-only report)
//   - in_len > 63 (hid max)    -> TOO_BIG      (exceeds single-report payload limit)
//   - dlen > in_len            -> BAD_LEN      (more data than the report holds)
//   - otherwise                -> 0            (accept; caller zero-pads to in_len)
static inline uint8_t mh_classify_send(const mh_report_info_t *info, uint32_t dlen) {
  if (!info)                             return MH_NACK_UNKNOWN_ID;
  if (info->in_len == 0)                 return MH_NACK_NOT_SENDABLE;
  if (info->in_len > MH_HID_MAX_PAYLOAD) return MH_NACK_TOO_BIG;
  if (dlen > info->in_len)               return MH_NACK_BAD_LEN;
  return 0;
}

// True if any of the first len bytes is non-zero. A report is "held" while non-zero;
// the watchdog auto-releases held reports (spec/PROTOCOL.md §4 Safety).
static inline bool mh_report_nonzero(const uint8_t *buf, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) if (buf[i]) return true;
  return false;
}

#endif // MH_POLICY_H_
