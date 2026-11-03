/* Port of src/compat/qc/mod-clients.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_clients.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/info_string.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mod_client_input.h"
typedef qa_compat_qc_mod_client_bindings B;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
static bool client_eq(qa_client_id a, qa_client_id b){return qa_same_client(a, b);}
static qa_compat_qc_mod_client_entry *find_entry(B *b, qa_actor_id a){ for(size_t i=0;i<b->entry_count;i++){ if(actor_eq(b->entries[i].actor,a)) return &b->entries[i]; } return NULL; }
static qa_mod_client_services *svc(B *b){ return b->operations.services; }
static qa_compat_qc_mod_client_entry *require_entry(B *b, qa_actor_id actor) {
  qa_compat_qc_mod_client_entry *entry = find_entry(b, actor);
  qa_client_id client; bool has_client = svc(b)->for_actor(svc(b)->ctx, actor, &client);
  qa_actor_id back; bool has_back = has_client && svc(b)->actor(svc(b)->ctx, client, &back);
  if (!entry || !has_client || (entry->has_client && !client_eq(entry->client, client)) || !has_back || !actor_eq(back, actor))
    qa_range_error("QuakeC component client identity is no longer live");
  if (entry->has_client) return entry;
  /* TS replaces the Map value, which ends any frame holding the old object. */
  entry->has_client = true; entry->client = client; entry->generation = ++b->next_generation; return entry;
}
static void store_entry(B *b, qa_compat_qc_mod_client_entry value) {
  value.generation = ++b->next_generation;
  qa_compat_qc_mod_client_entry *existing = find_entry(b, value.actor);
  if (existing) { *existing = value; return; }
  if (b->entry_count==b->entry_capacity){ size_t c=b->entry_capacity?b->entry_capacity*2:8; b->entries=realloc(b->entries,c*sizeof *b->entries); if(!b->entries) qa_range_error("mod client allocation failed"); b->entry_capacity=c; }
  b->entries[b->entry_count++] = value;
}
static void invoke_calls(B *b, const qa_mod_source_call *calls, size_t n, qa_actor_id actor) {
  for (size_t i = 0; i < n; i++) { require_entry(b, actor); b->operations.invoke(b->operations.ctx, calls[i], actor, NULL); }
}
void qa_compat_qc_mod_client_bindings_init(B *b, qa_compat_qc_mod_client_operations o) { memset(b,0,sizeof *b); b->operations=o; }
void qa_compat_qc_mod_client_bindings_destroy(B *b) { free(b->entries); memset(b,0,sizeof *b); }
bool qa_compat_qc_mod_client_bindings_admitted(B *b, qa_actor_id actor) { return find_entry(b,actor) && require_entry(b,actor)->admitted; }
bool qa_compat_qc_mod_client_bindings_slot(B *b, qa_actor_id actor, int64_t *out) {
  qa_client_id client; bool has_client = svc(b)->for_actor(svc(b)->ctx, actor, &client);
  qa_compat_qc_mod_client_entry *entry = find_entry(b, actor);
  if (!has_client) { if (!entry) return false; *out = require_entry(b, actor)->slot; return true; }
  if (entry) { *out = require_entry(b, actor)->slot; return true; }
  int64_t slot = 1;
  for (;;) { bool used=false; for(size_t i=0;i<b->entry_count;i++) if(b->entries[i].slot==slot) used=true; if(!used) break; slot++; }
  if (slot > (int64_t)b->operations.declaration->maximum) qa_range_error("QuakeC component source client capacity exceeded");
  store_entry(b, (qa_compat_qc_mod_client_entry){actor, true, client, slot, false, 0});
  *out = slot; return true;
}
static void delete_entry(B *b, qa_actor_id actor) {
  for(size_t i=0;i<b->entry_count;i++) if(actor_eq(b->entries[i].actor,actor)){ memmove(&b->entries[i],&b->entries[i+1],(b->entry_count-i-1)*sizeof *b->entries); b->entry_count--; return; }
}
static void admit(B *b, qa_actor_id actor) {
  int64_t slot; if (!qa_compat_qc_mod_client_bindings_slot(b, actor, &slot)) qa_range_error("QuakeC component admission requires a live client");
  qa_compat_qc_mod_client_entry entry = *require_entry(b, actor); /* TS keeps this object across reserve/project */
  if (b->operations.reserve) b->operations.reserve(b->operations.ctx, actor);
  b->operations.project(b->operations.ctx, actor);
  if (!entry.admitted) { entry.admitted = true; store_entry(b, entry); invoke_calls(b, b->operations.declaration->admit, b->operations.declaration->admit_count, actor); }
  if (b->operations.admitted) b->operations.admitted(b->operations.ctx, actor);
}
static void on_event(void *ctx, qa_mod_client_event event) {
  B *b = ctx; qa_actor_id actor = event.identity.actor;
  if (strcmp(event.kind, "admitted") == 0) admit(b, actor);
  else if (strcmp(event.kind, "userinfo") == 0) { admit(b, actor); invoke_calls(b, b->operations.declaration->userinfo, b->operations.declaration->userinfo_count, actor); }
  else if (find_entry(b, actor)) {
    require_entry(b, actor); invoke_calls(b, b->operations.declaration->disconnect, b->operations.declaration->disconnect_count, actor);
    if (strcmp(b->operations.release(b->operations.ctx, actor), "released") == 0) delete_entry(b, actor);
  }
}
static void in_open(void *ctx, qa_mod_client_application *app, void (**cleanup)(void *), void **cleanup_ctx) {
  B *b = ctx;
  if (!require_entry(b, app->identity.actor)->admitted) qa_range_error("QuakeC client input requires source admission");
  b->operations.input->open(b->operations.input->ctx, app, cleanup, cleanup_ctx);
}
static void in_invoke(void *ctx, const void *call, qa_mod_client_application *app) {
  B *b = ctx; require_entry(b, app->identity.actor);
  b->operations.input->invoke(b->operations.input->ctx, *(const qa_mod_source_call *)call, app);
}
static void in_output(void *ctx, const void *outputs, size_t output_count, size_t elem, qa_mod_client_application *app, void (*run)(void *), void *run_ctx, qa_mod_client_input_output **out_outputs, size_t *out_count) {
  (void)elem; B *b = ctx;
  b->operations.input->output(b->operations.input->ctx, (const qa_mod_qc_input_output *)outputs, output_count, app, run, run_ctx, out_outputs, out_count);
}
void qa_compat_qc_mod_client_bindings_start(B *b) {
  if (b->has_unsubscribe) return;
  b->unsubscribe = svc(b)->subscribe(svc(b)->ctx, on_event, b); b->has_unsubscribe = true;
  size_t count = 0; const qa_mod_client_identity *clients = svc(b)->clients(svc(b)->ctx, &count);
  for (size_t i = 0; i < count; i++) admit(b, clients[i].actor);
  size_t binding_count = b->operations.declaration->input_count;
  if (binding_count) {
    if (!b->operations.input) qa_range_error("QuakeC client input requires a source application owner");
    qa_mod_client_input_operations ops = {b, in_open, in_invoke, in_output};
    b->unsubscribe_input = qa_subscribe_mod_client_input(svc(b), b->operations.declaration->input, binding_count, ops);
    b->has_unsubscribe_input = true;
  }
}
char *qa_compat_qc_mod_client_bindings_userinfo(B *b, qa_actor_id actor, const char *key) {
  qa_compat_qc_mod_client_entry *entry = require_entry(b, actor);
  if (!entry->has_client) qa_range_error("QuakeC component userinfo requires a restored client");
  return qa_info_value_for_key(svc(b)->userinfo(svc(b)->ctx, entry->client), key);
}
typedef struct { B *b; qa_compat_qc_mod_client_entry entry; } LiveCtx;
static bool live_fn(void *ctx) {
  LiveCtx *c = ctx; B *b = c->b; const qa_compat_qc_mod_client_entry *entry = &c->entry;
  qa_client_id client; bool has_client = svc(b)->for_actor(svc(b)->ctx, entry->actor, &client);
  qa_actor_id back; bool has_back = has_client && svc(b)->actor(svc(b)->ctx, client, &back);
  const qa_compat_qc_mod_client_entry *now = find_entry(b, entry->actor);
  return entry->admitted && now && now->generation == entry->generation && has_client
      && (!entry->has_client || client_eq(entry->client, client)) && has_back && actor_eq(back, entry->actor);
}
bool qa_compat_qc_mod_client_bindings_frame(B *b, int64_t slot, qa_frame_context frame) {
  if (slot < 1 || slot > (int64_t)b->operations.declaration->maximum) return false;
  const qa_compat_qc_mod_client_entry *entry = NULL;
  for (size_t i = 0; i < b->entry_count; i++) if (b->entries[i].slot == slot) { entry = &b->entries[i]; break; }
  if (!entry) return false;
  LiveCtx live = {b, *entry}; /* callbacks may grow or shift the entry table */
  if (live_fn(&live) && b->operations.think) b->operations.think(b->operations.ctx, live.entry.actor, frame, &live, live_fn);
  for (size_t i = 0; i < b->operations.declaration->frame_count; i++) {
    if (!live_fn(&live)) break;
    b->operations.invoke(b->operations.ctx, b->operations.declaration->frame[i], live.entry.actor, &frame);
  }
  return true;
}
void qa_compat_qc_mod_client_bindings_set_userinfo(B *b, qa_actor_id actor, const char *key, const char *value) {
  if (strchr(value, '\\')) qa_range_error("QuakeC component userinfo field cannot contain a delimiter or NUL");
  qa_compat_qc_mod_client_entry *entry = require_entry(b, actor);
  if (!entry->has_client) qa_range_error("QuakeC component userinfo requires a restored client");
  const char *source = svc(b)->userinfo(svc(b)->ctx, entry->client);
  /* split on backslash into pairs */
  char *copy = strdup(source), *result = malloc(strlen(source) + strlen(key) + strlen(value) + 4);
  if (!copy || !result) qa_range_error("userinfo allocation failed");
  result[0] = 0;
  size_t field_capacity = 1; for (const char *q = source; *q; q++) if (*q == '\\') field_capacity++;
  char **fields = malloc(field_capacity * sizeof *fields); if (!fields) qa_range_error("userinfo allocation failed");
  size_t nf = 0; char *p = copy;
  fields[nf++] = p;
  for (; *p; p++) if (*p == '\\') { *p = 0; fields[nf++] = p + 1; }
  size_t start = (nf && fields[0][0] == 0) ? 1 : 0;
  for (size_t i = start; i + 1 < nf; i += 2) {
    if (strcmp(fields[i], key) != 0) { strcat(result, "\\"); strcat(result, fields[i]); strcat(result, "\\"); strcat(result, fields[i+1]); }
  }
  if (value[0]) { strcat(result, "\\"); strcat(result, key); strcat(result, "\\"); strcat(result, value); }
  svc(b)->set_userinfo(svc(b)->ctx, entry->client, result);
  free(fields); free(copy); free(result);
}
size_t qa_compat_qc_mod_client_bindings_checkpoint(B *b, qa_compat_qc_mod_client_slot *out, size_t cap) {
  for (size_t i = 0; i < b->entry_count && i < cap; i++) out[i] = (qa_compat_qc_mod_client_slot){b->entries[i].actor, b->entries[i].slot, b->entries[i].admitted};
  return b->entry_count;
}
void qa_compat_qc_mod_client_bindings_restore(B *b, const qa_compat_qc_mod_client_slot *entries, size_t count) {
  b->entry_count = 0;
  for (size_t i = 0; i < count; i++) {
    bool bad = entries[i].slot < 1 || entries[i].slot > (int64_t)b->operations.declaration->maximum;
    for (size_t j = 0; j < b->entry_count; j++) if (b->entries[j].slot == entries[i].slot || actor_eq(b->entries[j].actor, entries[i].actor)) bad = true;
    if (bad) qa_range_error("Invalid saved QuakeC component client mapping");
    if (b->entry_count==b->entry_capacity){ size_t c=b->entry_capacity?b->entry_capacity*2:8; b->entries=realloc(b->entries,c*sizeof *b->entries); if(!b->entries) qa_range_error("mod client allocation failed"); b->entry_capacity=c; }
    qa_client_id client; bool has = svc(b)->for_actor(svc(b)->ctx, entries[i].actor, &client);
    qa_compat_qc_mod_client_entry restored; memset(&restored, 0, sizeof restored);
    restored.actor = entries[i].actor; restored.has_client = has; if (has) restored.client = client;
    restored.slot = entries[i].slot; restored.admitted = entries[i].admitted; restored.generation = ++b->next_generation;
    b->entries[b->entry_count++] = restored;
  }
}
void qa_compat_qc_mod_client_bindings_forget(B *b, qa_actor_id actor) { delete_entry(b, actor); }
void qa_compat_qc_mod_client_bindings_close(B *b) {
  if (b->has_unsubscribe) { b->unsubscribe.invoke(b->unsubscribe.ctx); b->has_unsubscribe = false; }
  bool had_input = b->has_unsubscribe_input;
  b->has_unsubscribe_input = false; b->entry_count = 0; /* TS finally: holds if the unsubscribe raises */
  if (had_input) b->unsubscribe_input.invoke(b->unsubscribe_input.ctx);
}
