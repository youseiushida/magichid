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
//  Constants come from mh_protocol_defs.h (generated from spec/protocol.yaml). The COBS/
//  CRC algorithm below is hand-written; spec/protocol_vectors.txt is the golden oracle
//  that any client must match byte-for-byte.
//  Pure header (static inline) so the .ino and any .cpp can include it.
// =====================================================================================
#ifndef MH_PROTOCOL_H_
#define MH_PROTOCOL_H_

#include <stdint.h>
#include <string.h>

// ---- protocol constants (GENERATED from spec/protocol.yaml; edit there, not here) ----
#include "mh_protocol_defs.h"   // MH_DELIM, MH_T_*, MH_ST_*, MH_NACK_*, MH_PROTO_VERSION,
                                // MH_MAX_PAYLOAD, MH_HID_MAX_PAYLOAD, MH_DEDUP_WINDOW

// ---- derived sizes -------------------------------------------------------------------
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
