/* Port of src/compat/q2/rerelease/combat-binding.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/combat_binding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/public_state.h"
#include "qa/compat/q2/rerelease/semantics.h"
#include "qa/compat/q2/rerelease/source_state.h"
#include "qa/foundation/js.h"

typedef struct bind_ctx bind_ctx_t;
struct qa_compat_q2_rerelease_combat_bindings {
  qa_compat_q2_rerelease_q2_guest_host *host;
  const qa_compat_q2_rerelease_inventory_item *items; size_t item_count; bool has_items;
  bind_ctx_t **binds; size_t bind_count, bind_cap;
};
struct bind_ctx {
  qa_compat_q2_rerelease_combat_bindings *owner;
  qa_raw_entity_view view;
  qa_compat_q2_rerelease_source_edict *source;
  qa_compat_q2_rerelease_public_edict *public_state;
  qa_combat_state_binding source_binding;
  char team_buffer[64];
};
static qa_compat_q2_rerelease_guest_module *bmod(qa_compat_q2_rerelease_combat_bindings *b) {
  return qa_compat_q2_rerelease_q2_guest_host_get_module(b->host);
}
static qa_guest_address core_string(void *ctx, const char *text) {
  return qa_compat_q2_rerelease_core_imports_string(ctx, text);
}
static const qa_compat_q2_rerelease_inventory_item *items_of(qa_compat_q2_rerelease_combat_bindings *b, size_t *count) {
  if (!b->has_items) {
    b->items = qa_compat_q2_rerelease_inventory_items(bmod(b), qa_compat_q2_rerelease_q2_guest_host_get_core(b->host),
                                                      core_string, &b->item_count);
    b->has_items = true;
  }
  *count = b->item_count;
  return b->items;
}
static qa_compat_q2_rerelease_source_client *client_of(bind_ctx_t *x) {
  qa_guest_address address;
  if (!qa_compat_q2_rerelease_source_edict_client(x->source, &address)) return NULL;
  qa_compat_q2_rerelease_guest_module *module = bmod(x->owner);
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  qa_compat_q2_rerelease_source_client *c = qa_compat_q2_rerelease_source_client_create(address, module, &world->client);
  if (!c) qa_range_error("Q2 rerelease combat binding out of memory");
  return c;
}
static qa_guest_address item_address(bind_ctx_t *x, qa_compat_q2_rerelease_source_client *value, qa_item_id item) {
  size_t count; const qa_compat_q2_rerelease_inventory_item *items = items_of(x->owner, &count);
  for (size_t i = 0; i < count; i++) {
    if (items[i].item && item && strcmp(items[i].item, item) == 0) {
      qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(bmod(x->owner));
      return qa_compat_q2_mem_offset(memory, qa_compat_q2_rerelease_source_client_at(value, "pers.inventory"),
                                     (int64_t)(items[i].source_index * 4));
    }
  }
  char message[512];
  snprintf(message, sizeof message, "Native armor item is absent: %s", item ? item : "");
  qa_range_error(message);
}
static int32_t item_count(bind_ctx_t *x, qa_compat_q2_rerelease_source_client *value, qa_item_id item) {
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(bmod(x->owner));
  return qa_compat_q2_mem_read_i32(memory, item_address(x, value, item));
}
static double protection_of(bind_ctx_t *x, qa_item_id item, double offset) {
  size_t count; const qa_compat_q2_rerelease_inventory_item *items = items_of(x->owner, &count);
  bool found = false; double index = 0;
  for (size_t i = 0; i < count; i++)
    if (items[i].item && item && strcmp(items[i].item, item) == 0) { found = true; index = items[i].source_index; break; }
  if (!found) qa_range_error("Native armor item is absent");
  qa_compat_q2_rerelease_guest_module *module = bmod(x->owner);
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  qa_compat_q2_rerelease_foreign_actors *damage = qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(x->owner->host);
  qa_compat_q2_rerelease_native_entries entries = qa_compat_q2_rerelease_foreign_actors_get_entries(damage);
  qa_guest_address info;
  if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, entries.armor_info_table, (int64_t)(index * world->armor.stride)), &info))
    return 0;
  return qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, info, (int64_t)offset));
}
static qa_armor_state armor_none(void) {
  qa_armor_state s; memset(&s, 0, sizeof s);
  s.regular.kind = QA_REGULAR_ARMOR_NONE; s.powered.kind = "none";
  return s;
}
static qa_armor_state armor_of(bind_ctx_t *x) {
  qa_compat_q2_rerelease_source_client *value = client_of(x);
  if (!value) return armor_none();
  qa_compat_q2_rerelease_guest_module *module = bmod(x->owner);
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  qa_item_id regular = NULL;
  for (size_t i = 0; i < world->armor.regular_count; i++)
    if (item_count(x, value, world->armor.regular[i]) > 0) { regular = world->armor.regular[i]; break; }
  bool powered = (qa_compat_q2_mem_read_u64(memory, qa_compat_q2_rerelease_source_edict_at(x->source, "flags")) &
                  (uint64_t)world->flags.power_armor) != 0;
  const char *power = NULL;
  if (powered) {
    if (item_count(x, value, world->armor.shield) > 0) power = "shield";
    else if (item_count(x, value, world->armor.screen) > 0) power = "screen";
  }
  qa_armor_state out = armor_none();
  if (regular) {
    out.regular.kind = QA_REGULAR_ARMOR_Q2;
    out.regular.points = item_count(x, value, regular);
    out.regular.item = regular;
    out.regular.normal_protection = protection_of(x, regular, world->armor.normal);
    out.regular.energy_protection = protection_of(x, regular, world->armor.energy);
  }
  if (power) {
    out.powered.kind = power;
    out.powered.cells = item_count(x, value, world->armor.cells);
  }
  qa_compat_q2_rerelease_source_client_destroy(value);
  return out;
}
static qa_armor_state armor_cb(void *v) { return armor_of(v); }
static void validate_armor(bind_ctx_t *x, qa_armor_state state) {
  if (state.regular.kind != QA_REGULAR_ARMOR_NONE && state.regular.kind != QA_REGULAR_ARMOR_Q2)
    qa_range_error("Native Q2 armor cannot store another game's armor record");
  const char *current = armor_of(x).powered.kind;
  if (!current || !state.powered.kind || strcmp(state.powered.kind, current) != 0)
    qa_range_error("Native power activation requires its original source equipment operation");
}
static void write_armor(bind_ctx_t *x, qa_armor_state state) {
  validate_armor(x, state);
  qa_compat_q2_rerelease_source_client *value = client_of(x);
  qa_regular_armor_state regular = state.regular;
  if (!value) {
    if (regular.kind != QA_REGULAR_ARMOR_NONE || !state.powered.kind || strcmp(state.powered.kind, "none") != 0)
      qa_range_error("Native non-client armor requires its own source declaration");
    return;
  }
  qa_compat_q2_rerelease_guest_module *module = bmod(x->owner);
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  if (regular.kind != QA_REGULAR_ARMOR_NONE && regular.kind != QA_REGULAR_ARMOR_Q2)
    qa_range_error("Native Q2 armor cannot store another game's armor record");
  if (regular.kind == QA_REGULAR_ARMOR_Q2 && regular.points != 0) {
    bool known = false;
    for (size_t i = 0; i < world->armor.regular_count; i++)
      if (regular.item && world->armor.regular[i] && strcmp(regular.item, world->armor.regular[i]) == 0) known = true;
    if (!known) qa_range_error("Unknown native armor item");
  }
  qa_regular_armor_state current = armor_of(x).regular;
  for (size_t i = 0; i < world->armor.regular_count; i++) {
    qa_item_id item = world->armor.regular[i];
    if (regular.kind == QA_REGULAR_ARMOR_Q2 && current.kind == QA_REGULAR_ARMOR_Q2 &&
        regular.item && current.item && strcmp(regular.item, current.item) == 0 &&
        (!item || !current.item || strcmp(item, current.item) != 0)) continue;
    qa_guest_address address = item_address(x, value, item);
    double points = (regular.kind == QA_REGULAR_ARMOR_Q2 && regular.item && item && strcmp(regular.item, item) == 0)
                         ? regular.points : 0;
    /* readInt32 !== points compares numbers; writeInt32 applies ToInt32. */
    if ((double)qa_compat_q2_mem_read_i32(memory, address) != points) qa_compat_q2_mem_write_i32(memory, address, qa_js_to_int32(points));
  }
  if (!state.powered.kind || strcmp(state.powered.kind, "none") != 0)
    qa_compat_q2_mem_write_i32(memory, item_address(x, value, world->armor.cells), qa_js_to_int32(state.powered.cells));
  qa_compat_q2_rerelease_source_client_destroy(value);
}
static void write_armor_cb(void *v, qa_armor_state state) { write_armor(v, state); }
static qa_compat_q2_rerelease_protection_traits traits_of(bind_ctx_t *x) {
  qa_compat_q2_rerelease_guest_module *module = bmod(x->owner);
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  qa_compat_q2_rerelease_foreign_actors *damage = qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(x->owner->host);
  qa_compat_q2_rerelease_source_client *value = client_of(x);
  uint64_t flags = qa_compat_q2_mem_read_u64(memory, qa_compat_q2_rerelease_source_edict_at(x->source, "flags"));
  bool has_team_field = false;
  for (size_t i = 0; i < world->client.layout.field_count; i++)
    if (strcmp(world->client.layout.fields[i].name, "resp.ctf_team") == 0) has_team_field = true;
  int64_t team = 0;
  if (value) {
    team = has_team_field ? qa_compat_q2_mem_read_i32(memory, qa_compat_q2_rerelease_source_client_at(value, "resp.ctf_team"))
                          : (int64_t)qa_compat_q2_rerelease_public_edict_player_state(x->public_state).team_id;
  }
  int64_t invincible_until;
  if (value) invincible_until = qa_compat_q2_rerelease_source_client_invincible_until_milliseconds(value);
  else if (((uint64_t)qa_compat_q2_rerelease_public_edict_uint(x->public_state, "svflags") & 4) != 0)
    invincible_until = qa_compat_q2_mem_read_i64(memory, qa_compat_q2_mem_offset(memory, x->view.address, (int64_t)world->monster.invincible_time));
  else invincible_until = 0;
  if (value) qa_compat_q2_rerelease_source_client_destroy(value);
  qa_compat_q2_rerelease_protection_traits out;
  out.invulnerable = (flags & (uint64_t)world->flags.godmode) != 0 ||
                     invincible_until > qa_compat_q2_mem_read_i64(memory, qa_compat_q2_rerelease_foreign_actors_get_entries(damage).time);
  if (team > 0) { snprintf(x->team_buffer, sizeof x->team_buffer, "q2:%lld", (long long)team); out.team = x->team_buffer; }
  else out.team = NULL;
  out.no_knockback = (flags & (uint64_t)world->flags.no_knockback) != 0;
  return out;
}
static qa_compat_q2_rerelease_protection_traits traits_cb(void *v) { return traits_of(v); }
static qa_combat_state binding_read(void *v) { bind_ctx_t *x = v; return x->source_binding.read(x->source_binding.ctx); }
static void binding_write_health(void *v, double health) { bind_ctx_t *x = v; x->source_binding.write_health(x->source_binding.ctx, health); }
static void binding_write_armor(void *v, qa_armor_state armor) { bind_ctx_t *x = v; x->source_binding.write_armor(x->source_binding.ctx, armor); }
static void binding_validate(void *v, qa_armor_state armor) { validate_armor(v, armor); }
static qa_regular_armor_state binding_empty(void *v, double points) {
  bind_ctx_t *x = v;
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(bmod(x->owner));
  qa_regular_armor_state out; memset(&out, 0, sizeof out);
  out.kind = QA_REGULAR_ARMOR_Q2; out.item = world->armor.empty; out.points = points;
  out.normal_protection = protection_of(x, world->armor.empty, world->armor.normal);
  out.energy_protection = protection_of(x, world->armor.empty, world->armor.energy);
  return out;
}
static qa_armor_state binding_normalize(void *v, qa_armor_state legacy) {
  bind_ctx_t *x = v;
  return qa_normalize_legacy_power_only_armor(legacy, armor_of(x), "q2:none");
}
static qa_damage_outcome binding_source_damage(void *v, qa_damage_request request) {
  bind_ctx_t *x = v;
  qa_compat_q2_rerelease_foreign_actors *damage = qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(x->owner->host);
  return qa_compat_q2_rerelease_foreign_actors_damage_native(damage, request, NULL, 0);
}
qa_compat_q2_rerelease_combat_bindings *qa_compat_q2_rerelease_combat_bindings_create(qa_compat_q2_rerelease_q2_guest_host *host) {
  qa_compat_q2_rerelease_combat_bindings *b = calloc(1, sizeof *b);
  if (!b) return NULL;
  b->host = host;
  return b;
}
void qa_compat_q2_rerelease_combat_bindings_destroy(qa_compat_q2_rerelease_combat_bindings *b) {
  if (!b) return;
  for (size_t i = 0; i < b->bind_count; i++) {
    qa_compat_q2_rerelease_public_edict_destroy(b->binds[i]->public_state);
    qa_compat_q2_rerelease_source_edict_destroy(b->binds[i]->source);
    free(b->binds[i]);
  }
  free(b->binds); free(b);
}
qa_compat_q2_rerelease_q2_guest_host *qa_compat_q2_rerelease_combat_bindings_get_host(const qa_compat_q2_rerelease_combat_bindings *b) {
  return b->host;
}
bool qa_compat_q2_rerelease_combat_bindings_notarget(qa_compat_q2_rerelease_combat_bindings *b, qa_raw_entity_view view, bool *out) {
  qa_compat_q2_rerelease_guest_module *module = bmod(b);
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  if (!world) return false;
  qa_compat_q2_rerelease_source_edict *source = qa_compat_q2_rerelease_source_edict_create(view, module);
  if (!source) qa_range_error("Q2 rerelease combat binding out of memory");
  const qa_compat_q2_memory * m = qa_compat_q2_rerelease_guest_module_get_memory(module);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(source, "flags");
  uint64_t flags = qa_compat_q2_mem_read_u64(m, a);
  qa_compat_q2_rerelease_source_edict_destroy(source);
  *out = (flags & (uint64_t)world->flags.notarget) != 0;
  return true;
}
bool qa_compat_q2_rerelease_combat_bindings_bind(qa_compat_q2_rerelease_combat_bindings *b, qa_raw_entity_view view, qa_combat_state_binding *out) {
  qa_compat_q2_rerelease_guest_module *module = bmod(b);
  const qa_compat_q2_rerelease_primary_world_profile *world = qa_compat_q2_rerelease_guest_module_get_world_profile(module);
  qa_compat_q2_rerelease_foreign_actors *damage = qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(b->host);
  if (!world || !damage) return false;
  bind_ctx_t *x = calloc(1, sizeof *x);
  if (!x) qa_range_error("Q2 rerelease combat binding out of memory");
  x->owner = b; x->view = view;
  x->source = qa_compat_q2_rerelease_source_edict_create(view, module);
  x->public_state = qa_compat_q2_rerelease_public_edict_create(qa_compat_q2_rerelease_guest_module_get_memory(module), view);
  if (!x->source || !x->public_state) qa_range_error("Q2 rerelease combat binding out of memory");
  if (b->bind_count == b->bind_cap) {
    b->bind_cap = b->bind_cap ? b->bind_cap * 2 : 8;
    b->binds = realloc(b->binds, b->bind_cap * sizeof *b->binds);
    if (!b->binds) qa_range_error("Q2 rerelease combat binding out of memory");
  }
  b->binds[b->bind_count++] = x;
  qa_compat_q2_rerelease_source_protection protection = {x, armor_cb, write_armor_cb, traits_cb};
  x->source_binding = qa_compat_q2_rerelease_source_edict_combat(x->source, protection);
  memset(out, 0, sizeof *out);
  out->ctx = x;
  out->read = binding_read;
  out->write_health = binding_write_health;
  out->write_armor = binding_write_armor;
  out->validate_armor = binding_validate;
  out->empty_regular_armor = binding_empty;
  out->normalize_legacy_armor = binding_normalize;
  out->source_damage = binding_source_damage;
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  out->protection_regular.declared = true; out->protection_regular.owner_is_null = false;
  out->protection_regular.owner = memory->module.id; out->protection_regular.has_stage = true;
  out->protection_regular.stage = qa_compat_q2_rerelease_foreign_actors_armor_stage(damage, view, x, armor_cb, QA_PROTECTION_REGULAR);
  out->protection_powered.declared = true; out->protection_powered.owner_is_null = false;
  out->protection_powered.owner = memory->module.id; out->protection_powered.has_stage = true;
  out->protection_powered.stage = qa_compat_q2_rerelease_foreign_actors_armor_stage(damage, view, x, armor_cb, QA_PROTECTION_POWERED);
  return true;
}
