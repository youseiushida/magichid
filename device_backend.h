// =====================================================================================
//  device_backend.h  --  seam between the relay CORE and a device-impersonation BACKEND
// =====================================================================================
//  The CORE (magichid.ino) owns mechanism and knows nothing about report layouts:
//    USB enumeration, UART (COBS+CRC) framing, command routing, identity/profile
//    persistence, watchdog/mount handling.
//  A BACKEND owns policy: what to do with operator reports, host writes/reads,
//    capability queries, and any periodic streaming.
//
//  Exactly one backend is active per boot (chosen by the persisted profile index and
//  applied before usb_hid.begin()). Decoupling contract:
//    - the core calls a backend ONLY through DeviceBackend (below),
//    - a backend calls the core ONLY through CoreServices (injected once via begin()).
//  Adding or removing a backend touches only its own files + one line in backends.cpp.
//  Any DeviceBackend callback may be NULL; the core treats NULL as "not supported".
// =====================================================================================
#ifndef MH_DEVICE_BACKEND_H_
#define MH_DEVICE_BACKEND_H_

#include <stdint.h>

// ---- core -> backend services (backend stashes the pointer in begin()) ---------------
struct CoreServices {
  bool (*hid_ready)();                                                    // usb_hid.ready()
  bool (*hid_send)(uint8_t report_id, const uint8_t *buf, uint16_t len);  // INPUT report -> host
  void (*tx)(uint8_t type, uint8_t seq, const uint8_t *payload, uint32_t plen); // framed UART out
  void (*ack)(uint8_t seq);                                              // convenience: T_ACK
  void (*nack)(uint8_t seq, uint8_t reason);                             // convenience: T_NACK
  void (*log)(const char *s);                                            // convenience: T_LOG
};

// ---- a device-impersonation backend --------------------------------------------------
//  FIELD ORDER IS LOAD-BEARING: backends use positional aggregate initialization.
struct DeviceBackend {
  const char    *name;
  uint16_t       vid, pid, bcd;     // declared identity; 0 = leave core/SET_IDENTITY default
  const uint8_t *desc;              // HID report descriptor
  uint16_t       desc_len;

  void     (*begin)(const CoreServices *sys);                            // bind services / init
  void     (*on_operator)(uint8_t seq, const uint8_t *p, uint32_t plen); // T_SEND_REPORT
  void     (*on_set_feature)(uint8_t seq, const uint8_t *p, uint32_t plen); // T_SET_FEATURE
  void     (*get_caps)(uint8_t seq);                                     // T_GET_CAPS
  void     (*on_host_out)(uint8_t id, uint8_t rtype,
                          const uint8_t *buf, uint16_t len);             // host OUTPUT/FEATURE write
  uint16_t (*on_host_get)(uint8_t id, uint8_t *buf, uint16_t maxlen);    // host FEATURE read
  void     (*task)();                                                    // periodic (loop ctx)
  void     (*release_all)();                                             // watchdog / unmount
  bool     (*any_held)();                                                // watchdog: anything held?
};

#endif // MH_DEVICE_BACKEND_H_
