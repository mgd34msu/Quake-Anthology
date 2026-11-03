/* Port of src/network/common/diagnostics.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/network/common/diagnostics.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/foundation/js.h"
/* Length of a well-formed UTF-8 sequence at p, or 1 for a stray byte (one character). */
static size_t utf8_sequence(const unsigned char *p) {
  size_t need = *p < 0x80 ? 1 : *p >= 0xc2 && *p < 0xe0 ? 2 : *p >= 0xe0 && *p < 0xf0 ? 3 : *p >= 0xf0 && *p < 0xf5 ? 4 : 0;
  if (need == 0) return 1;
  for (size_t i = 1; i < need; i++) if ((p[i] & 0xc0) != 0x80) return 1;
  return need;
}
/* text.replace(controls, "?").slice(0, limit): the limit counts UTF-16 units, so a
 * four-byte character takes two and a cut through it leaves a lone surrogate, which
 * encodes as U+FFFD. */
char *qa_printable_peer_text(const char *text, size_t limit) {
  size_t length = strlen(text);
  char *out = malloc((length < limit * 4 ? length : limit * 4) + 4);
  if (!out) return NULL;
  size_t used = 0, n = 0;
  for (size_t i = 0; i < length && used < limit;) {
    size_t step = utf8_sequence((const unsigned char *)text + i);
    if (step == 4 && used + 2 > limit) { memcpy(out + n, "\xef\xbf\xbd", 3); n += 3; used++; break; }
    if (step == 1) {
      unsigned char c = (unsigned char)text[i];
      out[n++] = (c <= 0x08 || (c >= 0x0b && c <= 0x1f) || c == 0x7f) ? '?' : (char)c;
    } else { memcpy(out + n, text + i, step); n += step; }
    used += step == 4 ? 2 : 1; i += step;
  }
  out[n] = '\0';
  return out;
}
static double monotonic_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}
static double clock_now(const qa_diagnostic_budget *b) { return b->clock ? b->clock(b->clock_ctx) : monotonic_ms(); }
void qa_diagnostic_budget_init(qa_diagnostic_budget *b, double burst, double per_second) {
  b->burst = burst;
  b->per_second = per_second;
  b->tokens = burst;
  b->clock = NULL;
  b->clock_ctx = NULL;
  b->last = clock_now(b);
}
bool qa_diagnostic_budget_allow(qa_diagnostic_budget *b) {
  double now = clock_now(b);
  b->tokens = qa_js_min(b->burst, b->tokens + (now - b->last) / 1000 * b->per_second);
  b->last = now;
  if (b->tokens < 1) return false;
  b->tokens--;
  return true;
}
