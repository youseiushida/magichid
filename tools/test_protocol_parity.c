// Conformance test (C): verify the firmware codec (mh_protocol.h) against the golden
// vectors in spec/protocol_vectors.txt. The Python client is checked against the SAME
// file by test_protocol_parity.py, so both conform to one contract.
//   Build:  gcc -I.. tools/test_protocol_parity.c -o parity
//   Run  :  ./parity spec/protocol_vectors.txt
#include "mh_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t unhex(const char *s, uint8_t *out) {
  if (strcmp(s, "-") == 0) return 0;
  size_t n = strlen(s) / 2;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    sscanf(s + 2 * i, "%2x", &v);
    out[i] = (uint8_t)v;
  }
  return n;
}

static void tohex(const uint8_t *b, size_t n, char *out) {
  for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
  out[2 * n] = 0;
}

int main(int argc, char **argv) {
  const char *path = (argc > 1) ? argv[1] : "spec/protocol_vectors.txt";
  FILE *f = fopen(path, "r");
  if (!f) { perror("open vectors"); return 2; }

  char line[8192], kind[16], s1[4096], s2[4096];
  int total = 0, fails = 0;
  unsigned t, seq;
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
    if (sscanf(line, "%15s", kind) != 1) continue;

    if (strcmp(kind, "FRAME") == 0) {
      if (sscanf(line, "%*s %x %u %4095s %4095s", &t, &seq, s1, s2) != 4) continue;
      uint8_t pl[4096];
      size_t pn = unhex(s1, pl);
      uint8_t out[MH_COBS_MAX];
      uint32_t on = mh_build_frame((uint8_t)t, (uint8_t)seq, pl, pn, out);
      char hx[2 * MH_COBS_MAX + 1];
      tohex(out, on, hx);
      total++;
      if (strcmp(hx, s2) != 0) {
        fails++;
        printf("FRAME mismatch t=%x seq=%u\n  exp %s\n  got %s\n", t, seq, s2, hx);
      }
    } else if (strcmp(kind, "COBS") == 0) {
      if (sscanf(line, "%*s %4095s %4095s", s1, s2) != 2) continue;
      uint8_t in[4096];
      size_t inn = unhex(s1, in);
      uint8_t enc[8192];
      uint32_t en = mh_cobs_encode(in, inn, enc);
      char hx[16400];
      tohex(enc, en, hx);
      total++;
      if (strcmp(hx, s2) != 0) {
        fails++;
        printf("COBS mismatch\n  exp %s\n  got %s\n", s2, hx);
      }
    }
  }
  fclose(f);
  printf("C conformance: %d/%d vectors OK\n", total - fails, total);
  return fails ? 1 : 0;
}
