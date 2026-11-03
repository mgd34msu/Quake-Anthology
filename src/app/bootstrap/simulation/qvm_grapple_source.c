/* Port of src/app/bootstrap/simulation/qvm-grapple-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/qvm_grapple_source.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/app/bootstrap/simulation/qvm_weapon_behavior.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/core/commands/index.h"
#include "qa/core/common_parse.h"
#include "qa/core/cvars/index.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *grown = realloc(p, size == 0 ? 1 : size);
  if (grown == NULL) qa_range_error("Out of memory");
  return grown;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length + 1);
  return copy;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---------------- checkpoint reader ---------------- */
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o;
  if (!qa_save_reader_field(r, n, &o)) qa_range_error("Out of memory");
  return o;
}
static qa_save_reader itm(const qa_save_reader *r, size_t i) {
  qa_save_reader o;
  if (!qa_save_reader_item(r, i, &o)) qa_range_error("Out of memory");
  return o;
}
void qa_app_sim_qvm_grapple_source_checkpoint_destroy(qa_app_sim_qvm_grapple_source_checkpoint *checkpoint) {
  if (checkpoint->read) {
    free((void *)checkpoint->grapple.module.random);
    free((void *)checkpoint->grapple.module.callbacks);
    free(checkpoint->grapple.owners);
  } else qa_qvm_grapple_checkpoint_destroy(&checkpoint->grapple);
  free(checkpoint->bindings);
  free(checkpoint->tethers);
  memset(checkpoint, 0, sizeof *checkpoint);
}
typedef struct read_scope { qa_app_sim_qvm_grapple_source_checkpoint out; } read_scope;
static void read_release(void *arg) { qa_app_sim_qvm_grapple_source_checkpoint_destroy(&((read_scope *)arg)->out); }
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_read_qvm_grapple_source_checkpoint(const qa_save_reader *reader) {
  read_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_app_sim_qvm_grapple_source_checkpoint *out = &scope.out;
  out->read = true;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, read_release, &scope);
  qa_save_reader grapple = fld(reader, "grapple"), f = fld(&grapple, "module");
  out->grapple.module = qa_read_guest(&f);
  if (out->grapple.module.kind != QA_GUEST_CHECKPOINT_QVM) qa_save_reader_fail(&f, "Expected QVM grapple continuation");
  qa_save_reader_destroy(&f);
  f = fld(reader, "version"); out->version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(&grapple, "version"); out->grapple.version = qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(&grapple, "profile"); out->grapple.profile = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
  f = fld(&grapple, "owners");
  size_t count = qa_save_reader_list_count(&f);
  out->grapple.owners = xcalloc(count, sizeof *out->grapple.owners);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader e = itm(&f, i);
    out->grapple.owners[i] = qa_read_saved_actor(&e);
    out->grapple.owner_count = i + 1;
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&grapple);
  f = fld(reader, "bindings");
  count = qa_save_reader_list_count(&f);
  out->bindings = xcalloc(count, sizeof *out->bindings);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader e = itm(&f, i), b;
    b = fld(&e, "actor"); out->bindings[i].actor = qa_read_saved_actor(&b); qa_save_reader_destroy(&b);
    b = fld(&e, "pointer"); out->bindings[i].pointer = qa_save_reader_integer_min(&b, 1); qa_save_reader_destroy(&b);
    b = fld(&e, "client"); out->bindings[i].client = qa_save_reader_boolean(&b); qa_save_reader_destroy(&b);
    b = fld(&e, "origin"); out->bindings[i].origin = qa_read_vector(&b); qa_save_reader_destroy(&b);
    out->binding_count = i + 1;
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  f = fld(reader, "tethers");
  count = qa_save_reader_list_count(&f);
  out->tethers = xcalloc(count, sizeof *out->tethers);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader e = itm(&f, i), b;
    b = fld(&e, "owner"); out->tethers[i].owner = qa_read_saved_actor(&b); qa_save_reader_destroy(&b);
    b = fld(&e, "actor"); out->tethers[i].actor = qa_read_saved_actor(&b); qa_save_reader_destroy(&b);
    out->tether_count = i + 1;
    qa_save_reader_destroy(&e);
  }
  qa_save_reader_destroy(&f);
  qa_cleanup_pop(&cleanup);
  return scope.out;
}

/* ---------------- source state ---------------- */
typedef struct binding { qa_actor_id actor; double pointer; bool client; qa_vec3 origin; } binding;
typedef struct tether { qa_actor_id owner; qa_owned_actor actor; qa_qvm_grapple_projection projection; } tether;
typedef struct indexed_text { int64_t index; char *value; } indexed_text;
typedef struct restored_tether { qa_actor_id owner; qa_owned_actor actor; } restored_tether;

struct qa_app_sim_qvm_grapple_source {
  qa_app_sim_qvm_grapple_source_options options;
  qa_qvm_game vm;
  bool vm_ready;
  qa_qvm_grapple_provider *provider;
  qa_cvar_registry *cvars;
  qa_qvm_files *files;
  qa_command_buffer *commands;
  indexed_text *configstrings; size_t configstring_count, configstring_capacity;
  indexed_text *userinfo; size_t userinfo_count, userinfo_capacity;
  binding *bindings; size_t binding_count, binding_capacity;
  tether *tethers; size_t tether_count, tether_capacity;
  qa_common_parse_state parser;
  qa_common_parse_cursor cursor;
  bool cursor_ready;
  qa_qvm_server_spatial_operations spatial;
  qa_qvm_server_game_services services;
  qa_qvm_host_state host_state;
  int64_t release_subscription;
  bool subscribed;
  double milliseconds;
  bool closed, synchronizing;
  const restored_tether *restoring; size_t restoring_count; bool has_restoring;
  /* Scratch outputs (TS GC'd values). */
  uint8_t *host_bytes;
  qa_app_simulation_presentation *presentations; size_t presentation_capacity;
  qa_app_sim_q3_grapple_cable *cables; size_t cable_capacity;
};

static void current(qa_app_sim_qvm_grapple_source *s) {
  s->options.assert_current(s->options.ctx);
  if (s->closed) qa_raise("Error", 0, 0, "Grapple source is closed");
}
static void print_event(qa_app_sim_qvm_grapple_source *s, const char *text) {
  qa_app_q3_source_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_PRINT;
  event.text = text;
  s->options.event(s->options.ctx, event);
}
static const char *indexed_get(const indexed_text *list, size_t count, int64_t index) {
  for (size_t i = 0; i < count; i++) if (list[i].index == index) return list[i].value;
  return NULL;
}
static void indexed_set(indexed_text **list, size_t *count, size_t *capacity, int64_t index, const char *value) {
  for (size_t i = 0; i < *count; i++)
    if ((*list)[i].index == index) {
      char *copy = xstrdup(value);
      free((*list)[i].value);
      (*list)[i].value = copy;
      return;
    }
  if (*count == *capacity) {
    *capacity = *capacity ? *capacity * 2 : 8;
    *list = xrealloc(*list, *capacity * sizeof **list);
  }
  (*list)[*count].index = index;
  (*list)[*count].value = xstrdup(value);
  (*count)++;
}
static void indexed_delete(indexed_text *list, size_t *count, int64_t index) {
  for (size_t i = 0; i < *count; i++)
    if (list[i].index == index) {
      free(list[i].value);
      memmove(&list[i], &list[i + 1], (*count - i - 1) * sizeof *list);
      (*count)--;
      return;
    }
}
static void indexed_clear(indexed_text *list, size_t *count) {
  for (size_t i = 0; i < *count; i++) free(list[i].value);
  *count = 0;
}
static binding *find_binding(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->binding_count; i++) if (qa_same_actor(s->bindings[i].actor, actor)) return &s->bindings[i];
  return NULL;
}
static binding *add_binding(qa_app_sim_qvm_grapple_source *s, binding value) {
  if (s->binding_count == s->binding_capacity) {
    s->binding_capacity = s->binding_capacity ? s->binding_capacity * 2 : 8;
    s->bindings = xrealloc(s->bindings, s->binding_capacity * sizeof *s->bindings);
  }
  s->bindings[s->binding_count] = value;
  return &s->bindings[s->binding_count++];
}
static void delete_binding(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->binding_count; i++)
    if (qa_same_actor(s->bindings[i].actor, actor)) {
      memmove(&s->bindings[i], &s->bindings[i + 1], (s->binding_count - i - 1) * sizeof *s->bindings);
      s->binding_count--;
      return;
    }
}
static tether *find_tether(const qa_app_sim_qvm_grapple_source *s, qa_actor_id owner) {
  for (size_t i = 0; i < s->tether_count; i++) if (qa_same_actor(s->tethers[i].owner, owner)) return &s->tethers[i];
  return NULL;
}
static void delete_tether(qa_app_sim_qvm_grapple_source *s, qa_actor_id owner) {
  for (size_t i = 0; i < s->tether_count; i++)
    if (qa_same_actor(s->tethers[i].owner, owner)) {
      memmove(&s->tethers[i], &s->tethers[i + 1], (s->tether_count - i - 1) * sizeof *s->tethers);
      s->tether_count--;
      return;
    }
}
static const qa_app_sim_qvm_grapple_target *find_target(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  size_t count = 0;
  const qa_app_sim_qvm_grapple_target *targets = s->options.targets(s->options.ctx, &count);
  for (size_t i = 0; i < count; i++) if (qa_same_actor(targets[i].actor, actor)) return &targets[i];
  return NULL;
}
static int32_t word_at(const uint8_t *bytes, size_t length, size_t offset) {
  if (offset + 4 > length) qa_range_error("Offset is outside the bounds of the DataView");
  uint32_t bits = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) | ((uint32_t)bytes[offset + 2] << 16)
                  | ((uint32_t)bytes[offset + 3] << 24);
  return (int32_t)bits;
}

