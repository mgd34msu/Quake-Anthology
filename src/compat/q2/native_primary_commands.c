/* Port of src/compat/q2/native-primary-commands.ts: original commands retain
 * cheat checks, grants and map-owned drop decisions. GPL-2.0-or-later. */
#include "qa/compat/q2/native_primary_commands.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_mod_entries.h"
#include "qa/compat/q2/native_throw.h"
#include "qa/foundation/js.h"

typedef struct { qa_item_id item; double index; qa_guest_address address; bool weapon, ammunition; const char *icon; qa_item_id ammo; } source_item;
typedef struct { qa_compat_q2_native_primary_commands *s; const char *category; } give_observe;
typedef struct { qa_compat_q2_native_primary_commands *s; const qa_compat_q2_ammo_grant *grant; } ammo_region;
typedef struct { bool has_actor; qa_actor_id actor; } actor_slot;
typedef struct { actor_slot *items; size_t count, capacity; } actor_stack;
typedef struct { qa_compat_q2_native_primary_commands *s; actor_stack *stack; } command_entry;
struct qa_compat_q2_native_primary_commands {
  qa_compat_q2_native_primary_weapon_host host;
  const qa_compat_q2_native_primary_command_profile *profile;
  qa_compat_q2_native_primary_command_hooks hooks;
  actor_stack giving, dropping;
  source_item *items; size_t item_count;
  qa_compat_q2_native_inventory_slot *slots;
  qa_compat_q2_native_source_weapon *weapons; size_t weapon_count;
  char **strings; size_t string_count, string_capacity;
  command_entry entries[2]; qa_compat_q2_native_entry *bindings[2];
  give_observe observes[2]; uint64_t observe_handles[2];
  uint64_t unknown_region, drop_region; uint64_t *ammo_regions; ammo_region *ammo_ctx;
  bool closed;
};
static const qa_guest_value_layout pointer_layout = {.is_aggregate = false, .storage = "pointer"};
static const qa_guest_value_layout integer_layout = {.is_aggregate = false, .storage = "int32"};
static const qa_guest_value_layout command_parameters[1] = {{.is_aggregate = false, .storage = "pointer"}};
static const qa_guest_value_layout argv_parameters[1] = {{.is_aggregate = false, .storage = "int32"}};

