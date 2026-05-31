// =====================================================================================
//  magichid.ino  --  MagicHID transparent HID bridge (ESP32-S3 + Adafruit TinyUSB)
// =====================================================================================
//
//   操作PC ──UART(framed)──▶ ESP32-S3 ──USB HID(native)──▶ 被操作デバイス(PC/スマホ/Switch)
//
//  The ESP32 is a DUMB, SAFE relay. It does not interpret report payloads; it forwards
//  operator commands to the target as HID reports and relays target->device traffic
//  back. All logic lives in the operator-PC client. See DESIGN.md for the full design.
//
//  Responsibilities owned here (mechanism, not policy):
//    - USB enumeration: present desc_hid_report[] (the 35-page "universal" profile).
//    - UART transport: COBS + CRC16 framing (mh_protocol.h).
//    - Faithful relay: validate then sendReport() verbatim (zero-padded to report size).
//    - Bidirectional: serve GET_REPORT(Feature) from a client-supplied cache; relay
//      host Output/Feature writes up as HOST_EVENT.
//    - Safety: watchdog auto-release of held inputs if the operator goes silent; clean
//      release on USB unmount. (Must be local because the operator can vanish.)
//    - Identity/profiles: VID/PID + descriptor profile, applied across a clean reboot.
//
//  Build notes (see README.md): Tools > USB Stack: "Adafruit TinyUSB"; UART0 = Serial0.
//  A single HID report is capped at 63 data bytes (CONFIG_TINYUSB_HID_BUFSIZE=64); the
//  generated table mh_reports.h carries each report's true size and this code enforces it.
// =====================================================================================

#include "Adafruit_TinyUSB.h"
#include "hid_descriptor.h"   // desc_hid_report[] + REPORT_ID_* enum
#include "mh_reports.h"       // generated MH_REPORTS[] {id,in_len,out_len,feat_len}
#include "mh_protocol.h"      // wire protocol + COBS + CRC16

// ---- tunables ------------------------------------------------------------------------
static const uint32_t MH_UART_BAUD   = 1000000;  // operator link (UART0)
static const uint32_t WATCHDOG_MS    = 500;      // silence before auto-release of held inputs
static const uint8_t  HID_POLL_MS    = 2;        // HID IN endpoint poll interval

// ---- USB HID device ------------------------------------------------------------------
Adafruit_USBD_HID usb_hid;

// ---- descriptor profiles (add target-specific descriptors here, select via SET_IDENTITY)
struct Profile { const uint8_t *desc; uint16_t len; const char *name; };
static const Profile PROFILES[] = {
  { desc_hid_report, (uint16_t)sizeof(desc_hid_report), "universal" },
  // e.g. { switch_horipad_desc, sizeof(switch_horipad_desc), "switch" },
};
static const uint8_t N_PROFILES = sizeof(PROFILES) / sizeof(PROFILES[0]);

// ---- identity persisted across a software reboot (survives ESP.restart, not power loss)
#define MH_ID_MAGIC 0x4D484944u  // "MHID"
RTC_NOINIT_ATTR uint32_t g_id_magic;
RTC_NOINIT_ATTR uint16_t g_vid, g_pid, g_bcd;
RTC_NOINIT_ATTR uint8_t  g_profile;

// ---- held-input tracking (for watchdog auto-release) ---------------------------------
static bool     g_held[256];          // g_held[report_id] = currently non-zero (pressed)
static uint32_t g_last_rx_ms = 0;
static bool     g_watchdog_fired = false;
static uint8_t  g_last_send_seq = 0;  // last successfully-sent seq; dedup retransmits (1..255)

// ---- feature answer cache (client-supplied; served on host GET_REPORT(Feature)) ------
struct FCache { uint8_t id; uint8_t len; uint8_t data[MH_HID_MAX_PAYLOAD]; };
static const uint8_t FCACHE_N = 16;
static FCache g_fcache[FCACHE_N];
static portMUX_TYPE g_fc_mux = portMUX_INITIALIZER_UNLOCKED;