/* TS slot(pointer) = vm.data.numberFromPointer(pointer). */
static double slot_of(qa_app_sim_qvm_grapple_source *s, double pointer) {
  return qa_qvm_game_data_number_from_pointer(&s->vm.data, pointer);
}
static bool actor_for_slot(qa_app_sim_qvm_grapple_source *s, double slot, qa_actor_id *out) {
  for (size_t i = 0; i < s->binding_count; i++)
    if (slot_of(s, s->bindings[i].pointer) == slot) { *out = s->bindings[i].actor; return true; }
  for (size_t i = 0; i < s->tether_count; i++)
    if (slot_of(s, s->tethers[i].projection.hook) == slot) { *out = s->tethers[i].actor.id; return true; }
  return false;
}
static bool actor_for_pointer(qa_app_sim_qvm_grapple_source *s, double pointer, qa_actor_id *out) {
  if (pointer == 0) return false;
  qa_qvm_game_data_state layout = qa_qvm_game_data_checkpoint(&s->vm.data);
  double slot = (pointer - layout.entities_word) / layout.entity_stride;
  if (slot == 1022 || slot == 1023) return false;
  return actor_for_slot(s, slot_of(s, pointer), out);
}
static double entity_pointer(qa_app_sim_qvm_grapple_source *s, double slot) {
  qa_qvm_writable_view view;
  qa_qvm_game_data_entity_bytes(&s->vm.data, slot, &view);
  return (double)view.start;
}
static void sound(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor, const char *path, bool loop) {
  qa_body_state_result body = qa_shared_body_table_read(s->options.bodies, actor);
  if (path == NULL || !body.has_value) return;
  qa_app_q3_source_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_SOUND;
  event.actor = actor;
  event.path = path;
  event.origin = body.value.origin;
  event.velocity = body.value.velocity;
  event.channel = 0;
  event.volume = 1;
  event.loop = loop;
  s->options.event(s->options.ctx, event);
}

/* TS private mirror(actor, known?). */
static double mirror(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor, const qa_app_sim_qvm_grapple_target *known) {
  const qa_app_sim_qvm_grapple_target *found = known != NULL ? known : find_target(s, actor);
  if (found == NULL) qa_raise("Error", 0, 0, "Grapple collision refers to an unavailable shared actor");
  qa_app_sim_qvm_grapple_target target = *found;
  binding *b = find_binding(s, target.actor);
  if (b == NULL) {
    if (target.is_player) {
      double slot = 0;
      while (slot < 64 && indexed_get(s->userinfo, s->userinfo_count, (int64_t)slot) != NULL) slot++;
      if (slot == 64) qa_raise("Error", 0, 0, "Grapple source exhausted its client capacity");
      binding value = {target.actor, entity_pointer(s, slot), true, target.body.origin};
      add_binding(s, value);
      indexed_set(&s->userinfo, &s->userinfo_count, &s->userinfo_capacity, (int64_t)slot, target.userinfo);
      char *denial = qa_qvm_game_client_connect(&s->vm, slot, true, false);
      if (denial != NULL) {
        char message[512];
        snprintf(message, sizeof message, "Grapple source rejected client: %s", denial);
        free(denial);
        qa_raise("Error", 0, 0, "%s", message);
      }
      qa_qvm_game_client_begin(&s->vm, slot);
    } else {
      double pointer = qa_qvm_module_call(&s->vm.module, NULL, 0, (int64_t)s->options.profile->callbacks.allocate);
      (void)slot_of(s, pointer);
      for (size_t i = 0; i < s->binding_count; i++)
        if (s->bindings[i].pointer == pointer) qa_raise("Error", 0, 0, "Grapple source reused an occupied actor");
      binding value = {target.actor, pointer, false, target.body.origin};
      add_binding(s, value);
    }
    b = find_binding(s, target.actor);
  }
  double pointer = b->pointer;
  qa_qvm_shared_entity *entity = qa_qvm_game_data_entity_from_pointer(&s->vm.data, pointer);
  qa_body_state body = target.body;
  if (target.is_player) {
    double slot = slot_of(s, pointer);
    qa_qvm_writable_view ps;
    qa_qvm_game_data_public_player_bytes(&s->vm.data, slot, &ps);
    const char *userinfo = indexed_get(s->userinfo, s->userinfo_count, (int64_t)slot);
    if (userinfo == NULL || strcmp(userinfo, target.userinfo) != 0) {
      indexed_set(&s->userinfo, &s->userinfo_count, &s->userinfo_capacity, (int64_t)slot, target.userinfo);
      qa_qvm_game_client_userinfo_changed(&s->vm, slot);
    }
    const struct { int64_t offset; qa_vec3 vector; } vectors[] = {{20, body.origin}, {32, body.velocity}, {152, body.angles}};
    for (size_t i = 0; i < 3; i++) {
      qa_qvm_writable_view_set_float32(&ps, vectors[i].offset, vectors[i].vector.x, true);
      qa_qvm_writable_view_set_float32(&ps, vectors[i].offset + 4, vectors[i].vector.y, true);
      qa_qvm_writable_view_set_float32(&ps, vectors[i].offset + 8, vectors[i].vector.z, true);
    }
    qa_qvm_writable_view_set_int32(&ps, 184, qa_js_to_int32(trunc(target.health)), true);
    qa_qvm_writable_view_set_int32(&ps, 164, qa_js_to_int32(trunc(target.view_height)), true);
    int32_t team = is(target.team, "red") ? 1 : is(target.team, "blue") ? 2 : is(target.team, "spectator") ? 3 : 0;
    qa_qvm_writable_view_set_int32(&ps, 260, team, true);
    qa_qvm_writable_view_set_int32(&ps, 4, is(target.team, "spectator") ? 2 : target.health <= 0 ? 3 : 0, true);
    qa_qvm_shared_entity_set_contents(entity, is(target.team, "spectator") ? 0 : target.health > 0 ? 0x2000000 : 0x4000000);
    qa_qvm_entity_state_set_e_type(&entity->s, 1);
  } else {
    qa_qvm_entity_state_set_e_type(&entity->s, target.mover ? 4 : 0);
    if (target.mover && s->provider != NULL) {
      qa_vec3 delta = {body.origin.x - b->origin.x, body.origin.y - b->origin.y, body.origin.z - b->origin.z};
      if (delta.x != 0 || delta.y != 0 || delta.z != 0) {
        qa_qvm_grapple_provider_mover_moved(s->provider, actor, delta);
        entity = qa_qvm_game_data_entity_from_pointer(&s->vm.data, pointer);
      }
    }
  }
  qa_qvm_shared_entity_set_current_origin(entity, body.origin);
  qa_qvm_shared_entity_set_current_angles(entity, body.angles);
  qa_qvm_shared_entity_set_mins(entity, body.bounds.min);
  qa_qvm_shared_entity_set_maxs(entity, body.bounds.max);
  qa_qvm_entity_state_set_origin(&entity->s, body.origin);
  qa_qvm_entity_state_set_angles(&entity->s, body.angles);
  qa_qvm_trajectory pos = qa_qvm_entity_state_get_pos(&entity->s);
  pos.base = body.origin; pos.delta = body.velocity; pos.time = s->milliseconds;
  qa_qvm_entity_state_set_pos(&entity->s, pos);
  qa_qvm_writable_view record;
  qa_qvm_game_data_entity_bytes(&s->vm.data, slot_of(s, pointer), &record);
  qa_qvm_writable_view_set_int32(&record, (int64_t)s->options.profile->fields.health, qa_js_to_int32(trunc(target.health)), true);
  qa_qvm_game_data_entity_bytes(&s->vm.data, slot_of(s, pointer), &record);
  qa_qvm_writable_view_set_int32(&record, (int64_t)s->options.profile->fields.takedamage, target.health > 0 ? 1 : 0, true);
  b = find_binding(s, target.actor);
  if (b != NULL) b->origin = body.origin;
  return pointer;
}

