/* Port of src/compat/q2/native-primary-drop.ts: original command guards, item
 * callbacks and accepted decrements own each drop. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_drop.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/foundation/js.h"

typedef enum { DEBIT_PENDING, DEBIT_ACCEPTED, DEBIT_REFUSED } debit_state;
typedef struct { qa_guest_address address; uint8_t bytes[8]; size_t length; } saved_field;
typedef struct {
  bool has_actor; qa_actor_id actor; bool named;
  bool has_row; qa_compat_q2_native_inventory_row row;
  bool has_counter; qa_guest_address counter;
  bool has_restore; saved_field saved[4]; size_t saved_count; qa_actor_id restore_actor;
  bool has_pickup; qa_actor_id pickup;
  debit_state debit;
} drop_frame;
typedef struct { qa_compat_q2_native_primary_drop *s; bool named; } command_entry;
struct qa_compat_q2_native_primary_drop {
  qa_compat_q2_native_primary_weapon_host host;
  const qa_compat_q2_native_primary_drop_profile *profile;
  qa_compat_q2_native_primary_commands *commands;
  qa_compat_q2_native_primary_drop_hooks hooks;
  drop_frame **frames; size_t frame_count, frame_capacity;
  command_entry entries[2];
  qa_compat_q2_native_entry *command_bindings[2], *find_binding, *allocate_binding;
  uint64_t admitted, consumer; uint64_t *callbacks, *debits;
  bool closed;
};
static const qa_guest_value_layout pointer_parameters[2] = {{.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "pointer"}};
static const qa_guest_value_layout pointer_layout = {.is_aggregate = false, .storage = "pointer"};

static qa_guest_address at(const qa_compat_q2_native_primary_drop *s, double offset) { return qa_compat_q2_mem_offset(s->host.memory, s->host.image, (int64_t)offset); }
static bool actor_of(const qa_compat_q2_native_primary_drop *s, const qa_guest_call_value *value, qa_actor_id *out) {
  return value != NULL && value->kind == QA_GUEST_VALUE_POINTER && value->pointer_value.has_value && s->host.actor(s->host.ctx, s->host.record(s->host.ctx, value->pointer_value.value), out);
}
static bool current(const qa_compat_q2_native_primary_drop *s, qa_actor_id actor) {
  if (s->closed) return false;
  qa_raw_entity_view record; qa_actor_id owner;
  return s->host.record_for(s->host.ctx, actor, &record) && s->host.actor(s->host.ctx, record, &owner) && qa_same_actor(owner, actor);
}
static drop_frame *top_frame(const qa_compat_q2_native_primary_drop *s) { return s->frame_count == 0 ? NULL : s->frames[s->frame_count - 1]; }
static void run_restore(const qa_compat_q2_native_primary_drop *s, drop_frame *frame) {
  if (!frame->has_restore) return;
  frame->has_restore = false; frame->has_counter = false;
  if (current(s, frame->restore_actor))
    for (size_t i = 0; i < frame->saved_count; i++) s->host.memory->write(s->host.memory->ctx, frame->saved[i].address, frame->saved[i].bytes, frame->saved[i].length);
}
static void save_field(qa_compat_q2_memory *memory, drop_frame *frame, qa_guest_address address, size_t length) {
  saved_field *field = &frame->saved[frame->saved_count++];
  field->address = address; field->length = length;
  memory->copy(memory->ctx, address, field->bytes, (double)length);
}
static bool source_weapon(const qa_compat_q2_native_primary_drop *s, qa_item_id item, qa_guest_address *out) {
  if (item == NULL) return false;
  qa_compat_q2_native_source_item value;
  if (!qa_compat_q2_native_primary_commands_source_item(s->commands, item, &value)) qa_range_error("Selected drop weapon has no source descriptor");
  *out = value.address;
  return true;
}
static qa_guest_address project(const qa_compat_q2_native_primary_drop *s, drop_frame *frame, const qa_compat_q2_native_inventory_row *row, bool cursor) {
  if (!frame->has_actor || frame->has_restore) qa_range_error("Original drop projection has no unique actor scope");
  qa_compat_q2_native_drop_projection projection = s->hooks.projection(s->hooks.ctx, frame->actor, row->item);
  qa_compat_q2_memory *memory = s->host.memory;
  qa_compat_q2_native_source_item descriptor; bool has_descriptor = qa_compat_q2_native_primary_commands_source_item(s->commands, projection.source, &descriptor);
  qa_raw_entity_view record; qa_guest_address client;
  bool has_client = s->host.record_for(s->host.ctx, frame->actor, &record) && qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record.address, (int64_t)s->profile->client_pointer), &client);
  if (!has_descriptor || !has_client) qa_range_error("Selected drop has no source item or client");
  qa_guest_address counter = qa_compat_q2_mem_offset(memory, client, (int64_t)(s->profile->client_inventory + descriptor.index * 4));
  qa_guest_address weapon = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->client_weapon);
  qa_guest_address pending = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->client_pending);
  qa_guest_address selection = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->client_cursor);
  frame->saved_count = 0;
  save_field(memory, frame, counter, 4);
  save_field(memory, frame, weapon, (size_t)memory->pointer_bytes);
  save_field(memory, frame, pending, (size_t)memory->pointer_bytes);
  if (cursor) save_field(memory, frame, selection, 4);
  frame->has_row = true; frame->row = *row;
  frame->has_counter = true; frame->counter = counter;
  frame->has_restore = true; frame->restore_actor = frame->actor;
  /* A fatal failure in the writes below aborts, so TS catch-restore cannot run. */
  qa_compat_q2_mem_write_i32(memory, counter, row->presence_only ? (row->count == 0 ? 0 : 1) : qa_js_to_int32(row->count));
  qa_guest_address value = {0};
  bool has = source_weapon(s, projection.current, &value);
  qa_compat_q2_mem_write_pointer(memory, weapon, has, value);
  has = source_weapon(s, projection.pending, &value);
  qa_compat_q2_mem_write_pointer(memory, pending, has, value);
  if (cursor) qa_compat_q2_mem_write_i32(memory, selection, qa_js_to_int32(descriptor.index));
  return descriptor.address;
}
typedef struct { qa_compat_q2_native_entry *binding; const qa_guest_call_value *values; size_t count; qa_guest_call_result result; } original_scope;
static void original_body(void *v) { original_scope *o = v; o->result = qa_compat_q2_native_entry_call_original(o->binding, o->values, o->count); }
static qa_guest_call_result command_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  command_entry *entry = v; qa_compat_q2_native_primary_drop *s = entry->s;
  drop_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.has_actor = actor_of(s, count > 0 ? &values[0] : NULL, &frame.actor);
  frame.named = entry->named; frame.debit = DEBIT_PENDING;
  if (s->frame_count == s->frame_capacity) {
    size_t capacity = s->frame_capacity ? s->frame_capacity * 2 : 4;
    drop_frame **grown = realloc(s->frames, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native drop out of memory");
    s->frames = grown; s->frame_capacity = capacity;
  }
  s->frames[s->frame_count++] = &frame;
  size_t depth = s->frame_count;
  original_scope scope = {binding, values, count, {0}};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(original_body, &scope, &thrown);
  run_restore(s, &frame);
  s->frame_count = depth - 1;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return scope.result;
}
static bool find_accepts(void *v) {
  qa_compat_q2_native_primary_drop *s = v; qa_compat_q2_memory *memory = s->host.memory;
  drop_frame *frame = top_frame(s);
  uint64_t raw = s->host.runner->read_register(s->host.runner->ctx, "rsp", memory->pointer_bytes == 4 ? 32 : 64);
  qa_guest_address_result stack = qa_compat_q2_mem_pointer(memory, (int64_t)raw); qa_guest_address returned;
  return frame != NULL && frame->named && stack.has_value && qa_compat_q2_mem_read_pointer(memory, stack.value, &returned) && returned.byte_offset == at(s, s->profile->lookup_return).byte_offset;
}
static char *ascii_lower(char *text) { for (char *c = text; *c != 0; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32); return text; }
static qa_guest_call_result find_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  qa_compat_q2_native_primary_drop *s = v;
  qa_guest_call_result result = qa_compat_q2_native_entry_call_original(binding, values, count);
  drop_frame *frame = top_frame(s);
  if (frame == NULL || !frame->named || !frame->has_actor || count == 0 || values[0].kind != QA_GUEST_VALUE_POINTER || !values[0].pointer_value.has_value || !current(s, frame->actor)) return result;
  char *text = ascii_lower(qa_compat_q2_read_classic_string(s->host.memory, true, values[0].pointer_value.value, 65536));
  const qa_compat_q2_native_inventory_row *rows = NULL; size_t row_count = 0;
  if (!s->hooks.rows(s->hooks.ctx, frame->actor, &rows, &row_count)) { rows = NULL; row_count = 0; }
  qa_source_item_named_entry *named = calloc(row_count ? row_count : 1, sizeof *named);
  size_t *matches = calloc(row_count ? row_count : 1, sizeof *matches);
  if (named == NULL || matches == NULL) qa_range_error("Native drop out of memory");
  for (size_t i = 0; i < row_count; i++) { named[i].item = rows[i].item; named[i].label = rows[i].label; }
  qa_item_id original = result.kind == QA_GUEST_VALUE_POINTER && result.pointer_value.has_value ? qa_compat_q2_native_primary_commands_item_at(s->commands, result.pointer_value.value) : NULL;
  qa_source_item_named_result choice = qa_source_item_named(named, row_count, text, original, matches, row_count);
  if (choice.kind == QA_SOURCE_ITEM_NAMED_AMBIGUOUS) {
    if (result.kind == QA_GUEST_VALUE_POINTER && !result.pointer_value.has_value) {
      size_t length = strlen(text) + 40;
      for (size_t i = 0; i < choice.match_count; i++) length += strlen(rows[matches[i]].item) + 2;
      char *message = malloc(length);
      if (message == NULL) qa_range_error("Native drop out of memory");
      size_t used = (size_t)snprintf(message, length, "Ambiguous item \"%s\"; use ", text);
      for (size_t i = 0; i < choice.match_count; i++) used += (size_t)snprintf(message + used, length - used, "%s%s", i == 0 ? "" : ", ", rows[matches[i]].item);
      snprintf(message + used, length - used, "\n");
      s->hooks.print(s->hooks.ctx, frame->actor, message);
      free(message);
    }
  } else if (choice.kind == QA_SOURCE_ITEM_NAMED_MATCH && rows[choice.item_index].selected) {
    result = qa_compat_q2_call_pointer(true, project(s, frame, &rows[choice.item_index], false));
  }
  free(named); free(matches); free(text);
  return result;
}
static void admitted_observed(void *v) {
  qa_compat_q2_native_primary_drop *s = v; drop_frame *frame = top_frame(s);
  if (frame == NULL || frame->named || !frame->has_actor || !current(s, frame->actor)) return;
  qa_item_id chosen = s->hooks.selected(s->hooks.ctx, frame->actor);
  const qa_compat_q2_native_inventory_row *rows = NULL; size_t count = 0;
  if (!s->hooks.rows(s->hooks.ctx, frame->actor, &rows, &count) || chosen == NULL) return;
  for (size_t i = 0; i < count; i++) if (strcmp(rows[i].item, chosen) == 0 && rows[i].selected) { project(s, frame, &rows[i], true); return; }
}
static qa_guest_call_result allocate_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  qa_compat_q2_native_primary_drop *s = v; drop_frame *frame = top_frame(s);
  qa_guest_call_result result = qa_compat_q2_native_entry_call_original(binding, values, count);
  qa_actor_id actor;
  if (frame != NULL && frame->has_counter && frame->has_actor && actor_of(s, count > 0 ? &values[0] : NULL, &actor) && qa_same_actor(actor, frame->actor)
      && result.kind == QA_GUEST_VALUE_POINTER && result.pointer_value.has_value)
    frame->has_pickup = s->host.actor(s->host.ctx, s->host.record(s->host.ctx, result.pointer_value.value), &frame->pickup);
  return result;
}
static void callback_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_drop *s = v; drop_frame *frame = top_frame(s);
  if (frame == NULL || !frame->has_row || !frame->has_actor || !current(s, frame->actor)) { continuation->execute(continuation->ctx); return; }
  qa_compat_q2_native_drop_action action = s->hooks.action(s->hooks.ctx, frame->actor, frame->row.item);
  if (action.invoke == NULL) { continuation->execute(continuation->ctx); return; }
  run_restore(s, frame);
  action.invoke(action.ctx);
  continuation->skip(continuation->ctx);
}
static void debit_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_drop *s = v; drop_frame *frame = top_frame(s);
  if (frame == NULL || !frame->has_counter || !frame->has_row || !frame->has_actor || !current(s, frame->actor)) { continuation->execute(continuation->ctx); return; }
  qa_compat_q2_memory *memory = s->host.memory;
  int32_t before = qa_compat_q2_mem_read_i32(memory, frame->counter);
  continuation->execute(continuation->ctx);
  int32_t after = qa_compat_q2_mem_read_i32(memory, frame->counter);
  double count = (double)before - (double)after;
  bool has_pickup = frame->has_pickup; qa_actor_id pickup = frame->pickup;
  run_restore(s, frame);
  if (count <= 0 || !has_pickup || !current(s, pickup)) qa_range_error("Accepted original drop lacks its source allocation or positive debit");
  frame->debit = s->hooks.dropped(s->hooks.ctx, frame->actor, pickup, frame->row.item, count) ? DEBIT_ACCEPTED : DEBIT_REFUSED;
  if (frame->debit == DEBIT_REFUSED && current(s, pickup)) {
    qa_raw_entity_view record;
    if (s->host.record_for(s->host.ctx, pickup, &record)) {
      qa_guest_call_value value = qa_compat_q2_call_pointer(true, record.address);
      qa_compat_q2_guest_call_signature signature = {s->profile->abi, pointer_parameters, 1, true, {0}, false};
      s->host.invoke(s->host.ctx, at(s, s->profile->free_entry), signature, &value, 1);
    }
  }
}
typedef struct { qa_compat_q2_inline_continuation *continuation; bool executed; } consume_scope;
static void consume_execute(void *v) { consume_scope *c = v; c->executed = true; c->continuation->execute(c->continuation->ctx); }
static void consumer_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_drop *s = v; drop_frame *frame = top_frame(s);
  if (frame != NULL && frame->debit == DEBIT_REFUSED) { continuation->skip(continuation->ctx); return; }
  if (frame == NULL || frame->debit != DEBIT_ACCEPTED || !frame->has_actor || !current(s, frame->actor)) { continuation->execute(continuation->ctx); return; }
  consume_scope scope = {continuation, false};
  s->hooks.consume(s->hooks.ctx, frame->actor, &scope, consume_execute);
  if (!scope.executed) continuation->skip(continuation->ctx);
}
qa_compat_q2_native_primary_drop *qa_compat_q2_native_primary_drop_create(qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_drop_profile *profile, qa_compat_q2_native_primary_commands *commands, qa_compat_q2_native_primary_drop_hooks hooks) {
  if (strcmp(host.memory->module.digest, profile->digest) != 0) qa_range_error("Native drop profile belongs to another artifact");
  qa_compat_q2_native_primary_drop *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Native drop out of memory");
  s->host = host; s->profile = profile; s->commands = commands; s->hooks = hooks;
  qa_compat_q2_runner *runner = host.runner;
  qa_compat_q2_guest_call_signature command = {profile->abi, pointer_parameters, 1, true, {0}, false};
  qa_compat_q2_guest_call_signature find = {profile->abi, pointer_parameters, 1, false, pointer_layout, false};
  qa_compat_q2_guest_call_signature allocate = {profile->abi, pointer_parameters, 2, false, pointer_layout, false};
  const char *module = host.memory->module.id; char id[320];
  s->entries[0] = (command_entry){s, true}; s->entries[1] = (command_entry){s, false};
  snprintf(id, sizeof id, "%s:selected-named-drop", module);
  s->command_bindings[0] = qa_compat_q2_native_entry_bind(host.memory, runner, host.ctx, host.invoke, at(s, profile->named), id, command, &s->entries[0], command_execute, NULL, NULL);
  snprintf(id, sizeof id, "%s:selected-inventory-drop", module);
  s->command_bindings[1] = qa_compat_q2_native_entry_bind(host.memory, runner, host.ctx, host.invoke, at(s, profile->inv_entry), id, command, &s->entries[1], command_execute, NULL, NULL);
  snprintf(id, sizeof id, "%s:selected-drop-item", module);
  s->find_binding = qa_compat_q2_native_entry_bind(host.memory, runner, host.ctx, host.invoke, at(s, profile->find), id, find, s, find_execute, s, find_accepts);
  s->admitted = runner->observe_entry(runner->ctx, at(s, profile->inv_admitted), s, admitted_observed);
  snprintf(id, sizeof id, "%s:selected-drop-allocation", module);
  s->allocate_binding = qa_compat_q2_native_entry_bind(host.memory, runner, host.ctx, host.invoke, at(s, profile->allocate), id, allocate, s, allocate_execute, NULL, NULL);
  s->callbacks = calloc(profile->callback_count ? profile->callback_count : 1, sizeof *s->callbacks);
  s->debits = calloc(profile->debit_count ? profile->debit_count : 1, sizeof *s->debits);
  if (s->callbacks == NULL || s->debits == NULL) qa_range_error("Native drop out of memory");
  for (size_t i = 0; i < profile->callback_count; i++)
    s->callbacks[i] = runner->bind_inline_region(runner->ctx, at(s, profile->callbacks[i].entry), at(s, profile->callbacks[i].join), profile->abi, s, callback_region);
  for (size_t i = 0; i < profile->debit_count; i++)
    s->debits[i] = runner->bind_inline_region(runner->ctx, at(s, profile->debits[i].entry), at(s, profile->debits[i].join), profile->abi, s, debit_region);
  if (profile->has_consumer) s->consumer = runner->bind_inline_region(runner->ctx, at(s, profile->consumer_entry), at(s, profile->consumer_join), profile->abi, s, consumer_region);
  return s;
}
void qa_compat_q2_native_primary_drop_close(qa_compat_q2_native_primary_drop *s) {
  if (s->closed) return;
  for (size_t i = s->frame_count; i-- > 0;) run_restore(s, s->frames[i]);
  s->closed = true;
  qa_compat_q2_runner *runner = s->host.runner;
  if (s->profile->has_consumer) runner->remove_inline_region(runner->ctx, s->consumer);
  for (size_t i = s->profile->debit_count; i-- > 0;) runner->remove_inline_region(runner->ctx, s->debits[i]);
  for (size_t i = s->profile->callback_count; i-- > 0;) runner->remove_inline_region(runner->ctx, s->callbacks[i]);
  qa_compat_q2_native_entry_close(s->allocate_binding);
  runner->remove_entry(runner->ctx, s->admitted);
  qa_compat_q2_native_entry_close(s->find_binding);
  qa_compat_q2_native_entry_close(s->command_bindings[1]);
  qa_compat_q2_native_entry_close(s->command_bindings[0]);
}
void qa_compat_q2_native_primary_drop_destroy(qa_compat_q2_native_primary_drop *s) {
  if (s == NULL) return;
  qa_compat_q2_native_primary_drop_close(s);
  free(s->frames); free(s->callbacks); free(s->debits); free(s);
}