// ---- host-event ring buffer (set_report_cb runs in the USB task; loop() drains it) ----
struct HEvent { uint8_t id; uint8_t rtype; uint8_t len; uint8_t data[MH_HID_MAX_PAYLOAD]; };
static const uint8_t HQ_N = 8;
static volatile HEvent g_hq[HQ_N];
static volatile uint8_t g_hq_head = 0, g_hq_tail = 0;
static portMUX_TYPE g_hq_mux = portMUX_INITIALIZER_UNLOCKED;

// ---- UART RX frame accumulator -------------------------------------------------------
static uint8_t g_rx[MH_COBS_MAX];
static uint32_t g_rx_len = 0;
static uint8_t g_decoded[MH_COBS_MAX];   // >= g_rx: COBS-decode output is <= input length

// ---- mount state ---------------------------------------------------------------------
static bool g_was_mounted = false;

// =====================================================================================
//  helpers
// =====================================================================================
static const mh_report_info_t *find_info(uint8_t id) {
  for (uint8_t i = 0; i < MH_REPORT_COUNT; i++)
    if (MH_REPORTS[i].id == id) return &MH_REPORTS[i];
  return nullptr;
}

static void mh_tx(uint8_t type, uint8_t seq, const uint8_t *payload, uint32_t plen) {
  uint8_t out[MH_COBS_MAX];
  uint32_t n = mh_build_frame(type, seq, payload, plen, out);
  Serial0.write(out, n);
}

static uint8_t status_flags() {
  uint8_t f = 0;
  if (TinyUSBDevice.mounted()) f |= MH_ST_MOUNTED;
  if (usb_hid.ready())         f |= MH_ST_READY;
  if (g_watchdog_fired)        f |= MH_ST_WATCHDOG;
  return f;
}

static void send_status() { uint8_t f = status_flags(); mh_tx(MH_T_STATUS, 0, &f, 1); }
static void send_ack(uint8_t seq) { mh_tx(MH_T_ACK, seq, nullptr, 0); }
static void send_nack(uint8_t seq, uint8_t reason) {
  uint8_t p[1] = { reason }; mh_tx(MH_T_NACK, seq, p, 1);
}
static void send_log(const char *s) { mh_tx(MH_T_LOG, 0, (const uint8_t *)s, strlen(s)); }

// Send one INPUT report, zero-padded to the report's declared size. Tracks held state.
static bool send_report_padded(const mh_report_info_t *info,
                               const uint8_t *data, uint8_t dlen) {
  uint8_t buf[MH_HID_MAX_PAYLOAD];
  memset(buf, 0, sizeof(buf));
  if (dlen > info->in_len) dlen = info->in_len;
  memcpy(buf, data, dlen);
  bool ok = usb_hid.sendReport(info->id, buf, info->in_len);
  if (ok) {
    bool nz = false;
    for (uint8_t i = 0; i < info->in_len; i++) if (buf[i]) { nz = true; break; }
    g_held[info->id] = nz;
  }
  return ok;
}

// Re-send a zeroed report for every held id (release keys/buttons).
static void release_all_held() {
  for (uint16_t id = 0; id < 256; id++) {
    if (!g_held[id]) continue;
    const mh_report_info_t *info = find_info((uint8_t)id);
    if (info && info->in_len && usb_hid.ready()) {
      uint8_t z[MH_HID_MAX_PAYLOAD]; memset(z, 0, sizeof(z));
      usb_hid.sendReport((uint8_t)id, z, info->in_len);
    }
    g_held[id] = false;
  }
}

