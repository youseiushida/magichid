// =====================================================================================
//  backend_universal.cpp  --  the "universal" 35-page chimera relay, as a DeviceBackend
// =====================================================================================
//  This is the original MagicHID policy, verbatim, moved behind the DeviceBackend seam:
//  a DUMB, SAFE relay over the generated 35-report descriptor (hid_descriptor.h).
//    - operator T_SEND_REPORT  -> validate against mh_reports.h, sendReport() zero-padded
//    - host OUTPUT/FEATURE write -> queue, relayed up as HOST_EVENT from task()
//    - host GET_REPORT(Feature)  -> answer from a client-supplied cache
//    - T_SET_FEATURE / T_GET_CAPS -> feature cache / capability table
//    - release_all/any_held       -> watchdog auto-release of held inputs
//  It touches the core ONLY through CoreServices; it never sees usb_hid or the UART.
//  Pure decisions (validate->NACK, held detection) live in mh_policy.h (host-tested).
// =====================================================================================
#include <Arduino.h>
#include <string.h>

#include "device_backend.h"
#include "backend_universal.h"
#include "mh_protocol.h"      // MH_T_*, MH_NACK_*, MH_HID_MAX_PAYLOAD
#include "mh_reports.h"       // MH_REPORTS[], mh_report_info_t, MH_REPORT_COUNT
#include "mh_policy.h"        // mh_find_info / mh_classify_send / mh_report_nonzero (pure)
#include "hid_descriptor.h"   // desc_hid_report[]

// ---- injected core services ----------------------------------------------------------
static const CoreServices *sys = nullptr;

// ---- held-input tracking (for watchdog auto-release) ---------------------------------
static bool    g_held[256];           // g_held[report_id] = currently non-zero (pressed)
static uint8_t g_last_send_seq = 0;   // last successfully-sent seq; dedup retransmits (1..255)

// ---- feature answer cache (client-supplied; served on host GET_REPORT(Feature)) ------
struct FCache { uint8_t id; uint8_t len; uint8_t data[MH_HID_MAX_PAYLOAD]; };
static const uint8_t FCACHE_N = 16;
static FCache g_fcache[FCACHE_N];
static portMUX_TYPE g_fc_mux = portMUX_INITIALIZER_UNLOCKED;

// ---- host-event ring buffer (on_host_out runs in the USB task; task() drains it) ------
struct HEvent { uint8_t id; uint8_t rtype; uint8_t len; uint8_t data[MH_HID_MAX_PAYLOAD]; };
static const uint8_t HQ_N = 8;
static volatile HEvent g_hq[HQ_N];
static volatile uint8_t g_hq_head = 0, g_hq_tail = 0;
static portMUX_TYPE g_hq_mux = portMUX_INITIALIZER_UNLOCKED;

// =====================================================================================
//  helpers
// =====================================================================================
// Send one INPUT report, zero-padded to the report's declared size. Tracks held state.
static bool send_report_padded(const mh_report_info_t *info,
                               const uint8_t *data, uint8_t dlen) {
  uint8_t buf[MH_HID_MAX_PAYLOAD];
  memset(buf, 0, sizeof(buf));
  if (dlen > info->in_len) dlen = info->in_len;
  memcpy(buf, data, dlen);
  bool ok = sys->hid_send(info->id, buf, info->in_len);
  if (ok) g_held[info->id] = mh_report_nonzero(buf, info->in_len);
  return ok;
}

// =====================================================================================
//  DeviceBackend callbacks
// =====================================================================================
static void u_begin(const CoreServices *s) {
  sys = s;
  memset(g_held, 0, sizeof(g_held));
  g_last_send_seq = 0;
  // g_fcache / g_hq are static-zero-initialized at load.
}

static void u_on_operator(uint8_t seq, const uint8_t *p, uint32_t plen) {
  if (plen < 1) { sys->nack(seq, MH_NACK_BAD_LEN); return; }
  // Idempotent retransmit: if this seq was already applied, just re-ACK. Prevents a lost
  // ACK from double-applying RELATIVE reports (e.g. mouse move counted twice).
  if (seq != 0 && seq == g_last_send_seq) { sys->ack(seq); return; }
  uint8_t id = p[0];
  const uint8_t *data = p + 1;
  uint32_t dlen = plen - 1;
  const mh_report_info_t *info = mh_find_info(id);
  uint8_t reason = mh_classify_send(info, dlen);   // pure policy (spec/PROTOCOL.md §3.4)
  if (reason)            { sys->nack(seq, reason);            return; }
  if (!sys->hid_ready()) { sys->nack(seq, MH_NACK_NOT_READY); return; }
  if (send_report_padded(info, data, (uint8_t)dlen)) { g_last_send_seq = seq; sys->ack(seq); }
  else sys->nack(seq, MH_NACK_NOT_READY);
}