static void remove_actor(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  if (s->closed || s->provider == NULL) return;
  qa_qvm_grapple_provider_actor_released(s->provider, actor);
  binding *b = find_binding(s, actor);
  if (b == NULL) return;
  binding value = *b;
  if (value.client) {
    double slot = slot_of(s, value.pointer);
    qa_qvm_game_client_disconnect(&s->vm, slot);
    indexed_delete(s->userinfo, &s->userinfo_count, (int64_t)slot);
  } else {
    int32_t word = (int32_t)value.pointer;
    (void)qa_qvm_module_call(&s->vm.module, &word, 1, (int64_t)s->options.profile->callbacks.free);
  }
  delete_binding(s, actor);
}
typedef struct sync_scope { qa_app_sim_qvm_grapple_source *source; qa_app_sim_qvm_grapple_target *targets; } sync_scope;
static void sync_finally(void *arg) {
  sync_scope *scope = arg;
  scope->source->synchronizing = false;
  free(scope->targets);
}
static void synchronize(qa_app_sim_qvm_grapple_source *s) {
  if (s->synchronizing) return;
  s->synchronizing = true;
  size_t count = 0;
  sync_scope scope = {s, NULL};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, sync_finally, &scope);
  const qa_app_sim_qvm_grapple_target *targets = s->options.targets(s->options.ctx, &count);
  scope.targets = xcalloc(count, sizeof *scope.targets);
  memcpy(scope.targets, targets, count * sizeof *scope.targets);
  size_t bound = s->binding_count;
  qa_actor_id *keys = xcalloc(bound, sizeof *keys);
  for (size_t i = 0; i < bound; i++) keys[i] = s->bindings[i].actor;
  for (size_t i = 0; i < bound; i++) {
    bool present = false;
    for (size_t j = 0; j < count; j++) if (qa_same_actor(scope.targets[j].actor, keys[i])) present = true;
    if (!present) remove_actor(s, keys[i]);
  }
  free(keys);
  for (size_t j = 0; j < count; j++) (void)mirror(s, scope.targets[j].actor, &scope.targets[j]);
  qa_cleanup_pop(&cleanup);
  sync_finally(&scope);
}
static void publish(qa_app_sim_qvm_grapple_source *s, qa_actor_id owner, const qa_qvm_grapple_projection *projection) {
  tether *found = find_tether(s, owner);
  bool has_previous = found != NULL;
  tether previous;
  memset(&previous, 0, sizeof previous);
  if (has_previous) previous = *found;
  if (projection == NULL) {
    if (has_previous) {
      delete_tether(s, owner);
      if (qa_session_actor_registry_is_live(s->options.actors, previous.actor.id))
        qa_session_actor_registry_release(s->options.actors, previous.actor);
    }
    return;
  }
  qa_owned_actor actor;
  memset(&actor, 0, sizeof actor);
  bool restored = false;
  if (!has_previous && s->has_restoring)
    for (size_t i = 0; i < s->restoring_count; i++)
      if (qa_same_actor(s->restoring[i].owner, owner)) { actor = s->restoring[i].actor; restored = true; }
  if (has_previous) actor = previous.actor;
  else if (!restored) actor = qa_session_actor_registry_allocate(s->options.actors, s->options.provider, "q3:equipment/hook");
  tether *entry = find_tether(s, owner);
  if (entry == NULL) {
    if (s->tether_count == s->tether_capacity) {
      s->tether_capacity = s->tether_capacity ? s->tether_capacity * 2 : 4;
      s->tethers = xrealloc(s->tethers, s->tether_capacity * sizeof *s->tethers);
    }
    entry = &s->tethers[s->tether_count++];
    entry->owner = owner;
  }
  entry->actor = actor;
  entry->projection = *projection;
  qa_qvm_shared_entity *record = qa_qvm_game_data_entity_from_pointer(&s->vm.data, projection->hook);
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.origin = projection->pulling ? projection->point : projection->origin;
  body.velocity = projection->velocity;
  body.angles = qa_qvm_shared_entity_get_current_angles(record);
  body.bounds.min = qa_qvm_shared_entity_get_mins(record);
  body.bounds.max = qa_qvm_shared_entity_get_maxs(record);
  body.ground = projection->has_mover ? &projection->mover : NULL;
  qa_shared_body_table_write(s->options.bodies, actor, body);
  qa_shared_body_table_link(s->options.bodies, actor, NULL);
  if (projection->pulling && has_previous && !previous.projection.pulling && !s->has_restoring)
    sound(s, actor.id, s->options.profile->presentation.attach_sound, false);
}

/* ---------------- provider bridge ---------------- */
static void bridge_synchronize(void *ctx) { synchronize(ctx); }
static bool bridge_entity(void *ctx, qa_actor_id actor, double *out) {
  binding *b = find_binding(ctx, actor);
  if (b == NULL) return false;
  *out = b->pointer;
  return true;
}
static bool bridge_actor(void *ctx, double pointer, qa_actor_id *out) { return actor_for_pointer(ctx, pointer, out); }
static qa_actor_id restore_actor(qa_app_sim_qvm_grapple_source *s, qa_saved_actor_id saved) {
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_saved(s->options.actors, saved, &actor))
    qa_raise("Error", 0, 0, "Saved grapple refers to a missing shared actor");
  return actor.id;
}
static qa_actor_id bridge_restore_actor(void *ctx, qa_saved_actor_id saved) { return restore_actor(ctx, saved); }
static void bridge_publish(void *ctx, qa_actor_id owner, const qa_qvm_grapple_projection *projection) {
  publish(ctx, owner, projection);
}
static void bridge_velocity(void *ctx, qa_actor_id actor, qa_vec3 velocity) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  s->options.velocity(s->options.ctx, actor, velocity);
}

