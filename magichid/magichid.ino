// =====================================================================================
//  magichid.ino  --  MagicHID transparent HID bridge (ESP32-S3 + Adafruit TinyUSB)
// =====================================================================================
//
//   operator PC ──UART(framed)──▶ ESP32-S3 ──USB HID(native)──▶ target device (PC / phone / Switch)
//
//  This file is the CORE: pure mechanism, no report-layout knowledge. It owns USB
//  enumeration, UART (COBS+CRC16) framing, command routing, identity/profile persistence,
//  and the safety watchdog. All report/feature/relay POLICY lives in a DeviceBackend
//  (device_backend.h); exactly one backend is active per boot, selected by the persisted
//  profile id and listed in backends.cpp. The default "universal" backend is the original
//  35-page chimera relay (backend_universal.cpp).
//
//  Core <-> backend contract:
//    - core calls the backend only through DeviceBackend callbacks,
//    - the backend calls the core only through CoreServices (injected via begin()).
//  This keeps target-specific code (e.g. a future Switch emulator) out of the core and
//  individually removable.
//
//  Build notes: Arduino IDE Tools > USB Stack: "Adafruit TinyUSB"; UART0 = Serial0.
//  A single HID report is capped at 63 data bytes (CONFIG_TINYUSB_HID_BUFSIZE=64).
// =====================================================================================

#include "Adafruit_TinyUSB.h"
#include "mh_protocol.h"      // wire protocol + COBS + CRC16 (static inline)
#include "device_backend.h"   // DeviceBackend / CoreServices seam
#include "backends.h"         // BACKENDS[] registry + N_BACKENDS

// ---- tunables ------------------------------------------------------------------------
static const uint32_t MH_UART_BAUD   = 1000000;  // operator link (UART0)
static const uint32_t WATCHDOG_MS    = 500;      // silence before auto-release of held inputs
static const uint8_t  HID_POLL_MS    = 2;        // HID IN endpoint poll interval

// ---- USB HID device ------------------------------------------------------------------
Adafruit_USBD_HID usb_hid;

// ---- active backend (chosen in setup() from the persisted profile) -------------------
static const DeviceBackend *g_active = nullptr;

// ---- identity persisted across a software reboot (survives ESP.restart, not power loss)
#define MH_ID_MAGIC 0x4D484944u  // "MHID"
RTC_NOINIT_ATTR uint32_t g_id_magic;
RTC_NOINIT_ATTR uint16_t g_vid, g_pid, g_bcd;
RTC_NOINIT_ATTR uint8_t  g_profile;

// ---- core state ----------------------------------------------------------------------
static uint32_t g_last_rx_ms = 0;
static bool     g_watchdog_fired = false;
static bool     g_was_mounted = false;
static uint32_t g_session_epoch = 0;   // device-minted; bumped on each SESSION_OPEN

// ---- UART RX frame accumulator -------------------------------------------------------
static uint8_t  g_rx[MH_COBS_MAX];
static uint32_t g_rx_len = 0;
static uint8_t  g_decoded[MH_COBS_MAX];   // >= g_rx: COBS-decode output is <= input length

// =====================================================================================
//  framing helpers (transport; offered to the backend via CoreServices)
// =====================================================================================
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

static void send_status() {
  uint8_t p[2] = { status_flags(), MH_PROTO_VERSION };   // [flags][proto_version]
  mh_tx(MH_T_STATUS, 0, p, 2);
}
static void send_ack(uint8_t seq) { mh_tx(MH_T_ACK, seq, nullptr, 0); }
static void send_nack(uint8_t seq, uint8_t reason) {
  uint8_t p[1] = { reason }; mh_tx(MH_T_NACK, seq, p, 1);
}
static void send_log(const char *s) { mh_tx(MH_T_LOG, 0, (const uint8_t *)s, strlen(s)); }

// SESSION reply: the device-minted epoch as 4 LE bytes. The client scopes its SEQ space to
// this epoch; the device's dedup window was just cleared, so reused SEQs apply cleanly.
static void send_session(uint32_t epoch) {
  uint8_t p[4] = { (uint8_t)(epoch & 0xFF), (uint8_t)((epoch >> 8) & 0xFF),
                   (uint8_t)((epoch >> 16) & 0xFF), (uint8_t)((epoch >> 24) & 0xFF) };
  mh_tx(MH_T_SESSION, 0, p, 4);
}

// =====================================================================================
//  CoreServices: the backend-facing view of the core
// =====================================================================================
static bool core_hid_ready() { return usb_hid.ready(); }
static bool core_hid_send(uint8_t id, const uint8_t *buf, uint16_t len) {
  return usb_hid.sendReport(id, buf, len);
}
static void core_tx(uint8_t type, uint8_t seq, const uint8_t *payload, uint32_t plen) {
  mh_tx(type, seq, payload, plen);
}
static const CoreServices CORE = {
  core_hid_ready, core_hid_send, core_tx, send_ack, send_nack, send_log,
};

