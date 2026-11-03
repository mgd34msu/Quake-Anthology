/* Port of src/compat/q2/rerelease/weapon-behavior-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/weapon_behavior_profile.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/api.h"
#include "qa/compat/q2/rerelease/imports.h"
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/compat/q2/rerelease/q2eaks_weapon_profile.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool read_declaration(const qa_native_weapon_behavior_declaration *declaration, const qa_module_identity *module,
                             qa_native_weapon_behavior_declaration *out) {
  char *json = qa_compat_q2_serialize_native_weapon_declaration(declaration);
  if (!json) return false;
  qa_save_value *tree = qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  free(json);
  if (!tree) return false;
  bool ok = qa_compat_q2_read_native_weapon_declaration(tree, module, out);
  qa_save_value_free(tree);
  return ok;
}
bool qa_compat_q2_built_in_rerelease_weapon_declaration(qa_module_identity module, qa_native_weapon_behavior_declaration *out) {
  if (strcmp(module.digest, qa_compat_q2_q2eaks_weapon_digest) != 0) return false;
  qa_native_weapon_behavior_declaration built;
  if (!qa_compat_q2_q2eaks_weapon_declaration(module.artifact_path, &built)) return false;
  bool ok = read_declaration(&built, &module, out);
  qa_compat_q2_native_weapon_declaration_free(&built);
  return ok;
}
static qa_native_weapon_behavior_declaration retained;
static bool retained_valid = false;
static bool definition_from(qa_module_identity module, const qa_native_weapon_behavior_declaration *profile,
                            qa_weapon_behavior_definition *out) {
  memset(out, 0, sizeof *out);
  out->id = profile->id; out->title = profile->title; out->module = module; out->role = profile->role; out->aspect = profile->aspect;
  out->has_activate = profile->has_activate_rva;
  if (profile->has_activate_rva) {
    out->activate.kind = QA_WEAPON_CALLBACK_NATIVE_ARTIFACT; out->activate.module = module;
    out->activate.image_offset = (int64_t)profile->activate_rva; out->activate.abi = qa_compat_q2_rerelease_abi;
  }
  out->fire.kind = QA_WEAPON_CALLBACK_NATIVE_ARTIFACT; out->fire.module = module;
  out->fire.image_offset = (int64_t)profile->fire_rva; out->fire.abi = qa_compat_q2_rerelease_abi;
  return true;
}
bool qa_compat_q2_rerelease_weapon_definition(qa_module_identity module, const qa_native_weapon_behavior_declaration *declaration,
                                              qa_weapon_behavior_definition *out) {
  qa_native_weapon_behavior_declaration parsed;
  bool has;
  if (!declaration) has = qa_compat_q2_built_in_rerelease_weapon_declaration(module, &parsed);
  else has = read_declaration(declaration, &module, &parsed);
  if (!has) return false;
  if (retained_valid) qa_compat_q2_native_weapon_declaration_free(&retained);
  retained = parsed; retained_valid = true;
  return definition_from(module, &retained, out);
}
char *qa_compat_q2_rerelease_weapon_initialization_entities(const char *text, qa_string_view_list initialization_classes) {
  size_t count = 0;
  qa_q1_entity *entities = qa_q1_parse_entities(text, &count);
  size_t worldspawn = 0;
  for (size_t i = 0; i < count; i++) {
    const char *classname = qa_q1_entity_value(&entities[i], "classname");
    bool included = false;
    for (size_t j = 0; j < initialization_classes.count; j++)
      if (classname && strcmp(classname, initialization_classes.values[j]) == 0) included = true;
    if (included && classname && strcmp(classname, "worldspawn") == 0) worldspawn++;
  }
  if (worldspawn != 1) { qa_q1_entities_destroy(entities, count); qa_range_error("Native component initialization requires one authored worldspawn"); }
  size_t cap = 1024, len = 0;
  char *out = malloc(cap);
  if (!out) { qa_q1_entities_destroy(entities, count); return NULL; }
  out[0] = 0;
  for (size_t i = 0; i < count; i++) {
    const char *classname = qa_q1_entity_value(&entities[i], "classname");
    bool included = false;
    for (size_t j = 0; j < initialization_classes.count; j++)
      if (classname && strcmp(classname, initialization_classes.values[j]) == 0) included = true;
    if (!included) continue;
    size_t need = 4;
    for (size_t j = 0; j < entities[i].property_count; j++) {
      const char *key = entities[i].properties[j].key, *value = entities[i].properties[j].value;
      if (strchr(key, '"') || strchr(value, '"')) { free(out); qa_q1_entities_destroy(entities, count); qa_range_error("Native component entity field cannot be quoted"); }
      need += strlen(key) + strlen(value) + 6;
    }
    if (len + need + 1 > cap) { while (len + need + 1 > cap) cap *= 2; out = realloc(out, cap); if (!out) { qa_q1_entities_destroy(entities, count); return NULL; } }
    len += (size_t)snprintf(out + len, cap - len, "{\n");
    for (size_t j = 0; j < entities[i].property_count; j++) {
      if (j) len += (size_t)snprintf(out + len, cap - len, "\n");
      len += (size_t)snprintf(out + len, cap - len, "\"%s\" \"%s\"", entities[i].properties[j].key, entities[i].properties[j].value);
    }
    len += (size_t)snprintf(out + len, cap - len, "\n}\n");
  }
  qa_q1_entities_destroy(entities, count);
  return out;
}
static bool value_equal(const qa_save_value *a, const qa_save_value *b) {
  if (!a || !b) return a == b;
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_SAVE_BOOLEAN: return a->boolean == b->boolean;
    case QA_SAVE_NUMBER: return a->number == b->number;
    case QA_SAVE_STRING: case QA_SAVE_BIGINT: return strcmp(a->text ? a->text : "", b->text ? b->text : "") == 0;
    case QA_SAVE_BYTES: return a->byte_length == b->byte_length && (a->byte_length == 0 || memcmp(a->bytes, b->bytes, a->byte_length) == 0);
    case QA_SAVE_ARRAY:
      if (a->item_count != b->item_count) return false;
      for (size_t i = 0; i < a->item_count; i++) if (!value_equal(a->items[i], b->items[i])) return false;
      return true;
    case QA_SAVE_OBJECT:
      if (a->entry_count != b->entry_count) return false;
      for (size_t i = 0; i < a->entry_count; i++)
        if (strcmp(a->entries[i].key, b->entries[i].key) != 0 || !value_equal(a->entries[i].value, b->entries[i].value)) return false;
      return true;
    default: return true;
  }
}
static bool independent_equal(const qa_save_value *left, const qa_save_value *right, const qa_native_weapon_cvar *names, size_t count) {
  const qa_save_value *lv = qa_save_value_get(left, "variables"), *rv = qa_save_value_get(right, "variables");
  if (!lv || !rv || lv->kind != QA_SAVE_ARRAY || rv->kind != QA_SAVE_ARRAY) return value_equal(left, right);
  size_t li = 0, ri = 0;
  for (;;) {
    while (li < lv->item_count) {
      const qa_save_value *v = lv->items[li];
      const qa_save_value *nm = v && v->kind == QA_SAVE_OBJECT ? qa_save_value_get(v, "name") : NULL;
      bool skip = !v || v->kind == QA_SAVE_NULL;
      if (!skip && nm && nm->kind == QA_SAVE_STRING) for (size_t j = 0; j < count; j++) if (strcmp(nm->text, names[j].name) == 0) skip = true;
      if (!skip) break;
      li++;
    }
    while (ri < rv->item_count) {
      const qa_save_value *v = rv->items[ri];
      const qa_save_value *nm = v && v->kind == QA_SAVE_OBJECT ? qa_save_value_get(v, "name") : NULL;
      bool skip = !v || v->kind == QA_SAVE_NULL;
      if (!skip && nm && nm->kind == QA_SAVE_STRING) for (size_t j = 0; j < count; j++) if (strcmp(nm->text, names[j].name) == 0) skip = true;
      if (!skip) break;
      ri++;
    }
    if (li >= lv->item_count || ri >= rv->item_count) return li >= lv->item_count && ri >= rv->item_count;
    if (!value_equal(lv->items[li], rv->items[ri])) return false;
    li++; ri++;
  }
}
typedef struct provisioning_scope {
  qa_cvar_registry *cvars;
  qa_save_value *saved;
  void *ctx;
  void (*refresh)(void *ctx);
} provisioning_scope;
/* The TS `finally`: restore the captured cvars and refresh, on return and on throw. */
static void provisioning_finally(void *arg) {
  provisioning_scope *scope = arg;
  qa_cvar_registry_restore_save_state(scope->cvars, scope->saved);
  qa_save_value *saved = scope->saved;
  scope->saved = NULL;
  scope->refresh(scope->ctx);
  qa_save_value_free(saved);
}
void *qa_compat_q2_with_rerelease_weapon_provisioning(const qa_native_weapon_cvar *provisioning_cvars, size_t count,
                                                      qa_cvar_registry *cvars, void *ctx, void (*refresh)(void *ctx),
                                                      void *(*run)(void *ctx)) {
  provisioning_scope scope = {cvars, qa_cvar_registry_capture_world_transfer_state(cvars), ctx, refresh};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, provisioning_finally, &scope);
  for (size_t i = 0; i < count; i++) {
    qa_cvar_snapshot snap; memset(&snap, 0, sizeof snap);
    if (!qa_cvar_registry_get(cvars, provisioning_cvars[i].name, &snap))
      qa_raise("Error", 0, 0, "Native source has not registered its provisioning capability");
    qa_cvar_read_free(&snap);
    memset(&snap, 0, sizeof snap);
    if (qa_cvar_registry_set(cvars, provisioning_cvars[i].name, provisioning_cvars[i].value, true, &snap)) qa_cvar_read_free(&snap);
  }
  refresh(ctx);
  void *result = run(ctx);
  qa_save_value *current = qa_cvar_registry_capture_world_transfer_state(cvars);
  bool changed = !independent_equal(current, scope.saved, provisioning_cvars, count);
  qa_save_value_free(current);
  if (changed) qa_raise("Error", 0, 0, "Native provisioning changed an undeclared source cvar");
  qa_cleanup_pop(&cleanup);
  provisioning_finally(&scope);
  return result;
}
typedef struct bind_ctx {
  qa_compat_q2_rerelease_guest_module *module;
  qa_native_weapon_behavior_declaration profile;
  qa_guest_address *equip; size_t equip_count;
  qa_guest_address *launch; size_t launch_count;
  qa_guest_address allocate, freed, touch, weapon_think, time;
  qa_compat_q2_guest_call_signature think_signature, allocate_signature;
} bind_ctx_t;
static qa_compat_q2_memory *bmem(bind_ctx_t *b) { return qa_compat_q2_rerelease_guest_module_get_memory(b->module); }
static qa_guest_address record_address(bind_ctx_t *b, qa_raw_entity_view record) {
  qa_compat_q2_memory *memory = bmem(b);
  if (strcmp(record.module.digest, memory->module.digest) != 0 || strcmp(record.module.id, memory->module.id) != 0 ||
      record.address.address_space != memory->address_space || b->profile.entity.byte_length > record.stride_bytes)
    qa_range_error("Native trajectory record differs from declared source layout");
  return record.address;
}
static qa_guest_address at(bind_ctx_t *b, qa_raw_entity_view record, double offset) {
  return qa_compat_q2_mem_offset(bmem(b), record_address(b, record), (int64_t)offset);
}
static qa_vec3 read_vector(bind_ctx_t *b, qa_guest_address address) {
  qa_compat_q2_memory *m = bmem(b);
  double x = qa_compat_q2_mem_read_f32(m, address);
  double y = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 4));
  double z = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 8));
  return (qa_vec3){x, y, z};
}
static void write_vector(bind_ctx_t *b, qa_guest_address address, qa_vec3 value) {
  qa_compat_q2_memory *m = bmem(b);
  qa_compat_q2_mem_write_f32(m, address, value.x);
  qa_compat_q2_mem_write_f32(m, qa_compat_q2_mem_offset(m, address, 4), value.y);
  qa_compat_q2_mem_write_f32(m, qa_compat_q2_mem_offset(m, address, 8), value.z);
}
static qa_guest_address client_field(bind_ctx_t *b, qa_raw_entity_view record, double offset) {
  qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(bmem(b), at(b, record, b->profile.entity.client), &client))
    qa_range_error("Native weapon context requires an admitted source client");
  bmem(b)->check(bmem(b)->ctx, client, b->profile.client.byte_length, "read");
  return qa_compat_q2_mem_offset(bmem(b), client, (int64_t)offset);
}
static void do_project(bind_ctx_t *b, qa_raw_entity_view record, qa_body_state body) {
  qa_compat_q2_rerelease_layouts_ensure();
  write_vector(b, at(b, record, b->profile.entity.origin), body.origin);
  write_vector(b, at(b, record, b->profile.entity.angles), body.angles);
  write_vector(b, at(b, record, b->profile.entity.velocity), body.velocity);
  write_vector(b, at(b, record, qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "mins")), body.bounds.min);
  write_vector(b, at(b, record, qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "maxs")), body.bounds.max);
}
static void invoke_record(bind_ctx_t *b, qa_guest_address address, qa_raw_entity_view record) {
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_pointer(true, record_address(b, record));
  (void)qa_compat_q2_rerelease_guest_module_invoke(b->module, address, b->think_signature, &arg, 1, &record, NULL);
}
static void p_equip(void *v, qa_raw_entity_view record) {
  bind_ctx_t *b = v;
  for (size_t i = 0; i < b->equip_count; i++) invoke_record(b, b->equip[i], record);
  qa_guest_address weapon;
  const qa_compat_q2_memory * m = bmem(b);
  qa_guest_address a = client_field(b, record, b->profile.client.weapon);
  if (!qa_compat_q2_mem_read_pointer(m, a, &weapon))
    qa_range_error("Native source did not equip the artifact-qualified weapon");
  bmem(b)->check(bmem(b)->ctx, weapon, b->profile.equipped_weapon.byte_length, "read");
  qa_guest_address callback;
  bool has = qa_compat_q2_mem_read_pointer(bmem(b), qa_compat_q2_mem_offset(bmem(b), weapon, (int64_t)b->profile.equipped_weapon.callback), &callback);
  if (!has || callback.byte_offset != b->weapon_think.byte_offset)
    qa_range_error("Native source did not equip the artifact-qualified weapon");
}
static void p_launch(void *v, qa_raw_entity_view record) {
  bind_ctx_t *b = v;
  for (size_t i = 0; i < b->launch_count; i++) invoke_record(b, b->launch[i], record);
}
static bool p_matches(void *v, qa_raw_entity_view record, qa_raw_entity_view shooter) {
  bind_ctx_t *b = v;
  qa_guest_address owner, touch;
  bool has_owner = qa_compat_q2_mem_read_pointer(bmem(b), at(b, record, b->profile.entity.owner), &owner);
  bool has_touch = qa_compat_q2_mem_read_pointer(bmem(b), at(b, record, b->profile.entity.touch_callback), &touch);
  return has_owner && owner.byte_offset == record_address(b, shooter).byte_offset && has_touch && touch.byte_offset == b->touch.byte_offset;
}
static void p_project_shooter(void *v, qa_raw_entity_view record, qa_compat_q2_rerelease_weapon_shooter shooter) {
  bind_ctx_t *b = v;
  do_project(b, record, shooter.body);
  write_vector(b, client_field(b, record, b->profile.client.view_angles), shooter.view_angles);
  double pitch = shooter.view_angles.x * M_PI / 180, yaw = shooter.view_angles.y * M_PI / 180;
  write_vector(b, client_field(b, record, b->profile.client.forward),
               (qa_vec3){cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), -sin(pitch)});
  qa_compat_q2_mem_write_i32(bmem(b), at(b, record, b->profile.entity.view_height), qa_js_to_int32(shooter.view_height));
}
static void p_project(void *v, qa_raw_entity_view record, qa_body_state body) { do_project(v, record, body); }
static qa_weapon_trajectory_update p_trajectory(void *v, qa_raw_entity_view record) {
  bind_ctx_t *b = v;
  qa_weapon_trajectory_update out;
  out.origin = read_vector(b, at(b, record, b->profile.entity.origin));
  out.angles = read_vector(b, at(b, record, b->profile.entity.angles));
  out.velocity = read_vector(b, at(b, record, b->profile.entity.velocity));
  return out;
}
static double p_generation(void *v, qa_raw_entity_view record) { bind_ctx_t *b = v; return qa_compat_q2_mem_read_i32(bmem(b), at(b, record, b->profile.entity.generation)); }
static void p_time(void *v, int64_t milliseconds) { bind_ctx_t *b = v; qa_compat_q2_mem_write_i64(bmem(b), b->time, milliseconds); }
static int64_t p_next_think(void *v, qa_raw_entity_view record) { bind_ctx_t *b = v; return qa_compat_q2_mem_read_i64(bmem(b), at(b, record, b->profile.entity.next_think)); }
static void p_think(void *v, qa_raw_entity_view record) {
  bind_ctx_t *b = v;
  qa_compat_q2_memory *memory = bmem(b);
  qa_guest_address callback, registration;
  bool has_callback = qa_compat_q2_mem_read_pointer(memory, at(b, record, b->profile.entity.think_callback), &callback);
  bool has_registration = qa_compat_q2_mem_read_pointer(memory, at(b, record, b->profile.entity.think_registration), &registration);
  if (!has_callback) qa_range_error("Native projectile has a due think with no callback");
  qa_native_weapon_registration_layout layout = b->profile.think.registration;
  if (!has_registration) qa_range_error("Native projectile think lacks its typed source save registration");
  memory->check(memory->ctx, registration, layout.byte_length, "read");
  memory->check(memory->ctx, callback, 1, "execute");
  qa_guest_address registered_callback;
  bool has_registered = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, registration, (int64_t)layout.callback), &registered_callback);
  if (qa_compat_q2_mem_read_u32(memory, qa_compat_q2_mem_offset(memory, registration, (int64_t)layout.tag)) != (uint32_t)b->profile.think.tag ||
      !has_registered || registered_callback.byte_offset != callback.byte_offset)
    qa_range_error("Native projectile think lacks its typed source save registration");
  qa_compat_q2_mem_write_i64(memory, at(b, record, b->profile.entity.next_think), 0);
  invoke_record(b, callback, record);
}
static qa_raw_entity_view p_allocate(void *v) {
  bind_ctx_t *b = v;
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(b->module, b->allocate, b->allocate_signature, NULL, 0, NULL, NULL);
  qa_guest_address address;
  if (!qa_compat_q2_rerelease_result_pointer(result, &address)) qa_range_error("Native source entity allocation returned null");
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(b->module);
  qa_raw_entity_view record = table.from_pointer(table.ctx, address);
  (void)record_address(b, record);
  return record;
}
static void p_free_record(void *v, qa_raw_entity_view record) {
  bind_ctx_t *b = v;
  qa_compat_q2_rerelease_layouts_ensure();
  const qa_compat_q2_memory * m = bmem(b);
  qa_guest_address a = at(b, record, qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, "inuse"));
  if (qa_compat_q2_mem_read_u8(m, a))
    invoke_record(b, b->freed, record);
}
static qa_guest_address resolve_entry(bind_ctx_t *b, qa_guest_address image_base, qa_native_weapon_entry value) {
  qa_compat_q2_memory *memory = bmem(b);
  qa_guest_address address = qa_compat_q2_mem_offset(memory, image_base, (int64_t)value.rva);
  memory->check(memory->ctx, address, 1, "execute");
  if (value.registration) {
    const qa_native_weapon_registration *declared = value.registration;
    qa_native_weapon_registration_layout layout = declared->layout;
    qa_guest_address record = qa_compat_q2_mem_offset(memory, image_base, (int64_t)declared->rva);
    memory->check(memory->ctx, record, layout.byte_length, "read");
    qa_guest_address name, callback;
    bool has_name = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record, (int64_t)layout.name), &name);
    bool has_callback = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, record, (int64_t)layout.callback), &callback);
    bool differs = !has_name;
    if (!differs) {
      char *actual = qa_compat_q2_read_guest_string(memory, name, (double)strlen(declared->name) + 1);
      differs = strcmp(actual, declared->name) != 0;
      free(actual);
    }
    if (!differs) differs = qa_compat_q2_mem_read_u32(memory, qa_compat_q2_mem_offset(memory, record, (int64_t)layout.tag)) != (uint32_t)declared->tag;
    if (!differs) differs = !has_callback || callback.byte_offset != address.byte_offset;
    if (differs) qa_range_error("Native typed source callback registration differs: %s", declared->name);
  }
  return address;
}
qa_compat_q2_rerelease_weapon_profile *qa_compat_q2_rerelease_weapon_profile_create(
    qa_compat_q2_rerelease_guest_module *module, qa_guest_address image_base, const qa_native_weapon_behavior_declaration *declaration) {
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  qa_native_weapon_behavior_declaration profile;
  bool has;
  if (!declaration) has = qa_compat_q2_built_in_rerelease_weapon_declaration(memory->module, &profile);
  else has = read_declaration(declaration, &memory->module, &profile);
  if (!has) qa_range_error("Native trajectory behavior requires an artifact-qualified executable profile");
  bind_ctx_t *b = calloc(1, sizeof *b);
  qa_compat_q2_rerelease_weapon_profile *out = calloc(1, sizeof *out);
  if (!b || !out) { free(b); free(out); qa_compat_q2_native_weapon_declaration_free(&profile); return NULL; }
  b->module = module; b->profile = profile;
  out->ctx = b;
  if (memory->pointer_bytes != 8) {
    qa_compat_q2_rerelease_weapon_profile_destroy(out);
    qa_range_error("Native trajectory declaration requires Windows x64 pointers");
  }
  qa_weapon_behavior_definition definition;
  if (!qa_compat_q2_rerelease_weapon_definition(memory->module, &b->profile, &definition)) {
    qa_compat_q2_rerelease_weapon_profile_destroy(out);
    qa_range_error("Native trajectory declaration has no definition");
  }
  b->equip_count = b->profile.equip.call_count; b->launch_count = b->profile.launch.call_count;
  b->equip = calloc(b->equip_count ? b->equip_count : 1, sizeof *b->equip);
  b->launch = calloc(b->launch_count ? b->launch_count : 1, sizeof *b->launch);
  if (!b->equip || !b->launch) { qa_compat_q2_rerelease_weapon_profile_destroy(out); return NULL; }
  for (size_t i = 0; i < b->equip_count; i++) b->equip[i] = resolve_entry(b, image_base, b->profile.equip.calls[i]);
  for (size_t i = 0; i < b->launch_count; i++) b->launch[i] = resolve_entry(b, image_base, b->profile.launch.calls[i]);
  b->allocate = resolve_entry(b, image_base, b->profile.allocate.entry);
  b->freed = resolve_entry(b, image_base, b->profile.free.entry);
  b->touch = resolve_entry(b, image_base, b->profile.projectile_touch);
  b->weapon_think = resolve_entry(b, image_base, b->profile.equipped_weapon.expected);
  b->time = qa_compat_q2_mem_offset(memory, image_base, (int64_t)b->profile.time.rva);
  memory->check(memory->ctx, b->time, 8, "write");
  const qa_guest_value_layout ptr[] = {qa_compat_q2_rr_pointer_layout};
  b->think_signature = qa_compat_q2_rerelease_signature(ptr, 1, true, qa_compat_q2_rr_pointer_layout);
  b->allocate_signature = qa_compat_q2_rerelease_signature(NULL, 0, false, qa_compat_q2_rr_pointer_layout);
  /* The definition borrows the retained scratch; rebind its strings
   * onto this profile's own declaration, which outlives it. */
  definition.id = b->profile.id; definition.title = b->profile.title;
  definition.role = b->profile.role; definition.aspect = b->profile.aspect;
  out->definition = definition;
  out->initialization_classes = b->profile.initialization_classes;
  out->equipment = b->profile.equipment; out->equipment_count = b->profile.equipment_count;
  out->ammunition = b->profile.ammunition;
  out->initial_cvars = b->profile.initial_cvars; out->initial_cvar_count = b->profile.initial_cvar_count;
  out->provisioning_cvars = b->profile.provisioning_cvars; out->provisioning_cvar_count = b->profile.provisioning_cvar_count;
  out->equip = p_equip; out->launch = p_launch; out->matches = p_matches; out->project_shooter = p_project_shooter;
  out->project = p_project; out->trajectory = p_trajectory; out->generation = p_generation; out->time = p_time;
  out->next_think = p_next_think; out->think = p_think; out->allocate = p_allocate; out->free_record = p_free_record;
  return out;
}
void qa_compat_q2_rerelease_weapon_profile_destroy(qa_compat_q2_rerelease_weapon_profile *profile) {
  if (!profile) return;
  bind_ctx_t *b = profile->ctx;
  if (b) {
    free(b->equip); free(b->launch);
    qa_compat_q2_native_weapon_declaration_free(&b->profile);
    free(b);
  }
  free(profile);
}

