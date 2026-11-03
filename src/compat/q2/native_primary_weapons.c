/* Port of src/compat/q2/native-primary-weapons.ts: borrows the real primary
 * client's fields and the shared native dispatcher. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_weapons.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_mod_weapon_stage.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

typedef struct { qa_actor_id actor; double value; } actor_number;
typedef struct { actor_number *rows; size_t count, capacity; } actor_map;
struct qa_compat_q2_native_primary_weapons {
  qa_compat_q2_native_primary_weapon_host host;
  const qa_compat_q2_native_primary_weapon_profile *profile;
  qa_compat_q2_native_primary_weapon_hooks hooks;
  qa_compat_q2_native_weapon_dispatcher *dispatcher;
  qa_compat_q2_native_entry *spawn;
  uint64_t delay_region, damage_region;
  actor_map delay_flags, damage_factors;
  bool has_delay_result; qa_guest_address delay_result;
  bool closed;
};
static const qa_guest_value_layout one_pointer[1] = {{.is_aggregate = false, .storage = "pointer"}};
static const qa_guest_value_layout pointer_layout = {.is_aggregate = false, .storage = "pointer"};

static qa_guest_address at(const qa_compat_q2_native_primary_weapons *s, double offset) { return qa_compat_q2_mem_offset(s->host.memory, s->host.image, (int64_t)offset); }
static bool map_get(const actor_map *map, qa_actor_id actor, double *out) {
  for (size_t i = 0; i < map->count; i++) if (qa_same_actor(map->rows[i].actor, actor)) { *out = map->rows[i].value; return true; }
  return false;
}
static void map_set(actor_map *map, qa_actor_id actor, double value) {
  for (size_t i = 0; i < map->count; i++) if (qa_same_actor(map->rows[i].actor, actor)) { map->rows[i].value = value; return; }
  if (map->count == map->capacity) {
    size_t capacity = map->capacity ? map->capacity * 2 : 4;
    actor_number *grown = realloc(map->rows, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native weapons out of memory");
    map->rows = grown; map->capacity = capacity;
  }
  map->rows[map->count++] = (actor_number){actor, value};
}
static void map_delete(actor_map *map, qa_actor_id actor) {
  for (size_t i = 0; i < map->count; i++) if (qa_same_actor(map->rows[i].actor, actor)) { map->rows[i] = map->rows[--map->count]; return; }
}
static bool current(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) {
  qa_raw_entity_view record; qa_actor_id owner;
  return s->host.record_for(s->host.ctx, actor, &record) && s->host.actor(s->host.ctx, record, &owner) && qa_same_actor(owner, actor);
}
static qa_raw_entity_view record_of(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) {
  qa_raw_entity_view record; qa_actor_id owner;
  if (!s->host.record_for(s->host.ctx, actor, &record) || !s->host.actor(s->host.ctx, record, &owner) || !qa_same_actor(owner, actor)) qa_range_error("Native weapon owner was retired");
  return record;
}
static qa_guest_address address_of(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, const char *record, double offset) {
  qa_raw_entity_view entity = record_of(s, actor);
  qa_compat_q2_memory *memory = s->host.memory;
  if (strcmp(record, "image") == 0) return at(s, offset);
  if (strcmp(record, "entity") == 0) return qa_compat_q2_mem_offset(memory, entity.address, (int64_t)offset);
  if (strcmp(record, "client") != 0) qa_range_error("Native primary field has an unknown record");
  qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, entity.address, (int64_t)s->profile->entity_client), &client)) qa_range_error("Native weapon owner has no source client");
  memory->check(memory->ctx, client, s->profile->client_byte_length, "read");
  return qa_compat_q2_mem_offset(memory, client, (int64_t)offset);
}
static double read_address(const qa_compat_q2_native_primary_weapons *s, qa_guest_address address, qa_native_mod_scalar encoding) {
  qa_compat_q2_memory *memory = s->host.memory;
  if (strcmp(encoding, "int8") == 0) return (int8_t)qa_compat_q2_mem_read_u8(memory, address);
  if (strcmp(encoding, "uint8") == 0) return qa_compat_q2_mem_read_u8(memory, address);
  if (strcmp(encoding, "int16") == 0) return qa_compat_q2_mem_read_i16(memory, address);
  if (strcmp(encoding, "uint16") == 0) return qa_compat_q2_mem_read_u16(memory, address);
  if (strcmp(encoding, "int32") == 0) return qa_compat_q2_mem_read_i32(memory, address);
  if (strcmp(encoding, "uint32") == 0) return qa_compat_q2_mem_read_u32(memory, address);
  if (strcmp(encoding, "float32") == 0) return qa_compat_q2_mem_read_f32(memory, address);
  if (strcmp(encoding, "float64") == 0) {
    uint64_t bits = qa_compat_q2_mem_read_u64(memory, address); double value;
    memcpy(&value, &bits, sizeof value);
    return value;
  }
  double value = strcmp(encoding, "int64") == 0 ? (double)qa_compat_q2_mem_read_i64(memory, address) : (double)qa_compat_q2_mem_read_u64(memory, address);
  if (fabs(value) > (double)QA_MAX_SAFE_INTEGER) qa_range_error("Native weapon field exceeds exact numeric range");
  return value;
}
static double read_field(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, qa_native_item_field field) {
  return read_address(s, address_of(s, actor, field.record, field.field.offset), field.field.encoding);
}
static void write_field(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, qa_native_item_field field, double value) {
  qa_guest_address address = address_of(s, actor, field.record, field.field.offset);
  qa_compat_q2_memory *memory = s->host.memory; const char *encoding = field.field.encoding;
  if (strcmp(encoding, "int8") == 0 || strcmp(encoding, "uint8") == 0) qa_compat_q2_mem_write_u8(memory, address, (uint32_t)qa_int32(value) & 0xff);
  else if (strcmp(encoding, "int16") == 0) qa_compat_q2_mem_write_i16(memory, address, qa_int32(value));
  else if (strcmp(encoding, "uint16") == 0) qa_compat_q2_mem_write_u16(memory, address, qa_js_to_uint32(qa_int32(value)) & 0xffff);
  else if (strcmp(encoding, "int32") == 0) qa_compat_q2_mem_write_i32(memory, address, qa_int32(value));
  else if (strcmp(encoding, "uint32") == 0) qa_compat_q2_mem_write_u32(memory, address, qa_js_to_uint32(qa_int32(value)));
  else if (strcmp(encoding, "float32") == 0) qa_compat_q2_mem_write_f32(memory, address, value);
  else if (strcmp(encoding, "float64") == 0) { uint64_t bits; memcpy(&bits, &value, sizeof bits); qa_compat_q2_mem_write_u64(memory, address, bits); }
  else {
    /* writeInt64/writeUint64(BigInt(value)): BigInt throws for a non-integer; the
     * 64-bit store keeps the low 64 bits (two's complement) of any integer. */
    if (!isfinite(value) || value != trunc(value)) qa_range_error("The number %s cannot be converted to a BigInt because it is not an integer", QA_JS_NUMBER(value));
    uint64_t magnitude = (uint64_t)fmod(fabs(value), 18446744073709551616.0);
    uint64_t bits = value < 0 ? (uint64_t)0 - magnitude : magnitude;
    if (strcmp(encoding, "int64") == 0) { int64_t signed_bits; memcpy(&signed_bits, &bits, sizeof bits); qa_compat_q2_mem_write_i64(memory, address, signed_bits); }
    else qa_compat_q2_mem_write_u64(memory, address, bits);
  }
}
static bool matches(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, const qa_native_item_test *test) {
  qa_compat_q2_memory *memory = s->host.memory;
  if (strcmp(test->kind, "pointer") == 0) {
    qa_guest_address pointer;
    bool has = qa_compat_q2_mem_read_pointer(memory, address_of(s, actor, test->pointer_field.record, test->pointer_field.offset), &pointer);
    bool has_expected = test->pointer_value != NULL; qa_guest_address expected = {0};
    if (has_expected) {
      expected = at(s, test->pointer_value->rva);
      for (size_t i = 0; i < test->pointer_value->indirections.count && has_expected; i++)
        has_expected = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, expected, (int64_t)test->pointer_value->indirections.values[i]), &expected);
    }
    return has == has_expected && (!has || pointer.byte_offset == expected.byte_offset);
  }
  double raw = read_field(s, actor, test->field);
  double value = test->has_mask ? (double)(qa_int32(raw) & qa_int32(test->mask)) : raw;
  return strcmp(test->comparison, "equals") == 0 ? value == test->scalar_value : value <= test->scalar_value;
}
static bool all_match(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, const qa_native_item_test *tests, size_t count) {
  for (size_t i = 0; i < count; i++) if (!matches(s, actor, &tests[i])) return false;
  return true;
}
static bool any_row(const qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, const qa_native_item_test_row *rows, size_t count) {
  for (size_t i = 0; i < count; i++) if (all_match(s, actor, rows[i].tests, rows[i].count)) return true;
  return false;
}
/* --- Dispatcher operations. --- */
static bool op_actor(void *v, const char *record, qa_guest_address address, qa_actor_id *out) {
  qa_compat_q2_native_primary_weapons *s = v;
  if (strcmp(record, "entity") != 0) qa_range_error("Native primary dispatcher requires an entity argument");
  return s->host.actor(s->host.ctx, s->host.record(s->host.ctx, address), out);
}
static bool op_current(void *v, qa_actor_id actor) { return current(v, actor); }
static bool op_selected(void *v, qa_actor_id actor) { qa_compat_q2_native_primary_weapons *s = v; return s->hooks.selected(s->hooks.ctx, actor); }
static bool op_committed(void *v, qa_actor_id actor) { qa_compat_q2_native_primary_weapons *s = v; return any_row(s, actor, s->profile->committed_input, s->profile->committed_input_count); }
static double op_read(void *v, qa_actor_id actor, qa_native_item_field field) { return read_field(v, actor, field); }
static void op_write(void *v, qa_actor_id actor, qa_native_item_field field, double value) { write_field(v, actor, field, value); }
static void op_completed(void *v, qa_actor_id actor, bool reached) { qa_compat_q2_native_primary_weapons *s = v; s->hooks.completed(s->hooks.ctx, actor, reached); }
/* --- Bindings. --- */
static qa_guest_call_result spawn_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  qa_compat_q2_native_primary_weapons *s = v; qa_actor_id actor;
  bool has = count > 0 && values[0].kind == QA_GUEST_VALUE_POINTER && values[0].pointer_value.has_value && s->host.actor(s->host.ctx, s->host.record(s->host.ctx, values[0].pointer_value.value), &actor);
  qa_guest_call_result result = qa_compat_q2_native_entry_call_original(binding, values, count);
  if (has && current(s, actor) && all_match(s, actor, s->profile->spawn_accepted, s->profile->spawn_accepted_count)) s->hooks.spawned(s->hooks.ctx, actor);
  return result;
}
static void delay_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_weapons *s = v; qa_actor_id actor;
  bool has = qa_compat_q2_native_weapon_dispatcher_current_actor(s->dispatcher, &actor);
  continuation->execute(continuation->ctx);
  if (has && current(s, actor)) map_set(&s->delay_flags, actor, read_field(s, actor, s->profile->delay_flag));
}
static void damage_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_weapons *s = v; qa_actor_id actor;
  bool has = qa_compat_q2_native_weapon_dispatcher_current_actor(s->dispatcher, &actor);
  continuation->execute(continuation->ctx);
  if (has && current(s, actor)) {
    double value = read_address(s, at(s, s->profile->damage_address), s->profile->damage_encoding);
    if (value != trunc(value) || value < 0 || value >= (double)s->profile->damage_factor_count) qa_range_error("Original weapon damage flag has no declared source factor");
    map_set(&s->damage_factors, actor, s->profile->damage_factors[(size_t)value]);
  }
}
static qa_guest_call_result spawn_invoke(void *v, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  qa_compat_q2_native_primary_weapons *s = v;
  return s->host.invoke(s->host.ctx, address, signature, values, count);
}
qa_compat_q2_native_primary_weapons *qa_compat_q2_native_primary_weapons_create(qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_weapon_profile *profile, qa_compat_q2_native_primary_weapon_hooks hooks) {
  if (strcmp(host.memory->module.digest, profile->digest) != 0) qa_range_error("Native primary weapon profile does not identify this artifact");
  qa_compat_q2_native_primary_weapons *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Native weapons out of memory");
  s->host = host; s->profile = profile; s->hooks = hooks;
  qa_compat_q2_memory *memory = host.memory; qa_compat_q2_runner *runner = host.runner;
  if (strcmp(profile->delay_evaluate_kind, "source-animation") == 0) {
    s->delay_result = memory->allocate(memory->ctx, 8, true, 8, "selected source weapon delay");
    s->has_delay_result = true;
  }
  qa_compat_q2_native_weapon_host entry_host = {memory, runner, host.image, host.ctx, host.invoke, host.entry};
  qa_compat_q2_native_weapon_operations operations;
  memset(&operations, 0, sizeof operations);
  operations.ctx = s; operations.actor = op_actor; operations.current = op_current; operations.selected = op_selected;
  operations.committed_input = op_committed; operations.read = op_read; operations.write = op_write; operations.completed = op_completed;
  s->dispatcher = qa_compat_q2_native_weapon_dispatcher_create(&profile->dispatcher, profile->decisions, profile->decision_count, profile->abi, entry_host, memory->module.id, operations);
  char id[320];
  snprintf(id, sizeof id, "%s:selected-spawn", memory->module.id);
  qa_compat_q2_guest_call_signature signature = {profile->abi, one_pointer, 1, true, {0}, false};
  s->spawn = qa_compat_q2_native_entry_bind(memory, runner, s, spawn_invoke, at(s, profile->spawn_entry), id, signature, s, spawn_execute, NULL, NULL);
  s->delay_region = runner->bind_inline_region(runner->ctx, at(s, profile->delay_entry), at(s, profile->delay_join), profile->abi, s, delay_region);
  if (strcmp(profile->damage_kind, "source-flag") == 0)
    s->damage_region = runner->bind_inline_region(runner->ctx, at(s, profile->damage_region_entry), at(s, profile->damage_region_join), profile->abi, s, damage_region);
  return s;
}
const qa_compat_q2_native_primary_weapon_profile *qa_compat_q2_native_primary_weapons_profile(const qa_compat_q2_native_primary_weapons *s) { return s->profile; }
double qa_compat_q2_native_primary_weapons_read(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, qa_native_item_field field) { return read_field(s, actor, field); }
qa_compat_q2_native_primary_weapon_input qa_compat_q2_native_primary_weapons_input(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) {
  const qa_compat_q2_native_primary_weapon_profile *p = s->profile; qa_compat_q2_memory *memory = s->host.memory;
  qa_guest_address angles = address_of(s, actor, "client", p->client_view_angles);
  qa_compat_q2_native_primary_weapon_input out;
  out.buttons = read_field(s, actor, p->client_buttons);
  out.latched_buttons = read_field(s, actor, p->client_latched_buttons);
  out.view_angles.x = qa_compat_q2_mem_read_f32(memory, angles);
  out.view_angles.y = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, angles, 4));
  out.view_angles.z = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, angles, 8));
  out.view_height = read_field(s, actor, p->entity_view_height);
  out.water_level = read_field(s, actor, p->entity_water_level);
  out.max_health = read_field(s, actor, p->entity_max_health);
  return out;
}
qa_animation_state qa_compat_q2_native_primary_weapons_animation(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) {
  const qa_compat_q2_native_primary_weapon_profile *p = s->profile;
  qa_animation_state out;
  memset(&out, 0, sizeof out);
  out.kind = "q2";
  out.frame = read_field(s, actor, p->anim_frame);
  out.end_frame = read_field(s, actor, p->anim_end);
  out.priority = read_field(s, actor, p->anim_priority);
  out.duck = read_field(s, actor, p->anim_duck) != 0;
  out.run = read_field(s, actor, p->anim_run) != 0;
  return out;
}
double qa_compat_q2_native_primary_weapons_damage_factor(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) {
  const qa_compat_q2_native_primary_weapon_profile *p = s->profile;
  if (strcmp(p->damage_kind, "source-flag") == 0) {
    record_of(s, actor);
    double factor;
    if (!map_get(&s->damage_factors, actor, &factor)) qa_range_error("Original weapon damage modifier has not run for this actor");
    return factor;
  }
  qa_guest_value_layout result_layout = {.is_aggregate = false, .storage = p->damage_result};
  qa_compat_q2_guest_call_signature signature = {p->abi, one_pointer, 1, false, result_layout, false};
  qa_guest_call_value value = qa_compat_q2_call_pointer(true, record_of(s, actor).address);
  qa_guest_call_result result = s->host.invoke(s->host.ctx, at(s, p->damage_entry), signature, &value, 1);
  if (result.kind != QA_GUEST_VALUE_UINT32 && result.kind != QA_GUEST_VALUE_INT32) qa_range_error("Original weapon damage helper returned the wrong ABI type");
  return result.number_value;
}
static void skip_region(void *v, qa_compat_q2_inline_continuation *continuation) { (void)v; continuation->skip(continuation->ctx); }
typedef struct { qa_compat_q2_native_primary_weapons *s; qa_compat_q2_guest_call_signature signature; const qa_guest_call_value *values; size_t count; double target; } invoke_scope;
static void invoke_body(void *v) { invoke_scope *c = v; c->s->host.invoke(c->s->host.ctx, at(c->s, c->target), c->signature, c->values, c->count); }
void qa_compat_q2_native_primary_weapons_attack_animation(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) {
  if (!qa_compat_q2_native_primary_weapons_available(s, actor)) return;
  const qa_compat_q2_native_primary_weapon_profile *p = s->profile; qa_compat_q2_runner *runner = s->host.runner;
  uint64_t *removals = calloc(p->attack_skip_count ? p->attack_skip_count : 1, sizeof *removals);
  if (removals == NULL) qa_range_error("Native weapons out of memory");
  for (size_t i = 0; i < p->attack_skip_count; i++)
    removals[i] = runner->bind_inline_region(runner->ctx, at(s, p->attack_skip[i].entry), at(s, p->attack_skip[i].join), p->abi, s, skip_region);
  static const qa_guest_value_layout parameters[8] = {
    {.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "int32"}, {.is_aggregate = false, .storage = "int32"}, {.is_aggregate = false, .storage = "int32"},
    {.is_aggregate = false, .storage = "int32"}, {.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "pointer"}};
  qa_guest_address none = {0};
  qa_guest_call_value values[8] = {qa_compat_q2_call_pointer(true, record_of(s, actor).address), qa_compat_q2_call_int32(0), qa_compat_q2_call_int32(0), qa_compat_q2_call_int32(0), qa_compat_q2_call_int32(0),
                                   qa_compat_q2_call_pointer(false, none), qa_compat_q2_call_pointer(false, none), qa_compat_q2_call_pointer(false, none)};
  qa_compat_q2_guest_call_signature signature = {p->abi, parameters, 8, true, {0}, false};
  invoke_scope scope = {s, signature, values, 8, p->attack_animation_entry};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(invoke_body, &scope, &thrown);
  for (size_t i = p->attack_skip_count; i-- > 0;) runner->remove_inline_region(runner->ctx, removals[i]);
  free(removals);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
typedef struct { qa_native_item_field field; double value; } saved_value;
double qa_compat_q2_native_primary_weapons_weapon_delay(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor, double milliseconds) {
  const qa_compat_q2_native_primary_weapon_profile *p = s->profile;
  double flag;
  if (!map_get(&s->delay_flags, actor, &flag)) qa_range_error("Original firing modifier has not run for this actor");
  if (strcmp(p->delay_evaluate_kind, "source-flag") == 0) {
    if (flag != trunc(flag) || flag < 0 || flag >= (double)p->delay_factor_count) qa_range_error("Original firing flag has no declared source factor");
    return milliseconds * p->delay_factors[(size_t)flag];
  }
  if (!s->has_delay_result) qa_range_error("Original animation delay result storage is unavailable");
  size_t field_count = p->delay_projection_count + p->delay_write_count + 1;
  saved_value *saved = calloc(field_count, sizeof *saved);
  if (saved == NULL) qa_range_error("Native weapons out of memory");
  size_t used = 0;
  for (size_t i = 0; i < p->delay_projection_count; i++) saved[used++].field = p->delay_projection[i].field;
  for (size_t i = 0; i < p->delay_write_count; i++) saved[used++].field = p->delay_writes[i];
  saved[used++].field = p->delay_flag;
  for (size_t i = 0; i < used; i++) saved[i].value = read_field(s, actor, saved[i].field);
  for (size_t i = 0; i < p->delay_projection_count; i++) write_field(s, actor, p->delay_projection[i].field, p->delay_projection[i].value);
  write_field(s, actor, p->delay_flag, flag);
  static const qa_guest_value_layout parameters[2] = {{.is_aggregate = false, .storage = "pointer"}, {.is_aggregate = false, .storage = "pointer"}};
  qa_guest_call_value values[2] = {qa_compat_q2_call_pointer(true, s->delay_result), qa_compat_q2_call_pointer(true, record_of(s, actor).address)};
  qa_compat_q2_guest_call_signature signature = {p->abi, parameters, 2, false, pointer_layout, false};
  invoke_scope scope = {s, signature, values, 2, p->delay_anim_entry};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(invoke_body, &scope, &thrown);
  double delay = 0;
  if (completed) delay = milliseconds * (double)qa_compat_q2_mem_read_i64(s->host.memory, s->delay_result) / p->delay_baseline_ms;
  if (current(s, actor)) for (size_t i = 0; i < used; i++) write_field(s, actor, saved[i].field, saved[i].value);
  free(saved);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return delay;
}
bool qa_compat_q2_native_primary_weapons_available(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) { return current(s, actor) && all_match(s, actor, s->profile->active, s->profile->active_count); }
bool qa_compat_q2_native_primary_weapons_continuing(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) { return current(s, actor) && any_row(s, actor, s->profile->continuations, s->profile->continuation_count); }
double qa_compat_q2_native_primary_weapons_time_milliseconds(qa_compat_q2_native_primary_weapons *s) { return read_address(s, at(s, s->profile->time_address), s->profile->time_encoding) * s->profile->time_milliseconds; }
void qa_compat_q2_native_primary_weapons_release(qa_compat_q2_native_primary_weapons *s, qa_actor_id actor) { map_delete(&s->damage_factors, actor); map_delete(&s->delay_flags, actor); }
void qa_compat_q2_native_primary_weapons_close(qa_compat_q2_native_primary_weapons *s) {
  if (s->closed) return;
  s->closed = true;
  qa_compat_q2_runner *runner = s->host.runner;
  if (strcmp(s->profile->damage_kind, "source-flag") == 0) runner->remove_inline_region(runner->ctx, s->damage_region);
  runner->remove_inline_region(runner->ctx, s->delay_region);
  qa_compat_q2_native_entry_close(s->spawn);
  qa_compat_q2_native_weapon_dispatcher_close(s->dispatcher);
  s->damage_factors.count = 0; s->delay_flags.count = 0;
  if (s->has_delay_result) s->host.memory->unmap(s->host.memory->ctx, s->delay_result, 8);
}
void qa_compat_q2_native_primary_weapons_destroy(qa_compat_q2_native_primary_weapons *s) {
  if (s == NULL) return;
  qa_compat_q2_native_primary_weapons_close(s);
  qa_compat_q2_native_weapon_dispatcher_destroy(s->dispatcher);
  free(s->delay_flags.rows); free(s->damage_factors.rows); free(s);
}