/* ---------------- syscalls and services ---------------- */
static void s_print(void *ctx, const char *text) { print_event(ctx, text); }
static double s_milliseconds(void *ctx) { return ((qa_app_sim_qvm_grapple_source *)ctx)->milliseconds; }
static const char *const *s_arguments(void *ctx, size_t *out_count) { (void)ctx; *out_count = 0; return NULL; }
static double s_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  return s->options.real_time(s->options.ctx, has_output, out);
}
static void s_execute_now(void *ctx, const char *text) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  if (text != NULL) (void)qa_command_buffer_execute_now(s->commands, text, NULL, -1);
}
static void s_append(void *ctx, const char *text) { qa_command_buffer_append(((qa_app_sim_qvm_grapple_source *)ctx)->commands, text, NULL, -1); }
static void s_insert(void *ctx, const char *text) { qa_command_buffer_insert(((qa_app_sim_qvm_grapple_source *)ctx)->commands, text, NULL, -1); }
static qa_qvm_host_result syscall(void *ctx, const qa_qvm_host_call *call) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  current(s);
  qa_qvm_common_services common;
  memset(&common, 0, sizeof common);
  common.role = "qagame";
  common.cvars = s->cvars;
  common.ctx = s;
  common.print = s_print;
  common.milliseconds = s_milliseconds;
  common.arguments = s_arguments;
  common.real_time = s_real_time;
  common.commands.ctx = s;
  common.commands.execute_now = s_execute_now;
  common.commands.append = s_append;
  common.commands.insert = s_insert;
  qa_qvm_host_result result = 0;
  if (qa_qvm_common_syscall(call, common, &result)) return result;
  if (qa_qvm_file_syscall(call, s->files, &result)) return result;
  int32_t word = 0;
  if (qa_qvm_server_game_syscall(call, &s->services, &word)) return word;
  if (is(call->kind, "engine") && is(call->role, "qagame")
      && (call->code == QA_QVM_GAME_IMPORT_BOTLIB_SETUP || call->code == QA_QVM_GAME_IMPORT_BOTLIB_AAS_INITIALIZED))
    return 0;
  qa_qvm_reject_syscall(call);
}
static const char *cs_get(void *ctx, int32_t index) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  const char *value = indexed_get(s->configstrings, s->configstring_count, index);
  return value == NULL ? "" : value;
}
static void cs_set(void *ctx, int32_t index, const char *value) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  indexed_set(&s->configstrings, &s->configstring_count, &s->configstring_capacity, index, value);
  qa_app_q3_source_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_CONFIGSTRING;
  event.index = index;
  event.value = indexed_get(s->configstrings, s->configstring_count, index);
  s->options.event(s->options.ctx, event);
}
static const char *ui_get(void *ctx, double slot) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  const char *value = indexed_get(s->userinfo, s->userinfo_count, (int64_t)slot);
  return value == NULL ? "" : value;
}
static void ui_set(void *ctx, double slot, const char *value) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  indexed_set(&s->userinfo, &s->userinfo_count, &s->userinfo_capacity, (int64_t)slot, value);
}
static qa_qvmx_wire_user_command user_command(void *ctx, double slot) {
  (void)slot;
  qa_qvmx_wire_user_command command;
  memset(&command, 0, sizeof command);
  command.server_time = ((qa_app_sim_qvm_grapple_source *)ctx)->milliseconds;
  return command;
}
static void drop_client(void *ctx, double slot, const char *reason) {
  (void)ctx;
  qa_raise("Error", 0, 0, "Grapple source rejected borrowed client %s: %s", QA_JS_NUMBER(slot), reason);
}
static void send_server_command(void *ctx, double slot, const char *text) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_app_q3_source_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_SERVER_COMMAND;
  event.client = slot;
  event.text = text;
  s->options.event(s->options.ctx, event);
}
static qa_qvm_entity_token_result entity_token(void *ctx) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_qvm_entity_token_result result;
  result.token = qa_common_parse_state_parse(&s->parser, &s->cursor, true);
  result.ended = !s->cursor.has_offset;
  return result;
}
static qa_trace_policy q3_policy(double mask, bool curves, bool player_curve_clip) {
  qa_trace_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.kind = "q3";
  policy.contents_mask = mask;
  policy.curves = curves;
  policy.player_curve_clip = player_curve_clip;
  return policy;
}
static qa_qvm_trace_record sp_trace(void *ctx, const qa_qvm_server_trace_query *input) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = input->start;
  query.end = input->end;
  query.shape.kind = input->shape_kind;
  query.shape.bounds = input->shape_bounds;
  query.target.is_model = false;
  query.numeric = qa_q3_binary32_profile;
  qa_actor_id pass;
  query.pass_actor = actor_for_slot(s, input->pass_entity_num, &pass) ? &pass : NULL;
  bool curves = qa_cvar_registry_variable_value(s->cvars, "cm_noCurves") == 0;
  bool player_curve_clip = qa_cvar_registry_variable_value(s->cvars, "cm_playerCurveClip") != 0;
  query.policy = q3_policy(input->mask, curves, player_curve_clip);
  qa_trace_result trace = qa_shared_trace(s->options.scene, query);
  if (!is(trace.kind, "q3")) qa_raise("Error", 0, 0, "Grapple collision lost its source policy");
  qa_qvm_trace_record record;
  memset(&record, 0, sizeof record);
  record.result.fraction = trace.fraction;
  record.result.end = trace.end;
  record.result.all_solid = trace.all_solid;
  record.result.start_solid = trace.start_solid;
  record.result.contents = trace.contents;
  record.result.surface_flags = trace.surface_flags;
  record.result.plane.plane = trace.source_plane.plane;
  record.result.plane.type = trace.source_plane.type;
  record.result.plane.signbits = trace.source_plane.signbits;
  record.entity_num = trace.hit.kind == QA_TRACE_HIT_ACTOR ? (int32_t)slot_of(s, mirror(s, trace.hit.actor, NULL))
                      : trace.fraction == 1 ? 1023 : 1022;
  return record;
}
static int32_t sp_point_contents(void *ctx, qa_vec3 point, int32_t pass) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = point;
  query.target.is_model = false;
  qa_actor_id pass_actor;
  query.pass_actor = actor_for_slot(s, pass, &pass_actor) ? &pass_actor : NULL;
  query.numeric = qa_q3_binary32_profile;
  query.policy = q3_policy(-1, true, true);
  qa_point_contents_result sample = qa_shared_point_contents(s->options.scene, query);
  if (!is(sample.kind, "q3")) qa_raise("Error", 0, 0, "Grapple contents lost its source policy");
  return qa_js_to_int32(sample.contents);
}
static size_t sp_area_entities(void *ctx, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  size_t count = 0;
  const qa_spatial_actor *found = qa_shared_query_actors(s->options.scene, bounds, NULL, &count);
  qa_actor_id *actors = xcalloc(count, sizeof *actors);
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    bool tethered = false;
    for (size_t j = 0; j < s->tether_count; j++) if (qa_same_actor(s->tethers[j].actor.id, found[i].body.actor)) tethered = true;
    if (!tethered) actors[kept++] = found[i].body.actor;
  }
  size_t limit = maximum < 0 ? kept : kept < (size_t)maximum ? kept : (size_t)maximum;
  for (size_t i = 0; i < limit; i++) {
    int32_t slot = (int32_t)slot_of(s, mirror(s, actors[i], NULL));
    if (i < capacity) out[i] = slot;
  }
  free(actors);
  return limit;
}
static bool sp_entity_contact(void *ctx, qa_bounds bounds, int32_t slot, bool capsule) {
  (void)ctx; (void)bounds; (void)slot; (void)capsule;
  qa_raise("Error", 0, 0, "Grapple source requested undeclared brush contact");
}
static void sp_set_brush_model(void *ctx, int32_t slot, const char *name) {
  (void)ctx; (void)slot; (void)name;
  qa_raise("Error", 0, 0, "Grapple source attempted to own a map brush");
}
static void sp_adjust_area_portal_state(void *ctx, int32_t slot, bool open) {
  (void)ctx; (void)slot; (void)open;
  qa_raise("Error", 0, 0, "Grapple source attempted to change map portals");
}
static bool sp_in_pvs(void *ctx, qa_vec3 first, qa_vec3 second, bool ignore) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  double a = qa_shared_point_leaf(s->options.scene, first), b = qa_shared_point_leaf(s->options.scene, second);
  double ca = qa_shared_leaf_cluster(s->options.scene, a), cb = qa_shared_leaf_cluster(s->options.scene, b);
  if (!qa_shared_cluster_visible(s->options.scene, ca, cb, "pvs")) return false;
  if (ignore) return true;
  double aa = qa_shared_leaf_area(s->options.scene, a), ab = qa_shared_leaf_area(s->options.scene, b);
  return qa_shared_areas_connected(s->options.scene, aa, ab);
}
static bool sp_areas_connected(void *ctx, int32_t first, int32_t second) {
  return qa_shared_areas_connected(((qa_app_sim_qvm_grapple_source *)ctx)->options.scene, first, second);
}
static void sp_link(void *ctx, int32_t slot) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_qvm_shared_entity *r = qa_qvm_game_data_entity(&s->vm.data, slot);
  qa_qvm_shared_entity_set_linked(r, true);
  qa_vec3 origin = qa_qvm_shared_entity_get_current_origin(r), mins = qa_qvm_shared_entity_get_mins(r);
  qa_vec3 maxs = qa_qvm_shared_entity_get_maxs(r);
  qa_vec3 absmin = {origin.x + mins.x - 1, origin.y + mins.y - 1, origin.z + mins.z - 1};
  qa_vec3 absmax = {origin.x + maxs.x + 1, origin.y + maxs.y + 1, origin.z + maxs.z + 1};
  qa_qvm_shared_entity_set_absmin(r, absmin);
  qa_qvm_shared_entity_set_absmax(r, absmax);
}
static void sp_unlink(void *ctx, int32_t slot) {
  qa_qvm_shared_entity_set_linked(qa_qvm_game_data_entity(&((qa_app_sim_qvm_grapple_source *)ctx)->vm.data, slot), false);
}

/* ---------------- bound VM functions ---------------- */
static int32_t proceed(qa_qvm_module_function_call *call) {
  return is(call->execution, "asynchronous") ? call->proceed_async(call->call_ctx) : call->proceed(call->call_ctx);
}
static int32_t same_team(void *ctx, qa_qvm_module_function_call *call) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_actor_id first, second;
  bool has_first = actor_for_pointer(s, word_at(call->words, call->words_length, 0), &first);
  bool has_second = actor_for_pointer(s, word_at(call->words, call->words_length, 4), &second);
  if (!has_first || !has_second) return proceed(call);
  size_t count = 0;
  const qa_app_sim_qvm_grapple_target *targets = s->options.targets(s->options.ctx, &count);
  const qa_app_sim_qvm_grapple_target *a = NULL, *b = NULL;
  for (size_t i = 0; i < count && a == NULL; i++) if (qa_same_actor(targets[i].actor, first)) a = &targets[i];
  for (size_t i = 0; i < count && b == NULL; i++) if (qa_same_actor(targets[i].actor, second)) b = &targets[i];
  return a != NULL && a->is_player && b != NULL && b->is_player && (is(a->team, "red") || is(a->team, "blue"))
         && is(a->team, b->team);
}
static qa_vec3 guest_vector(qa_app_sim_qvm_grapple_source *s, int32_t word) {
  qa_vec3 out = {0, 0, 0};
  if (word == 0) return out;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(s->vm.module.memory, word, 12, 0, &view);
  out.x = qa_qvm_writable_view_get_float32(&view, 0, true);
  out.y = qa_qvm_writable_view_get_float32(&view, 4, true);
  out.z = qa_qvm_writable_view_get_float32(&view, 8, true);
  return out;
}
static int32_t damage(void *ctx, qa_qvm_module_function_call *call) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  int32_t pointer = word_at(call->words, call->words_length, 0);
  qa_actor_id actor;
  if (!actor_for_slot(s, slot_of(s, pointer), &actor) || find_binding(s, actor) == NULL) return proceed(call);
  qa_app_sim_qvm_grapple_damage request;
  memset(&request, 0, sizeof request);
  qa_actor_id inflictor, attacker;
  request.target = actor;
  request.inflictor = actor_for_pointer(s, word_at(call->words, call->words_length, 4), &inflictor) ? &inflictor : NULL;
  request.attacker = actor_for_pointer(s, word_at(call->words, call->words_length, 8), &attacker) ? &attacker : NULL;
  request.direction = guest_vector(s, word_at(call->words, call->words_length, 12));
  request.point = guest_vector(s, word_at(call->words, call->words_length, 16));
  request.amount = word_at(call->words, call->words_length, 20);
  request.flags = word_at(call->words, call->words_length, 24);
  request.method = word_at(call->words, call->words_length, 28);
  s->options.damage(s->options.ctx, request);
  const qa_app_sim_qvm_grapple_target *target = find_target(s, actor);
  qa_qvm_writable_view record;
  qa_qvm_game_data_entity_bytes(&s->vm.data, slot_of(s, pointer), &record);
  qa_qvm_writable_view_set_int32(&record, (int64_t)s->options.profile->fields.health,
                                 qa_js_to_int32(trunc(target == NULL ? 0 : target->health)), true);
  return 0;
}