static void u_on_set_feature(uint8_t seq, const uint8_t *p, uint32_t plen) {
  if (plen < 1) { sys->nack(seq, MH_NACK_BAD_LEN); return; }
  uint8_t id = p[0];
  uint32_t dlen = plen - 1;
  if (dlen > MH_HID_MAX_PAYLOAD) { sys->nack(seq, MH_NACK_TOO_BIG); return; }
  portENTER_CRITICAL(&g_fc_mux);
  int slot = -1, freeslot = -1;
  for (uint8_t i = 0; i < FCACHE_N; i++) {
    if (g_fcache[i].id == id) { slot = i; break; }
    if (freeslot < 0 && g_fcache[i].len == 0 && g_fcache[i].id == 0) freeslot = i;
  }
  if (slot < 0) slot = (freeslot >= 0) ? freeslot : 0;
  g_fcache[slot].id = id;
  g_fcache[slot].len = (uint8_t)dlen;
  memcpy(g_fcache[slot].data, p + 1, dlen);
  portEXIT_CRITICAL(&g_fc_mux);
  sys->ack(seq);
}

static void u_get_caps(uint8_t seq) {
  uint8_t p[4 * MH_REPORT_COUNT];
  uint32_t n = 0;
  for (uint8_t i = 0; i < MH_REPORT_COUNT; i++) {
    p[n++] = MH_REPORTS[i].id;
    p[n++] = MH_REPORTS[i].in_len;
    p[n++] = MH_REPORTS[i].out_len;
    p[n++] = MH_REPORTS[i].feat_len;
  }
  sys->tx(MH_T_CAPS, seq, p, n);
}

// Host writes an OUTPUT/FEATURE report (e.g. keyboard LEDs, LampArray) -> queue for relay.
// Runs in the USB task context; task() (loop ctx) drains and transmits.
static void u_on_host_out(uint8_t id, uint8_t rtype, const uint8_t *buffer, uint16_t bufsize) {
  portENTER_CRITICAL(&g_hq_mux);
  uint8_t next = (g_hq_head + 1) % HQ_N;
  if (next != g_hq_tail) {                 // drop if full
    g_hq[g_hq_head].id = id;
    g_hq[g_hq_head].rtype = rtype;
    g_hq[g_hq_head].len = (uint8_t)bufsize;
    for (uint16_t i = 0; i < bufsize; i++) g_hq[g_hq_head].data[i] = buffer[i];
    g_hq_head = next;
  }
  portEXIT_CRITICAL(&g_hq_mux);
}

// Host reads a FEATURE report -> answer from the client-supplied cache (instant).
static uint16_t u_on_host_get(uint8_t report_id, uint8_t *buffer, uint16_t reqlen) {
  uint16_t out = 0;
  portENTER_CRITICAL(&g_fc_mux);
  for (uint8_t i = 0; i < FCACHE_N; i++) {
    if (g_fcache[i].id == report_id && g_fcache[i].len) {
      out = g_fcache[i].len; if (out > reqlen) out = reqlen;
      memcpy(buffer, g_fcache[i].data, out);
      break;
    }
  }
  portEXIT_CRITICAL(&g_fc_mux);
  return out;   // 0 -> STALL (host treats as unsupported)
}

// Drain the host-event queue and relay each as a HOST_EVENT frame (from loop() context).
static void u_task() {
  while (true) {
    HEvent ev; bool have = false;
    portENTER_CRITICAL(&g_hq_mux);
    if (g_hq_tail != g_hq_head) {
      ev.id = g_hq[g_hq_tail].id; ev.rtype = g_hq[g_hq_tail].rtype; ev.len = g_hq[g_hq_tail].len;
      for (uint8_t i = 0; i < ev.len; i++) ev.data[i] = g_hq[g_hq_tail].data[i];
      g_hq_tail = (g_hq_tail + 1) % HQ_N;
      have = true;
    }
    portEXIT_CRITICAL(&g_hq_mux);
    if (!have) break;
    uint8_t p[2 + MH_HID_MAX_PAYLOAD];
    p[0] = ev.id; p[1] = ev.rtype;
    memcpy(p + 2, ev.data, ev.len);
    sys->tx(MH_T_HOST_EVENT, 0, p, 2 + ev.len);
  }
}

// Re-send a zeroed report for every held id (release keys/buttons).
static void u_release_all() {
  for (uint16_t id = 0; id < 256; id++) {
    if (!g_held[id]) continue;
    const mh_report_info_t *info = mh_find_info((uint8_t)id);
    if (info && info->in_len && sys->hid_ready()) {
      uint8_t z[MH_HID_MAX_PAYLOAD]; memset(z, 0, sizeof(z));
      sys->hid_send((uint8_t)id, z, info->in_len);
    }
    g_held[id] = false;
  }
}

static bool u_any_held() {
  for (uint16_t id = 0; id < 256; id++) if (g_held[id]) return true;
  return false;
}

// ---- the backend (field order must match struct DeviceBackend) -----------------------
const DeviceBackend BACKEND_UNIVERSAL = {
  "universal",                              // name
  0, 0, 0,                                  // vid, pid, bcd  (0 = keep core/SET_IDENTITY default)
  desc_hid_report,                          // desc
  (uint16_t)sizeof(desc_hid_report),        // desc_len
  u_begin,                                  // begin
  u_on_operator,                            // on_operator
  u_on_set_feature,                         // on_set_feature
  u_get_caps,                               // get_caps
  u_on_host_out,                            // on_host_out
  u_on_host_get,                            // on_host_get
  u_task,                                   // task
  u_release_all,                            // release_all
  u_any_held,                               // any_held
};
