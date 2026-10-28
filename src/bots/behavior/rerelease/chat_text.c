/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/chat_text.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *dups(const char *s) { size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p; }
char *qa_q1_bot_chat_text(const char *locstring, const char *(*lookup)(void *, const char *), void *ctx, qa_bot_random_t random) {
  const char *key = locstring[0] == '$' ? locstring + 1 : locstring;
  const char *variants[64]; size_t n = 0; char buf[1024];
  for (int i = 0; i < 64; i++) { snprintf(buf, sizeof buf, "%s_%d", key, i); const char *t = lookup(ctx, buf); if (!t) break; variants[n++] = t; }
  if (n) { int64_t idx = qa_bot_random_index(random, (int64_t)n); if (idx < 0 || idx >= (int64_t)n) qa_range_error("Bot chat random selection exceeded its source family"); return dups(variants[idx]); }
  const char *t = lookup(ctx, key); return dups(t ? t : key);
}