/* ---------------- host state (VM private checkpoint) ---------------- */
static void put(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("Out of memory");
}
static void push(qa_save_value *array, qa_save_value *child) {
  if (child == NULL || !qa_save_value_push(array, child)) qa_range_error("Out of memory");
}
static qa_save_value *object(void) {
  qa_save_value *o = qa_save_value_object();
  if (o == NULL) qa_range_error("Out of memory");
  return o;
}
static qa_save_value *array(void) {
  qa_save_value *a = qa_save_value_array();
  if (a == NULL) qa_range_error("Out of memory");
  return a;
}
/* TS QvmFiles.captureCheckpoint value form (see compat/qvm/mod_provider.c). */
static qa_save_value *mount_value(const qa_mount_identity *identity, bool archive, qa_archive_format format,
                                  const char *path, qa_content_digest digest) {
  qa_save_value *value = object(), *identity_value = object();
  put(identity_value, "id", qa_save_value_string(identity->id));
  put(identity_value, "content", qa_save_value_string(identity->content));
  put(identity_value, "generation", qa_save_value_number((double)identity->generation));
  put(value, "identity", identity_value);
  put(value, "kind", qa_save_value_string(archive ? "archive" : "loose"));
  if (archive) {
    static const char *const formats[] = {"pak", "pk3", "kpf", "zip"};
    put(value, "format", qa_save_value_string(formats[format]));
    put(value, "archivePath", qa_save_value_string(path));
    put(value, "archiveDigest", qa_save_value_string(digest));
  } else put(value, "rootPath", qa_save_value_string(path));
  return value;
}
static qa_save_value *resource_value(const qa_resolved_resource_reference *resource) {
  qa_save_value *value = object(), *provenance = object(), *resolution = object();
  put(value, "id", qa_save_value_string(resource->id));
  put(value, "requestedPath", qa_save_value_string(resource->requested_path));
  put(provenance, "memberPath", qa_save_value_string(resource->provenance.member_path));
  if (resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    const qa_archive_mount *mount = resource->provenance.archive_mount;
    put(provenance, "kind", qa_save_value_string("archive"));
    put(provenance, "mount", mount_value(&mount->identity, true, mount->format, mount->archive_path, mount->archive_digest));
    put(provenance, "memberIndex", qa_save_value_number((double)resource->provenance.member_index));
  } else {
    const qa_loose_mount *mount = resource->provenance.loose_mount;
    put(provenance, "kind", qa_save_value_string("loose"));
    put(provenance, "mount", mount_value(&mount->identity, false, QA_ARCHIVE_FORMAT_PAK, mount->root_path, NULL));
  }
  put(value, "provenance", provenance);
  put(value, "digest", qa_save_value_string(resource->digest));
  put(value, "byteLength", qa_save_value_number((double)resource->byte_length));
  put(resolution, "plan", qa_save_value_string(resource->resolution.plan));
  switch (resource->resolution.kind) {
    case QA_RESOURCE_RESOLUTION_DEFAULT_ORDER:
      put(resolution, "kind", qa_save_value_string("default-order"));
      put(resolution, "rank", qa_save_value_number((double)resource->resolution.rank));
      break;
    case QA_RESOURCE_RESOLUTION_PREFIX_ORDER:
      put(resolution, "kind", qa_save_value_string("prefix-order"));
      put(resolution, "rank", qa_save_value_number((double)resource->resolution.rank));
      put(resolution, "prefix", qa_save_value_string(resource->resolution.prefix));
      break;
    case QA_RESOURCE_RESOLUTION_LINK:
      put(resolution, "kind", qa_save_value_string("link"));
      put(resolution, "sourcePrefix", qa_save_value_string(resource->resolution.source_prefix));
      put(resolution, "targetPath", qa_save_value_string(resource->resolution.target_path));
      break;
  }
  put(value, "resolution", resolution);
  return value;
}
static qa_save_value *files_value(qa_qvm_files *files) {
  qa_qvm_files_checkpoint checkpoint;
  if (!qa_qvm_files_capture_checkpoint(files, &checkpoint)) qa_range_error("Out of memory");
  qa_save_value *value = object(), *handles = array();
  for (size_t i = 0; i < checkpoint.handle_count; i++) {
    const qa_qvm_file_handle_checkpoint *entry = &checkpoint.handles[i];
    qa_save_value *handle = object();
    put(handle, "slot", qa_save_value_number(entry->slot));
    put(handle, "kind", qa_save_value_string(entry->is_write ? "write" : "read"));
    if (entry->is_write) {
      qa_save_value *file = object();
      put(file, "path", qa_save_value_string(entry->file.path));
      const char *mode = entry->file.mode == QA_WRITABLE_WRITE ? "write"
                         : entry->file.mode == QA_WRITABLE_APPEND ? "append" : "append-sync";
      put(file, "mode", qa_save_value_string(mode));
      put(file, "position", qa_save_value_number((double)entry->file.position));
      put(handle, "file", file);
    } else {
      qa_save_value *resource = object();
      put(resource, "reference", resource_value(&entry->reference));
      put(resource, "bytes", qa_save_value_bytes(entry->bytes, entry->byte_length));
      put(handle, "resource", resource);
      put(handle, "position", qa_save_value_number(entry->position));
    }
    push(handles, handle);
  }
  put(value, "handles", handles);
  qa_qvm_files_checkpoint_destroy(&checkpoint);
  return value;
}
static qa_qvm_host_checkpoint host_checkpoint(void *ctx) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_save_value *value = object();
  put(value, "version", qa_save_value_number(1));
  put(value, "milliseconds", qa_save_value_number(s->milliseconds));
  qa_qvm_game_data_state layout = qa_qvm_game_data_checkpoint(&s->vm.data);
  qa_save_value *data = object();
  put(data, "entitiesWord", qa_save_value_number(layout.entities_word));
  put(data, "numEntities", qa_save_value_number(layout.num_entities));
  put(data, "entityStride", qa_save_value_number(layout.entity_stride));
  put(data, "clientsWord", qa_save_value_number(layout.clients_word));
  put(data, "clientStride", qa_save_value_number(layout.client_stride));
  put(value, "data", data);
  put(value, "cvars", qa_cvar_registry_capture_save_state(s->cvars));
  qa_save_value *configstrings = array();
  for (size_t i = 0; i < s->configstring_count; i++) {
    qa_save_value *cell = object();
    put(cell, "index", qa_save_value_number((double)s->configstrings[i].index));
    put(cell, "value", qa_save_value_string(s->configstrings[i].value));
    push(configstrings, cell);
  }
  put(value, "configstrings", configstrings);
  qa_save_value *userinfo = array();
  for (size_t i = 0; i < s->userinfo_count; i++) {
    qa_save_value *cell = object();
    put(cell, "slot", qa_save_value_number((double)s->userinfo[i].index));
    put(cell, "value", qa_save_value_string(s->userinfo[i].value));
    push(userinfo, cell);
  }
  put(value, "userinfo", userinfo);
  put(value, "files", files_value(s->files));
  put(value, "entityText", qa_save_value_string(s->cursor.source));
  put(value, "cursor", s->cursor.has_offset ? qa_save_value_number((double)s->cursor.offset) : qa_save_value_null());
  put(value, "parser", qa_common_parse_state_capture(&s->parser));
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  if (bytes == NULL) qa_range_error("Out of memory");
  free(s->host_bytes);
  s->host_bytes = bytes;
  qa_qvm_host_checkpoint checkpoint;
  memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.state.module = s->options.artifact->module;
  checkpoint.state.format = "q3:grapple-host";
  checkpoint.state.bytes.bytes = bytes;
  checkpoint.state.bytes.length = length;
  return checkpoint;
}
static double int_field(const qa_save_reader *reader, const char *name, bool minimum) {
  qa_save_reader f = fld(reader, name);
  double value = minimum ? qa_save_reader_integer_min(&f, 0) : qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
typedef struct host_restore_scope { qa_save_value *value; } host_restore_scope;
static void host_restore_release(void *arg) { qa_save_value_free(((host_restore_scope *)arg)->value); }
static void host_restore(void *ctx, const qa_qvm_host_checkpoint *checkpoint) {
  qa_app_sim_qvm_grapple_source *s = ctx;
  if (!is(checkpoint->state.format, "q3:grapple-host") || checkpoint->random_count != 0 || checkpoint->callback_count != 0)
    qa_raise("Error", 0, 0, "Invalid QVM grapple host checkpoint");
  host_restore_scope scope = {qa_decode_checkpoint_value(checkpoint->state.bytes.bytes, checkpoint->state.bytes.length)};
  if (scope.value == NULL) qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, host_restore_release, &scope);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, scope.value, "qvm.grapple.host")) qa_range_error("Out of memory");
  qa_save_reader f = fld(&reader, "version"); (void)qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  f = fld(&reader, "entityText"); (void)qa_save_reader_literal_string(&f, s->cursor.source); qa_save_reader_destroy(&f);
  s->milliseconds = int_field(&reader, "milliseconds", true);
  qa_save_reader data = fld(&reader, "data");
  qa_qvm_game_data_state layout;
  layout.entities_word = int_field(&data, "entitiesWord", false);
  layout.num_entities = int_field(&data, "numEntities", true);
  layout.entity_stride = int_field(&data, "entityStride", true);
  layout.clients_word = int_field(&data, "clientsWord", false);
  layout.client_stride = int_field(&data, "clientStride", true);
  qa_save_reader_destroy(&data);
  qa_qvm_game_data_restore(&s->vm.data, layout);
  f = fld(&reader, "cvars"); qa_cvar_registry_restore_save_state(s->cvars, f.value); qa_save_reader_destroy(&f);
  indexed_clear(s->configstrings, &s->configstring_count);
  indexed_clear(s->userinfo, &s->userinfo_count);
  qa_save_reader list = fld(&reader, "userinfo");
  size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell = itm(&list, i);
    double slot = int_field(&cell, "slot", true);
    qa_save_reader v = fld(&cell, "value");
    const char *text = qa_save_reader_string(&v);
    qa_save_reader_destroy(&v);
    if (slot >= 64 || indexed_get(s->userinfo, s->userinfo_count, (int64_t)slot) != NULL)
      qa_raise("Error", 0, 0, "Invalid saved grapple client");
    indexed_set(&s->userinfo, &s->userinfo_count, &s->userinfo_capacity, (int64_t)slot, text);
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  list = fld(&reader, "configstrings");
  count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell = itm(&list, i);
    double index = int_field(&cell, "index", true);
    qa_save_reader v = fld(&cell, "value");
    const char *text = qa_save_reader_string(&v);
    qa_save_reader_destroy(&v);
    if (index >= 1024 || indexed_get(s->configstrings, s->configstring_count, (int64_t)index) != NULL)
      qa_raise("Error", 0, 0, "Invalid saved grapple configstring");
    indexed_set(&s->configstrings, &s->configstring_count, &s->configstring_capacity, (int64_t)index, text);
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  f = fld(&reader, "cursor");
  if (qa_save_reader_is_null(&f)) qa_common_parse_cursor_set_null(&s->cursor);
  else qa_common_parse_cursor_set_offset(&s->cursor, (size_t)qa_save_reader_integer_min(&f, 0));
  qa_save_reader_destroy(&f);
  f = fld(&reader, "parser"); qa_common_parse_state_restore(&s->parser, f.value); qa_save_reader_destroy(&f);
  f = fld(&reader, "files"); qa_qvm_files_restore_checkpoint(s->files, f.value); qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&cleanup);
  host_restore_release(&scope);
}

