/* Port of src/network/q3/rcon.ts. GPL-2.0-or-later. */
#include "qa/network/q3/rcon.h"
#include "qa/core/commands/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct qa_q3_rcon { qa_q3_rcon_state *state; qa_q3_rcon_bindings bindings; };
bool qa_q3_rcon_state_init(qa_q3_rcon_state *s) { s->last_time = 0; return true; }
bool qa_q3_rcon_init(qa_q3_rcon **out, qa_q3_rcon_state *state, const qa_q3_rcon_bindings *bindings) {
  qa_q3_rcon *r = calloc(1, sizeof *r);
  if (!r) return false;
  r->state = state; r->bindings = *bindings;
  *out = r; return true;
}
void qa_q3_rcon_destroy(qa_q3_rcon *r) { free(r); }
qa_q3_rcon_state *qa_q3_rcon_get_state(const qa_q3_rcon *r) { return r->state; }
const qa_q3_rcon_bindings *qa_q3_rcon_get_bindings(const qa_q3_rcon *r) { return &r->bindings; }
typedef struct { qa_q3_rcon *r; const qa_q3_address *from; } flush_ctx_t;
static void rcon_flush(void *ctx, const char *text) {
  flush_ctx_t *f = ctx;
  size_t n = strlen(text) + 8;
  char *buf = malloc(n);
  if (!buf) return;
  snprintf(buf, n, "print\n%s", text);
  uint8_t *bytes = NULL; size_t len = 0;
  if (qa_q3_encode_connectionless_text(buf, &bytes, &len)) {
    f->r->bindings.send(f->r->bindings.ctx, f->from, bytes, len);
    free(bytes);
  }
  free(buf);
}
typedef struct { qa_q3_rcon *r; const qa_q3_connectionless_packet *packet; bool valid; } exec_ctx_t;
static void rcon_exec(void *ctx) {
  exec_ctx_t *e = ctx;
  qa_q3_rcon_bindings *b = &e->r->bindings;
  char *pw = qa_source_command_text(b->password(b->ctx));
  size_t pw_len = pw ? strlen(pw) : 0;
  free(pw);
  if (pw_len == 0) { if (b->print) b->print(b->print_ctx, "No rconpassword set on the server.\n"); return; }
  if (!e->valid) { if (b->print) b->print(b->print_ctx, "Bad rconpassword.\n"); return; }
  char *line = qa_source_command_text(e->packet->line);
  if (!line) return;
  size_t cursor = 4;
  while (line[cursor] == ' ') cursor++;
  while (line[cursor] && line[cursor] != ' ') cursor++;
  while (line[cursor] == ' ') cursor++;
  char command[1024];
  size_t n = strlen(line + cursor); if (n > 1023) n = 1023;
  memcpy(command, line + cursor, n); command[n] = 0;
  free(line);
  if (n != 0) b->execute(b->ctx, command);
}
void qa_q3_rcon_receive(qa_q3_rcon *r, const qa_q3_address *from, const qa_q3_connectionless_packet *packet, double milliseconds) {
  uint32_t time = (uint32_t)milliseconds;
  if (time < (uint32_t)(r->state->last_time + 500)) return;
  r->state->last_time = time;
  char *password = qa_source_command_text(r->bindings.password(r->bindings.ctx));
  bool valid = password && password[0] != 0
    && strcmp(password, packet->argument_count ? packet->arguments[0] : "") == 0;
  /* TS re-reads sourceCommandText(password()) inside redirect for the empty check. */
  free(password);
  flush_ctx_t fc = { r, from };
  exec_ctx_t ec = { r, packet, valid };
  r->bindings.redirect(r->bindings.ctx, 1008, rcon_flush, &fc, rcon_exec, &ec);
}
bool qa_q3_encode_rcon(const char *password, const char *command, uint8_t **out, size_t *out_len) {
  char *pw = qa_source_command_text(password);
  char *cmd = qa_source_command_text(command);
  if (!pw || !cmd) { free(pw); free(cmd); return false; }
  size_t n = strlen(pw) + strlen(cmd) + 8;
  char *buf = malloc(n);
  if (!buf) { free(pw); free(cmd); return false; }
  snprintf(buf, n, "rcon %s %s", pw, cmd);
  bool ok = qa_q3_encode_connectionless_text(buf, out, out_len);
  free(pw); free(cmd); free(buf);
  return ok;
}
