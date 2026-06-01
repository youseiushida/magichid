// Host conformance test for the firmware codec (mh_protocol.h) against the golden vectors
// in spec/protocol_vectors.txt -- the language-neutral contract every client must also
// match, so all implementations agree. Run from the repo root (relative vectors path).
#include "doctest.h"
#include "mh_protocol.h"
#include <cstdio>
#include <cstring>
#include <string>

static size_t unhex(const char *s, uint8_t *out) {
  if (std::strcmp(s, "-") == 0) return 0;
  size_t n = std::strlen(s) / 2;
  for (size_t i = 0; i < n; i++) { unsigned v; std::sscanf(s + 2 * i, "%2x", &v); out[i] = (uint8_t)v; }
  return n;
}
static std::string tohex(const uint8_t *b, size_t n) {
  std::string s; char t[3];
  for (size_t i = 0; i < n; i++) { std::sprintf(t, "%02x", b[i]); s += t; }
  return s;
}

TEST_CASE("codec conforms to spec/protocol_vectors.txt") {
  const char *path = "spec/protocol_vectors.txt";
  FILE *f = std::fopen(path, "r");
  REQUIRE_MESSAGE(f != nullptr, "cannot open spec/protocol_vectors.txt (run from repo root)");

  char line[8192], kind[16], s1[4096], s2[4096];
  unsigned t, seq;
  int n = 0;
  while (std::fgets(line, sizeof line, f)) {
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
    if (std::sscanf(line, "%15s", kind) != 1) continue;
    if (std::strcmp(kind, "FRAME") == 0) {
      if (std::sscanf(line, "%*s %x %u %4095s %4095s", &t, &seq, s1, s2) != 4) continue;
      uint8_t pl[4096]; size_t pn = unhex(s1, pl);
      uint8_t out[MH_COBS_MAX]; uint32_t on = mh_build_frame((uint8_t)t, (uint8_t)seq, pl, pn, out);
      CHECK(tohex(out, on) == std::string(s2));
      n++;
    } else if (std::strcmp(kind, "COBS") == 0) {
      if (std::sscanf(line, "%*s %4095s %4095s", s1, s2) != 2) continue;
      uint8_t in[4096]; size_t inn = unhex(s1, in);
      uint8_t enc[8192]; uint32_t en = mh_cobs_encode(in, inn, enc);
      CHECK(tohex(enc, en) == std::string(s2));
      n++;
    }
  }
  std::fclose(f);
  CHECK(n > 0);   // sanity: vectors were actually read
}