/* ---------------- construction ---------------- */
static void cvar_print(void *ctx, const char *text) { print_event(ctx, text); }
static void command_print(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  print_event(ctx, text);
}
static qa_script_read_result read_script(void *ctx, const char *name, qa_command_context source) {
  (void)source;
  qa_app_sim_qvm_grapple_source *s = ctx;
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_READY;
  qa_content_opened_resource resource;
  if (!qa_content_mounted_open(s->options.mounts, name, NULL, NULL, &resource)) return result;
  char *text = malloc(resource.byte_length + 1);
  if (text == NULL) { free(resource.bytes); qa_range_error("Out of memory"); }
  memcpy(text, resource.bytes, resource.byte_length);
  text[resource.byte_length] = '\0';
  free(resource.bytes);
  result.has_text = true;
  result.text = text;
  return result;
}
static void files_print(void *ctx, const char *text) { print_event(ctx, text); }
static void files_assert_current(void *ctx) { current(ctx); }
static void on_release(void *ctx, qa_owned_actor actor) { remove_actor(ctx, actor.id); }
static void register_cvar(qa_cvar_registry *cvars, const char *name, const char *value, bool set) {
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (!qa_cvar_registry_register(cvars, name, value, 0, &snapshot)) qa_range_error("Out of memory");
  if (set && !qa_cvar_registry_set(cvars, name, value, true, &snapshot)) qa_range_error("Out of memory");
}
static void construct(qa_app_sim_qvm_grapple_source *s) {
  const qa_app_sim_qvm_grapple_source_options *options = &s->options;
  char *entities = qa_app_sim_qvm_weapon_initialization_entities(options->entity_text);
  bool cursor_ok = qa_common_parse_cursor_init(&s->cursor, entities, strlen(entities), false);
  free(entities);
  if (!cursor_ok) qa_range_error("Out of memory");
  s->cursor_ready = true;
  qa_common_parse_state_init(&s->parser);
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context = options->context;
  cvar_options.print = cvar_print;
  cvar_options.print_ctx = s;
  s->cvars = qa_cvar_registry_create(&cvar_options);
  if (s->cvars == NULL) qa_range_error("Out of memory");
  static const char *const defaults[][2] = {{"g_log", ""}, {"cm_noCurves", "0"}, {"cm_playerCurveClip", "1"},
      {"bot_enable", "0"}, {"sv_maxclients", "64"}, {"dedicated", "1"}, {"g_gametype", "0"}};
  for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++) register_cvar(s->cvars, defaults[i][0], defaults[i][1], false);
  for (size_t i = 0; i < options->profile->initial_cvar_count; i++)
    register_cvar(s->cvars, options->profile->initial_cvars[i].name, options->profile->initial_cvars[i].value, true);
  qa_command_buffer_options command_options;
  memset(&command_options, 0, sizeof command_options);
  command_options.dialect = QA_COMMAND_DIALECT_Q3;
  command_options.context = options->context;
  command_options.cvars = s->cvars;
  command_options.print = command_print;
  command_options.print_ctx = s;
  command_options.read_script = read_script;
  command_options.read_script_ctx = s;
  command_options.builtins = true;
  s->commands = qa_command_buffer_create(&command_options);
  if (s->commands == NULL) qa_range_error("Out of memory");
  qa_qvm_file_services file_services = {options->mounts, NULL, s, files_print, files_assert_current};
  s->files = qa_qvm_files_create(file_services);
  if (s->files == NULL) qa_range_error("Out of memory");
  s->host_state.ctx = s;
  s->host_state.checkpoint = host_checkpoint;
  s->host_state.restore = host_restore;
  qa_qvm_module_options module_options;
  memset(&module_options, 0, sizeof module_options);
  module_options.artifact = options->artifact;
  module_options.host = syscall;
  module_options.host_ctx = s;
  module_options.host_state = &s->host_state;
  qa_qvm_game_init(&s->vm, &module_options);
  s->vm_ready = true;
  qa_qvm_game_data_set_client_count(&s->vm.data, 64);
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = options->artifact->module;
  reference.instruction_index = options->profile->callbacks.same_team;
  (void)qa_qvm_module_bind_function(&s->vm.module, &reference, same_team, s);
  reference.instruction_index = options->profile->callbacks.damage;
  (void)qa_qvm_module_bind_function(&s->vm.module, &reference, damage, s);
  qa_qvm_server_spatial_operations spatial = {s, sp_trace, sp_point_contents, sp_area_entities, sp_entity_contact,
      sp_set_brush_model, sp_adjust_area_portal_state, sp_in_pvs, sp_areas_connected, sp_link, sp_unlink};
  s->spatial = spatial;
  memset(&s->services, 0, sizeof s->services);
  s->services.data = &s->vm.data;
  s->services.cvars = s->cvars;
  s->services.configstrings_ctx = s;
  s->services.configstring_get = cs_get;
  s->services.configstring_set = cs_set;
  s->services.max_clients = 64;
  s->services.spatial = &s->spatial;
  s->services.ctx = s;
  s->services.get_userinfo = ui_get;
  s->services.set_userinfo = ui_set;
  s->services.get_user_command = user_command;
  s->services.drop_client = drop_client;
  s->services.send_server_command = send_server_command;
  s->services.entity_tokens.ctx = s;
  s->services.entity_tokens.entity_token = entity_token;
  s->release_subscription = qa_session_actor_registry_on_release(options->actors, on_release, s);
  s->subscribed = true;
}
typedef struct create_scope { qa_app_sim_qvm_grapple_source *source; } create_scope;
static void create_failed(void *arg) { qa_app_sim_qvm_grapple_source_destroy(((create_scope *)arg)->source); }
static void no_dispatch(void *ctx) { (void)ctx; }
qa_app_sim_qvm_grapple_source *qa_app_sim_qvm_grapple_source_create(qa_app_sim_qvm_grapple_source_options options) {
  create_scope scope = {xcalloc(1, sizeof(qa_app_sim_qvm_grapple_source))};
  qa_app_sim_qvm_grapple_source *s = scope.source;
  s->options = options;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, create_failed, &scope);
  construct(s);
  qa_qvm_game_initialize_async(&s->vm, 0, options.seed, false);
  (void)qa_command_buffer_execute_scripts_async(s->commands, no_dispatch, NULL, NULL, NULL);
  const qa_qvm_image *image = &options.artifact->image;
  double word = ceil((double)(image->data_length + image->literal_length + image->bss_length) / 16) * 16;
  double byte_length = qa_js_max(64, options.profile->movement.byte_length);
  if (word + byte_length > (double)image->allocated_data_length - 65536)
    qa_raise("Error", 0, 0, "Grapple source has no reserved scratch below the VM stack");
  qa_qvm_grapple_bridge bridge = {s, bridge_synchronize, bridge_entity, bridge_actor, bridge_restore_actor,
                                  bridge_publish, bridge_velocity, word, byte_length};
  s->provider = qa_qvm_grapple_provider_create(&s->vm, options.artifact, *options.profile, bridge);
  if (s->provider == NULL) qa_range_error("Out of memory");
  qa_cleanup_pop(&cleanup);
  return s;
}

