/* Port of src/compat/q2/native-mod-clients.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_clients.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/world/session/mod_client_input.h"
#include "qa/foundation/js.h"

typedef qa_compat_q2_native_mod_clients_binding binding;
typedef qa_compat_q2_native_client_entry entry_t;
#define OPS(s) (&(s)->operations)
#define SERVICES(s) ((s)->operations.services)
void qa_compat_q2_native_mod_clients_init(binding *s, qa_compat_q2_native_mod_client_operations operations) { memset(s, 0, sizeof *s); s->operations = operations; }
void qa_compat_q2_native_mod_clients_destroy(binding *s) { free(s->entries); free(s->denied); s->entries = NULL; s->denied = NULL; s->entry_count = s->entry_capacity = s->denied_count = s->denied_capacity = 0; }
static entry_t *find_entry(const binding *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->entry_count; i++) if (qa_same_actor(s->entries[i].actor, actor)) return &s->entries[i];
  return NULL;
}
static void delete_entry(binding *s, qa_actor_id actor) {
  entry_t *entry = find_entry(s, actor); if (entry == NULL) return;
  size_t index = (size_t)(entry - s->entries);
  memmove(entry, entry + 1, (s->entry_count - index - 1) * sizeof *entry); s->entry_count--;
}
static bool is_denied(const binding *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->denied_count; i++) if (qa_same_actor(s->denied[i], actor)) return true;
  return false;
}
static void delete_denied(binding *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->denied_count; i++) if (qa_same_actor(s->denied[i], actor)) { memmove(&s->denied[i], &s->denied[i + 1], (s->denied_count - i - 1) * sizeof *s->denied); s->denied_count--; return; }
}
/* TS services.actor(client)?.equals(actor) === true. */
static bool client_is_actor(const binding *s, qa_client_id client, qa_actor_id actor) {
  qa_actor_id current; return SERVICES(s)->actor(SERVICES(s)->ctx, client, &current) && qa_same_actor(current, actor);
}
static entry_t require_entry(const binding *s, qa_actor_id actor) {
  const entry_t *entry = find_entry(s, actor); qa_client_id client;
  if (entry == NULL || !SERVICES(s)->for_actor(SERVICES(s)->ctx, actor, &client) || !qa_same_client(entry->client, client) || !client_is_actor(s, client, actor))
    qa_range_error("Native component client identity is no longer live");
  return *entry;
}
bool qa_compat_q2_native_mod_clients_slot(binding *s, qa_actor_id actor, double *out) {
  if (find_entry(s, actor) != NULL) { *out = require_entry(s, actor).slot; return true; }
  if (is_denied(s, actor)) qa_range_error("Rejected native client lost its reserved source row");
  qa_client_id client;
  if (!SERVICES(s)->for_actor(SERVICES(s)->ctx, actor, &client)) return false;
  if (!client_is_actor(s, client, actor)) qa_range_error("Native component client identity is no longer live");
  double slot = 0;
  for (;;) {
    bool occupied = false;
    for (size_t i = 0; i < s->entry_count && !occupied; i++) occupied = s->entries[i].slot == slot;
    if (!occupied) break;
    slot++;
  }
  if (slot >= OPS(s)->declaration->maximum) qa_range_error("Native component source client capacity exceeded");
  entry_t *entry = qa_compat_q2_grow((void **)&s->entries, &s->entry_count, &s->entry_capacity, sizeof *entry);
  entry->actor = actor; entry->client = client; entry->slot = slot; entry->admitted = false; entry->identity = ++s->next_identity;
  *out = slot; return true;
}
bool qa_compat_q2_native_mod_clients_has(const binding *s, qa_actor_id actor) { return find_entry(s, actor) != NULL; }
bool qa_compat_q2_native_mod_clients_admitted(binding *s, qa_actor_id actor) {
  return find_entry(s, actor) != NULL && require_entry(s, actor).admitted && !is_denied(s, actor);
}
bool qa_compat_q2_native_mod_clients_rejects(const binding *s, qa_actor_id actor) { return is_denied(s, actor); }
static void run_calls(binding *s, const qa_native_mod_source_call *calls, size_t count, qa_actor_id actor) {
  uint64_t identity = require_entry(s, actor).identity;
  for (size_t i = 0; i < count; i++) {
    const entry_t *now = find_entry(s, actor);
    if (now == NULL || now->identity != identity) break;
    (void)require_entry(s, actor);
    double result; if (!OPS(s)->invoke(OPS(s)->ctx, &calls[i], actor, &result)) break;
  }
}
static bool admit(binding *s, qa_actor_id actor) {
  if (is_denied(s, actor)) return false;
  double slot;
  if (!qa_compat_q2_native_mod_clients_slot(s, actor, &slot)) qa_range_error("Native component admission requires a live client");
  entry_t entry = require_entry(s, actor);
  OPS(s)->project(OPS(s)->ctx, actor);
  if (entry.admitted) return true;
  const qa_native_mod_clients *d = OPS(s)->declaration;
  for (size_t i = 0; i < d->admit_count; i++) {
    (void)require_entry(s, actor);
    double result;
    if (!OPS(s)->invoke(OPS(s)->ctx, &d->admit[i].call, actor, &result)) return false;
    if (!strcmp(d->admit[i].accepts, "nonzero") && result == 0) {
      char scratch[1024]; const char *reason = qa_q2_userinfo_get(qa_compat_q2_native_mod_clients_userinfo(s, actor), "rejmsg", scratch, sizeof scratch);
      if (reason == NULL || reason[0] == '\0') reason = "Connection refused";
      SERVICES(s)->drop(SERVICES(s)->ctx, require_entry(s, actor).client, reason, OPS(s)->content);
      qa_actor_id *slot_denied = qa_compat_q2_grow((void **)&s->denied, &s->denied_count, &s->denied_capacity, sizeof *slot_denied);
      *slot_denied = actor; return false;
    }
  }
  (void)require_entry(s, actor);
  entry_t *now = find_entry(s, actor); now->admitted = true; now->identity = ++s->next_identity;
  if (OPS(s)->admitted != NULL) OPS(s)->admitted(OPS(s)->ctx, actor);
  return true;
}
static void disconnect(binding *s, qa_actor_id actor) {
  if (require_entry(s, actor).admitted) run_calls(s, OPS(s)->declaration->disconnect, OPS(s)->declaration->disconnect_count, actor);
  OPS(s)->release(OPS(s)->ctx, actor); delete_entry(s, actor);
}
static void on_client_event(void *ctx, qa_mod_client_event event) {
  binding *s = ctx; qa_actor_id actor = event.identity.actor;
  if (!strcmp(event.kind, "admitted")) (void)admit(s, actor);
  else if (!strcmp(event.kind, "userinfo")) { if (admit(s, actor)) run_calls(s, OPS(s)->declaration->userinfo, OPS(s)->declaration->userinfo_count, actor); }
  else { if (find_entry(s, actor) != NULL) disconnect(s, actor); delete_denied(s, actor); }
}
static void input_noop(void *ctx) { (void)ctx; }
static void input_open(void *ctx, qa_mod_client_application *application, void (**cleanup)(void *), void **cleanup_ctx) {
  binding *s = ctx; qa_actor_id actor = application->identity.actor;
  if (require_entry(s, actor).admitted && !is_denied(s, actor)) { OPS(s)->open_input(OPS(s)->ctx, application, cleanup, cleanup_ctx); return; }
  *cleanup = input_noop; *cleanup_ctx = NULL;
}
static void input_invoke(void *ctx, const void *call, qa_mod_client_application *application) {
  binding *s = ctx; qa_actor_id actor = application->identity.actor;
  if (require_entry(s, actor).admitted && !is_denied(s, actor)) OPS(s)->invoke_input(OPS(s)->ctx, call, application);
}
static void input_output(void *ctx, const void *outputs, size_t output_count, size_t output_elem_size, qa_mod_client_application *application,
    void (*run)(void *), void *run_ctx, qa_mod_client_input_output **out_outputs, size_t *out_count) {
  (void)output_elem_size; /* always sizeof qa_native_mod_input_output for this declaration. */
  binding *s = ctx; OPS(s)->input_output(OPS(s)->ctx, outputs, output_count, application, run, run_ctx, out_outputs, out_count);
}
void qa_compat_q2_native_mod_clients_start(binding *s) {
  if (s->subscribed) return;
  s->unsubscribe = SERVICES(s)->subscribe(SERVICES(s)->ctx, on_client_event, s); s->subscribed = true;
  size_t count = 0; const qa_mod_client_identity *listed = SERVICES(s)->clients(SERVICES(s)->ctx, &count);
  qa_mod_client_identity *identities = calloc(count + 1, sizeof *identities); if (identities == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) identities[i] = listed[i];
  for (size_t i = 0; i < count; i++) (void)admit(s, identities[i].actor);
  free(identities);
  qa_mod_client_input_operations operations; memset(&operations, 0, sizeof operations);
  operations.ctx = s; operations.open = input_open; operations.invoke = input_invoke; operations.output = input_output;
  s->unsubscribe_input = qa_subscribe_mod_client_input(SERVICES(s), OPS(s)->declaration->input, OPS(s)->declaration->input_count, operations); s->input_subscribed = true;
}
static bool entry_live(const binding *s, qa_actor_id actor) {
  qa_client_id client; return SERVICES(s)->for_actor(SERVICES(s)->ctx, actor, &client) && client_is_actor(s, client, actor);
}
bool qa_compat_q2_native_mod_clients_frame(binding *s, double slot) {
  const entry_t *entry = NULL;
  for (size_t i = 0; i < s->entry_count && entry == NULL; i++) if (s->entries[i].slot + 1 == slot) entry = &s->entries[i];
  if (entry == NULL) return false;
  qa_actor_id actor = entry->actor;
  if (entry_live(s, actor) && qa_compat_q2_native_mod_clients_admitted(s, actor)) run_calls(s, OPS(s)->declaration->frame, OPS(s)->declaration->frame_count, actor);
  return true;
}
static int by_slot(const void *a, const void *b) { double l = ((const entry_t *)a)->slot, r = ((const entry_t *)b)->slot; return l < r ? -1 : l > r ? 1 : 0; }
void qa_compat_q2_native_mod_clients_end_frame(binding *s) {
  if (OPS(s)->declaration->end_frame_count == 0) return;
  size_t count = s->entry_count; entry_t *sorted = calloc(count + 1, sizeof *sorted); if (sorted == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(sorted, s->entries, count * sizeof *sorted);
  qa_js_sort(sorted, count, sizeof *sorted, by_slot);
  for (size_t i = 0; i < count; i++) if (entry_live(s, sorted[i].actor) && qa_compat_q2_native_mod_clients_admitted(s, sorted[i].actor))
    run_calls(s, OPS(s)->declaration->end_frame, OPS(s)->declaration->end_frame_count, sorted[i].actor);
  free(sorted);
}
const char *qa_compat_q2_native_mod_clients_userinfo(binding *s, qa_actor_id actor) { return SERVICES(s)->userinfo(SERVICES(s)->ctx, require_entry(s, actor).client); }
void qa_compat_q2_native_mod_clients_set_userinfo(binding *s, qa_actor_id actor, const char *value) { SERVICES(s)->set_userinfo(SERVICES(s)->ctx, require_entry(s, actor).client, value); }
typedef struct command_call { binding *s; qa_actor_id actor; } command_call;
static void command_body(void *value) { command_call *c = value; run_calls(c->s, OPS(c->s)->declaration->command, OPS(c->s)->declaration->command_count, c->actor); }
bool qa_compat_q2_native_mod_clients_invoke_command(binding *s, qa_command_invocation *command) {
  if (OPS(s)->declaration->command_count == 0) return false;
  const qa_command_origin *origin = &command->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT && origin->kind != QA_COMMAND_ORIGIN_REMOTE_CLIENT) return false;
  qa_actor_id actor;
  if (!SERVICES(s)->actor(SERVICES(s)->ctx, origin->client, &actor)) qa_range_error("Native component command client is no longer live");
  if (!admit(s, actor)) return true;
  command_call call = {s, actor};
  OPS(s)->with_command(OPS(s)->ctx, command, command_body, &call);
  return true;
}
qa_compat_q2_native_mod_client_slot *qa_compat_q2_native_mod_clients_checkpoint(binding *s, size_t *out_count) {
  for (size_t i = s->denied_count; i > 0; i--) { qa_client_id client; if (!SERVICES(s)->for_actor(SERVICES(s)->ctx, s->denied[i - 1], &client)) delete_denied(s, s->denied[i - 1]); }
  if (s->denied_count != 0) qa_range_error("Cannot save a native component before its rejected client disconnects");
  qa_compat_q2_native_mod_client_slot *rows = calloc(s->entry_count + 1, sizeof *rows); if (rows == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < s->entry_count; i++) { rows[i].actor = s->entries[i].actor; rows[i].slot = s->entries[i].slot; rows[i].admitted = s->entries[i].admitted; }
  *out_count = s->entry_count; return rows;
}
void qa_compat_q2_native_mod_clients_validate_restore(binding *s, const qa_compat_q2_native_mod_client_slot *entries, size_t count) {
  for (size_t i = 0; i < count; i++) if (!entry_live(s, entries[i].actor)) qa_range_error("Saved native component client is unavailable");
}
void qa_compat_q2_native_mod_clients_restore(binding *s, const qa_compat_q2_native_mod_client_slot *entries, size_t count) {
  qa_compat_q2_native_mod_clients_validate_restore(s, entries, count); s->entry_count = 0; s->denied_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_client_id client; if (!SERVICES(s)->for_actor(SERVICES(s)->ctx, entries[i].actor, &client)) qa_range_error("Saved native component client is unavailable");
    entry_t *entry = find_entry(s, entries[i].actor);
    if (entry == NULL) entry = qa_compat_q2_grow((void **)&s->entries, &s->entry_count, &s->entry_capacity, sizeof *entry);
    entry->actor = entries[i].actor; entry->client = client; entry->slot = entries[i].slot; entry->admitted = entries[i].admitted; entry->identity = ++s->next_identity;
  }
}
void qa_compat_q2_native_mod_clients_forget(binding *s, qa_actor_id actor) { delete_entry(s, actor); }
void qa_compat_q2_native_mod_clients_close(binding *s) {
  if (s->subscribed) { s->unsubscribe.invoke(s->unsubscribe.ctx); s->subscribed = false; }
  if (s->input_subscribed) { s->unsubscribe_input.invoke(s->unsubscribe_input.ctx); s->input_subscribed = false; }
  while (s->entry_count > 0) { size_t last = s->entry_count; qa_actor_id actor = s->entries[0].actor; OPS(s)->release(OPS(s)->ctx, actor); if (s->entry_count == last) delete_entry(s, actor); }
  s->entry_count = 0; s->denied_count = 0;
}
