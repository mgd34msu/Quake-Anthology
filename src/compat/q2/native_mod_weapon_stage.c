/* Port of src/compat/q2/native-mod-weapon-stage.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_weapon_stage.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JS ToInt32: the TS `&`, `|` and `~` operands. */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  return (int32_t)(uint32_t)(int64_t)fmod(trunc(value), 4294967296.0);
}
typedef struct frame { qa_actor_id actor; bool committed_input, reached_decision; } frame;
typedef struct region_binding { struct qa_compat_q2_native_weapon_dispatcher *owner; const qa_native_weapon_decision *region; uint64_t handle; } region_binding;
struct qa_compat_q2_native_weapon_dispatcher {
  const qa_native_weapon_dispatcher *dispatcher;
  qa_compat_q2_native_weapon_host host;
  qa_compat_q2_native_weapon_operations operations;
  qa_native_abi abi;
  qa_guest_value_layout *parameters;
  qa_compat_q2_native_entry *binding;
  region_binding *regions; size_t region_count;
  frame *frames; size_t frame_count, frame_capacity;
};
typedef qa_compat_q2_native_weapon_dispatcher dispatcher_state;

static frame *push_frame(dispatcher_state *s) {
  if (s->frame_count == s->frame_capacity) {
    size_t next = s->frame_capacity == 0 ? 4 : s->frame_capacity * 2;
    frame *grown = realloc(s->frames, next * sizeof *grown); if (grown == NULL) qa_range_error("Out of memory");
    s->frames = grown; s->frame_capacity = next;
  }
  return &s->frames[s->frame_count++];
}
typedef struct original_call {
  dispatcher_state *s; qa_compat_q2_native_entry *binding; const qa_guest_call_value *values; size_t count;
  qa_guest_call_result result; qa_actor_id actor; size_t frame_index;
} original_call;
static void dispatch_body(void *value) {
  original_call *c = value; dispatcher_state *s = c->s;
  c->result = qa_compat_q2_native_entry_call_original(c->binding, c->values, c->count);
  if (s->operations.current(s->operations.ctx, c->actor))
    s->operations.completed(s->operations.ctx, c->actor, s->frames[c->frame_index].reached_decision);
}
static qa_guest_call_result dispatch(void *ctx, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  dispatcher_state *s = ctx;
  const qa_guest_call_value *argument = (size_t)s->dispatcher->argument < count ? &values[(size_t)s->dispatcher->argument] : NULL;
  if (argument == NULL || argument->kind != QA_GUEST_VALUE_POINTER || !argument->pointer_value.has_value)
    return qa_compat_q2_native_entry_call_original(binding, values, count);
  qa_actor_id actor;
  if (!s->operations.actor(s->operations.ctx, s->dispatcher->record, argument->pointer_value.value, &actor))
    return qa_compat_q2_native_entry_call_original(binding, values, count);
  bool committed = s->operations.committed_input != NULL && s->operations.committed_input(s->operations.ctx, actor);
  qa_compat_q2_native_weapon_cancellation cancellation;
  bool has_cancellation = s->operations.cancellation != NULL && s->operations.cancellation(s->operations.ctx, actor, &cancellation);
  const qa_compat_q2_runner *runner = s->host.runner;
  void *processor = has_cancellation ? runner->capture_processor(runner->ctx) : NULL;
  double depth = qa_compat_q2_native_depth(runner);
  size_t frame_index = s->frame_count;
  frame *slot = push_frame(s); slot->actor = actor; slot->committed_input = committed; slot->reached_decision = false;
  original_call call = {s, binding, values, count, {0}, actor, frame_index};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(dispatch_body, &call, &thrown);
  s->frame_count = frame_index; /* finally: dispatchers.pop() */
  if (!completed) {
    if (!has_cancellation || processor == NULL || !cancellation.accepts(cancellation.ctx, &thrown) || !cancellation.source_current(cancellation.ctx)) {
      if (processor != NULL && runner->free_processor != NULL) runner->free_processor(runner->ctx, processor);
      qa_compat_q2_native_rethrow(&thrown);
    }
    qa_compat_q2_native_unwind(runner, depth);
    runner->restore_processor(runner->ctx, processor);
    call.result = qa_compat_q2_call_void();
  }
  if (processor != NULL && runner->free_processor != NULL) runner->free_processor(runner->ctx, processor);
  return call.result;
}
typedef struct decision_call { qa_compat_q2_inline_continuation *continuation; } decision_call;
static void decision_body(void *value) { decision_call *c = value; c->continuation->execute(c->continuation->ctx); }
static void decision(void *ctx, qa_compat_q2_inline_continuation *continuation) {
  region_binding *binding = ctx; dispatcher_state *s = binding->owner; const qa_native_weapon_decision *region = binding->region;
  frame *top = s->frame_count == 0 ? NULL : &s->frames[s->frame_count - 1];
  if (top != NULL) top->reached_decision = true;
  if (top == NULL || top->committed_input || s->operations.selected(s->operations.ctx, top->actor)) { continuation->execute(continuation->ctx); return; }
  qa_actor_id actor = top->actor;
  if (!s->operations.current(s->operations.ctx, actor)) qa_range_error("Native weapon input outlived its source actor");
  double *originals = calloc(region->field_count == 0 ? 1 : region->field_count, sizeof *originals); if (originals == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < region->field_count; i++) originals[i] = s->operations.read(s->operations.ctx, actor, region->fields[i].field);
  for (size_t i = 0; i < region->field_count; i++)
    s->operations.write(s->operations.ctx, actor, region->fields[i].field, (double)(to_int32(originals[i]) & ~to_int32(region->fields[i].clear_mask)));
  decision_call call = {continuation};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(decision_body, &call, &thrown);
  /* Regions contain source reads, never source writes; preserve unrelated bits defensively. */
  for (size_t i = 0; i < region->field_count; i++) {
    int32_t mask = to_int32(region->fields[i].clear_mask);
    int32_t now = to_int32(s->operations.read(s->operations.ctx, actor, region->fields[i].field));
    s->operations.write(s->operations.ctx, actor, region->fields[i].field, (double)((now & ~mask) | (to_int32(originals[i]) & mask)));
  }
  free(originals);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
dispatcher_state *qa_compat_q2_native_weapon_dispatcher_create(const qa_native_weapon_dispatcher *dispatcher, const qa_native_weapon_decision *decisions, size_t decision_count,
    qa_native_abi abi, qa_compat_q2_native_weapon_host host, qa_provider_id provider, qa_compat_q2_native_weapon_operations operations) {
  dispatcher_state *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->dispatcher = dispatcher; s->host = host; s->operations = operations; s->abi = abi;
  qa_guest_address entry = !strcmp(dispatcher->entry.kind, "rva") ? qa_compat_q2_mem_offset(host.memory, host.image_base, (int64_t)dispatcher->entry.rva) : host.entry(host.ctx, dispatcher->entry.name);
  size_t arguments = (size_t)dispatcher->arguments;
  s->parameters = calloc(arguments == 0 ? 1 : arguments, sizeof *s->parameters); if (s->parameters == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < arguments; i++) { s->parameters[i].is_aggregate = false; s->parameters[i].storage = "pointer"; }
  qa_compat_q2_guest_call_signature signature; memset(&signature, 0, sizeof signature);
  signature.abi = abi; signature.parameters = s->parameters; signature.parameter_count = arguments; signature.result_is_void = true; signature.variadic = false;
  char id[256]; (void)snprintf(id, sizeof id, "%s:weapon-dispatcher", provider);
  s->binding = qa_compat_q2_native_entry_bind(host.memory, host.runner, host.ctx, host.invoke, entry, id, signature, s, dispatch, NULL, NULL);
  s->regions = calloc(decision_count == 0 ? 1 : decision_count, sizeof *s->regions); if (s->regions == NULL) qa_range_error("Out of memory");
  s->region_count = decision_count;
  for (size_t i = 0; i < decision_count; i++) {
    s->regions[i].owner = s; s->regions[i].region = &decisions[i];
    s->regions[i].handle = host.runner->bind_inline_region_accepts(host.runner->ctx, qa_compat_q2_mem_offset(host.memory, host.image_base, (int64_t)decisions[i].entry),
        qa_compat_q2_mem_offset(host.memory, host.image_base, (int64_t)decisions[i].join), abi, &s->regions[i], decision, NULL, NULL);
  }
  return s;
}
bool qa_compat_q2_native_weapon_dispatcher_current_actor(const dispatcher_state *s, qa_actor_id *out) {
  if (s->frame_count == 0) return false;
  *out = s->frames[s->frame_count - 1].actor; return true;
}
void qa_compat_q2_native_weapon_dispatcher_close(dispatcher_state *s) {
  /* removals.runReverse(): regions (last pushed first), then the entry. */
  for (size_t i = s->region_count; i > 0; i--) if (s->regions[i - 1].handle != 0) { s->host.runner->remove_inline_region(s->host.runner->ctx, s->regions[i - 1].handle); s->regions[i - 1].handle = 0; }
  if (s->binding != NULL) { qa_compat_q2_native_entry_close(s->binding); s->binding = NULL; }
  s->frame_count = 0;
}
void qa_compat_q2_native_weapon_dispatcher_destroy(dispatcher_state *s) {
  if (s == NULL) return;
  qa_compat_q2_native_weapon_dispatcher_close(s);
  free(s->frames); free(s->regions); free(s->parameters); free(s);
}

struct qa_compat_q2_native_mod_weapon_stage {
  dispatcher_state *dispatcher;
  const qa_native_weapon_stage *definition;
  qa_compat_q2_native_weapon_host host;
  qa_compat_q2_native_weapon_operations operations;
};
typedef qa_compat_q2_native_mod_weapon_stage stage;
static bool matches(stage *s, qa_actor_id actor, const qa_native_item_test *test) {
  const qa_compat_q2_native_weapon_operations *o = &s->operations;
  if (!strcmp(test->kind, "pointer")) {
    qa_guest_address current; bool has = qa_compat_q2_mem_read_pointer(s->host.memory, o->pointer(o->ctx, actor, test->pointer_field), &current);
    /* TS: current?.byteOffset === (value === null ? undefined : resolved). */
    if (test->pointer_value == NULL) return !has;
    return has && current.byte_offset == o->resolve(o->ctx, *test->pointer_value).byte_offset;
  }
  double raw = o->read(o->ctx, actor, test->field);
  double value = test->has_mask ? (double)(to_int32(raw) & to_int32(test->mask)) : raw;
  return !strcmp(test->comparison, "equals") ? value == test->scalar_value : value <= test->scalar_value;
}
static bool any_row(stage *s, qa_actor_id actor, const qa_native_item_test_row *rows, size_t count) {
  for (size_t i = 0; i < count; i++) {
    bool all = true;
    for (size_t j = 0; j < rows[i].count && all; j++) all = matches(s, actor, &rows[i].tests[j]);
    if (all) return true;
  }
  return false;
}
static bool stage_committed(void *ctx, qa_actor_id actor) {
  stage *s = ctx;
  return s->operations.current(s->operations.ctx, actor) && any_row(s, actor, s->definition->committed_input, s->definition->committed_input_row_count);
}
/* TS { ...operations, committedInput }: the dispatcher's table carries the stage
 * as ctx, so every leaf forwards to the caller's function with the caller's ctx. */
static bool forward_actor(void *ctx, const char *record, qa_guest_address address, qa_actor_id *out) { stage *s = ctx; return s->operations.actor(s->operations.ctx, record, address, out); }
static bool forward_current(void *ctx, qa_actor_id actor) { stage *s = ctx; return s->operations.current(s->operations.ctx, actor); }
static bool forward_selected(void *ctx, qa_actor_id actor) { stage *s = ctx; return s->operations.selected(s->operations.ctx, actor); }
static double forward_read(void *ctx, qa_actor_id actor, qa_native_item_field field) { stage *s = ctx; return s->operations.read(s->operations.ctx, actor, field); }
static void forward_write(void *ctx, qa_actor_id actor, qa_native_item_field field, double value) { stage *s = ctx; s->operations.write(s->operations.ctx, actor, field, value); }
static void forward_completed(void *ctx, qa_actor_id actor, bool reached) { stage *s = ctx; s->operations.completed(s->operations.ctx, actor, reached); }
static bool forward_cancellation(void *ctx, qa_actor_id actor, qa_compat_q2_native_weapon_cancellation *out) { stage *s = ctx; return s->operations.cancellation != NULL && s->operations.cancellation(s->operations.ctx, actor, out); }
stage *qa_compat_q2_native_mod_weapon_stage_create(const qa_native_weapon_stage *definition, qa_native_abi abi, qa_compat_q2_native_weapon_host host, qa_provider_id provider, qa_compat_q2_native_weapon_operations operations) {
  stage *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->definition = definition; s->host = host; s->operations = operations;
  qa_compat_q2_native_weapon_operations forwarded = operations;
  forwarded.ctx = s; forwarded.actor = forward_actor; forwarded.current = forward_current; forwarded.selected = forward_selected; forwarded.read = forward_read;
  forwarded.write = forward_write; forwarded.completed = forward_completed; forwarded.cancellation = forward_cancellation; forwarded.committed_input = stage_committed;
  s->dispatcher = qa_compat_q2_native_weapon_dispatcher_create(&definition->dispatcher, definition->decisions, definition->decision_count, abi, host, provider, forwarded);
  return s;
}
bool qa_compat_q2_native_mod_weapon_stage_continuing(stage *s, qa_actor_id actor) {
  return s->operations.current(s->operations.ctx, actor) && any_row(s, actor, s->definition->continuations, s->definition->continuation_row_count);
}
bool qa_compat_q2_native_mod_weapon_stage_settled(stage *s, qa_actor_id actor) {
  return s->operations.current(s->operations.ctx, actor) && any_row(s, actor, s->definition->settled, s->definition->settled_row_count);
}
static qa_item_id selected_item(stage *s, qa_actor_id actor, qa_native_item_pointer field) {
  const qa_compat_q2_native_weapon_operations *o = &s->operations;
  qa_guest_address pointer;
  if (!qa_compat_q2_mem_read_pointer(s->host.memory, o->pointer(o->ctx, actor, field), &pointer)) return NULL;
  const qa_native_weapon_selection *selection = &s->definition->selection;
  for (size_t i = 0; i < selection->value_count; i++)
    if (o->resolve(o->ctx, selection->values[i].address).byte_offset == pointer.byte_offset) return selection->values[i].item;
  qa_range_error("Original native selected an undeclared source weapon");
}
qa_item_id qa_compat_q2_native_mod_weapon_stage_active(stage *s, qa_actor_id actor) { return selected_item(s, actor, s->definition->selection.active); }
qa_item_id qa_compat_q2_native_mod_weapon_stage_pending(stage *s, qa_actor_id actor) {
  return s->definition->selection.pending == NULL ? NULL : selected_item(s, actor, *s->definition->selection.pending);
}
static bool same_item(qa_item_id a, qa_item_id b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
bool qa_compat_q2_native_mod_weapon_stage_request(stage *s, qa_actor_id actor, qa_item_id item) {
  const qa_native_weapon_selection *selection = &s->definition->selection; const qa_native_weapon_selection_value *value = NULL;
  for (size_t i = 0; i < selection->value_count && value == NULL; i++) if (same_item(selection->values[i].item, item)) value = &selection->values[i];
  const qa_compat_q2_native_weapon_operations *o = &s->operations;
  if (value == NULL || !o->current(o->ctx, actor)) return false;
  o->invoke(o->ctx, actor, &value->request);
  return o->current(o->ctx, actor) && (same_item(qa_compat_q2_native_mod_weapon_stage_active(s, actor), item) || same_item(qa_compat_q2_native_mod_weapon_stage_pending(s, actor), item));
}
void qa_compat_q2_native_mod_weapon_stage_close(stage *s) { qa_compat_q2_native_weapon_dispatcher_close(s->dispatcher); }
void qa_compat_q2_native_mod_weapon_stage_destroy(stage *s) {
  if (s == NULL) return;
  qa_compat_q2_native_weapon_dispatcher_destroy(s->dispatcher); free(s);
}