/* ---------------- public surface ---------------- */
qa_session_actor_registry *qa_app_sim_qvm_grapple_source_actors(const qa_app_sim_qvm_grapple_source *s) { return s->options.actors; }
qa_shared_body_table *qa_app_sim_qvm_grapple_source_bodies(const qa_app_sim_qvm_grapple_source *s) { return s->options.bodies; }
qa_qvm_game *qa_app_sim_qvm_grapple_source_vm(qa_app_sim_qvm_grapple_source *s) { return &s->vm; }
qa_qvm_grapple_provider *qa_app_sim_qvm_grapple_source_core(qa_app_sim_qvm_grapple_source *s) {
  if (s->provider == NULL) qa_raise("Error", 0, 0, "Grapple source has not initialized");
  return s->provider;
}
void qa_app_sim_qvm_grapple_source_begin_frame(qa_app_sim_qvm_grapple_source *s, double milliseconds, double frame) {
  current(s);
  s->milliseconds = milliseconds;
  qa_qvm_grapple_provider_begin_frame(qa_app_sim_qvm_grapple_source_core(s), milliseconds, frame);
  qa_qvm_grapple_provider_step(qa_app_sim_qvm_grapple_source_core(s));
  const qa_qvm_grapple_definition *profile = s->options.profile;
  for (double slot = 64; slot < qa_qvm_game_data_get_num_entities(&s->vm.data); slot++) {
    qa_qvm_writable_view record;
    qa_qvm_game_data_entity_bytes(&s->vm.data, slot, &record);
    if (qa_qvm_writable_view_get_int32(&record, (int64_t)profile->fields.inuse, true) != 0
        && qa_qvm_writable_view_get_int32(&record, (int64_t)profile->fields.free_after_event, true) != 0
        && milliseconds - qa_qvm_writable_view_get_int32(&record, (int64_t)profile->fields.event_time, true)
               > profile->event_lifetime_milliseconds) {
      int32_t word = (int32_t)record.start;
      (void)qa_qvm_module_call(&s->vm.module, &word, 1, (int64_t)profile->callbacks.free);
    }
  }
  for (size_t i = 0; i < s->tether_count; i++) {
    if (!s->tethers[i].projection.pulling) continue;
    qa_actor_id owner = s->tethers[i].owner;
    qa_vec3 point = s->tethers[i].projection.point;
    qa_body_state_result body = qa_shared_body_table_read(s->options.bodies, owner);
    bool hanging = body.has_value
                   && qa_js_hypot3(point.x - body.value.origin.x, point.y - body.value.origin.y,
                                   point.z - body.value.origin.z - 26) <= 64;
    sound(s, owner, hanging ? profile->presentation.hang_sound : profile->presentation.pull_sound, true);
  }
}
bool qa_app_sim_qvm_grapple_source_hook(const qa_app_sim_qvm_grapple_source *s, qa_actor_id actor, qa_actor_id *out) {
  tether *t = find_tether(s, actor);
  if (t == NULL) return false;
  *out = t->actor.id;
  return true;
}
void qa_app_sim_qvm_grapple_source_fire(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  current(s);
  qa_actor_id hook;
  bool previous = qa_app_sim_qvm_grapple_source_hook(s, actor, &hook);
  qa_qvm_grapple_provider_fire(qa_app_sim_qvm_grapple_source_core(s), actor);
  if (!previous && qa_app_sim_qvm_grapple_source_hook(s, actor, &hook))
    sound(s, actor, s->options.profile->presentation.fire_sound, false);
}
void qa_app_sim_qvm_grapple_source_admit(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  current(s);
  (void)mirror(s, actor, NULL);
  binding *b = find_binding(s, actor);
  if (b == NULL || !b->client) qa_raise("Error", 0, 0, "Grapple equipment requires an admitted source client");
}
void qa_app_sim_qvm_grapple_source_release(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  qa_actor_id hook;
  bool previous = qa_app_sim_qvm_grapple_source_hook(s, actor, &hook);
  qa_qvm_grapple_provider_release(qa_app_sim_qvm_grapple_source_core(s), actor, false);
  if (previous && !qa_app_sim_qvm_grapple_source_hook(s, actor, &hook))
    sound(s, actor, s->options.profile->presentation.release_sound, false);
}
qa_app_sim_grapple_weapon_view qa_app_sim_qvm_grapple_source_weapon_view(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  binding *b = find_binding(s, actor);
  if (b == NULL || !b->client) qa_raise("Error", 0, 0, "Hook view has no source player");
  qa_q3_player_state state;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_qvm_game_data_copy_player_state(&s->vm.data, slot_of(s, b->pointer), &state, stats, persistent, powerups, ammo);
  const qa_qvm_grapple_presentation *presentation = &s->options.profile->presentation;
  qa_app_sim_grapple_weapon_view view;
  memset(&view, 0, sizeof view);
  view.path = presentation->view_model;
  view.has_q3 = true;
  view.model_attachments = presentation->view_attachments;
  view.model_attachment_count = presentation->view_attachment_count;
  view.model_anchor = &presentation->view_anchor;
  view.q3_time_milliseconds = s->milliseconds;
  view.q3_torso_animation = state.torso_animation;
  qa_actor_id hook;
  view.q3_firing = qa_app_sim_qvm_grapple_source_hook(s, actor, &hook);
  view.q3_horizontal_speed = qa_js_hypot2(state.velocity.x, state.velocity.y);
  view.q3_bob_cycle = state.bob_cycle;
  view.q3_weapon = presentation->weapon_index;
  return view;
}
const qa_app_simulation_presentation *qa_app_sim_qvm_grapple_source_presentations(qa_app_sim_qvm_grapple_source *s,
                                                                                  qa_content_id content, size_t *out_count) {
  const qa_qvm_grapple_presentation *presentation = &s->options.profile->presentation;
  size_t needed = s->tether_count * 2;
  if (needed > s->presentation_capacity) {
    s->presentations = xrealloc(s->presentations, needed * sizeof *s->presentations);
    s->presentation_capacity = needed;
  }
  if (s->tether_count > s->cable_capacity) {
    s->cables = xrealloc(s->cables, s->tether_count * sizeof *s->cables);
    s->cable_capacity = s->tether_count;
  }
  size_t count = 0;
  for (size_t i = 0; i < s->tether_count; i++) {
    const tether *t = &s->tethers[i];
    qa_body_state_result body = qa_shared_body_table_read(s->options.bodies, t->actor.id);
    const qa_app_sim_qvm_grapple_target *player = find_target(s, t->owner);
    if (!body.has_value || player == NULL) continue;
    qa_app_simulation_presentation common;
    memset(&common, 0, sizeof common);
    common.actor = t->actor.id;
    common.content = content;
    common.family = QA_GAME_FAMILY_Q3;
    common.origin = body.value.origin;
    common.angles = body.value.angles;
    common.scale = 1;
    common.visible = true;
    qa_app_simulation_presentation projectile = common;
    projectile.path = presentation->projectile_model;
    s->presentations[count++] = projectile;
    double view_height = player->is_player ? player->view_height : 0;
    qa_vec3 start = {player->body.origin.x, player->body.origin.y, player->body.origin.z + view_height};
    qa_app_simulation_presentation cable = common;
    if (presentation->cable.is_shader) {
      cable.path = "";
      cable.origin = start;
      cable.has_shader_beam = true;
      cable.shader_beam_path = presentation->cable.path;
      cable.shader_beam_end = body.value.origin;
      cable.shader_beam_width = presentation->cable.width;
    } else {
      qa_app_sim_q3_grapple_cable *payload = &s->cables[i];
      payload->owner = t->owner;
      payload->owner_origin = player->body.origin;
      payload->owner_angles = player->body.angles;
      payload->view_height = view_height;
      payload->offhand = !is(s->options.binding, "slot");
      payload->attached = t->projection.pulling;
      payload->flight = presentation->cable.flight;
      payload->pull = presentation->cable.pull;
      payload->hold = presentation->cable.hold;
      payload->segment_length = presentation->cable.segment_length;
      cable.path = presentation->cable.flight;
      cable.q3_grapple_cable = payload;
    }
    s->presentations[count++] = cable;
  }
  *out_count = count;
  return s->presentations;
}
bool qa_app_sim_qvm_grapple_source_pulling(const qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  tether *t = find_tether(s, actor);
  return t != NULL && t->projection.pulling;
}
void qa_app_sim_qvm_grapple_source_pull(qa_app_sim_qvm_grapple_source *s, qa_actor_id actor) {
  qa_body_state_result body = qa_shared_body_table_read(s->options.bodies, actor);
  if (!body.has_value) return;
  synchronize(s);
  qa_vec3 ignored;
  (void)qa_qvm_grapple_provider_pull(qa_app_sim_qvm_grapple_source_core(s), actor,
                                     qa_qvm_angle_vectors(body.value.angles).forward, &ignored);
}
qa_app_sim_qvm_grapple_source_checkpoint qa_app_sim_qvm_grapple_source_capture(qa_app_sim_qvm_grapple_source *s) {
  qa_app_sim_qvm_grapple_source_checkpoint out;
  memset(&out, 0, sizeof out);
  out.version = 1;
  qa_qvm_grapple_provider_capture(qa_app_sim_qvm_grapple_source_core(s), &out.grapple);
  out.bindings = xcalloc(s->binding_count, sizeof *out.bindings);
  out.binding_count = s->binding_count;
  for (size_t i = 0; i < s->binding_count; i++) {
    out.bindings[i].actor = qa_saved_actor_id_of(s->bindings[i].actor);
    out.bindings[i].pointer = s->bindings[i].pointer;
    out.bindings[i].client = s->bindings[i].client;
    out.bindings[i].origin = s->bindings[i].origin;
  }
  out.tethers = xcalloc(s->tether_count, sizeof *out.tethers);
  out.tether_count = s->tether_count;
  for (size_t i = 0; i < s->tether_count; i++) {
    out.tethers[i].owner = qa_saved_actor_id_of(s->tethers[i].owner);
    out.tethers[i].actor = qa_saved_actor_id_of(s->tethers[i].actor.id);
  }
  return out;
}
typedef struct restore_scope { qa_app_sim_qvm_grapple_source *source; restored_tether *restored; } restore_scope;
static void restore_finally(void *arg) {
  restore_scope *scope = arg;
  scope->source->has_restoring = false;
  scope->source->restoring = NULL;
  scope->source->restoring_count = 0;
  free(scope->restored);
  scope->restored = NULL;
}
void qa_app_sim_qvm_grapple_source_restore(qa_app_sim_qvm_grapple_source *s,
                                           const qa_app_sim_qvm_grapple_source_checkpoint *checkpoint) {
  if (checkpoint->version != 1) qa_raise("Error", 0, 0, "Invalid grapple source checkpoint");
  s->binding_count = 0;
  for (size_t i = 0; i < checkpoint->binding_count; i++) {
    const qa_app_sim_qvm_grapple_binding *entry = &checkpoint->bindings[i];
    qa_actor_id actor = restore_actor(s, entry->actor);
    bool duplicate = find_binding(s, actor) != NULL;
    for (size_t j = 0; j < s->binding_count; j++) if (s->bindings[j].pointer == entry->pointer) duplicate = true;
    if (duplicate) qa_raise("Error", 0, 0, "Duplicate saved grapple binding");
    binding value = {actor, entry->pointer, entry->client, entry->origin};
    add_binding(s, value);
  }
  restore_scope scope = {s, xcalloc(checkpoint->tether_count, sizeof(restored_tether))};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, restore_finally, &scope);
  size_t restored_count = 0;
  for (size_t i = 0; i < checkpoint->tether_count; i++) {
    qa_actor_id owner = restore_actor(s, checkpoint->tethers[i].owner);
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(s->options.actors, checkpoint->tethers[i].actor, &actor);
    bool duplicate = false;
    for (size_t j = 0; j < restored_count; j++) if (qa_same_actor(scope.restored[j].owner, owner)) duplicate = true;
    if (!resolved || !is(actor.owner, s->options.provider) || duplicate)
      qa_raise("Error", 0, 0, "Invalid saved shared tether");
    scope.restored[restored_count].owner = owner;
    scope.restored[restored_count].actor = actor;
    restored_count++;
  }
  s->tether_count = 0;
  s->restoring = scope.restored;
  s->restoring_count = restored_count;
  s->has_restoring = true;
  qa_qvm_grapple_provider_restore(qa_app_sim_qvm_grapple_source_core(s), &checkpoint->grapple);
  qa_cleanup_pop(&cleanup);
  restore_finally(&scope);
}

