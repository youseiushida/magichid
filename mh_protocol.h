// =====================================================================================
//  mh_protocol.h  --  MagicHID UART wire protocol (shared definitions + COBS + CRC16)
// =====================================================================================
//  Operator PC  <--UART(framed)-->  ESP32-S3 bridge.
//
//  On-wire frame:
//      COBS( [TYPE:1][SEQ:1][PAYLOAD:0..N][CRC16:2(LE)] )  +  0x00 delimiter
//      - CRC16 (CCITT, poly 0x1021, init 0xFFFF) is computed over TYPE..PAYLOAD.
//      - COBS removes all 0x00 from the body so 0x00 is an unambiguous frame end.
//
//  The Python client mirrors these constants in client/magichid_bridge/protocol.py.
//  Pure header (static inline) so the .ino and any .cpp can include it.
// =====================================================================================
#ifndef MH_PROTOCOL_H_
#define MH_PROTOCOL_H_

#include <stdint.h>
#include <string.h>

// ---- frame delimiter -----------------------------------------------------------------
#define MH_DELIM             0x00

// ---- message types: operator PC -> ESP32 ---------------------------------------------
#define MH_T_SEND_REPORT     0x01  // [report_id][hid data...]  inject one INPUT report
#define MH_T_PING            0x02  // []                         ask status / readiness
#define MH_T_RELEASE_ALL     0x03  // []                         zero every held report
#define MH_T_GET_CAPS        0x04  // []                         request report table
#define MH_T_SET_IDENTITY    0x05  // [vid:2][pid:2][bcd:2][profile:1]  change + reboot
#define MH_T_SET_FEATURE     0x06  // [report_id][data...]       fill FEATURE answer cache

// ---- message types: ESP32 -> operator PC ---------------------------------------------
#define MH_T_STATUS          0x81  // [flags:1]
#define MH_T_ACK             0x82  // [seq:1]
#define MH_T_NACK            0x83  // [seq:1][reason:1]
#define MH_T_HOST_EVENT      0x84  // [report_id][report_type:1][data...]  host->device
#define MH_T_LOG             0x85  // [ascii...]
#define MH_T_CAPS            0x86  // N * [id][in_len][out_len][feat_len]

// ---- STATUS flag bits -----------------------------------------------------------------
#define MH_ST_MOUNTED        0x01  // enumerated by the target host
#define MH_ST_SUSPENDED      0x02  // USB suspended
#define MH_ST_READY          0x04  // HID IN endpoint ready to accept a report
#define MH_ST_WATCHDOG       0x08  // a watchdog auto-release just happened

// ---- NACK reasons ---------------------------------------------------------------------
#define MH_NACK_BAD_CRC      1
#define MH_NACK_BAD_LEN      2
#define MH_NACK_UNKNOWN_ID   3
#define MH_NACK_NOT_READY    4
#define MH_NACK_NOT_SENDABLE 5      // report has no INPUT (output/feature-only)
#define MH_NACK_BAD_FRAME    6
#define MH_NACK_TOO_BIG      7      // report exceeds the 63-byte HID buffer payload

// ---- sizes ----------------------------------------------------------------------------
#define MH_MAX_PAYLOAD       192    // logical payload max (CAPS reply ~ 4*35 = 140 B)
#define MH_HID_MAX_PAYLOAD   63     // CFG_TUD_HID_EP_BUFSIZE(64) - 1 report-id byte
#define MH_MAX_FRAME         (MH_MAX_PAYLOAD + 8)
#define MH_COBS_MAX          (MH_MAX_FRAME + (MH_MAX_FRAME / 254) + 2)

// ---- CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection) ---------------------------
static inline uint16_t mh_crc16(const uint8_t *d, uint32_t n) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < n; i++) {
    crc ^= (uint16_t)d[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// ---- COBS encode: src[len] -> dst (no delimiter). Returns encoded length. -------------
static inline uint32_t mh_cobs_encode(const uint8_t *src, uint32_t len, uint8_t *dst) {
  uint32_t out = 0;
  uint32_t code_i = out++;     // reserve slot for the first code byte
  uint8_t code = 1;
  for (uint32_t i = 0; i < len; i++) {
    if (src[i] == 0) {
      dst[code_i] = code;
      code_i = out++;
      code = 1;
    } else {
      dst[out++] = src[i];
      if (++code == 0xFF) {
        dst[code_i] = code;
        code_i = out++;
        code = 1;
      }
    }
  }
  dst[code_i] = code;
  return out;
}

// ---- COBS decode: src[len] (no delimiter) -> dst. Returns length or -1 on error. ------
static inline int32_t mh_cobs_decode(const uint8_t *src, uint32_t len, uint8_t *dst) {
  uint32_t out = 0, i = 0;
  while (i < len) {
    uint8_t code = src[i++];
    if (code == 0) return -1;                 // 0 cannot appear inside a COBS block
    for (uint8_t j = 1; j < code; j++) {
      if (i >= len) return -1;
      dst[out++] = src[i++];
    }
    if (code != 0xFF && i < len) dst[out++] = 0;
  }
  return (int32_t)out;
}

// ---- build a complete frame (incl. trailing 0x00) into out[]. Returns total bytes. ----
//      out[] must hold at least MH_COBS_MAX bytes.
static inline uint32_t mh_build_frame(uint8_t type, uint8_t seq,
                                      const uint8_t *payload, uint32_t plen,
                                      uint8_t *out) {
  uint8_t tmp[MH_MAX_FRAME];
  uint32_t n = 0;
  tmp[n++] = type;
  tmp[n++] = seq;
  if (plen > MH_MAX_PAYLOAD) plen = MH_MAX_PAYLOAD;
  memcpy(tmp + n, payload, plen);
  n += plen;
  uint16_t crc = mh_crc16(tmp, n);
  tmp[n++] = (uint8_t)(crc & 0xFF);
  tmp[n++] = (uint8_t)((crc >> 8) & 0xFF);
  uint32_t enc = mh_cobs_encode(tmp, n, out);
  out[enc++] = MH_DELIM;
  return enc;
}

// ---- parse one de-framed COBS chunk (without the 0x00). -------------------------------
//      decoded[] is scratch (>= MH_MAX_FRAME). On success sets *type,*seq,*payload(ptr
//      into decoded) and returns payload length (>=0). Returns -1 on COBS/CRC error.
static inline int32_t mh_parse_frame(const uint8_t *src, uint32_t len, uint8_t *decoded,
                                     uint8_t *type, uint8_t *seq, const uint8_t **payload) {
  int32_t d = mh_cobs_decode(src, len, decoded);
  if (d < 4) return -1;                         // need TYPE+SEQ+CRC(2)
  uint16_t want = mh_crc16(decoded, (uint32_t)(d - 2));
  uint16_t got = (uint16_t)decoded[d - 2] | ((uint16_t)decoded[d - 1] << 8);
  if (want != got) return -1;
  *type = decoded[0];
  *seq = decoded[1];
  *payload = decoded + 2;
  return d - 4;
}

#endif // MH_PROTOCOL_H_