// =====================================================================================
//  USB HID callbacks  (bidirectional)
// =====================================================================================
// Host reads a FEATURE report -> answer from the client-supplied cache (instant).
static uint16_t hid_get_report_cb(uint8_t report_id, hid_report_type_t type,
                                  uint8_t *buffer, uint16_t reqlen) {
  if (type != HID_REPORT_TYPE_FEATURE) return 0;   // INPUT GET handled by interrupt IN
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

// Host writes an OUTPUT/FEATURE report (e.g. keyboard LEDs, LampArray) -> queue for relay.
static void hid_set_report_cb(uint8_t report_id, hid_report_type_t type,
                              uint8_t const *buffer, uint16_t bufsize) {
  // OUT-endpoint (interrupt) reports arrive as report_id=0 with the real id as the
  // first byte (our descriptor uses Report IDs). SET_REPORT(control) instead passes the
  // real report_id and a buffer with no id prefix.
  if (report_id == 0 && bufsize >= 1) { report_id = buffer[0]; buffer++; bufsize--; }
  if (bufsize > MH_HID_MAX_PAYLOAD) bufsize = MH_HID_MAX_PAYLOAD;
  portENTER_CRITICAL(&g_hq_mux);
  uint8_t next = (g_hq_head + 1) % HQ_N;
  if (next != g_hq_tail) {                 // drop if full
    g_hq[g_hq_head].id = report_id;
    g_hq[g_hq_head].rtype = (uint8_t)type;
    g_hq[g_hq_head].len = (uint8_t)bufsize;
    for (uint16_t i = 0; i < bufsize; i++) g_hq[g_hq_head].data[i] = buffer[i];
    g_hq_head = next;
  }
  portEXIT_CRITICAL(&g_hq_mux);
}

// Drain the host-event queue and relay each as a HOST_EVENT frame (from loop() context).
static void flush_host_events() {
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
    mh_tx(MH_T_HOST_EVENT, 0, p, 2 + ev.len);
  }
}

// =====================================================================================
//  command handlers
// =====================================================================================
static void handle_send_report(uint8_t seq, const uint8_t *p, uint32_t plen) {
  if (plen < 1) { send_nack(seq, MH_NACK_BAD_LEN); return; }
  // Idempotent retransmit: if this seq was already applied, just re-ACK. Prevents a lost
  // ACK from double-applying RELATIVE reports (e.g. mouse move counted twice).
  if (seq != 0 && seq == g_last_send_seq) { send_ack(seq); return; }
  uint8_t id = p[0];
  const uint8_t *data = p + 1;
  uint32_t dlen = plen - 1;
  const mh_report_info_t *info = find_info(id);
  if (!info)                 { send_nack(seq, MH_NACK_UNKNOWN_ID);   return; }
  if (info->in_len == 0)     { send_nack(seq, MH_NACK_NOT_SENDABLE); return; }
  if (info->in_len > MH_HID_MAX_PAYLOAD) { send_nack(seq, MH_NACK_TOO_BIG); return; }
  if (dlen > info->in_len)   { send_nack(seq, MH_NACK_BAD_LEN);      return; }
  if (!usb_hid.ready())      { send_nack(seq, MH_NACK_NOT_READY);    return; }
  if (send_report_padded(info, data, (uint8_t)dlen)) { g_last_send_seq = seq; send_ack(seq); }
  else send_nack(seq, MH_NACK_NOT_READY);
}

static void handle_set_feature(uint8_t seq, const uint8_t *p, uint32_t plen) {
  if (plen < 1) { send_nack(seq, MH_NACK_BAD_LEN); return; }
  uint8_t id = p[0];
  uint32_t dlen = plen - 1;
  if (dlen > MH_HID_MAX_PAYLOAD) { send_nack(seq, MH_NACK_TOO_BIG); return; }
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
  send_ack(seq);
}

static void handle_get_caps(uint8_t seq) {
  uint8_t p[4 * MH_REPORT_COUNT];
  uint32_t n = 0;
  for (uint8_t i = 0; i < MH_REPORT_COUNT; i++) {
    p[n++] = MH_REPORTS[i].id;
    p[n++] = MH_REPORTS[i].in_len;
    p[n++] = MH_REPORTS[i].out_len;
    p[n++] = MH_REPORTS[i].feat_len;
  }
  mh_tx(MH_T_CAPS, seq, p, n);
}

static void handle_set_identity(uint8_t seq, const uint8_t *p, uint32_t plen) {
  if (plen < 7) { send_nack(seq, MH_NACK_BAD_LEN); return; }
  uint16_t vid = p[0] | (p[1] << 8);
  uint16_t pid = p[2] | (p[3] << 8);
  uint16_t bcd = p[4] | (p[5] << 8);
  uint8_t  prof = p[6];
  if (prof >= N_PROFILES) { send_nack(seq, MH_NACK_BAD_LEN); return; }
  // Persist and reboot for a clean re-enumeration with the new identity/descriptor.
  g_vid = vid; g_pid = pid; g_bcd = bcd; g_profile = prof; g_id_magic = MH_ID_MAGIC;
  send_ack(seq);
  send_log("SET_IDENTITY: rebooting to re-enumerate");
  Serial0.flush();
  delay(20);
  ESP.restart();
}

