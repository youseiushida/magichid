// Parity test: emits frames/COBS using the firmware's mh_protocol.h so we can diff
// against the Python implementation. Build: gcc -I.. test_protocol_parity.c
#include "mh_protocol.h"
#include <stdio.h>

static void hex(const uint8_t *b, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) printf("%02x", b[i]);
  printf("\n");
}

int main(void) {
  uint8_t out[MH_COBS_MAX];
  uint32_t n;

  uint8_t p1[] = {7, 0, 0, 4, 0, 0, 0, 0, 0};
  printf("FRAME "); n = mh_build_frame(0x01, 5, p1, sizeof(p1), out); hex(out, n);

  uint8_t p2[140];
  for (int i = 0; i < 140; i++) p2[i] = (uint8_t)(i * 7 + 1);
  printf("FRAME "); n = mh_build_frame(0x86, 1, p2, sizeof(p2), out); hex(out, n);

  uint8_t dummy = 0;
  printf("FRAME "); n = mh_build_frame(0x02, 0, &dummy, 0, out); hex(out, n);

  uint8_t p4[] = {8, 2, 0, 0, 0, 0};
  printf("FRAME "); n = mh_build_frame(0x84, 0, p4, sizeof(p4), out); hex(out, n);

  // COBS-only with > 254 nonzero bytes to exercise the 0xFF code path
  uint8_t big[300];
  for (int i = 0; i < 300; i++) big[i] = (uint8_t)(1 + (i % 200));
  uint8_t enc[400];
  uint32_t en = mh_cobs_encode(big, 300, enc);
  printf("COBS %u ", en); hex(enc, en);
  uint8_t dec[400];
  int32_t dn = mh_cobs_decode(enc, en, dec);
  printf("DEC %d ", dn); hex(dec, dn);
  return 0;
}
