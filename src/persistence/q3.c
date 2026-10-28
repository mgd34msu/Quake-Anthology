/* Port of src/persistence/q3.ts. GPL-2.0-or-later. */
#include "qa/persistence/q3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/value.h"

/* TS sourceBuffer: cut at NUL, then at 1023 chars; charCodes above
 * 255 throw (impossible for C bytes). Returns the effective length. */
static size_t source_buffer(const char *value, size_t length) {
  size_t end = length;
  for (size_t i = 0; i < length; i++)
    if (value[i] == '\0') { end = i; break; }
  if (end > 1023) end = 1023;
  return end;
}
typedef struct scan_result { int32_t value; size_t next; } scan_result;
static scan_result scan_integer(const char *buffer, size_t length, size_t start) {
  if (start > length)
    qa_save_format_error("q3-session", "source integer scan passed its terminating NUL");
  size_t cursor = start;
  while (cursor < length) {
    int byte = (unsigned char)buffer[cursor];
    int signed_byte = byte < 128 ? byte : byte - 256;
    if (signed_byte > 32) break;
    cursor++;
  }
  if (cursor == length) return (scan_result){0, cursor};
  int32_t sign = buffer[cursor] == '-' ? -1 : 1;
  if (buffer[cursor] == '+' || buffer[cursor] == '-') cursor++;
  int32_t value = 0;
  for (;;) {
    if (cursor == length)
      return (scan_result){(int32_t)((uint32_t)value * (uint32_t)sign), cursor + 1};
    int byte = (unsigned char)buffer[cursor++];
    if (byte < 48 || byte > 57)
      return (scan_result){(int32_t)((uint32_t)value * (uint32_t)sign), cursor};
    value = (int32_t)((uint32_t)value * 10u + (uint32_t)(byte - 48));
  }
}

qa_persist_q3_client_session qa_decode_q3_client_session(const char *value, size_t length) {
  size_t buffer_length = source_buffer(value, length);
  size_t offset = 0;
  qa_persist_q3_client_session session;
  int32_t fields[7];
  for (int i = 0; i < 7; i++) {
    scan_result result = scan_integer(value, buffer_length, offset);
    offset = result.next;
    fields[i] = result.value;
  }
  session.team = fields[0];
  session.spectator_time = fields[1];
  session.spectator_state = fields[2];
  session.spectator_client = fields[3];
  session.wins = fields[4];
  session.losses = fields[5];
  session.team_leader = fields[6];
  return session;
}
char *qa_encode_q3_client_session(qa_persist_q3_client_session session) {
  char buffer[128];
  int written = snprintf(buffer, sizeof buffer, "%d %d %d %d %d %d %d",
                         session.team, session.spectator_time, session.spectator_state,
                         session.spectator_client, session.wins, session.losses,
                         session.team_leader);
  if (written < 0) return NULL;
  char *out = (char *)malloc((size_t)written + 1);
  if (out != NULL) memcpy(out, buffer, (size_t)written + 1);
  return out;
}

qa_q3_session_save qa_read_q3_sessions(qa_q3_session_cvars cvars,
                                       const int64_t *client_slots, size_t slot_count) {
  size_t game_length = 0;
  const char *game = cvars.get(cvars.ctx, "session", &game_length);
  qa_q3_session_save save;
  save.game_type = scan_integer(game, source_buffer(game, game_length), 0).value;
  save.clients = slot_count > 0
      ? (qa_q3_session_save_client *)malloc(slot_count * sizeof *save.clients) : NULL;
  save.client_count = slot_count;
  if (slot_count > 0 && save.clients == NULL) { save.client_count = 0; return save; }
  for (size_t i = 0; i < slot_count; i++) {
    char name[64];
    (void)snprintf(name, sizeof name, "session%lld", (long long)client_slots[i]);
    size_t length = 0;
    const char *value = cvars.get(cvars.ctx, name, &length);
    save.clients[i].slot = client_slots[i];
    save.clients[i].session = qa_decode_q3_client_session(value, length);
  }
  return save;
}
void qa_write_q3_sessions(qa_q3_session_cvars cvars, qa_q3_session_save save) {
  char game[32];
  int written = snprintf(game, sizeof game, "%d", save.game_type);
  cvars.set(cvars.ctx, "session", game, written > 0 ? (size_t)written : 0);
  for (size_t i = 0; i < save.client_count; i++) {
    char name[64];
    (void)snprintf(name, sizeof name, "session%lld", (long long)save.clients[i].slot);
    char *value = qa_encode_q3_client_session(save.clients[i].session);
    if (value == NULL) continue;
    cvars.set(cvars.ctx, name, value, strlen(value));
    free(value);
  }
}