typedef struct close_try { qa_app_sim_qvm_grapple_source *source; } close_try;
static void close_provider(void *ctx) {
  qa_app_sim_qvm_grapple_source *s = ((close_try *)ctx)->source;
  if (s->provider != NULL) qa_qvm_grapple_provider_close(s->provider);
}
static void close_files(void *ctx) {
  qa_app_sim_qvm_grapple_source *s = ((close_try *)ctx)->source;
  if (s->files != NULL) qa_qvm_files_close_all(s->files);
}
void qa_app_sim_qvm_grapple_source_close(qa_app_sim_qvm_grapple_source *s) {
  if (s->closed) return;
  if (s->subscribed) {
    qa_session_actor_registry_off_release(s->options.actors, s->release_subscription);
    s->subscribed = false;
  }
  close_try job = {s};
  qa_caught_error provider_error, files_error;
  bool provider_ok = qa_try(close_provider, &job, &provider_error);
  s->closed = true;
  s->binding_count = 0;
  s->tether_count = 0;
  bool files_ok = qa_try(close_files, &job, &files_error);
  if (s->vm_ready) qa_qvm_game_retire(&s->vm);
  if (!files_ok) qa_rethrow(&files_error);
  if (!provider_ok) qa_rethrow(&provider_error);
}
void qa_app_sim_qvm_grapple_source_destroy(qa_app_sim_qvm_grapple_source *s) {
  if (s == NULL) return;
  if (!s->closed) qa_app_sim_qvm_grapple_source_close(s);
  if (s->provider != NULL) qa_qvm_grapple_provider_destroy(s->provider);
  if (s->vm_ready) qa_qvm_game_destroy(&s->vm);
  if (s->files != NULL) qa_qvm_files_destroy(s->files);
  if (s->commands != NULL) qa_command_buffer_destroy(s->commands);
  if (s->cvars != NULL) qa_cvar_registry_destroy(s->cvars);
  if (s->cursor_ready) qa_common_parse_cursor_destroy(&s->cursor);
  indexed_clear(s->configstrings, &s->configstring_count);
  indexed_clear(s->userinfo, &s->userinfo_count);
  free(s->configstrings); free(s->userinfo); free(s->bindings); free(s->tethers);
  free(s->host_bytes); free(s->presentations); free(s->cables);
  free(s);
}
