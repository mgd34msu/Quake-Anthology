/* Port of src/network/q3/configstrings.ts. GPL-2.0-or-later. */
#include "qa/network/q3/configstrings.h"
#include "qa/core/commands/text.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
bool qa_q3_configstring_commands(int64_t index, const char *value, char ***out, size_t *count) {
  size_t len = strlen(value);
  size_t chunks = len == 0 ? 1 : (len + 998) / 999;
  *out = calloc(chunks, sizeof(char *));
  if (!*out) return false;
  *count = chunks;
  for (size_t part = 0; part < chunks; part++) {
    const char *word = chunks == 1 ? "cs" : part == 0 ? "bcs0" : part == chunks - 1 ? "bcs2" : "bcs1";
    size_t cn = len - part * 999; if (cn > 999) cn = 999;
    size_t need = strlen(word) + 24 + cn + 4;
    char *buf = malloc(need);
    if (!buf) {
      for (size_t i = 0; i < part; i++) free((*out)[i]);
      free(*out); *out = NULL; *count = 0;
      return false;
    }
    int o = snprintf(buf, need, "%s %lld \"", word, (long long)index);
    memcpy(buf + o, value + part * 999, cn);
    buf[o + (int)cn] = '"'; buf[o + (int)cn + 1] = 0;
    (*out)[part] = buf;
  }
  return true;
}
struct qa_q3_server_config_strings {
  char *values[1024];
  qa_q3_configstring_bindings bindings;
};
bool qa_q3_server_config_strings_init(qa_q3_server_config_strings **out, const qa_q3_configstring_bindings *bindings) {
  qa_q3_server_config_strings *s = calloc(1, sizeof *s);
  if (!s) return false;
  for (int i = 0; i < 1024; i++) { s->values[i] = xstrdup(""); if (!s->values[i]) { qa_q3_server_config_strings_destroy(s); return false; } }
  s->bindings = *bindings;
  *out = s; return true;
}
void qa_q3_server_config_strings_destroy(qa_q3_server_config_strings *s) {
  if (!s) return;
  for (int i = 0; i < 1024; i++) free(s->values[i]);
  free(s);
}
const qa_q3_configstring_bindings *qa_q3_server_config_strings_get_bindings(const qa_q3_server_config_strings *s) { return &s->bindings; }
const char *qa_q3_server_config_strings_get(const qa_q3_server_config_strings *s, int64_t index) {
  if (index < 0 || index >= 1024) {
    char buf[96]; snprintf(buf, sizeof buf, "SV_GetConfigstring: bad index %lld\n", (long long)index);
    qa_common_error(QA_COMMON_ERROR_DROP, buf);
  }
  return s->values[index];
}
const char *qa_q3_server_config_strings_get_buffer(const qa_q3_server_config_strings *s, int64_t index, int64_t buffer_size, char *out, size_t out_cap) {
  if (buffer_size < 1) {
    char buf[96]; snprintf(buf, sizeof buf, "SV_GetConfigstring: bufferSize == %lld", (long long)buffer_size);
    qa_common_error(QA_COMMON_ERROR_DROP, buf);
  }
  const char *v = qa_q3_server_config_strings_get(s, index);
  size_t n = strlen(v);
  if ((int64_t)n > buffer_size - 1) n = (size_t)(buffer_size - 1);
  if (n >= out_cap) n = out_cap - 1;
  memcpy(out, v, n); out[n] = 0;
  return out;
}
/* One TS `print(\`cmd ${String(sequence).padStart(5)}: ${text}\n\`)` call. */
static void print_pending(const qa_q3_configstring_client *client, int64_t seq, const char *text) {
  char head[32];
  int hn = snprintf(head, sizeof head, "cmd %5lld: ", (long long)seq);
  size_t tn = strlen(text);
  char *line = malloc((size_t)hn + tn + 2);
  if (!line) qa_range_error("configstring print allocation failed");
  memcpy(line, head, (size_t)hn); memcpy(line + hn, text, tn); line[(size_t)hn + tn] = '\n'; line[(size_t)hn + tn + 1] = 0;
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, line);
  client->print(client->print_ctx, line);
  qa_cleanup_pop(&guard);
  free(line);
}
static bool cs_command(const qa_q3_configstring_client *client, const char *command) {
  qa_q3_server_command_append result = qa_q3_server_reliable_commands_add(client->reliable, command);
  if (result.kind == QA_Q3_APPEND_QUEUED) return true;
  if (client->print) {
    client->print(client->print_ctx, "===== pending server commands =====\n");
    int64_t seq = qa_q3_reliable_ring_acknowledge(client->reliable) + 1;
    for (; seq <= qa_q3_reliable_ring_sequence(client->reliable); seq++)
      print_pending(client, seq, qa_q3_reliable_ring_lookup_masked(client->reliable, seq));
    print_pending(client, seq, command);
  }
  if (client->drop) client->drop(client->drop_ctx, "Server command overflow");
  return false;
}
typedef struct { char **commands; size_t count; char *with_nl; } cs_scratch;
static void cs_scratch_release(void *arg) {
  cs_scratch *sc = arg;
  for (size_t i = 0; i < sc->count; i++) free(sc->commands[i]);
  free(sc->commands); free(sc->with_nl);
  sc->commands = NULL; sc->count = 0; sc->with_nl = NULL;
}
void qa_q3_server_config_strings_set(qa_q3_server_config_strings *s, int64_t index, const char *value) {
  if (index < 0 || index >= 1024) {
    char buf[96]; snprintf(buf, sizeof buf, "SV_SetConfigstring: bad index %lld\n", (long long)index);
    qa_common_error(QA_COMMON_ERROR_DROP, buf);
  }
  char *text = qa_source_command_text(value ? value : "");
  if (!text) qa_range_error("configstring allocation failed");
  if (strcmp(s->values[index], text) == 0) { free(text); return; }
  free(s->values[index]); s->values[index] = text;
  if (!s->bindings.running(s->bindings.ctx) && !s->bindings.restarting(s->bindings.ctx)) return;
  size_t client_count = 0;
  const qa_q3_configstring_client *clients = s->bindings.clients(s->bindings.ctx, &client_count);
  cs_scratch sc = { NULL, 0, NULL };
  if (!qa_q3_configstring_commands(index, text, &sc.commands, &sc.count)) qa_range_error("configstring allocation failed");
  /* drop/print/assertCurrent are host code that may raise (assertCurrent rejects a stale connection). */
  qa_cleanup guard;
  qa_cleanup_push(&guard, cs_scratch_release, &sc);
  size_t text_len = strlen(text);
  for (size_t c = 0; c < client_count; c++) {
    const qa_q3_configstring_client *client = &clients[c];
    if (client->phase != QA_Q3_CS_PHASE_PRIMED && client->phase != QA_Q3_CS_PHASE_ACTIVE) continue;
    if (index == 0 && client->no_server_info) continue;
    for (size_t i = 0; i < sc.count; i++) {
      size_t need = strlen(sc.commands[i]) + 2;
      sc.with_nl = malloc(need);
      if (!sc.with_nl) qa_range_error("configstring allocation failed");
      snprintf(sc.with_nl, need, "%s\n", sc.commands[i]);
      bool ok = cs_command(client, sc.with_nl);
      free(sc.with_nl); sc.with_nl = NULL;
      if (!ok) break;
      if (text_len >= 1000 && client->assert_current) client->assert_current(client->assert_ctx);
    }
  }
  qa_cleanup_pop(&guard);
  cs_scratch_release(&sc);
}
