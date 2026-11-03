/* Port of src/network/common/diagnostics.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_DIAGNOSTICS_H
#define QA_NETWORK_COMMON_DIAGNOSTICS_H
#include <stdbool.h>
#include <stddef.h>
/* Peer text can carry terminal escape sequences; replaces control characters
 * (0x00-0x08, 0x0b-0x1f, 0x7f) with '?' and keeps the first `limit` characters
 * (TS default 256). Returns malloc'd text. */
char *qa_printable_peer_text(const char *text, size_t limit);
/* Bounds per-packet diagnostics so malformed traffic cannot flood the console. */
typedef struct qa_diagnostic_budget {
  double burst, per_second, tokens, last;
  double (*clock)(void *ctx); /* milliseconds; NULL = monotonic clock (TS performance.now) */
  void *clock_ctx;
} qa_diagnostic_budget;
/* TS defaults: burst 20, per second 2. */
void qa_diagnostic_budget_init(qa_diagnostic_budget *b, double burst, double per_second);
bool qa_diagnostic_budget_allow(qa_diagnostic_budget *b);
#endif