// =====================================================================================
//  USB HID callbacks  (bidirectional) -- forwarded to the active backend
// =====================================================================================
static uint16_t hid_get_report_cb(uint8_t report_id, hid_report_type_t type,
                                  uint8_t *buffer, uint16_t reqlen) {
  if (type != HID_REPORT_TYPE_FEATURE) return 0;   // INPUT GET handled by interrupt IN
  if (g_active && g_active->on_host_get) return g_active->on_host_get(report_id, buffer, reqlen);
  return 0;   // 0 -> STALL (host treats as unsupported)
}

static void hid_set_report_cb(uint8_t report_id, hid_report_type_t type,
                              uint8_t const *buffer, uint16_t bufsize) {
  // OUT-endpoint (interrupt) reports arrive as report_id=0 with the real id as the first
  // byte (our descriptors use Report IDs). SET_REPORT(control) instead passes the real
  // report_id and a buffer with no id prefix.
  if (report_id == 0 && bufsize >= 1) { report_id = buffer[0]; buffer++; bufsize--; }
  if (bufsize > MH_HID_MAX_PAYLOAD) bufsize = MH_HID_MAX_PAYLOAD;
  if (g_active && g_active->on_host_out)
    g_active->on_host_out(report_id, (uint8_t)type, buffer, bufsize);
}

// =====================================================================================
//  command routing
// =====================================================================================
static void handle_set_identity(uint8_t seq, const uint8_t *p, uint32_t plen) {
  if (plen < 7) { send_nack(seq, MH_NACK_BAD_LEN); return; }
  uint16_t vid = p[0] | (p[1] << 8);
  uint16_t pid = p[2] | (p[3] << 8);
  uint16_t bcd = p[4] | (p[5] << 8);
  uint8_t  prof = p[6];
  if (prof >= N_BACKENDS) { send_nack(seq, MH_NACK_BAD_LEN); return; }
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
    case MH_T_SEND_REPORT:
      if (g_active->on_operator) g_active->on_operator(seq, payload, plen);
      else send_nack(seq, MH_NACK_BAD_FRAME);
      break;
    case MH_T_PING:         send_status(); break;
    case MH_T_SESSION_OPEN: {
      // New operator session: mint a fresh epoch and drop the per-session SEQ dedup window
      // so a reconnecting client's SEQs (which restart at 1) are not mistaken for duplicates
      // of the previous session. millis() is monotonic across a run -> always a new value.
      g_session_epoch++;
      if (g_active->session_reset) g_active->session_reset();
      send_session(g_session_epoch);
      break;
    }
    case MH_T_RELEASE_ALL:
      if (g_active->release_all) g_active->release_all();
      send_ack(seq);
      break;
    case MH_T_GET_CAPS:
      if (g_active->get_caps) g_active->get_caps(seq);
      else send_nack(seq, MH_NACK_BAD_FRAME);
      break;
    case MH_T_SET_IDENTITY: handle_set_identity(seq, payload, plen); break;
    case MH_T_SET_FEATURE:
      if (g_active->on_set_feature) g_active->on_set_feature(seq, payload, plen);
      else send_nack(seq, MH_NACK_BAD_FRAME);
      break;
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
  bool any = g_active->any_held ? g_active->any_held() : false;
  if (any && (millis() - g_last_rx_ms) > WATCHDOG_MS) {
    if (g_active->release_all) g_active->release_all();
    g_watchdog_fired = true;
    send_status();
  }
}

static void check_mount() {
  bool m = TinyUSBDevice.mounted();
  if (m != g_was_mounted) {
    g_was_mounted = m;
    if (!m && g_active->release_all) g_active->release_all();   // target gone -> drop state
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

  // --- select the active backend from the persisted profile (set by SET_IDENTITY) ---
  uint8_t prof = 0;
  if (g_id_magic == MH_ID_MAGIC && g_profile < N_BACKENDS) prof = g_profile;
  g_active = BACKENDS[prof];
  g_active->begin(&CORE);

  // --- identity: an explicit SET_IDENTITY override wins; else the backend's declared id ---
  bool have_override = (g_id_magic == MH_ID_MAGIC);
  uint16_t vid = (have_override && g_vid) ? g_vid : g_active->vid;
  uint16_t pid = (have_override && g_vid) ? g_pid : g_active->pid;
  uint16_t bcd = (have_override && g_bcd) ? g_bcd : g_active->bcd;
  if (vid) TinyUSBDevice.setID(vid, pid);
  if (bcd) TinyUSBDevice.setVersion(bcd);

  // --- USB HID (to the target) ---
  usb_hid.setPollInterval(HID_POLL_MS);
  usb_hid.enableOutEndpoint(true);                       // receive host Output reports
  usb_hid.setReportDescriptor(g_active->desc, g_active->desc_len);
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

  g_last_rx_ms = millis();
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();                  // no-op on ESP32 (USB runs in a background task)
#endif
  // 1. pull operator bytes
  while (Serial0.available()) rx_byte((uint8_t)Serial0.read());
  // 2. backend periodic work (e.g. universal relays queued host events here)
  if (g_active->task) g_active->task();
  // 3. safety: auto-release held inputs if operator went silent
  check_watchdog();
  // 4. handshake: report mount/unmount changes
  check_mount();
}
