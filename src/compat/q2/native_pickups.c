/* Port of src/compat/q2/native-pickups.ts: the complete original caller owns
 * eligibility, feedback, targets and item lifetime. Each region leaves the
 * original stack, saved registers and map continuation intact.
 * GPL-2.0-or-later. */
#include "qa/compat/q2/native_pickups.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"

typedef qa_compat_q2_native_primary_pickups pickups;
typedef qa_compat_q2_native_pickup_grant grant_t;
typedef struct {
  qa_raw_entity_view pickup, recipient;
  qa_original_pickup_offer offer;
  qa_pickup_resource default_resource;
  char *item; /* owned q2:<classname> */
  qa_source_pickup_selection selection;
  const grant_t *grant;
  qa_guest_address descriptor;
  bool invalid, called;
  int projections;
  bool has_supply;
  qa_compat_q2_native_pickup_supply_evaluation supply;
} pickup_frame;
typedef struct { pickups *s; const grant_t *grant; } grant_ctx;
typedef struct {
  qa_compat_q2_native_entry *touch;
  qa_compat_q2_native_entry **grants;
  grant_ctx *grant_ctxs;
  uint64_t targets;
} removals_t;
#define CANCELLED_MESSAGE "Native pickup caller was cancelled"
#define DECLINED_MESSAGE "Native supply was declined"
#define STOP_MESSAGE "Native ammo grant reached its stop"