static void process_frame(const uint8_t *src, uint32_t len) {
  uint8_t type, seq; const uint8_t *payload;
  int32_t plen = mh_parse_frame(src, len, g_decoded, &type, &seq, &payload);
  if (plen < 0) { send_nack(0, MH_NACK_BAD_CRC); return; }
  g_last_rx_ms = millis();
  switch (type) {
    case MH_T_SEND_REPORT:  handle_send_report(seq, payload, plen); break;
    case MH_T_PING:         send_status(); break;
    case MH_T_RELEASE_ALL:  release_all_held(); send_ack(seq); break;
    case MH_T_GET_CAPS:     handle_get_caps(seq); break;
    case MH_T_SET_IDENTITY: handle_set_identity(seq, payload, plen); break;
    case MH_T_SET_FEATURE:  handle_set_feature(seq, payload, plen); break;
    default:                send_nack(seq, MH_NACK_BAD_FRAME); break;
  }
}

// Feed one received byte into the COBS frame accumulator.
static void rx_byte(uint8_t b) {
  if (b == MH_DELIM) {
    if (g_rx_len > 0) { process_frame(g_rx, g_rx_len); g_rx_len = 0; }
    return;
  }
  if (g_rx_len < sizeof(g_rx)) g_rx[g_rx_len++] = b;
  else g_rx_len = 0;             // overflow -> drop and resync on next delimiter
}

// =====================================================================================
//  periodic checks
// =====================================================================================
static void check_watchdog() {
  bool any = false;
  for (uint16_t id = 0; id < 256; id++) if (g_held[id]) { any = true; break; }
  if (any && (millis() - g_last_rx_ms) > WATCHDOG_MS) {
    release_all_held();
    g_watchdog_fired = true;
    send_status();
  }
}

static void check_mount() {
  bool m = TinyUSBDevice.mounted();
  if (m != g_was_mounted) {
    g_was_mounted = m;
    if (!m) { release_all_held(); }   // target gone -> drop state
    g_watchdog_fired = false;
    send_status();                    // handshake: tell operator about the change
  }
}

// =====================================================================================
//  setup / loop
// =====================================================================================
void setup() {
  // Required before adding interfaces (no-op if the core already started the device).
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);

  // --- apply persisted identity / profile (set by a prior SET_IDENTITY) ---
  uint8_t prof = 0;
  if (g_id_magic == MH_ID_MAGIC) {
    if (g_profile < N_PROFILES) prof = g_profile;
    if (g_vid) TinyUSBDevice.setID(g_vid, g_pid);
    if (g_bcd) TinyUSBDevice.setVersion(g_bcd);
  }

  // --- USB HID (to the target) ---
  usb_hid.setPollInterval(HID_POLL_MS);
  usb_hid.enableOutEndpoint(true);                       // receive host Output reports
  usb_hid.setReportDescriptor(PROFILES[prof].desc, PROFILES[prof].len);
  usb_hid.setReportCallback(hid_get_report_cb, hid_set_report_cb);
  usb_hid.setStringDescriptor("MagicHID Bridge");
  usb_hid.begin();

  // If the core already enumerated us, re-enumerate so the HID interface (and any new
  // identity/profile) takes effect.
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  // --- operator link (UART0) ---
  Serial0.setRxBufferSize(2048);
  Serial0.begin(MH_UART_BAUD);

  memset(g_held, 0, sizeof(g_held));
  g_last_rx_ms = millis();
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();                  // no-op on ESP32 (USB runs in a background task)
#endif
  // 1. pull operator bytes
  while (Serial0.available()) rx_byte((uint8_t)Serial0.read());
  // 2. relay any host->device traffic
  flush_host_events();
  // 3. safety: auto-release held inputs if operator went silent
  check_watchdog();
  // 4. handshake: report mount/unmount changes
  check_mount();
}
