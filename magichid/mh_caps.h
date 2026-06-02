// =====================================================================================
//  mh_caps.h  --  serialize a profile's report table into a CAPS reply (shared by backends)
// =====================================================================================
//  Every backend answers T_GET_CAPS the same way: stream its generated report table as
//      N x [id][in_len][out_len][feat_len][flags]   (5 bytes/entry, flags bit0 = RELATIVE)
//  echoing the request SEQ. The table comes from mh_reports.h (generated per profile from
//  each descriptor), so adding a backend means "point mh_emit_caps at your table" -- no
//  hand-written CAPS layout. 5*35 = 175 B for universal, still < MH_MAX_PAYLOAD (192).
// =====================================================================================
#ifndef MH_CAPS_H_
#define MH_CAPS_H_

#include "mh_protocol.h"      // MH_T_CAPS, MH_MAX_PAYLOAD
#include "device_backend.h"   // CoreServices
#include "mh_reports.h"       // mh_report_info_t (+ the generated MH_REPORTS* tables)

static inline void mh_emit_caps(const CoreServices *sys, uint8_t seq,
                                const mh_report_info_t *table, uint8_t count) {
  uint8_t p[MH_MAX_PAYLOAD];
  uint32_t n = 0;
  for (uint8_t i = 0; i < count && n + 5 <= sizeof(p); i++) {
    p[n++] = table[i].id;
    p[n++] = table[i].in_len;
    p[n++] = table[i].out_len;
    p[n++] = table[i].feat_len;
    p[n++] = table[i].flags;
  }
  sys->tx(MH_T_CAPS, seq, p, n);
}

#endif // MH_CAPS_H_