static qa_compat_q2_memory *mem(const pickups *s) { return s->host.memory; }
static qa_guest_address at(const pickups *s, double offset) { return qa_compat_q2_mem_offset(mem(s), s->image, (int64_t)offset); }
static qa_guest_address field(const pickups *s, qa_raw_entity_view record, double offset) { return qa_compat_q2_mem_offset(mem(s), record.address, (int64_t)offset); }
static qa_guest_address argument(const qa_guest_call_value *values, size_t count, size_t index) {
  if (index >= count || values[index].kind != QA_GUEST_VALUE_POINTER || !values[index].pointer_value.has_value) qa_range_error("Native item callback requires its original record arguments");
  return values[index].pointer_value.value;
}
static pickup_frame *top_frame(const pickups *s) { return s->frame_count == 0 ? NULL : s->frames[s->frame_count - 1]; }
static void push_frame(pickups *s, pickup_frame *frame) {
  if (s->frame_count == s->frame_capacity) {
    size_t capacity = s->frame_capacity ? s->frame_capacity * 2 : 4;
    void **grown = realloc(s->frames, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native pickups out of memory");
    s->frames = grown; s->frame_capacity = capacity;
  }
  s->frames[s->frame_count++] = frame;
}
static bool in_table(const pickups *s, qa_guest_address descriptor, int64_t *index) {
  const qa_compat_q2_native_pickup_profile *p = s->profile;
  int64_t difference = descriptor.byte_offset - at(s, p->items_table).byte_offset, stride = (int64_t)p->items_stride;
  if (difference < 0 || difference % stride != 0 || difference / stride >= (int64_t)p->items_count) return false;
  *index = difference / stride;
  return true;
}
static int64_t item_index(const pickups *s, qa_guest_address descriptor) {
  int64_t index;
  if (!in_table(s, descriptor, &index)) qa_range_error("Native pickup descriptor is outside its original item table");
  return index;
}
/* TS `q2:${classname}` with its 256-byte terminator bound. Malloc'd. */
static char *read_item_name(const pickups *s, qa_guest_address name) {
  qa_compat_q2_memory *memory = mem(s);
  char *out = malloc(256 + 3);
  if (out == NULL) qa_range_error("Native pickups out of memory");
  memcpy(out, "q2:", 3);
  for (int index = 0; index < 256; index++) {
    uint8_t byte = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, name, index));
    out[3 + index] = (char)byte;
    if (byte == 0) return out;
  }
  qa_range_error("Native item classname has no terminator");
}
static char *item_name(const pickups *s, qa_guest_address descriptor) {
  item_index(s, descriptor);
  qa_guest_address name;
  if (!qa_compat_q2_mem_read_pointer(mem(s), qa_compat_q2_mem_offset(mem(s), descriptor, (int64_t)s->profile->items_classname), &name)) qa_range_error("Native pickup descriptor has no classname");
  return read_item_name(s, name);
}
static qa_guest_address client_of(const pickups *s, const pickup_frame *frame) {
  qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(mem(s), field(s, frame->recipient, s->profile->supply_client), &client)) qa_range_error("Native pickup recipient has no original client");
  return client;
}
static qa_guest_address counter_of(const pickups *s, const pickup_frame *frame, qa_guest_address descriptor) {
  const qa_compat_q2_memory * m = mem(s);
  qa_guest_address a = client_of(s, frame);
  int64_t d = (int64_t)s->profile->supply_inventory + item_index(s, descriptor) * 4;
  return qa_compat_q2_mem_offset(m, a, d);
}
static bool live(const pickups *s, qa_raw_entity_view record) {
  qa_guest_address address = field(s, record, s->profile->entity_inuse);
  return (s->profile->entity_inuse_bytes == 1 ? (int32_t)qa_compat_q2_mem_read_u8(mem(s), address) : qa_compat_q2_mem_read_i32(mem(s), address)) != 0;
}
static bool still(const pickups *s, qa_raw_entity_view record, qa_actor_id expected) {
  qa_actor_id actor;
  return s->host.current(s->host.ctx, record, &actor) && qa_same_actor(actor, expected);
}
static bool frame_current(const pickups *s, const pickup_frame *frame) {
  if (s->closed || frame->invalid || !live(s, frame->pickup) || !live(s, frame->recipient)) return false;
  if (!still(s, frame->pickup, frame->offer.pickup) || !still(s, frame->recipient, frame->offer.recipient)) return false;
  qa_guest_address item;
  if (!qa_compat_q2_mem_read_pointer(mem(s), field(s, frame->pickup, s->profile->entity_item), &item) || item.byte_offset != frame->descriptor.byte_offset) return false;
  return frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT || frame->selection.current(frame->selection.ctx);
}
static void require_current(const pickups *s, const pickup_frame *frame) {
  if (!frame_current(s, frame)) qa_compat_q2_native_throw(frame, CANCELLED_MESSAGE);
}
static bool grant_caller(void *v) {
  grant_ctx *g = v; const pickups *s = g->s; const pickup_frame *frame = top_frame(s);
  if (frame == NULL || frame->grant != g->grant || frame->selection.kind == QA_SOURCE_PICKUP_ORIGINAL) return false;
  qa_compat_q2_memory *memory = mem(s);
  qa_guest_address_result stack = qa_compat_q2_mem_pointer(memory, (int64_t)s->host.runner->read_register(s->host.runner->ctx, "rsp", memory->pointer_bytes == 4 ? 32 : 64));
  qa_guest_address returned;
  return stack.has_value && qa_compat_q2_mem_read_pointer(memory, stack.value, &returned) && returned.byte_offset == at(s, s->profile->grant_return).byte_offset;
}
typedef enum { DESCRIBE_NONE, DESCRIBE_STALE, DESCRIBE_FRAME } describe_result;
static describe_result describe(const pickups *s, const qa_guest_call_value *values, size_t count, pickup_frame *out) {
  const qa_compat_q2_native_pickup_profile *p = s->profile; qa_compat_q2_memory *memory = mem(s);
  qa_raw_entity_view pickup = s->host.record(s->host.ctx, argument(values, count, 0)), recipient = s->host.record(s->host.ctx, argument(values, count, 1));
  qa_guest_address descriptor;
  if (!qa_compat_q2_mem_read_pointer(memory, field(s, pickup, p->entity_item), &descriptor)) return DESCRIBE_NONE;
  int64_t index;
  if (!in_table(s, descriptor, &index)) return DESCRIBE_NONE;
  qa_guest_address entry; bool has_entry = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, descriptor, (int64_t)p->items_pickup), &entry);
  const grant_t *grant = NULL;
  for (size_t i = 0; i < p->grant_count && grant == NULL; i++) if (has_entry && entry.byte_offset == at(s, p->grants[i].entry).byte_offset) grant = &p->grants[i];
  if (grant == NULL) return DESCRIBE_NONE;
  qa_actor_id pickup_actor, recipient_actor;
  bool has_pickup = s->host.current(s->host.ctx, pickup, &pickup_actor), has_recipient = s->host.current(s->host.ctx, recipient, &recipient_actor);
  if (!has_pickup || !has_recipient || !live(s, pickup) || !live(s, recipient)) return DESCRIBE_STALE;
  qa_guest_address name;
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, descriptor, (int64_t)p->items_classname), &name)) return DESCRIBE_NONE;
  memset(out, 0, sizeof *out);
  out->item = read_item_name(s, name);
  int32_t amount = qa_compat_q2_mem_read_i32(memory, field(s, pickup, p->entity_count));
  qa_source_time time;
  if (strcmp(p->time_storage, "float32-seconds") == 0) { time.kind = QA_SOURCE_TIME_SECONDS; time.value = qa_compat_q2_mem_read_f32(memory, at(s, p->time_address)); }
  else { time.kind = QA_SOURCE_TIME_MILLISECONDS; time.value = (double)qa_compat_q2_mem_read_i64(memory, at(s, p->time_address)); }
  if (!isfinite(time.value) || fabs(trunc(time.value)) > (double)QA_MAX_SAFE_INTEGER) qa_range_error("Native pickup clock cannot be represented");
  out->pickup = pickup; out->recipient = recipient; out->descriptor = descriptor; out->grant = grant;
  if (strcmp(grant->resource, "regular") == 0) { out->default_resource.kind = QA_PICKUP_RESOURCE_PROTECTION; out->default_resource.channel = QA_PROTECTION_REGULAR; }
  else { out->default_resource.kind = QA_PICKUP_RESOURCE_INVENTORY; out->default_resource.item = out->item; }
  out->offer.recipient = recipient_actor; out->offer.pickup = pickup_actor; out->offer.source = memory->module.id; out->offer.item = out->item;
  out->offer.default_resource = &out->default_resource;
  out->offer.count.is_override = amount != 0; out->offer.count.amount = amount;
  out->offer.dropped = (qa_compat_q2_mem_read_u32(memory, field(s, pickup, p->entity_spawnflags)) & 0x30000) != 0;
  out->offer.time = time;
  return DESCRIBE_FRAME;
}
/* --- touch --- */
typedef struct { pickups *s; pickup_frame *frame; qa_raw_entity_view record; bool has_generation; qa_guest_address generation; int32_t captured; } record_observer;
static void inuse_written(void *v) { record_observer *o = v; if (!live(o->s, o->record)) o->frame->invalid = true; }
static void generation_written(void *v) { record_observer *o = v; if (qa_compat_q2_mem_read_i32(mem(o->s), o->generation) != o->captured) o->frame->invalid = true; }
typedef struct { pickups *s; pickup_frame *frame; qa_compat_q2_native_entry *binding; const qa_guest_call_value *values; size_t count; qa_guest_call_result result; } touch_call;
static void touch_body(void *v) { touch_call *c = v; c->result = qa_compat_q2_native_entry_call_original(c->binding, c->values, c->count); }
static void *touch_selected(void *v, qa_source_pickup_selection selection, qa_source_pickup_lifetime lifetime) {
  touch_call *c = v; pickups *s = c->s; pickup_frame *frame = c->frame; (void)lifetime;
  if (selection.kind == QA_SOURCE_PICKUP_STALE) { c->result = qa_compat_q2_call_void(); return &c->result; }
  frame->selection = selection;
  qa_compat_q2_memory *memory = mem(s); qa_compat_q2_runner *runner = s->host.runner;
  void *processor = runner->capture_processor(runner->ctx);
  double depth = qa_compat_q2_native_depth(runner);
  size_t frame_depth = s->frame_count;
  push_frame(s, frame);
  record_observer observers[2]; uint64_t handles[4]; size_t handle_count = 0;
  if (selection.kind != QA_SOURCE_PICKUP_ORIGINAL) {
    const qa_raw_entity_view records[2] = {frame->pickup, frame->recipient};
    for (size_t i = 0; i < 2; i++) {
      observers[i] = (record_observer){s, frame, records[i], false, {0}, 0};
      handles[handle_count++] = memory->observe_writes(memory->ctx, field(s, records[i], s->profile->entity_inuse), s->profile->entity_inuse_bytes, &observers[i], inuse_written);
      if (s->profile->has_entity_generation) {
        observers[i].has_generation = true;
        observers[i].generation = field(s, records[i], s->profile->entity_generation);
        observers[i].captured = qa_compat_q2_mem_read_i32(memory, observers[i].generation);
        handles[handle_count++] = memory->observe_writes(memory->ctx, observers[i].generation, 4, &observers[i], generation_written);
      }
    }
  }
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(touch_body, c, &thrown), handled = false;
  if (!completed && thrown.token == frame) {
    qa_compat_q2_native_unwind(runner, depth);
    runner->restore_processor(runner->ctx, processor);
    c->result = qa_compat_q2_call_void();
    handled = true;
  }
  for (size_t i = handle_count; i-- > 0;) memory->unobserve_writes(memory->ctx, handles[i]);
  s->frame_count = frame_depth;
  if (runner->free_processor != NULL) runner->free_processor(runner->ctx, processor);
  if (!completed && !handled) qa_compat_q2_native_rethrow(&thrown);
  return &c->result;
}
typedef struct { const qa_original_pickup_admission *admission; touch_call *call; void *returned; } admission_call;
static void admission_body(void *v) {
  admission_call *a = v;
  a->returned = a->admission->run_source(a->admission->ctx, a->call->frame->offer, touch_selected, a->call);
}
static qa_guest_call_result touch_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  pickups *s = v;
  pickup_frame frame;
  describe_result description = describe(s, values, count, &frame);
  if (description == DESCRIBE_STALE) return qa_compat_q2_call_void();
  touch_call call = {s, &frame, binding, values, count, {0}};
  qa_compat_q2_native_thrown thrown;
  if (description == DESCRIBE_NONE) {
    size_t depth = s->frame_count;
    push_frame(s, NULL);
    bool completed = qa_compat_q2_native_try(touch_body, &call, &thrown);
    s->frame_count = depth;
    if (!completed) qa_compat_q2_native_rethrow(&thrown);
    return call.result;
  }
  admission_call admission = {s->host.admission(s->host.ctx), &call, NULL};
  bool completed = qa_compat_q2_native_try(admission_body, &admission, &thrown);
  free(frame.item);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  if (admission.returned == NULL) qa_range_error("Native synchronous pickup caller yielded during admission");
  return call.result;
}
static void targets_observed(void *v) {
  pickups *s = v; const pickup_frame *frame = top_frame(s);
  if (frame != NULL && frame->selection.kind != QA_SOURCE_PICKUP_ORIGINAL) require_current(s, frame);
}
/* --- grantResources --- */
typedef struct { void *consume_ctx; qa_compat_q2_native_pickup_consume_fn consume; const qa_compat_q2_pickup_consumer *consumer; } publish_call;
typedef struct {
  pickups *s; pickup_frame *frame;
  bool (*consume)(void *ctx); void *consume_ctx;
  publish_call *calls; size_t call_count;
} resources_scope;
static qa_original_pickup_outcome resources(resources_scope *r, size_t index);
typedef struct { resources_scope *r; size_t index; qa_original_pickup_outcome outcome; } operation_ctx;
static void protect_current(void *v) { operation_ctx *o = v; require_current(o->r->s, o->r->frame); }
static bool protect_committed(void *v) { operation_ctx *o = v; return o->r->frame->projections == 0; }
static void resources_body(void *v) { operation_ctx *o = v; o->outcome = resources(o->r, o->index + 1); }
static void *protect_operation(void *v, void *consume_ctx, qa_compat_q2_native_pickup_consume_fn consume) {
  operation_ctx *o = v; resources_scope *r = o->r;
  size_t depth = r->call_count;
  r->calls[r->call_count++] = (publish_call){consume_ctx, consume, &r->frame->grant->consumers[o->index]};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(resources_body, o, &thrown);
  r->call_count = depth;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return &o->outcome;
}
typedef struct { pickups *s; const pickup_frame *frame; const qa_compat_q2_pickup_consumer *consumer; } consumer_call;
static void consumer_execute(void *v) {
  consumer_call *c = v;
  qa_guest_call_value value = qa_compat_q2_call_pointer(true, c->frame->recipient.address);
  c->s->host.invoke(c->s->host.ctx, at(c->s, c->consumer->entry), c->consumer->signature, &value, 1);
}
static qa_original_pickup_outcome resources(resources_scope *r, size_t index) {
  pickups *s = r->s; pickup_frame *frame = r->frame;
  if (index >= frame->grant->consumer_count) {
    if (frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT) qa_range_error("Native pickup resource owner changed");
    qa_original_pickup_outcome decision = frame->selection.grant(frame->selection.ctx);
    require_current(s, frame);
    if (decision == QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED && (r->consume == NULL || r->consume(r->consume_ctx))) {
      for (size_t i = 0; i < r->call_count; i++) {
        consumer_call call = {s, frame, r->calls[i].consumer};
        r->calls[i].consume(r->calls[i].consume_ctx, &call, consumer_execute);
        require_current(s, frame);
      }
    }
    return decision;
  }
  if (s->host.with_protection == NULL) qa_range_error("Native pickup protection binding disappeared");
  operation_ctx operation = {r, index, QA_ORIGINAL_PICKUP_OUTCOME_REFUSED};
  s->host.with_protection(s->host.ctx, frame->recipient, frame->grant->consumers[index].protection, &operation, protect_current, protect_operation, protect_committed);
  return operation.outcome;
}
typedef struct { resources_scope scope; qa_original_pickup_outcome outcome; } resources_call;
static void resources_call_body(void *v) { resources_call *c = v; c->outcome = resources(&c->scope, 0); }
static qa_original_pickup_outcome grant_resources(pickups *s, pickup_frame *frame, bool (*consume)(void *), void *consume_ctx) {
  publish_call *calls = calloc(frame->grant->consumer_count ? frame->grant->consumer_count : 1, sizeof *calls);
  if (calls == NULL) qa_range_error("Native pickups out of memory");
  resources_call call = {{s, frame, consume, consume_ctx, calls, 0}, QA_ORIGINAL_PICKUP_OUTCOME_REFUSED};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(resources_call_body, &call, &thrown);
  free(calls);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return call.outcome;
}
/* --- quantity --- */
typedef struct {
  pickups *s; pickup_frame *frame; qa_guest_address descriptor; double amount;
  bool accepted_ammo;
} supply_quantity;
static void ammo_stop_observed(void *v) { qa_compat_q2_native_throw(v, STOP_MESSAGE); }
typedef struct { pickups *s; const pickup_frame *frame; qa_guest_address descriptor; double amount; bool accepted; } ammo_invoke;
static void ammo_invoke_body(void *v) {
  ammo_invoke *c = v; const qa_compat_q2_native_pickup_profile *p = c->s->profile;
  qa_guest_call_value values[3] = {qa_compat_q2_call_pointer(true, c->frame->recipient.address), qa_compat_q2_call_pointer(true, c->descriptor), qa_compat_q2_call_int32(c->amount)};
  qa_guest_call_result result = c->s->host.invoke(c->s->host.ctx, at(c->s, p->ammo_entry), p->ammo_signature, values, 3);
  if (result.kind != QA_GUEST_VALUE_INT32 && result.kind != QA_GUEST_VALUE_UINT32) qa_range_error("Original ammo grant returned a non-boolean ABI value");
  c->accepted = result.number_value != 0;
}
static qa_source_pickup_quantity quantity(void *v, double count, double capacity) {
  supply_quantity *q = v; pickups *s = q->s; pickup_frame *frame = q->frame;
  const qa_compat_q2_native_pickup_profile *p = s->profile; qa_compat_q2_memory *memory = mem(s); qa_compat_q2_runner *runner = s->host.runner;
  require_current(s, frame);
  if (top_frame(s) != frame || !frame->has_supply) qa_range_error("Native pickup quantity has expired");
  int32_t tag = qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, q->descriptor, (int64_t)p->ammo_tag));
  if (tag < 0 || (size_t)tag >= p->ammo_capacity_count) qa_range_error("Native pickup ammo has no source capacity");
  double capacity_offset = p->ammo_capacities[tag];
  bool wide = p->ammo_capacity_bytes != 2;
  if (count != trunc(count) || count < -2147483648.0 || count > 2147483647.0 || capacity != trunc(capacity) || capacity < 0 || capacity > (wide ? 2147483647.0 : 32767.0)) qa_range_error("Selected pickup count or capacity exceeds its original ABI");
  qa_guest_address counter = counter_of(s, frame, q->descriptor), cap = qa_compat_q2_mem_offset(memory, client_of(s, frame), (int64_t)capacity_offset);
  int32_t previous_count = qa_compat_q2_mem_read_i32(memory, counter), previous_cap = wide ? qa_compat_q2_mem_read_i32(memory, cap) : qa_compat_q2_mem_read_i16(memory, cap);
  void *processor = runner->capture_processor(runner->ctx);
  double depth = qa_compat_q2_native_depth(runner);
  char stop = 0;
  uint64_t remove = p->has_ammo_stop ? runner->observe_entry(runner->ctx, at(s, p->ammo_stop), &stop, ammo_stop_observed) : 0;
  frame->projections++;
  qa_compat_q2_mem_write_i32(memory, counter, qa_js_to_int32(count));
  if (wide) qa_compat_q2_mem_write_i32(memory, cap, qa_js_to_int32(capacity)); else qa_compat_q2_mem_write_i16(memory, cap, qa_js_to_int32(capacity));
  ammo_invoke call = {s, frame, q->descriptor, q->amount, false};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(ammo_invoke_body, &call, &thrown), stopped = !completed && thrown.token == &stop;
  /* The qualified rerelease stop follows successful bookkeeping and precedes power-armor use. */
  if (stopped) call.accepted = true;
  qa_source_pickup_quantity result = {0, false};
  if (completed || stopped) { result.amount = (double)qa_compat_q2_mem_read_i32(memory, counter) - count; result.accepted = call.accepted; }
  if (p->has_ammo_stop) runner->remove_entry(runner->ctx, remove);
  qa_compat_q2_native_unwind(runner, depth);
  runner->restore_processor(runner->ctx, processor);
  if (runner->free_processor != NULL) runner->free_processor(runner->ctx, processor);
  qa_compat_q2_mem_write_i32(memory, counter, previous_count);
  if (wide) qa_compat_q2_mem_write_i32(memory, cap, previous_cap); else qa_compat_q2_mem_write_i16(memory, cap, previous_cap);
  frame->projections--;
  if (!completed && !stopped) qa_compat_q2_native_rethrow(&thrown);
  return result;
}
static qa_source_pickup_quantity supply_quantity_call(void *v, double count, double capacity) {
  supply_quantity *q = v;
  qa_source_pickup_quantity result = quantity(q, count, capacity);
  q->accepted_ammo = q->accepted_ammo || result.accepted;
  return result;
}
/* --- grantSupply --- */
typedef struct {
  pickups *s; pickup_frame *frame; const grant_t *operation;
  const qa_guest_call_value *values; size_t count; qa_compat_q2_native_entry *binding;
  bool settled;
  bool has_ammo; qa_guest_address ammo_descriptor; double ammo_amount;
  bool has_projected; qa_guest_address projected_address; int32_t projected_previous;
  supply_quantity quantity;
  qa_pickup_ammo_grant ammo_grant; char *ammo_item;
  char declined;
  uint64_t regions[3]; size_t region_count;
  uint64_t settle_handle; bool has_settle_handle;
  qa_compat_q2_native_entry *supply_entry;
  qa_guest_call_result result;
} supply_state;
static void supply_restore(supply_state *st) {
  if (!st->has_projected) return;
  st->has_projected = false;
  qa_compat_q2_mem_write_i32(mem(st->s), st->projected_address, st->projected_previous);
}
typedef struct { supply_state *st; qa_original_pickup_outcome outcome; } settle_decision;
static bool accepted_ammo(void *v) { return ((supply_state *)v)->quantity.accepted_ammo; }
static void settle_body(void *v) {
  settle_decision *d = v; supply_state *st = d->st;
  if (st->frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT) qa_range_error("Native supply owner changed during its source call");
  d->outcome = grant_resources(st->s, st->frame, accepted_ammo, st);
  require_current(st->s, st->frame);
  if (d->outcome == QA_ORIGINAL_PICKUP_OUTCOME_STALE) qa_compat_q2_native_throw(st->frame, CANCELLED_MESSAGE);
  if (d->outcome == QA_ORIGINAL_PICKUP_OUTCOME_REFUSED) qa_compat_q2_native_throw(&st->declined, DECLINED_MESSAGE);
}
static void settle(void *v) {
  supply_state *st = v; pickups *s = st->s; pickup_frame *frame = st->frame;
  if (st->settled) return;
  supply_restore(st);
  require_current(s, frame);
  st->settled = true;
  qa_compat_q2_memory *memory = mem(s);
  bool weapon = strcmp(st->operation->supply_kind, "weapon") == 0;
  if (st->has_ammo) st->ammo_item = item_name(s, st->ammo_descriptor);
  st->quantity.accepted_ammo = false;
  qa_pickup_supply_offer offer;
  memset(&offer, 0, sizeof offer);
  if (weapon) {
    offer.kind = QA_PICKUP_SUPPLY_WEAPON; offer.item = frame->offer.item;
    if (st->has_ammo) { st->ammo_grant = (qa_pickup_ammo_grant){st->ammo_item, st->ammo_amount}; offer.ammo = &st->ammo_grant; offer.ammo_count = 1; }
  } else {
    offer.kind = QA_PICKUP_SUPPLY_AMMO;
    offer.grant.item = frame->offer.item; offer.grant.amount = st->has_ammo ? st->ammo_amount : 0;
    uint32_t flags = qa_compat_q2_mem_read_u32(memory, qa_compat_q2_mem_offset(memory, frame->descriptor, (int64_t)s->profile->supply_flags));
    if ((flags & qa_js_to_uint32(s->profile->supply_weapon_flag)) != 0) { offer.kind = QA_PICKUP_SUPPLY_AMMO_WEAPON; offer.weapon = frame->offer.item; }
  }
  frame->supply.offer = offer; frame->supply.ctx = NULL; frame->supply.quantity = NULL;
  if (st->has_ammo) {
    st->quantity.s = s; st->quantity.frame = frame; st->quantity.descriptor = st->ammo_descriptor; st->quantity.amount = st->ammo_amount;
    frame->supply.ctx = &st->quantity; frame->supply.quantity = supply_quantity_call;
  }
  frame->has_supply = true;
  settle_decision decision = {st, QA_ORIGINAL_PICKUP_OUTCOME_REFUSED};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(settle_body, &decision, &thrown);
  frame->has_supply = false;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
static void ammo_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  supply_state *st = v; qa_compat_q2_runner *runner = st->s->host.runner;
  st->has_ammo = true; st->ammo_descriptor = st->frame->descriptor;
  st->ammo_amount = (int32_t)(uint32_t)runner->read_register(runner->ctx, st->operation->supply_amount, 32);
  settle(st);
  continuation->skip(continuation->ctx);
}
static void recipient_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  supply_state *st = v;
  supply_restore(st);
  require_current(st->s, st->frame);
  continuation->skip(continuation->ctx);
}
static void autoswitch_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  settle(v);
  continuation->skip(continuation->ctx);
}
static bool supply_ammo_accepts(void *v) {
  supply_state *st = v; pickups *s = st->s; qa_compat_q2_memory *memory = mem(s);
  qa_guest_address_result stack = qa_compat_q2_mem_pointer(memory, (int64_t)s->host.runner->read_register(s->host.runner->ctx, "rsp", memory->pointer_bytes == 4 ? 32 : 64));
  qa_guest_address returned;
  return top_frame(s) == st->frame && stack.has_value && qa_compat_q2_mem_read_pointer(memory, stack.value, &returned) && returned.byte_offset == at(s, st->operation->supply_ammo_return).byte_offset;
}
static qa_guest_call_result supply_ammo_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  supply_state *st = v; (void)binding;
  if (argument(values, count, 0).byte_offset != st->frame->recipient.address.byte_offset || st->has_ammo) qa_range_error("Native supply ammo changed its source recipient");
  if (count < 3 || values[2].kind != QA_GUEST_VALUE_INT32) qa_range_error("Native supply has no original ammo count");
  st->ammo_descriptor = argument(values, count, 1); st->ammo_amount = values[2].number_value; st->has_ammo = true;
  return qa_compat_q2_call_int32(1);
}
static void supply_body(void *v) {
  supply_state *st = v; pickups *s = st->s; pickup_frame *frame = st->frame; const grant_t *operation = st->operation;
  const qa_compat_q2_native_pickup_profile *p = s->profile; qa_compat_q2_memory *memory = mem(s); qa_compat_q2_runner *runner = s->host.runner;
  qa_native_call_abi abi = p->grant_signature.abi;
  if (strcmp(operation->supply_kind, "ammo") == 0) {
    st->regions[st->region_count++] = runner->bind_inline_region(runner->ctx, at(s, operation->supply_entry), at(s, operation->recipient_join), abi, st, ammo_region);
  } else {
    if (s->has_supply) {
      st->projected_address = counter_of(s, frame, frame->descriptor);
      st->projected_previous = qa_compat_q2_mem_read_i32(memory, st->projected_address);
      st->has_projected = true;
      qa_compat_q2_mem_write_i32(memory, st->projected_address, s->supply.owns(s->supply.ctx, frame->offer.recipient, frame->offer.item, frame->offer.pickup) ? 1 : 0);
    }
    st->regions[st->region_count++] = runner->bind_inline_region(runner->ctx, at(s, operation->recipient_entry), at(s, operation->recipient_join), abi, st, recipient_region);
    char id[320];
    snprintf(id, sizeof id, "%s:primary-supply-ammo", memory->module.id);
    st->supply_entry = qa_compat_q2_native_entry_bind(memory, runner, s->host.ctx, s->host.invoke, at(s, p->ammo_entry), id, p->ammo_signature, st, supply_ammo_execute, st, supply_ammo_accepts);
    st->settle_handle = runner->observe_entry(runner->ctx, at(s, operation->supply_settle), st, settle);
    st->has_settle_handle = true;
    st->regions[st->region_count++] = runner->bind_inline_region(runner->ctx, at(s, operation->supply_auto_entry), at(s, operation->supply_auto_join), abi, st, autoswitch_region);
  }
  st->result = qa_compat_q2_native_entry_call_original(st->binding, st->values, st->count);
  require_current(s, frame);
  if ((st->result.kind != QA_GUEST_VALUE_INT32 && st->result.kind != QA_GUEST_VALUE_UINT32) || (st->result.number_value != 0 && !st->settled)) qa_range_error("Native supply did not settle before its source continuation");
}
static qa_guest_call_result grant_supply(pickups *s, pickup_frame *frame, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  if (!frame->grant->has_supply || frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT) qa_range_error("Native supply has no selected grant");
  qa_compat_q2_runner *runner = s->host.runner;
  void *processor = runner->capture_processor(runner->ctx);
  double depth = qa_compat_q2_native_depth(runner);
  supply_state st;
  memset(&st, 0, sizeof st);
  st.s = s; st.frame = frame; st.operation = frame->grant; st.values = values; st.count = count; st.binding = binding;
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(supply_body, &st, &thrown), declined = !completed && thrown.token == &st.declined;
  if (declined) {
    qa_compat_q2_native_unwind(runner, depth);
    runner->restore_processor(runner->ctx, processor);
    st.result = qa_compat_q2_call_int32(0);
  }
  supply_restore(&st);
  frame->has_supply = false;
  for (size_t i = st.region_count; i-- > 0;) {
    if (i == 1 && st.has_settle_handle) runner->remove_entry(runner->ctx, st.settle_handle);
    runner->remove_inline_region(runner->ctx, st.regions[i]);
    if (i == 1 && st.supply_entry != NULL) qa_compat_q2_native_entry_close(st.supply_entry);
  }
  free(st.ammo_item);
  if (runner->free_processor != NULL) runner->free_processor(runner->ctx, processor);
  if (!completed && !declined) qa_compat_q2_native_rethrow(&thrown);
  return st.result;
}
/* --- grant --- */
typedef struct { pickups *s; pickup_frame *frame; uint64_t handle; bool removed, skipped; qa_compat_q2_native_entry *binding; const qa_guest_call_value *values; size_t count; qa_guest_call_result result; } grant_call;
static void grant_remove(grant_call *c) {
  if (c->removed) return;
  c->removed = true;
  c->s->host.runner->remove_inline_region(c->s->host.runner->ctx, c->handle);
}
static void grant_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  grant_call *c = v;
  require_current(c->s, c->frame);
  grant_remove(c);
  c->skipped = true;
  continuation->skip(continuation->ctx);
}
static void grant_body(void *v) {
  grant_call *c = v;
  c->result = qa_compat_q2_native_entry_call_original(c->binding, c->values, c->count);
  require_current(c->s, c->frame);
  if (!c->skipped || (c->result.kind != QA_GUEST_VALUE_INT32 && c->result.kind != QA_GUEST_VALUE_UINT32) || c->result.number_value == 0) qa_range_error("Original pickup map continuation did not accept its settled grant");
}
static qa_guest_call_result grant_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  pickups *s = v; pickup_frame *frame = top_frame(s);
  if (frame == NULL) qa_range_error("Native pickup grant has no owning caller");
  require_current(s, frame);
  if (argument(values, count, 0).byte_offset != frame->pickup.address.byte_offset || argument(values, count, 1).byte_offset != frame->recipient.address.byte_offset || frame->called) qa_range_error("Native pickup grant changed its owning source call");
  frame->called = true;
  if (frame->selection.kind == QA_SOURCE_PICKUP_BLOCKED) return qa_compat_q2_call_int32(0);
  if (frame->selection.kind != QA_SOURCE_PICKUP_REPLACEMENT) qa_range_error("Native pickup replacement has no resource owner");
  if (frame->grant->has_supply) return grant_supply(s, frame, values, count, binding);
  qa_original_pickup_outcome decision = grant_resources(s, frame, NULL, NULL);
  require_current(s, frame);
  if (decision == QA_ORIGINAL_PICKUP_OUTCOME_STALE) qa_compat_q2_native_throw(frame, CANCELLED_MESSAGE);
  if (decision == QA_ORIGINAL_PICKUP_OUTCOME_REFUSED) return qa_compat_q2_call_int32(0);
  grant_call call = {s, frame, 0, false, false, binding, values, count, {0}};
  qa_compat_q2_runner *runner = s->host.runner;
  call.handle = runner->bind_inline_region(runner->ctx, at(s, frame->grant->recipient_entry), at(s, frame->grant->recipient_join), s->profile->grant_signature.abi, &call, grant_region);
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(grant_body, &call, &thrown);
  grant_remove(&call);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return call.result;
}
/* --- public --- */
void qa_compat_q2_native_primary_pickups_init(pickups *s, qa_compat_q2_native_pickup_host host, qa_guest_address image, const qa_compat_q2_native_pickup_profile *profile) {
  bool any_consumer = false;
  for (size_t i = 0; i < profile->grant_count; i++) if (profile->grants[i].consumer_count != 0) any_consumer = true;
  if (any_consumer && host.with_protection == NULL) qa_range_error("Native pickup consumer has no protection owner binding");
  if (strcmp(host.memory->module.digest, profile->digest) != 0) qa_range_error("Native pickup profile does not identify this artifact");
  memset(s, 0, sizeof *s);
  s->host = host; s->image = image; s->profile = profile;
  removals_t *removals = calloc(1, sizeof *removals);
  if (removals == NULL) qa_range_error("Native pickups out of memory");
  s->removals = removals;
  removals->grants = calloc(profile->grant_count ? profile->grant_count : 1, sizeof *removals->grants);
  removals->grant_ctxs = calloc(profile->grant_count ? profile->grant_count : 1, sizeof *removals->grant_ctxs);
  if (removals->grants == NULL || removals->grant_ctxs == NULL) qa_range_error("Native pickups out of memory");
  const char *module = host.memory->module.id; char id[320];
  snprintf(id, sizeof id, "%s:primary-item-touch", module);
  removals->touch = qa_compat_q2_native_entry_bind(host.memory, host.runner, host.ctx, host.invoke, at(s, profile->touch), id, profile->touch_signature, s, touch_execute, NULL, NULL);
  for (size_t i = 0; i < profile->grant_count; i++) {
    snprintf(id, sizeof id, "%s:primary-item-grant-%s", module, QA_JS_NUMBER(profile->grants[i].entry));
    removals->grant_ctxs[i] = (grant_ctx){s, &profile->grants[i]};
    removals->grants[i] = qa_compat_q2_native_entry_bind(host.memory, host.runner, host.ctx, host.invoke, at(s, profile->grants[i].entry), id, profile->grant_signature, s, grant_execute, &removals->grant_ctxs[i], grant_caller);
  }
  removals->targets = host.runner->observe_entry(host.runner->ctx, at(s, profile->targets_return), s, targets_observed);
}
void qa_compat_q2_native_primary_pickups_assert_idle(const pickups *s) {
  if (s->frame_count != 0) qa_range_error("Native pickup caller is still active");
}
void qa_compat_q2_native_primary_pickups_bind_supply(pickups *s, qa_compat_q2_native_pickup_supply supply) {
  qa_compat_q2_native_primary_pickups_assert_idle(s);
  if (s->closed || s->has_supply) qa_range_error("Native pickups already have a supply owner or are closed");
  s->supply = supply; s->has_supply = true;
}
void qa_compat_q2_native_primary_pickups_unbind_supply(pickups *s, qa_compat_q2_native_pickup_supply supply) {
  if (s->has_supply && s->supply.ctx == supply.ctx && s->supply.owns == supply.owns) s->has_supply = false;
}
qa_compat_q2_native_pickup_supply_evaluation qa_compat_q2_native_primary_pickups_evaluate_supply(pickups *s, const qa_original_pickup_offer *offer) {
  pickup_frame *frame = top_frame(s);
  if (frame == NULL || !frame->has_supply || strcmp(frame->offer.item, offer->item) != 0 || strcmp(frame->offer.source, offer->source) != 0
      || !qa_same_actor(frame->offer.pickup, offer->pickup) || !qa_same_actor(frame->offer.recipient, offer->recipient)) qa_range_error("Native pickup supply requires its held source grant");
  require_current(s, frame);
  return frame->supply;
}
void qa_compat_q2_native_primary_pickups_close(pickups *s) {
  s->closed = true;
  for (size_t i = 0; i < s->frame_count; i++) if (s->frames[i] != NULL) ((pickup_frame *)s->frames[i])->invalid = true;
  removals_t *removals = s->removals;
  if (removals == NULL) return;
  s->removals = NULL;
  s->host.runner->remove_entry(s->host.runner->ctx, removals->targets);
  for (size_t i = s->profile->grant_count; i-- > 0;) qa_compat_q2_native_entry_close(removals->grants[i]);
  qa_compat_q2_native_entry_close(removals->touch);
  free(removals->grants); free(removals->grant_ctxs); free(removals);
  if (s->frame_count == 0) { free(s->frames); s->frames = NULL; s->frame_capacity = 0; }
}
