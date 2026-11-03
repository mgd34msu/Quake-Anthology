/* Port of src/compat/q2/native-primary-inventory.ts: original scanners own
 * usability, category filters and accepted cursor side effects.
 * GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_inventory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/foundation/js.h"

typedef struct {
  bool has_actor; qa_actor_id actor; double flags; bool named;
  bool has_row; qa_compat_q2_native_inventory_row row;
  bool has_restore; qa_actor_id restore_actor; qa_guest_address restore_address; int32_t restore_count;
} inventory_frame;
typedef struct { qa_actor_id actor; qa_item_id item; double index; } selected_entry;
typedef struct { qa_compat_q2_native_primary_inventory *s; bool validate, named; } bind_ctx;
typedef struct { qa_compat_q2_native_primary_inventory *s; double entry, join; int direction; uint64_t handle; } scan_ctx;
struct qa_compat_q2_native_primary_inventory {
  qa_compat_q2_native_primary_weapon_host host;
  const qa_compat_q2_native_primary_inventory_profile *profile;
  qa_compat_q2_native_primary_inventory_hooks hooks;
  selected_entry *selected; size_t selected_count, selected_capacity;
  inventory_frame **frames; size_t frame_count, frame_capacity;
  int evaluating; bool closed;
  qa_compat_q2_native_entry *next, *previous, *validate, *use, *named_use;
  bind_ctx binds[5];
  uint64_t lookup_region, use_regions[2];
  scan_ctx scans[3]; size_t scan_count;
  qa_compat_q2_native_inventory_readout_item *readout; size_t readout_capacity;
};
static const qa_guest_value_layout next_parameters[3] = {{.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "int32"}, {.is_aggregate = false, .storage = "uint8"}};

static qa_compat_q2_memory *mem(const qa_compat_q2_native_primary_inventory *s) { return s->host.memory; }
static qa_guest_address at(const qa_compat_q2_native_primary_inventory *s, double offset) { return qa_compat_q2_mem_offset(mem(s), s->host.image, (int64_t)offset); }
static double source_count(const qa_compat_q2_native_inventory_row *row) { return row->presence_only ? (row->count == 0 ? 0 : 1) : row->count; }
static bool current(const qa_compat_q2_native_primary_inventory *s, qa_actor_id actor) {
  if (s->closed) return false;
  qa_raw_entity_view record; qa_actor_id owner;
  return s->host.record_for(s->host.ctx, actor, &record) && s->host.actor(s->host.ctx, record, &owner) && qa_same_actor(owner, actor);
}
static qa_guest_address client_of(const qa_compat_q2_native_primary_inventory *s, qa_actor_id actor) {
  qa_raw_entity_view record; qa_guest_address client;
  if (!s->host.record_for(s->host.ctx, actor, &record) || !qa_compat_q2_mem_read_pointer(mem(s), qa_compat_q2_mem_offset(mem(s), record.address, (int64_t)s->profile->client), &client)) qa_range_error("Native inventory actor has no source client");
  return client;
}
static qa_guest_address cursor_address(const qa_compat_q2_native_primary_inventory *s, qa_actor_id actor) { const qa_compat_q2_memory * m = mem(s); qa_guest_address a = client_of(s, actor); return qa_compat_q2_mem_offset(m, a, (int64_t)s->profile->cursor); }
static selected_entry *find_selected(const qa_compat_q2_native_primary_inventory *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->selected_count; i++) if (qa_same_actor(s->selected[i].actor, actor)) return &s->selected[i];
  return NULL;
}
static void delete_selected(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor) {
  selected_entry *found = find_selected(s, actor);
  if (found != NULL) *found = s->selected[--s->selected_count];
}
static void set_selected(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, qa_item_id item, double index) {
  selected_entry *found = find_selected(s, actor);
  if (found == NULL) {
    if (s->selected_count == s->selected_capacity) {
      size_t capacity = s->selected_capacity ? s->selected_capacity * 2 : 4;
      selected_entry *grown = realloc(s->selected, capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Native inventory out of memory");
      s->selected = grown; s->selected_capacity = capacity;
    }
    found = &s->selected[s->selected_count++];
  }
  found->actor = actor; found->item = item; found->index = index;
}
/* TS cursor(): null is NULL. */
static const qa_compat_q2_native_inventory_row *cursor_row(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, const qa_compat_q2_native_inventory_row *rows, size_t count) {
  qa_guest_address address = cursor_address(s, actor);
  int32_t index = qa_compat_q2_mem_read_i32(mem(s), address);
  selected_entry *selected = find_selected(s, actor);
  if (selected != NULL && selected->index == (double)index) {
    for (size_t i = 0; i < count; i++) if (strcmp(rows[i].item, selected->item) == 0) return &rows[i];
    delete_selected(s, actor);
    qa_compat_q2_mem_write_i32(mem(s), address, qa_js_to_int32(s->profile->empty));
    return NULL;
  }
  delete_selected(s, actor);
  for (size_t i = 0; i < count; i++) if (!rows[i].selected && rows[i].source_index == (double)index) return &rows[i];
  return NULL;
}
typedef struct { qa_compat_q2_native_primary_inventory *s; qa_guest_call_value values[3]; size_t value_count; bool forward; } evaluate_call;
static void evaluate_body(void *v) {
  evaluate_call *c = v;
  qa_compat_q2_native_entry_call_original(c->forward ? c->s->next : c->s->previous, c->values, c->value_count);
}
static bool evaluate(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, const qa_compat_q2_native_inventory_row *row, int direction, double flags) {
  qa_compat_q2_memory *memory = mem(s);
  qa_guest_address client = client_of(s, actor);
  qa_raw_entity_view record;
  bool has_record = s->host.record_for(s->host.ctx, actor, &record);
  if (!has_record || row->source_index != trunc(row->source_index) || row->source_index <= 0 || row->source_index >= s->profile->count) qa_range_error("Canonical inventory row has no valid original item slot");
  qa_guest_address inventory = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->inventory);
  size_t inventory_bytes = (size_t)s->profile->count * 4;
  uint8_t *saved_inventory = malloc(inventory_bytes), *zeroes = calloc(1, inventory_bytes);
  size_t write_count = s->profile->selection_write_count;
  uint8_t **saved_selection = calloc(write_count ? write_count : 1, sizeof *saved_selection);
  if (saved_inventory == NULL || zeroes == NULL || saved_selection == NULL) qa_range_error("Native inventory out of memory");
  memory->copy(memory->ctx, inventory, saved_inventory, (double)inventory_bytes);
  for (size_t i = 0; i < write_count; i++) {
    const qa_compat_q2_selection_write *field = &s->profile->selection_writes[i];
    saved_selection[i] = malloc((size_t)field->bytes);
    if (saved_selection[i] == NULL) qa_range_error("Native inventory out of memory");
    memory->copy(memory->ctx, qa_compat_q2_mem_offset(memory, client, (int64_t)field->offset), saved_selection[i], field->bytes);
  }
  bool accepted = false;
  s->evaluating++;
  memory->write(memory->ctx, inventory, zeroes, inventory_bytes);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_mem_offset(memory, inventory, (int64_t)(row->source_index * 4)), qa_js_to_int32(source_count(row)));
  qa_guest_address cursor = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->cursor);
  qa_compat_q2_mem_write_i32(memory, cursor, 0);
  evaluate_call call = {s, {qa_compat_q2_call_pointer(true, record.address), qa_compat_q2_call_int32(flags), {0}}, 2, direction > 0};
  if (direction > 0 && s->profile->next_menu_argument) { call.values[2] = qa_compat_q2_call_uint32(0); call.value_count = 3; }
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(evaluate_body, &call, &thrown);
  if (completed) accepted = qa_compat_q2_mem_read_i32(memory, cursor) == (int32_t)row->source_index;
  s->evaluating--;
  if (current(s, actor)) {
    memory->write(memory->ctx, inventory, saved_inventory, inventory_bytes);
    if (!accepted) for (size_t i = 0; i < write_count; i++) {
      const qa_compat_q2_selection_write *field = &s->profile->selection_writes[i];
      memory->write(memory->ctx, qa_compat_q2_mem_offset(memory, client, (int64_t)field->offset), saved_selection[i], (size_t)field->bytes);
    }
  }
  for (size_t i = 0; i < write_count; i++) free(saved_selection[i]);
  free(saved_selection); free(saved_inventory); free(zeroes);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return accepted;
}
typedef struct { double index; double count; } refused_pair;
static void navigate(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, const qa_compat_q2_native_inventory_row *rows, size_t count, int direction, double flags) {
  const qa_compat_q2_native_inventory_row *chosen = cursor_row(s, actor, rows, count);
  int64_t start = chosen == NULL ? (direction > 0 ? -1 : 0) : (int64_t)(chosen - rows);
  refused_pair *refused = calloc(count ? count : 1, sizeof *refused); size_t refused_count = 0;
  if (refused == NULL) qa_range_error("Native inventory out of memory");
  for (size_t step = 1; step <= count; step++) {
    int64_t n = (int64_t)count;
    /* JS % keeps the dividend's sign; an index past either end has no row. */
    int64_t position = (start + direction * (int64_t)step + n) % n;
    if (position < 0 || position >= n) qa_range_error("Native inventory traversal lost its row");
    const qa_compat_q2_native_inventory_row *row = &rows[position];
    double sourced = source_count(row);
    bool skip = row->count == 0;
    for (size_t i = 0; i < refused_count && !skip; i++) if (refused[i].index == row->source_index && refused[i].count == sourced) skip = true;
    if (skip) continue;
    if (evaluate(s, actor, row, direction, flags)) { set_selected(s, actor, row->item, row->source_index); free(refused); return; }
    refused[refused_count++] = (refused_pair){row->source_index, sourced};
  }
  free(refused);
  const qa_compat_q2_memory * m = mem(s);
  qa_guest_address a = cursor_address(s, actor);
  qa_compat_q2_mem_write_i32(m, a, qa_js_to_int32(s->profile->empty));
  delete_selected(s, actor);
}
static inventory_frame *top_frame(const qa_compat_q2_native_primary_inventory *s) { return s->frame_count == 0 ? NULL : s->frames[s->frame_count - 1]; }
static void run_restore(const qa_compat_q2_native_primary_inventory *s, inventory_frame *frame) {
  if (!frame->has_restore) return;
  frame->has_restore = false;
  if (current(s, frame->restore_actor)) qa_compat_q2_mem_write_i32(mem(s), frame->restore_address, frame->restore_count);
}
typedef struct { qa_compat_q2_native_primary_inventory *s; inventory_frame *frame; bool validate; const qa_guest_call_value *values; size_t count; qa_compat_q2_native_entry *binding; qa_guest_call_result result; } bound_call;
typedef struct { qa_compat_q2_native_entry *binding; const qa_guest_call_value *values; size_t count; qa_guest_call_result result; } original_scope;
static void original_body(void *v) { original_scope *o = v; o->result = qa_compat_q2_native_entry_call_original(o->binding, o->values, o->count); }
static void bound_body(void *v) {
  bound_call *c = v; qa_compat_q2_native_primary_inventory *s = c->s; inventory_frame *frame = c->frame;
  if (!c->validate || !frame->has_actor || s->evaluating != 0) { c->result = qa_compat_q2_native_entry_call_original(c->binding, c->values, c->count); return; }
  const qa_compat_q2_native_inventory_row *rows = NULL, *chosen = NULL; size_t row_count = 0;
  if (s->hooks.rows(s->hooks.ctx, frame->actor, &rows, &row_count)) chosen = cursor_row(s, frame->actor, rows, row_count);
  if (chosen == NULL || !chosen->selected) { c->result = qa_compat_q2_native_entry_call_original(c->binding, c->values, c->count); return; }
  qa_compat_q2_memory *memory = mem(s);
  qa_guest_address address = qa_compat_q2_mem_offset(memory, client_of(s, frame->actor), (int64_t)(s->profile->inventory + chosen->source_index * 4));
  int32_t count = qa_compat_q2_mem_read_i32(memory, address);
  qa_compat_q2_mem_write_i32(memory, address, qa_js_to_int32(source_count(chosen)));
  original_scope scope = {c->binding, c->values, c->count, {0}};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(original_body, &scope, &thrown);
  if (current(s, frame->actor)) qa_compat_q2_mem_write_i32(memory, address, count);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  c->result = scope.result;
}
static qa_guest_call_result bound_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  bind_ctx *b = v; qa_compat_q2_native_primary_inventory *s = b->s;
  inventory_frame frame;
  memset(&frame, 0, sizeof frame);
  if (count > 0 && values[0].kind == QA_GUEST_VALUE_POINTER && values[0].pointer_value.has_value)
    frame.has_actor = s->host.actor(s->host.ctx, s->host.record(s->host.ctx, values[0].pointer_value.value), &frame.actor);
  frame.flags = count > 1 && values[1].kind == QA_GUEST_VALUE_INT32 ? values[1].number_value : -1;
  frame.named = b->named;
  if (s->frame_count == s->frame_capacity) {
    size_t capacity = s->frame_capacity ? s->frame_capacity * 2 : 4;
    inventory_frame **grown = realloc(s->frames, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native inventory out of memory");
    s->frames = grown; s->frame_capacity = capacity;
  }
  s->frames[s->frame_count++] = &frame;
  size_t depth = s->frame_count;
  bound_call call = {s, &frame, b->validate, values, count, binding, {0}};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(bound_body, &call, &thrown);
  run_restore(s, &frame);
  s->frame_count = depth - 1;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return call.result;
}
static char *ascii_lower(char *text) { for (char *c = text; *c != 0; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32); return text; }
static void lookup_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_inventory *s = v; inventory_frame *frame = top_frame(s);
  qa_compat_q2_memory *memory = mem(s); qa_compat_q2_runner *runner = s->host.runner;
  if (frame == NULL || !frame->named || !frame->has_actor || !current(s, frame->actor)) { continuation->execute(continuation->ctx); return; }
  int width = memory->pointer_bytes == 4 ? 32 : 64;
  qa_guest_address name; bool has_name;
  if (memory->pointer_bytes == 4) {
    qa_guest_address_result stack = qa_compat_q2_mem_pointer(memory, (int64_t)runner->read_register(runner->ctx, "rsp", width));
    has_name = stack.has_value && qa_compat_q2_mem_read_pointer(memory, stack.value, &name);
  } else {
    qa_guest_address_result value = qa_compat_q2_mem_pointer(memory, (int64_t)runner->read_register(runner->ctx, "rcx", 64));
    has_name = value.has_value; name = value.value;
  }
  if (!has_name) qa_range_error("Original item lookup has no name argument");
  char *text = ascii_lower(qa_compat_q2_read_classic_string(memory, true, name, 65536));
  continuation->execute(continuation->ctx);
  uint64_t rax = runner->read_register(runner->ctx, "rax", width);
  qa_guest_address_result original = qa_compat_q2_mem_pointer(memory, (int64_t)rax);
  const qa_compat_q2_native_inventory_row *rows = NULL; size_t count = 0;
  if (!s->hooks.rows(s->hooks.ctx, frame->actor, &rows, &count)) { rows = NULL; count = 0; }
  qa_source_item_named_entry *named = calloc(count ? count : 1, sizeof *named);
  size_t *matches = calloc(count ? count : 1, sizeof *matches);
  if (named == NULL || matches == NULL) qa_range_error("Native inventory out of memory");
  for (size_t i = 0; i < count; i++) { named[i].item = rows[i].item; named[i].label = rows[i].label; }
  qa_source_item_named_result choice = qa_source_item_named(named, count, text, original.has_value ? s->hooks.item_at(s->hooks.ctx, original.value) : NULL, matches, count);
  if (choice.kind == QA_SOURCE_ITEM_NAMED_AMBIGUOUS) {
    if (rax == 0) {
      size_t length = strlen(text) + 40;
      for (size_t i = 0; i < choice.match_count; i++) length += strlen(rows[matches[i]].item) + 2;
      char *message = malloc(length);
      if (message == NULL) qa_range_error("Native inventory out of memory");
      size_t used = (size_t)snprintf(message, length, "Ambiguous item \"%s\"; use ", text);
      for (size_t i = 0; i < choice.match_count; i++) used += (size_t)snprintf(message + used, length - used, "%s%s", i == 0 ? "" : ", ", rows[matches[i]].item);
      snprintf(message + used, length - used, "\n");
      s->hooks.print(s->hooks.ctx, frame->actor, message);
      free(message);
    }
  } else if (choice.kind == QA_SOURCE_ITEM_NAMED_MATCH && rows[choice.item_index].selected) {
    const qa_compat_q2_native_inventory_row *row = &rows[choice.item_index];
    qa_guest_address address = qa_compat_q2_mem_offset(memory, client_of(s, frame->actor), (int64_t)(s->profile->inventory + row->source_index * 4));
    frame->has_row = true; frame->row = *row;
    frame->has_restore = true; frame->restore_actor = frame->actor; frame->restore_address = address; frame->restore_count = qa_compat_q2_mem_read_i32(memory, address);
    qa_compat_q2_mem_write_i32(memory, address, qa_js_to_int32(source_count(row)));
    runner->write_register(runner->ctx, "rax", width, (uint64_t)s->hooks.descriptor(s->hooks.ctx, row->source_index).byte_offset);
  }
  free(named); free(matches); free(text);
}
static bool scan_accepts(void *v) {
  scan_ctx *scan = v; qa_compat_q2_native_primary_inventory *s = scan->s; inventory_frame *frame = top_frame(s);
  const qa_compat_q2_native_inventory_row *rows; size_t count;
  return s->evaluating == 0 && frame != NULL && frame->has_actor && current(s, frame->actor) && s->hooks.rows(s->hooks.ctx, frame->actor, &rows, &count);
}
static void scan_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  scan_ctx *scan = v; qa_compat_q2_native_primary_inventory *s = scan->s; inventory_frame *frame = top_frame(s);
  if (frame == NULL || !frame->has_actor || !current(s, frame->actor)) qa_range_error("Admitted inventory scan lost its actor");
  const qa_compat_q2_native_inventory_row *rows; size_t count;
  if (!s->hooks.rows(s->hooks.ctx, frame->actor, &rows, &count)) qa_range_error("Admitted inventory scan lost its rows");
  navigate(s, frame->actor, rows, count, scan->direction, frame->flags);
  continuation->skip(continuation->ctx);
}
static void use_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_inventory *s = v; inventory_frame *frame = top_frame(s);
  if (frame == NULL || !frame->has_actor || !current(s, frame->actor)) { continuation->execute(continuation->ctx); return; }
  const qa_compat_q2_native_inventory_row *rows = NULL, *chosen = NULL; size_t count = 0;
  bool has_rows = s->hooks.rows(s->hooks.ctx, frame->actor, &rows, &count);
  if (frame->named) chosen = frame->has_row ? &frame->row : NULL;
  else if (has_rows) chosen = cursor_row(s, frame->actor, rows, count);
  if (chosen == NULL || !chosen->selected) { continuation->execute(continuation->ctx); return; }
  qa_item_id item = chosen->item;
  run_restore(s, frame);
  s->hooks.use(s->hooks.ctx, frame->actor, item);
  continuation->skip(continuation->ctx);
}
static qa_compat_q2_native_entry *bind(qa_compat_q2_native_primary_inventory *s, size_t slot, double entry, const char *name, size_t parameter_count, bool validate, bool named) {
  char id[320];
  snprintf(id, sizeof id, "%s:primary-inventory-%s", mem(s)->module.id, name);
  s->binds[slot] = (bind_ctx){s, validate, named};
  qa_compat_q2_guest_call_signature signature = {s->profile->abi, next_parameters, parameter_count, true, {0}, false};
  return qa_compat_q2_native_entry_bind(mem(s), s->host.runner, s->host.ctx, s->host.invoke, at(s, entry), id, signature, &s->binds[slot], bound_execute, NULL, NULL);
}
qa_compat_q2_native_primary_inventory *qa_compat_q2_native_primary_inventory_create(qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_inventory_profile *profile, qa_compat_q2_native_primary_inventory_hooks hooks) {
  if (strcmp(host.memory->module.digest, profile->digest) != 0) qa_range_error("Native inventory profile belongs to another artifact");
  qa_compat_q2_native_primary_inventory *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Native inventory out of memory");
  s->host = host; s->profile = profile; s->hooks = hooks;
  qa_compat_q2_runner *runner = host.runner;
  s->next = bind(s, 0, profile->next_entry, "next", profile->next_menu_argument ? 3 : 2, false, false);
  s->previous = bind(s, 1, profile->prev_entry, "previous", 2, false, false);
  s->validate = bind(s, 2, profile->validate_entry, "validate", 1, true, false);
  s->use = bind(s, 3, profile->use_entry, "use", 1, false, false);
  s->named_use = bind(s, 4, profile->named_entry, "named-use", 1, false, true);
  s->lookup_region = runner->bind_inline_region(runner->ctx, at(s, profile->named_lookup_call), at(s, profile->named_lookup_return), profile->abi, s, lookup_region);
  s->scans[s->scan_count++] = (scan_ctx){s, profile->next_scan, profile->next_join, 1, 0};
  s->scans[s->scan_count++] = (scan_ctx){s, profile->prev_scan, profile->prev_join, -1, 0};
  if (profile->has_validate_scan) s->scans[s->scan_count++] = (scan_ctx){s, profile->validate_scan_entry, profile->validate_scan_join, 1, 0};
  for (size_t i = 0; i < s->scan_count; i++)
    s->scans[i].handle = runner->bind_inline_region_accepts(runner->ctx, at(s, s->scans[i].entry), at(s, s->scans[i].join), profile->abi, &s->scans[i], scan_region, &s->scans[i], scan_accepts);
  s->use_regions[0] = runner->bind_inline_region(runner->ctx, at(s, profile->use_call), at(s, profile->use_join), profile->abi, s, use_region);
  s->use_regions[1] = runner->bind_inline_region(runner->ctx, at(s, profile->named_call), at(s, profile->named_join), profile->abi, s, use_region);
  return s;
}
bool qa_compat_q2_native_primary_inventory_read(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, qa_compat_q2_native_inventory_readout *out) {
  if (!current(s, actor)) return false;
  const qa_compat_q2_native_inventory_row *rows; size_t count;
  if (!s->hooks.rows(s->hooks.ctx, actor, &rows, &count)) {
    selected_entry *tracked = find_selected(s, actor);
    if (tracked != NULL) {
      double index = tracked->index;
      delete_selected(s, actor);
      qa_guest_address cursor = cursor_address(s, actor);
      if ((double)qa_compat_q2_mem_read_i32(mem(s), cursor) == index) qa_compat_q2_mem_write_i32(mem(s), cursor, qa_js_to_int32(s->profile->empty));
    }
    return false;
  }
  const qa_compat_q2_native_inventory_row *chosen = cursor_row(s, actor, rows, count);
  if (count > s->readout_capacity) {
    qa_compat_q2_native_inventory_readout_item *grown = realloc(s->readout, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Native inventory out of memory");
    s->readout = grown; s->readout_capacity = count;
  }
  size_t used = 0;
  for (size_t i = 0; i < count; i++) if (rows[i].count != 0) s->readout[used++] = (qa_compat_q2_native_inventory_readout_item){rows[i].item, rows[i].label, rows[i].count};
  memset(out, 0, sizeof *out);
  out->items = s->readout; out->item_count = used; out->selected = chosen != NULL ? chosen->item : NULL;
  if (chosen != NULL && chosen->has_presentation) { out->has_presentation = true; out->presentation = chosen->presentation; }
  return true;
}
void qa_compat_q2_native_primary_inventory_restore(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, qa_item_id item) {
  delete_selected(s, actor);
  if (item == NULL) { const qa_compat_q2_memory * m2 = mem(s); qa_guest_address a2 = cursor_address(s, actor); qa_compat_q2_mem_write_i32(m2, a2, qa_js_to_int32(s->profile->empty)); return; }
  const qa_compat_q2_native_inventory_row *rows = NULL, *row = NULL; size_t count = 0;
  if (s->hooks.rows(s->hooks.ctx, actor, &rows, &count))
    for (size_t i = 0; i < count && row == NULL; i++) if (strcmp(rows[i].item, item) == 0) row = &rows[i];
  if (row == NULL) qa_range_error("Saved native inventory cursor is absent from its canonical rows");
  const qa_compat_q2_memory * m = mem(s);
  qa_guest_address a = cursor_address(s, actor);
  qa_compat_q2_mem_write_i32(m, a, qa_js_to_int32(row->source_index));
  set_selected(s, actor, item, row->source_index);
}
void qa_compat_q2_native_primary_inventory_release(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor) { delete_selected(s, actor); }
void qa_compat_q2_native_primary_inventory_close(qa_compat_q2_native_primary_inventory *s) {
  if (s->closed) return;
  s->closed = true;
  qa_compat_q2_runner *runner = s->host.runner;
  runner->remove_inline_region(runner->ctx, s->use_regions[1]);
  runner->remove_inline_region(runner->ctx, s->use_regions[0]);
  for (size_t i = s->scan_count; i-- > 0;) runner->remove_inline_region(runner->ctx, s->scans[i].handle);
  runner->remove_inline_region(runner->ctx, s->lookup_region);
  qa_compat_q2_native_entry_close(s->named_use);
  qa_compat_q2_native_entry_close(s->use);
  qa_compat_q2_native_entry_close(s->validate);
  qa_compat_q2_native_entry_close(s->previous);
  qa_compat_q2_native_entry_close(s->next);
  s->selected_count = 0;
}
void qa_compat_q2_native_primary_inventory_destroy(qa_compat_q2_native_primary_inventory *s) {
  if (s == NULL) return;
  qa_compat_q2_native_primary_inventory_close(s);
  free(s->selected); free(s->frames); free(s->readout); free(s);
}
