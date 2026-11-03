/* Port of src/network/q3/reliable.ts. GPL-2.0-or-later. */
#include "qa/network/q3/reliable.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
struct qa_q3_reliable_ring {
  int64_t current, acknowledged;
  char slots[QA_Q3_MAX_RELIABLE_COMMANDS][1024];
};
static void check_seq(int64_t v) { if (v < 0 || v > 0x7fffffff) qa_range_error("Reliable sequence must be a nonnegative int32"); }
static void check_signed(int64_t v) { if (v < -((int64_t)0x80000000) || v > 0x7fffffff) qa_range_error("Raw reliable sequence must be a signed int32"); }
static void stored_text(const char *text, char out[1024]) {
  size_t n = strlen(text); if (n > 1023) n = 1023;
  memcpy(out, text, n); out[n] = 0;
}
/* sscanf "cs %i" equivalent per TS regex. */
static int64_t config_string_number(const char *text) {
  if (text[0] != 'c' || text[1] != 's') qa_range_error("SV_ReplacePendingServerCommands: indeterminate sscanf configstring index");
  const char *p = text + 2;
  while (*p == '\t' || *p == '\n' || *p == '\v' || *p == '\f' || *p == '\r' || *p == ' ') p++;
  int sign = 1;
  if (*p == '+' || *p == '-') { if (*p == '-') sign = -1; p++; }
  int radix = 10; const char *digits = p;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    /* require at least one hex digit after 0x to match regex alternative order */
    const char *q = p + 2; bool any = false;
    while ((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'f') || (*q >= 'A' && *q <= 'F')) { any = true; q++; }
    if (any) radix = 16; else radix = 8;
  } else if (p[0] == '0') radix = 8;
  else if (p[0] >= '1' && p[0] <= '9') radix = 10;
  else qa_range_error("SV_ReplacePendingServerCommands: indeterminate sscanf configstring index");
  char *end = NULL;
  long long mag = strtoll(digits, &end, radix == 16 ? 0 : radix);
  if (radix == 16 && end == digits) { /* fallback: strtoll base 0 handles 0x */ }
  if (end == digits) qa_range_error("SV_ReplacePendingServerCommands: indeterminate sscanf configstring index");
  /* strtoll base 8 stops at 8/9 like regex 0[0-7]*; base 0 for hex consumes 0x. */
  int64_t value = sign * (int64_t)mag;
  if (value < -((int64_t)0x80000000) || value > 0x7fffffff)
    qa_range_error("SV_ReplacePendingServerCommands: sscanf index exceeds signed int32");
  return value;
}
static bool ring_init(qa_q3_reliable_ring **out) { *out = calloc(1, sizeof **out); return *out != NULL; }
bool qa_q3_client_reliable_commands_init(qa_q3_client_reliable_commands **out) { return ring_init(out); }
bool qa_q3_server_reliable_commands_init(qa_q3_server_reliable_commands **out) { return ring_init(out); }
void qa_q3_reliable_ring_destroy(qa_q3_reliable_ring *r) { free(r); }
int64_t qa_q3_reliable_ring_sequence(const qa_q3_reliable_ring *r) { return r->current; }
int64_t qa_q3_reliable_ring_acknowledge(const qa_q3_reliable_ring *r) { return r->acknowledged; }
int64_t qa_q3_reliable_ring_outstanding(const qa_q3_reliable_ring *r) { return r->current - r->acknowledged; }
const char *qa_q3_reliable_ring_lookup_masked(const qa_q3_reliable_ring *r, int64_t sequence) {
  check_signed(sequence);
  return r->slots[((int32_t)sequence) & (QA_Q3_MAX_RELIABLE_COMMANDS - 1)];
}
const char *qa_q3_reliable_ring_lookup(const qa_q3_reliable_ring *r, int64_t sequence) {
  check_seq(sequence);
  if (sequence > r->current) qa_range_error("Reliable lookup is ahead of generated commands");
  return qa_q3_reliable_ring_lookup_masked(r, sequence);
}
void qa_q3_reliable_ring_assign_acknowledgement(qa_q3_reliable_ring *r, int64_t sequence) {
  check_signed(sequence); r->acknowledged = sequence;
}
size_t qa_q3_reliable_ring_pending(const qa_q3_reliable_ring *r, qa_q3_reliable_command *out, size_t max) {
  size_t n = 0;
  for (int64_t s = r->acknowledged + 1; s <= r->current && n < max; s++) {
    out[n].sequence = s;
    strcpy(out[n].text, qa_q3_reliable_ring_lookup_masked(r, s));
    n++;
  }
  return n;
}
static void next_sequence(qa_q3_reliable_ring *r) {
  if (r->current == 0x7fffffff) qa_range_error("Reliable sequence exhausted; reconnect required");
  r->current++;
}
static void store(qa_q3_reliable_ring *r, const char *text, qa_q3_reliable_command *out) {
  out->sequence = r->current;
  stored_text(text, out->text);
  strcpy(r->slots[r->current & (QA_Q3_MAX_RELIABLE_COMMANDS - 1)], out->text);
}
static void replace_slot(qa_q3_reliable_ring *r, int64_t sequence, const char *text) {
  char buf[1024]; stored_text(text, buf);
  strcpy(r->slots[sequence & (QA_Q3_MAX_RELIABLE_COMMANDS - 1)], buf);
}
static void validate_ack(const qa_q3_reliable_ring *r, int64_t sequence) {
  check_seq(sequence);
  if (sequence > r->current) qa_range_error("Reliable acknowledgement is ahead of generated commands");
}
void qa_q3_client_reliable_commands_change_latest(qa_q3_client_reliable_commands *r) {
  char buf[1024];
  const char *cur = qa_q3_reliable_ring_lookup_masked(r, r->current);
  size_t n = strlen(cur); if (n > 1022) n = 1022;
  memcpy(buf, cur, n); buf[n] = '\n'; buf[n+1] = 0;
  replace_slot(r, r->current, buf);
}
bool qa_q3_client_reliable_commands_add(qa_q3_client_reliable_commands *r, const char *text, qa_q3_reliable_command *out) {
  qa_q3_reliable_command scratch;
  if (!out) out = &scratch;
  if (qa_q3_reliable_ring_outstanding(r) > QA_Q3_MAX_RELIABLE_COMMANDS) {
    out->sequence = r->current; out->text[0] = 0; /* ReliableOverflowError(sequence, acknowledge) */
    return false;
  }
  next_sequence(r);
  store(r, text, out);
  return true;
}
qa_q3_reliable_acknowledgement qa_q3_client_reliable_commands_acknowledge_through(qa_q3_client_reliable_commands *r, int64_t sequence) {
  validate_ack(r, sequence);
  qa_q3_reliable_acknowledgement a;
  if (sequence < r->current - QA_Q3_MAX_RELIABLE_COMMANDS) {
    r->acknowledged = r->current; a.kind = QA_Q3_ACK_CLAMPED; a.sequence = r->current; return a;
  }
  r->acknowledged = sequence; a.kind = QA_Q3_ACK_ACKNOWLEDGED; a.sequence = sequence; return a;
}
bool qa_q3_server_reliable_commands_replace_pending(qa_q3_server_reliable_commands *r, int64_t sent, const char *text) {
  check_signed(sent);
  for (int64_t s = sent + 1; s <= r->current; s++) {
    const char *pending = qa_q3_reliable_ring_lookup_masked(r, s);
    if (strncmp(text, pending, 2) != 0) continue;
    if (config_string_number(text) != config_string_number(pending)) continue;
    replace_slot(r, s, text);
    return true;
  }
  return false;
}
qa_q3_server_command_append qa_q3_server_reliable_commands_add(qa_q3_server_reliable_commands *r, const char *text) {
  qa_q3_server_command_append a; memset(&a, 0, sizeof a);
  next_sequence(r);
  if (qa_q3_reliable_ring_outstanding(r) == QA_Q3_MAX_RELIABLE_COMMANDS + 1) {
    a.kind = QA_Q3_APPEND_OVERFLOW; a.sequence = r->current; a.acknowledge = r->acknowledged; return a;
  }
  a.kind = QA_Q3_APPEND_QUEUED; store(r, text, &a.command); return a;
}
qa_q3_reliable_acknowledgement qa_q3_server_reliable_commands_acknowledge_through(qa_q3_server_reliable_commands *r, int64_t sequence) {
  validate_ack(r, sequence);
  qa_q3_reliable_acknowledgement a;
  if (sequence < r->current - QA_Q3_MAX_RELIABLE_COMMANDS) {
    r->acknowledged = r->current; a.kind = QA_Q3_ACK_REJECTED_STALE; a.sequence = r->current; return a;
  }
  r->acknowledged = sequence; a.kind = QA_Q3_ACK_ACKNOWLEDGED; a.sequence = sequence; return a;
}
void qa_q3_reliable_ring_replace(qa_q3_reliable_ring *r, int64_t sequence, const char *text) { replace_slot(r, sequence, text); }
void qa_q3_reliable_ring_set_sequences(qa_q3_reliable_ring *r, int64_t current, int64_t acknowledged) {
  r->current = current; r->acknowledged = acknowledged;
}