static qa_guest_address at(const qa_compat_q2_native_primary_commands *s, double offset) { return qa_compat_q2_mem_offset(s->host.memory, s->host.image, (int64_t)offset); }
static bool current(const qa_compat_q2_native_primary_commands *s, qa_actor_id actor) {
  if (s->closed) return false;
  qa_raw_entity_view record; qa_actor_id owner;
  return s->host.record_for(s->host.ctx, actor, &record) && s->host.actor(s->host.ctx, record, &owner) && qa_same_actor(owner, actor);
}
static void stack_push(actor_stack *stack, bool has, qa_actor_id actor) {
  if (stack->count == stack->capacity) {
    size_t capacity = stack->capacity ? stack->capacity * 2 : 4;
    actor_slot *grown = realloc(stack->items, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native commands out of memory");
    stack->items = grown; stack->capacity = capacity;
  }
  stack->items[stack->count].has_actor = has;
  if (has) stack->items[stack->count].actor = actor;
  stack->count++;
}
static bool stack_top(const actor_stack *stack, qa_actor_id *out) {
  if (stack->count == 0 || !stack->items[stack->count - 1].has_actor) return false;
  *out = stack->items[stack->count - 1].actor;
  return true;
}
static char *keep(qa_compat_q2_native_primary_commands *s, char *text) {
  if (s->string_count == s->string_capacity) {
    size_t capacity = s->string_capacity ? s->string_capacity * 2 : 32;
    char **grown = realloc(s->strings, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Native commands out of memory");
    s->strings = grown; s->string_capacity = capacity;
  }
  s->strings[s->string_count++] = text;
  return text;
}
static char *read_string(qa_compat_q2_native_primary_commands *s, qa_guest_address field) {
  qa_compat_q2_memory *memory = s->host.memory; qa_guest_address value;
  bool has = qa_compat_q2_mem_read_pointer(memory, field, &value);
  return keep(s, qa_compat_q2_read_classic_string(memory, has, value, 65536));
}
static bool qualified(const char *name) {
  if (name[0] < 'a' || name[0] > 'z') return false;
  for (const char *c = name + 1; *c != 0; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return false;
  return true;
}
static char *q2_name(qa_compat_q2_native_primary_commands *s, const char *classname) {
  size_t length = strlen(classname) + 4;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Native commands out of memory");
  snprintf(out, length, "q2:%s", classname);
  return keep(s, out);
}
static bool ascii_iequal(const char *a, const char *b) {
  for (;; a++, b++) {
    unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
    if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
    if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
    if (x != y) return false;
    if (x == 0) return true;
  }
}
typedef struct { qa_item_id item; qa_guest_address address; const char *icon; bool ammunition, weapon; const char *label; } source_row;
static void read_items(qa_compat_q2_native_primary_commands *s) {
  const qa_compat_q2_native_primary_command_profile *table = s->profile;
  qa_compat_q2_memory *memory = s->host.memory;
  size_t count = (size_t)table->items_count;
  source_row *source = calloc(count ? count : 1, sizeof *source);
  s->items = calloc(count ? count : 1, sizeof *s->items);
  if (source == NULL || s->items == NULL) qa_range_error("Native commands out of memory");
  bool by_name = strcmp(table->ammo_kind, "name") == 0;
  for (size_t index = 0; index < count; index++) {
    source_row *row = &source[index];
    row->address = at(s, table->items_table + (double)index * table->items_stride);
    const char *classname = read_string(s, qa_compat_q2_mem_offset(memory, row->address, (int64_t)table->items_classname));
    row->item = qualified(classname) ? q2_name(s, classname) : NULL;
    row->icon = read_string(s, qa_compat_q2_mem_offset(memory, row->address, (int64_t)table->items_icon));
    uint32_t flags = qa_compat_q2_mem_read_u32(memory, qa_compat_q2_mem_offset(memory, row->address, (int64_t)table->items_flags));
    row->ammunition = (flags & qa_js_to_uint32(table->items_ammunition_flag)) != 0;
    row->weapon = (flags & qa_js_to_uint32(table->items_weapon_flag)) != 0;
    row->label = by_name ? read_string(s, qa_compat_q2_mem_offset(memory, row->address, (int64_t)table->ammo_label)) : "";
  }
  for (size_t index = 0; index < count; index++) {
    const source_row *value = &source[index];
    if (value->item == NULL) { if (value->weapon) qa_range_error("Original weapon has no qualified classname"); continue; }
    for (size_t other = 0; other < s->item_count; other++) if (strcmp(s->items[other].item, value->item) == 0) qa_range_error("Original item table has duplicate classnames");
    qa_item_id ammo = NULL;
    if (value->weapon) {
      qa_guest_address field = qa_compat_q2_mem_offset(memory, value->address, (int64_t)table->ammo_offset);
      if (by_name) {
        const char *name = read_string(s, field);
        if (name[0] != 0) {
          const source_row *found = NULL;
          for (size_t row = 0; row < count && found == NULL; row++) if (ascii_iequal(source[row].label, name)) found = &source[row];
          if (found == NULL || found->item == NULL) qa_range_error("Original weapon ammo name has no source item");
          ammo = found->item;
        }
      } else {
        int32_t ammo_index = qa_compat_q2_mem_read_i32(memory, field);
        if (ammo_index != 0) {
          if (ammo_index < 0 || (size_t)ammo_index >= count || source[ammo_index].item == NULL) qa_range_error("Original weapon ammo index has no source item");
          ammo = source[ammo_index].item;
        }
      }
    }
    s->items[s->item_count++] = (source_item){value->item, (double)index, value->address, value->weapon, value->ammunition, value->icon, ammo};
  }
  free(source);
  s->slots = calloc(s->item_count ? s->item_count : 1, sizeof *s->slots);
  s->weapons = calloc(s->item_count ? s->item_count : 1, sizeof *s->weapons);
  if (s->slots == NULL || s->weapons == NULL) qa_range_error("Native commands out of memory");
  for (size_t i = 0; i < s->item_count; i++) {
    const source_item *v = &s->items[i];
    s->slots[i] = (qa_compat_q2_native_inventory_slot){v->item, v->index, v->weapon, v->ammunition, v->icon};
    if (v->weapon) s->weapons[s->weapon_count++] = (qa_compat_q2_native_source_weapon){v->item, v->ammo};
  }
}
static const source_item *find_item(const qa_compat_q2_native_primary_commands *s, qa_item_id item, bool weapon) {
  for (size_t i = 0; i < s->item_count; i++) if (strcmp(s->items[i].item, item) == 0 && (!weapon || s->items[i].weapon)) return &s->items[i];
  return NULL;
}
static qa_guest_call_result imported(const qa_compat_q2_native_primary_commands *s, double offset, const qa_guest_value_layout *parameters, size_t parameter_count, const qa_guest_value_layout *result, const qa_guest_call_value *values, size_t count) {
  qa_guest_address address;
  if (!qa_compat_q2_mem_read_pointer(s->host.memory, at(s, offset), &address)) qa_range_error("Original command import is null");
  qa_compat_q2_guest_call_signature signature = {s->profile->abi, parameters, parameter_count, false, *result, false};
  return s->host.invoke(s->host.ctx, address, signature, values, count);
}
/* TS arguments(): malloc'd array of malloc'd strings. */
static char **arguments(const qa_compat_q2_native_primary_commands *s, size_t *out_count) {
  qa_guest_call_result count = imported(s, s->profile->give_argc, NULL, 0, &integer_layout, NULL, 0);
  if (count.kind != QA_GUEST_VALUE_INT32 || count.number_value < 0 || count.number_value > 1024) qa_range_error("Original command argc is invalid");
  size_t length = count.number_value > 1 ? (size_t)count.number_value - 1 : 0;
  char **args = calloc(length ? length : 1, sizeof *args);
  if (args == NULL) qa_range_error("Native commands out of memory");
  for (size_t index = 0; index < length; index++) {
    qa_guest_call_value argument = qa_compat_q2_call_int32((double)index + 1);
    qa_guest_call_result value = imported(s, s->profile->give_argv, argv_parameters, 1, &pointer_layout, &argument, 1);
    if (value.kind != QA_GUEST_VALUE_POINTER || !value.pointer_value.has_value) qa_range_error("Original command argv is null");
    args[index] = qa_compat_q2_read_classic_string(s->host.memory, true, value.pointer_value.value, 65536);
  }
  *out_count = length;
  return args;
}
typedef struct { const qa_guest_call_value *values; size_t count; qa_compat_q2_native_entry *binding; qa_guest_call_result result; } original_scope;
static void original_body(void *v) { original_scope *o = v; o->result = qa_compat_q2_native_entry_call_original(o->binding, o->values, o->count); }
static qa_guest_call_result command_execute(void *v, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  command_entry *entry = v; qa_compat_q2_native_primary_commands *s = entry->s;
  qa_actor_id actor = {0}; bool has = false;
  if (count > 0 && values[0].kind == QA_GUEST_VALUE_POINTER && values[0].pointer_value.has_value)
    has = s->host.actor(s->host.ctx, s->host.record(s->host.ctx, values[0].pointer_value.value), &actor);
  stack_push(entry->stack, has, actor);
  size_t depth = entry->stack->count;
  original_scope scope = {values, count, binding, {0}};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(original_body, &scope, &thrown);
  entry->stack->count = depth - 1;
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
  return scope.result;
}
static void give_observed(void *v) {
  give_observe *o = v; qa_actor_id actor;
  if (stack_top(&o->s->giving, &actor) && current(o->s, actor)) o->s->hooks.give(o->s->hooks.ctx, actor, o->category);
}
static void unknown_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_commands *s = v; qa_actor_id actor;
  if (stack_top(&s->giving, &actor) && current(s, actor)) {
    size_t count = 0; char **args = arguments(s, &count);
    bool handled = s->hooks.give_item(s->hooks.ctx, actor, (const char *const *)args, count);
    for (size_t i = 0; i < count; i++) free(args[i]);
    free(args);
    if (handled) { continuation->skip(continuation->ctx); return; }
  }
  continuation->execute(continuation->ctx);
}
static void ammo_grant_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  ammo_region *region = v; qa_compat_q2_native_primary_commands *s = region->s; qa_actor_id actor;
  if (!stack_top(&s->giving, &actor) || !current(s, actor)) { continuation->execute(continuation->ctx); return; }
  qa_compat_q2_memory *memory = s->host.memory;
  uint64_t descriptor = s->host.runner->read_register(s->host.runner->ctx, region->grant->descriptor, memory->pointer_bytes == 4 ? 32 : 64);
  const source_item *item = NULL;
  for (size_t i = 0; i < s->item_count && item == NULL; i++) if ((uint64_t)s->items[i].address.byte_offset == descriptor) item = &s->items[i];
  qa_raw_entity_view record;
  if (item == NULL || !s->host.record_for(s->host.ctx, actor, &record)) qa_range_error("Original ammo command lacks its source item");
  qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record.address, (int64_t)s->profile->client_pointer), &client)) qa_range_error("Original ammo command lost its client");
  qa_guest_address address = qa_compat_q2_mem_offset(memory, client, (int64_t)(s->profile->client_inventory + item->index * 4));
  int32_t before = qa_compat_q2_mem_read_i32(memory, address);
  continuation->execute(continuation->ctx);
  if (current(s, actor)) {
    int32_t after = qa_compat_q2_mem_read_i32(memory, address);
    bool set = strcmp(region->grant->kind, "set") == 0;
    double amount = set ? after : (double)(int32_t)((uint32_t)after - (uint32_t)before);
    s->hooks.give_ammo(s->hooks.ctx, actor, item->item, region->grant->kind, amount);
  }
}
typedef struct { qa_guest_address address; uint8_t bytes[8]; size_t length; } saved_word;
typedef struct { qa_compat_q2_inline_continuation *continuation; } execute_scope;
static void execute_body(void *v) { execute_scope *e = v; e->continuation->execute(e->continuation->ctx); }
static void drop_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_native_primary_commands *s = v; qa_actor_id actor;
  bool has_actor = stack_top(&s->dropping, &actor);
  qa_item_id projected = NULL; double ammo_amount = 0;
  bool has_projection = has_actor && current(s, actor) && s->hooks.drop(s->hooks.ctx, actor, &projected, &ammo_amount);
  if (!has_projection) { continuation->execute(continuation->ctx); return; }
  qa_compat_q2_memory *memory = s->host.memory;
  qa_raw_entity_view record;
  if (!s->host.record_for(s->host.ctx, actor, &record)) qa_range_error("Original death drop lost its actor");
  qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record.address, (int64_t)s->profile->client_pointer), &client)) qa_range_error("Original death drop lost its client");
  const source_item *item = projected == NULL ? NULL : find_item(s, projected, true);
  if ((projected != NULL && item == NULL) || ammo_amount != trunc(ammo_amount) || ammo_amount < -2147483648.0 || ammo_amount > 2147483647.0) qa_range_error("Selected death drop lacks an original weapon or int32 ammunition");
  const source_item *ammo = item == NULL || item->ammo == NULL ? NULL : find_item(s, item->ammo, false);
  qa_guest_address weapon = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->client_weapon);
  saved_word saved[3]; size_t saved_count = 0;
  saved[saved_count].address = weapon; saved[saved_count].length = (size_t)memory->pointer_bytes;
  memory->copy(memory->ctx, weapon, saved[saved_count].bytes, (double)memory->pointer_bytes); saved_count++;
  qa_guest_address words[2]; int32_t values[2]; size_t word_count = 0;
  if (s->profile->has_client_ammo_index) { words[word_count] = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->client_ammo_index); values[word_count++] = ammo != NULL ? (int32_t)ammo->index : 0; }
  if (ammo != NULL) { words[word_count] = qa_compat_q2_mem_offset(memory, client, (int64_t)(s->profile->client_inventory + ammo->index * 4)); values[word_count++] = qa_js_to_int32(ammo_amount); }
  for (size_t i = 0; i < word_count; i++) {
    saved[saved_count].address = words[i]; saved[saved_count].length = 4;
    memory->copy(memory->ctx, words[i], saved[saved_count].bytes, 4); saved_count++;
  }
  qa_compat_q2_mem_write_pointer(memory, weapon, item != NULL, item != NULL ? item->address : (qa_guest_address){0});
  for (size_t i = 0; i < word_count; i++) qa_compat_q2_mem_write_i32(memory, words[i], values[i]);
  execute_scope scope = {continuation};
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(execute_body, &scope, &thrown);
  qa_raw_entity_view after;
  if (current(s, actor) && s->host.record_for(s->host.ctx, actor, &after) && after.address.byte_offset == record.address.byte_offset)
    for (size_t i = saved_count; i-- > 0;) memory->write(memory->ctx, saved[i].address, saved[i].bytes, saved[i].length);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
qa_compat_q2_native_primary_commands *qa_compat_q2_native_primary_commands_create(qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_command_profile *profile, qa_compat_q2_native_primary_command_hooks hooks) {
  if (strcmp(host.memory->module.digest, profile->digest) != 0) qa_range_error("Native command profile belongs to another artifact");
  qa_compat_q2_native_primary_commands *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Native commands out of memory");
  s->host = host; s->profile = profile; s->hooks = hooks;
  read_items(s);
  qa_compat_q2_guest_call_signature signature = {profile->abi, command_parameters, 1, true, {0}, false};
  const char *names[2] = {"give", "drop"}; const double entries[2] = {profile->give_entry, profile->drop_entry};
  actor_stack *stacks[2] = {&s->giving, &s->dropping};
  qa_compat_q2_runner *runner = host.runner;
  for (size_t i = 0; i < 2; i++) {
    char id[320]; snprintf(id, sizeof id, "%s:primary-%s", host.memory->module.id, names[i]);
    s->entries[i] = (command_entry){s, stacks[i]};
    s->bindings[i] = qa_compat_q2_native_entry_bind(host.memory, runner, host.ctx, host.invoke, at(s, entries[i]), id, signature, &s->entries[i], command_execute, NULL, NULL);
  }
  s->observes[0] = (give_observe){s, "weapons"}; s->observes[1] = (give_observe){s, "ammo"};
  s->observe_handles[0] = runner->observe_entry(runner->ctx, at(s, profile->give_weapons), &s->observes[0], give_observed);
  s->observe_handles[1] = runner->observe_entry(runner->ctx, at(s, profile->give_ammo), &s->observes[1], give_observed);
  s->unknown_region = runner->bind_inline_region(runner->ctx, at(s, profile->give_unknown_entry), at(s, profile->give_unknown_join), profile->abi, s, unknown_region);
  s->ammo_regions = calloc(profile->ammo_grant_count ? profile->ammo_grant_count : 1, sizeof *s->ammo_regions);
  s->ammo_ctx = calloc(profile->ammo_grant_count ? profile->ammo_grant_count : 1, sizeof *s->ammo_ctx);
  if (s->ammo_regions == NULL || s->ammo_ctx == NULL) qa_range_error("Native commands out of memory");
  for (size_t i = 0; i < profile->ammo_grant_count; i++) {
    s->ammo_ctx[i] = (ammo_region){s, &profile->ammo_grants[i]};
    s->ammo_regions[i] = runner->bind_inline_region(runner->ctx, at(s, profile->ammo_grants[i].entry), at(s, profile->ammo_grants[i].join), profile->abi, &s->ammo_ctx[i], ammo_grant_region);
  }
  s->drop_region = runner->bind_inline_region(runner->ctx, at(s, profile->drop_elig_entry), at(s, profile->drop_elig_join), profile->abi, s, drop_region);
  return s;
}
bool qa_compat_q2_native_primary_commands_source_item(const qa_compat_q2_native_primary_commands *s, qa_item_id item, qa_compat_q2_native_source_item *out) {
  const source_item *found = find_item(s, item, false);
  if (found == NULL) return false;
  out->index = found->index; out->address = found->address;
  return true;
}
qa_item_id qa_compat_q2_native_primary_commands_item_at(const qa_compat_q2_native_primary_commands *s, qa_guest_address address) {
  for (size_t i = 0; i < s->item_count; i++)
    if (s->items[i].address.address_space == address.address_space && s->items[i].address.byte_offset == address.byte_offset) return s->items[i].item;
  return NULL;
}
const qa_compat_q2_native_inventory_slot *qa_compat_q2_native_primary_commands_inventory_slots(const qa_compat_q2_native_primary_commands *s, size_t *out_count) { *out_count = s->item_count; return s->slots; }
const qa_compat_q2_native_source_weapon *qa_compat_q2_native_primary_commands_weapons(const qa_compat_q2_native_primary_commands *s, size_t *out_count) { *out_count = s->weapon_count; return s->weapons; }
void qa_compat_q2_native_primary_commands_with_weapon(qa_compat_q2_native_primary_commands *s, qa_actor_id actor, qa_item_id item, void *ctx, void (*run)(void *ctx)) {
  if (!current(s, actor)) qa_range_error("Original weapon projection lost its actor");
  if (item == NULL) { run(ctx); return; }
  const source_item *descriptor = find_item(s, item, true);
  qa_raw_entity_view record;
  if (descriptor == NULL || !s->host.record_for(s->host.ctx, actor, &record)) qa_range_error("Selected weapon has no original descriptor");
  qa_compat_q2_memory *memory = s->host.memory; qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record.address, (int64_t)s->profile->client_pointer), &client)) qa_range_error("Original weapon projection lost its client");
  qa_guest_address address = qa_compat_q2_mem_offset(memory, client, (int64_t)s->profile->client_weapon), previous = {0};
  bool has_previous = qa_compat_q2_mem_read_pointer(memory, address, &previous);
  qa_compat_q2_mem_write_pointer(memory, address, true, descriptor->address);
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(run, ctx, &thrown);
  qa_raw_entity_view after;
  if (current(s, actor) && s->host.record_for(s->host.ctx, actor, &after) && after.address.byte_offset == record.address.byte_offset)
    qa_compat_q2_mem_write_pointer(memory, address, has_previous, previous);
  if (!completed) qa_compat_q2_native_rethrow(&thrown);
}
void qa_compat_q2_native_primary_commands_close(qa_compat_q2_native_primary_commands *s) {
  if (s->closed) return;
  s->closed = true;
  qa_compat_q2_runner *runner = s->host.runner;
  runner->remove_inline_region(runner->ctx, s->drop_region);
  for (size_t i = s->profile->ammo_grant_count; i-- > 0;) runner->remove_inline_region(runner->ctx, s->ammo_regions[i]);
  runner->remove_inline_region(runner->ctx, s->unknown_region);
  runner->remove_entry(runner->ctx, s->observe_handles[1]);
  runner->remove_entry(runner->ctx, s->observe_handles[0]);
  qa_compat_q2_native_entry_close(s->bindings[1]);
  qa_compat_q2_native_entry_close(s->bindings[0]);
}
void qa_compat_q2_native_primary_commands_destroy(qa_compat_q2_native_primary_commands *s) {
  if (s == NULL) return;
  qa_compat_q2_native_primary_commands_close(s);
  for (size_t i = 0; i < s->string_count; i++) free(s->strings[i]);
  free(s->strings); free(s->items); free(s->slots); free(s->weapons); free(s->ammo_regions); free(s->ammo_ctx);
  free(s->giving.items); free(s->dropping.items); free(s);
}
