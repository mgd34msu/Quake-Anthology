/* Port of src/compat/qvm/grapple-provider.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/grapple_provider.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/entity_record.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

struct qa_qvm_grapple_provider {
  qa_qvm_game *game; /* borrowed */
  qa_qvm_grapple_profile profile;
  qa_qvm_grapple_bridge bridge;
  char *declaration; /* owned; TS JSON.stringify(declaration) identity */
  qa_actor_id *owners;
  size_t owner_count;
  size_t owner_capacity;
};

const qa_qvm_grapple_profile *qa_qvm_grapple_provider_get_profile(
    const qa_qvm_grapple_provider *provider) {
  return &provider->profile;
}

static bool is_integer(double value) { return isfinite(value) && trunc(value) == value; }
static bool is_safe_integer(double value) {
  return is_integer(value) && value >= -9007199254740991.0 && value <= 9007199254740991.0;
}

/* TS word(): memory.view(pointer, offset + 4).getInt32(offset, true). */
static int32_t word_at(qa_qvm_grapple_provider *p, int64_t pointer, int64_t offset) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(p->game->module.memory, pointer, offset + 4, 0, &view);
  return qa_qvm_writable_view_get_int32(&view, offset, true);
}
static int32_t word(qa_qvm_grapple_provider *p, double pointer, double offset) {
  return word_at(p, (int64_t)pointer, (int64_t)offset);
}
static void write_word(qa_qvm_grapple_provider *p, double pointer, double value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(p->game->module.memory, (int64_t)pointer, 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, (int32_t)value, true);
}
static qa_vec3 read_vector(qa_qvm_grapple_provider *p, double pointer) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(p->game->module.memory, (int64_t)pointer, 12, 0, &view);
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&view, 0, true),
               qa_qvm_writable_view_get_float32(&view, 4, true),
               qa_qvm_writable_view_get_float32(&view, 8, true)};
  return v;
}
static void write_vector(qa_qvm_grapple_provider *p, double pointer, qa_vec3 value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(p->game->module.memory, (int64_t)pointer, 12, 0, &view);
  qa_qvm_writable_view_set_float32(&view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(&view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(&view, 8, value.z, true);
}

static void validate_layout(qa_qvm_grapple_provider *p) {
  if (qa_qvm_game_data_get_entity_stride_bytes(&p->game->data) != p->profile.entity_stride ||
      qa_qvm_game_data_get_client_stride_bytes(&p->game->data) != p->profile.client_stride)
    qa_range_error("Grapple profile differs from located entity or client records");
}

typedef struct owner_record { int32_t entity; int32_t client; } owner_record;
static owner_record owner_of(qa_qvm_grapple_provider *p, qa_actor_id actor) {
  double entity = 0;
  bool has = p->bridge.entity(p->bridge.ctx, actor, &entity);
  if (!has || word(p, entity, p->profile.fields.inuse) == 0)
    qa_range_error("Grapple owner has no live source entity");
  (void)qa_qvm_game_data_number_from_pointer(&p->game->data, entity);
  int32_t client = word(p, entity, p->profile.fields.client);
  if (client == 0) qa_range_error("Grapple owner has no source player record");
  qa_qvm_writable_view view;
  qa_qvm_memory_view(p->game->module.memory, client, (int64_t)p->profile.client_stride, 0, &view);
  owner_record record = {(int32_t)entity, client};
  return record;
}

/* False is the TS null hook. */
static bool hook_of(qa_qvm_grapple_provider *p, qa_actor_id actor, int32_t *out) {
  owner_record player = owner_of(p, actor);
  int32_t hook = word_at(p, player.client, (int64_t)p->profile.fields.hook);
  if (hook == 0) return false;
  (void)qa_qvm_game_data_number_from_pointer(&p->game->data, hook);
  if (word_at(p, hook, (int64_t)p->profile.fields.inuse) == 0 ||
      word_at(p, hook, (int64_t)p->profile.fields.parent) != player.entity)
    qa_range_error("Source grapple points at an inactive or differently owned hook");
  *out = hook;
  return true;
}

static bool owners_contains(const qa_qvm_grapple_provider *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->owner_count; i++)
    if (qa_same_actor(p->owners[i], actor)) return true;
  return false;
}
static void owners_add(qa_qvm_grapple_provider *p, qa_actor_id actor) {
  if (owners_contains(p, actor)) return;
  if (p->owner_count == p->owner_capacity) {
    size_t capacity = p->owner_capacity == 0 ? 8 : p->owner_capacity * 2;
    qa_actor_id *grown = realloc(p->owners, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    p->owners = grown;
    p->owner_capacity = capacity;
  }
  p->owners[p->owner_count++] = actor;
}
static void owners_delete(qa_qvm_grapple_provider *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->owner_count; i++) {
    if (qa_same_actor(p->owners[i], actor)) {
      memmove(&p->owners[i], &p->owners[i + 1], (p->owner_count - i - 1) * sizeof *p->owners);
      p->owner_count--;
      return;
    }
  }
}

/* False is the TS null projection. */
static bool projection_of(qa_qvm_grapple_provider *p, qa_actor_id actor,
                          qa_qvm_grapple_projection *out) {
  int32_t hook = 0;
  if (!hook_of(p, actor, &hook)) return false;
  owner_record player = owner_of(p, actor);
  qa_qvm_shared_entity *entity = qa_qvm_game_data_entity_from_pointer(&p->game->data, hook);
  memset(out, 0, sizeof *out);
  out->hook = hook;
  out->origin = qa_qvm_shared_entity_get_current_origin(entity);
  qa_qvm_entity_state state = qa_qvm_borrow_entity_state(entity->s.view, entity->profile);
  out->velocity = qa_qvm_entity_state_get_pos(&state).delta;
  out->has_target =
      p->bridge.actor(p->bridge.ctx, word_at(p, hook, (int64_t)p->profile.fields.target),
                      &out->target);
  if (p->profile.fields.has_mover)
    out->has_mover =
        p->bridge.actor(p->bridge.ctx, word_at(p, hook, (int64_t)p->profile.fields.mover),
                        &out->mover);
  out->pulling = (word_at(p, player.client, 12) & (int32_t)p->profile.pulling_flag) != 0;
  out->point = read_vector(p, (double)player.client + 92);
  out->owner_velocity = read_vector(p, (double)player.client + 32);
  return true;
}

static void publish(qa_qvm_grapple_provider *p, qa_actor_id actor) {
  qa_qvm_grapple_projection projection;
  bool has = projection_of(p, actor, &projection);
  p->bridge.publish(p->bridge.ctx, actor, has ? &projection : NULL);
  p->bridge.velocity(p->bridge.ctx, actor,
                     read_vector(p, (double)owner_of(p, actor).client + 32));
}

/* JSON.stringify over the declaration's value kinds. */
typedef struct json_text {
  char *bytes;
  size_t length, capacity;
} json_text;

static void json_append(json_text *text, const char *bytes, size_t length) {
  if (text->length + length + 1 > text->capacity) {
    size_t capacity = text->capacity == 0 ? 256 : text->capacity;
    while (text->length + length + 1 > capacity) capacity *= 2;
    char *grown = realloc(text->bytes, capacity);
    if (grown == NULL) qa_range_error("QVM memory allocation failed");
    text->bytes = grown;
    text->capacity = capacity;
  }
  memcpy(text->bytes + text->length, bytes, length);
  text->length += length;
  text->bytes[text->length] = '\0';
}

static void json_string(json_text *text, const char *value) {
  json_append(text, "\"", 1);
  for (const unsigned char *c = (const unsigned char *)value; *c != '\0'; c++) {
    char escape[8];
    switch (*c) {
      case '"': json_append(text, "\\\"", 2); break;
      case '\\': json_append(text, "\\\\", 2); break;
      case '\b': json_append(text, "\\b", 2); break;
      case '\f': json_append(text, "\\f", 2); break;
      case '\n': json_append(text, "\\n", 2); break;
      case '\r': json_append(text, "\\r", 2); break;
      case '\t': json_append(text, "\\t", 2); break;
      default:
        if (*c < 0x20) {
          int length = snprintf(escape, sizeof escape, "\\u%04x", *c);
          json_append(text, escape, (size_t)length);
        } else {
          json_append(text, (const char *)c, 1);
        }
    }
  }
  json_append(text, "\"", 1);
}

static void json_value(json_text *text, const qa_save_value *value) {
  switch (value->kind) {
    case QA_SAVE_NULL: json_append(text, "null", 4); break;
    case QA_SAVE_BOOLEAN:
      if (value->boolean) json_append(text, "true", 4);
      else json_append(text, "false", 5);
      break;
    case QA_SAVE_NUMBER: {
      char number[40];
      qa_net_json_number(value->number, number, sizeof number);
      json_append(text, number, strlen(number));
      break;
    }
    case QA_SAVE_STRING: json_string(text, value->text); break;
    case QA_SAVE_ARRAY:
      json_append(text, "[", 1);
      for (size_t i = 0; i < value->item_count; i++) {
        if (i != 0) json_append(text, ",", 1);
        json_value(text, value->items[i]);
      }
      json_append(text, "]", 1);
      break;
    case QA_SAVE_OBJECT:
      json_append(text, "{", 1);
      for (size_t i = 0; i < value->entry_count; i++) {
        if (i != 0) json_append(text, ",", 1);
        json_string(text, value->entries[i].key);
        json_append(text, ":", 1);
        json_value(text, value->entries[i].value);
      }
      json_append(text, "}", 1);
      break;
    default: qa_range_error("Grapple declaration holds a value JSON cannot represent");
  }
}

static char *stringify_declaration(const qa_save_value *declaration) {
  json_text text = {NULL, 0, 0};
  json_value(&text, declaration);
  return text.bytes;
}

qa_qvm_grapple_provider *qa_qvm_grapple_provider_create(qa_qvm_game *game,
                                                        const qa_resolved_qvm_artifact *artifact,
                                                        qa_qvm_grapple_profile profile,
                                                        qa_qvm_grapple_bridge bridge) {
  qa_qvm_grapple_provider *p = calloc(1, sizeof *p);
  if (p == NULL) return NULL;
  p->game = game;
  p->profile = profile;
  p->bridge = bridge;
  const qa_module_identity *actual = &game->module.profile.module;
  const qa_module_identity *expected = &p->profile.module;
  if (strcmp(actual->id, expected->id) != 0 || strcmp(actual->digest, expected->digest) != 0 ||
      strcmp(actual->artifact_path, expected->artifact_path) != 0 ||
      strcmp(actual->revision, expected->revision) != 0 ||
      strcmp(qa_qvm_module_get_abi_profile(&game->module), p->profile.abi_profile) != 0)
    qa_range_error("Grapple module differs from its declared artifact");
  validate_layout(p);
  int64_t source_end = artifact->image.data_length + artifact->image.literal_length +
                       artifact->image.bss_length;
  double required = profile.movement.byte_length > 16 ? profile.movement.byte_length : 16;
  if (!is_safe_integer(bridge.scratch_word) || (int64_t)bridge.scratch_word % 4 != 0 ||
      bridge.scratch_word < (double)source_end || bridge.scratch_byte_length < required)
    qa_range_error("Grapple scratch must be a reserved span beyond declared source data");
  qa_qvm_writable_view view;
  qa_qvm_memory_view(game->module.memory, (int64_t)bridge.scratch_word,
                     (int64_t)bridge.scratch_byte_length, 0, &view);
  /* TS: this.profile = readQvmGrappleProfile(declaration); declaration =
   * JSON.stringify(...). The read revalidates every field; a rejected declaration
   * aborts. Strings in the validated copy borrow from the tree, so the caller's
   * profile, unchanged by the read, stays the stored one. */
  qa_save_value *declared = qa_qvm_grapple_profile_declaration(&p->profile);
  qa_qvm_grapple_profile *validated =
      declared == NULL ? NULL : qa_qvm_read_grapple_profile(declared, artifact);
  if (validated == NULL) {
    qa_save_value_free(declared);
    free(p);
    return NULL;
  }
  qa_qvm_grapple_profile_free(validated);
  p->declaration = stringify_declaration(declared);
  qa_save_value_free(declared);
  if (p->declaration == NULL) {
    free(p);
    return NULL;
  }
  return p;
}

void qa_qvm_grapple_provider_destroy(qa_qvm_grapple_provider *provider) {
  if (provider == NULL) return;
  free(provider->declaration);
  free(provider->owners);
  free(provider);
}

void qa_qvm_grapple_provider_begin_frame(qa_qvm_grapple_provider *provider,
                                         double time_milliseconds, double frame) {
  if (!is_integer(time_milliseconds) ||
      time_milliseconds < word(provider, provider->profile.globals.time, 0) ||
      time_milliseconds > 0x7fffffff || !is_integer(frame) ||
      frame < word(provider, provider->profile.globals.frame, 0) || frame > 0x7fffffff)
    qa_range_error("Invalid grapple source frame");
  write_word(provider, provider->profile.globals.time, time_milliseconds);
  write_word(provider, provider->profile.globals.frame, frame);
  provider->bridge.synchronize(provider->bridge.ctx);
}

void qa_qvm_grapple_provider_fire(qa_qvm_grapple_provider *provider, qa_actor_id actor) {
  provider->bridge.synchronize(provider->bridge.ctx);
  owner_record owner = owner_of(provider, actor);
  owners_add(provider, actor);
  int32_t hook = 0;
  if (!hook_of(provider, actor, &hook)) {
    /* TS call([owner.entity, ...profile.fireArguments], callbacks.fire). */
    size_t count = 1 + provider->profile.fire_arguments.count;
    int32_t *words = malloc((count == 0 ? 1 : count) * sizeof *words);
    if (words == NULL) qa_range_error("QVM memory allocation failed");
    words[0] = owner.entity;
    for (size_t i = 0; i < provider->profile.fire_arguments.count; i++)
      words[1 + i] = (int32_t)provider->profile.fire_arguments.values[i];
    (void)qa_qvm_module_call(&provider->game->module, words, count,
                             (int64_t)provider->profile.callbacks.fire);
    free(words);
  }
  publish(provider, actor);
}

void qa_qvm_grapple_provider_release(qa_qvm_grapple_provider *provider, qa_actor_id actor,
                                     bool force) {
  if (!owners_contains(provider, actor)) return;
  int32_t hook = 0;
  if (hook_of(provider, actor, &hook))
    (void)qa_qvm_module_call(&provider->game->module, &hook, 1,
                             (int64_t)(force ? provider->profile.callbacks.force_release
                                             : provider->profile.callbacks.release));
  publish(provider, actor);
  int32_t after = 0;
  if (!hook_of(provider, actor, &after)) owners_delete(provider, actor);
}

void qa_qvm_grapple_provider_step(qa_qvm_grapple_provider *provider) {
  provider->bridge.synchronize(provider->bridge.ctx);
  /* TS iterates a snapshot ([...this.owners]); copy it. */
  size_t count = provider->owner_count;
  qa_actor_id *snapshot = malloc((count == 0 ? 1 : count) * sizeof *snapshot);
  if (snapshot == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(snapshot, provider->owners, count * sizeof *snapshot);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = snapshot[i];
    if (word_at(provider, owner_of(provider, actor).entity,
                (int64_t)provider->profile.fields.health) <= 0) {
      qa_qvm_grapple_provider_release(provider, actor, true);
      continue;
    }
    int32_t hook = 0;
    if (hook_of(provider, actor, &hook)) {
      qa_qvm_shared_entity *entity =
          qa_qvm_game_data_entity_from_pointer(&provider->game->data, hook);
      qa_qvm_entity_state state = qa_qvm_borrow_entity_state(entity->s.view, entity->profile);
      if (qa_qvm_entity_state_get_e_type(&state) == 3) {
        (void)qa_qvm_module_call(&provider->game->module, &hook, 1,
                                 (int64_t)provider->profile.callbacks.missile);
      } else {
        if (provider->profile.callbacks.has_follow)
          (void)qa_qvm_module_call(&provider->game->module, &hook, 1,
                                   (int64_t)provider->profile.callbacks.follow);
        int32_t current = 0;
        if (hook_of(provider, actor, &current))
          (void)qa_qvm_module_call(&provider->game->module, &hook, 1,
                                   (int64_t)provider->profile.callbacks.think);
      }
    }
    publish(provider, actor);
    int32_t after = 0;
    if (!hook_of(provider, actor, &after)) owners_delete(provider, actor);
  }
  free(snapshot);
}

bool qa_qvm_grapple_provider_pull(qa_qvm_grapple_provider *provider, qa_actor_id actor,
                                  qa_vec3 forward, qa_vec3 *out) {
  if (!owners_contains(provider, actor)) return false;
  qa_qvm_grapple_projection projection;
  if (!projection_of(provider, actor, &projection) || !projection.pulling) return false;
  const qa_qvm_grapple_globals *globals = &provider->profile.globals;
  qa_qvm_memory *memory = provider->game->module.memory;
  double scratch = provider->bridge.scratch_word;
  int32_t movement = word(provider, globals->movement, 0);
  qa_qvm_memory_span forward_span =
      qa_qvm_memory_get_span(memory, (int64_t)globals->forward, 12, 0);
  uint8_t *saved_forward = malloc(12);
  int64_t scratch_length = (int64_t)provider->profile.movement.byte_length;
  uint8_t *saved_scratch = malloc(scratch_length > 0 ? (size_t)scratch_length : 1);
  if (saved_forward == NULL || saved_scratch == NULL)
    qa_range_error("QVM memory allocation failed");
  memcpy(saved_forward, memory->bytes + forward_span.byte_offset, 12);
  int32_t ground_plane = word(provider, globals->ground_plane, 0);
  qa_qvm_memory_span scratch_span =
      qa_qvm_memory_get_span(memory, (int64_t)scratch, scratch_length, 0);
  memcpy(saved_scratch, memory->bytes + scratch_span.byte_offset,
         scratch_length > 0 ? (size_t)scratch_length : 0);
  owner_record player = owner_of(provider, actor);
  /* TS try/finally: a throw is fatal in C, so restoring after the
   * call on the normal path preserves observable behavior. */
  qa_qvm_memory_fill_bytes(memory, scratch_span.byte_offset, scratch_length, 0);
  for (size_t i = 0; i < provider->profile.movement.word_count; i++)
    write_word(provider, scratch + provider->profile.movement.words[i].offset,
               provider->profile.movement.words[i].value);
  write_word(provider, scratch, player.client);
  write_word(provider, globals->movement, scratch);
  write_vector(provider, globals->forward, forward);
  (void)qa_qvm_module_call(&provider->game->module, NULL, 0,
                           (int64_t)provider->profile.callbacks.pull);
  publish(provider, actor);
  *out = read_vector(provider, (double)player.client + 32);
  write_word(provider, globals->movement, movement);
  qa_qvm_memory_write_bytes(memory, forward_span.byte_offset, saved_forward, 12);
  write_word(provider, globals->ground_plane, ground_plane);
  qa_qvm_memory_write_bytes(memory, scratch_span.byte_offset, saved_scratch,
                            scratch_length > 0 ? (size_t)scratch_length : 0);
  free(saved_forward);
  free(saved_scratch);
  return true;
}

void qa_qvm_grapple_provider_mover_moved(qa_qvm_grapple_provider *provider, qa_actor_id actor,
                                         qa_vec3 translation) {
  if (!provider->profile.callbacks.has_move_mover_hooks) return;
  double mover = 0;
  if (!provider->bridge.entity(provider->bridge.ctx, actor, &mover))
    qa_range_error("Grapple mover has no source entity");
  qa_qvm_memory *memory = provider->game->module.memory;
  double scratch = provider->bridge.scratch_word;
  qa_qvm_memory_span span = qa_qvm_memory_get_span(memory, (int64_t)scratch, 12, 0);
  uint8_t saved[12];
  memcpy(saved, memory->bytes + span.byte_offset, 12);
  write_vector(provider, scratch, translation);
  int32_t words[2] = {(int32_t)mover, (int32_t)scratch};
  (void)qa_qvm_module_call(&provider->game->module, words, 2,
                           (int64_t)provider->profile.callbacks.move_mover_hooks);
  for (size_t i = 0; i < provider->owner_count; i++) publish(provider, provider->owners[i]);
  qa_qvm_memory_write_bytes(memory, span.byte_offset, saved, 12);
}

void qa_qvm_grapple_provider_actor_released(qa_qvm_grapple_provider *provider,
                                            qa_actor_id actor) {
  size_t count = provider->owner_count;
  qa_actor_id *snapshot = malloc((count == 0 ? 1 : count) * sizeof *snapshot);
  if (snapshot == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(snapshot, provider->owners, count * sizeof *snapshot);
  for (size_t i = 0; i < count; i++) {
    qa_qvm_grapple_projection state;
    bool has = projection_of(provider, snapshot[i], &state);
    if (qa_same_actor(snapshot[i], actor) || (has && state.has_target &&
                                              qa_same_actor(state.target, actor)) ||
        (has && state.has_mover && qa_same_actor(state.mover, actor)))
      qa_qvm_grapple_provider_release(provider, snapshot[i], true);
  }
  free(snapshot);
}

void qa_qvm_grapple_provider_capture(qa_qvm_grapple_provider *provider,
                                     qa_qvm_grapple_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->version = 1;
  out->profile = provider->declaration;
  qa_qvm_module_checkpoint(&provider->game->module, &out->module);
  out->owner_count = provider->owner_count;
  out->owners = malloc((out->owner_count == 0 ? 1 : out->owner_count) * sizeof *out->owners);
  if (out->owners == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < out->owner_count; i++) {
    out->owners[i].slot = (double)provider->owners[i].slot;
    out->owners[i].generation = (double)provider->owners[i].generation;
  }
}

void qa_qvm_grapple_checkpoint_destroy(qa_qvm_grapple_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  free((void *)checkpoint->module.data.bytes);
  free((void *)checkpoint->module.host_state.bytes.bytes);
  free(checkpoint->owners);
  memset(checkpoint, 0, sizeof *checkpoint);
}

void qa_qvm_grapple_provider_restore(qa_qvm_grapple_provider *provider,
                                     const qa_qvm_grapple_checkpoint *checkpoint) {
  if (checkpoint->version != 1 || checkpoint->profile == NULL ||
      strcmp(checkpoint->profile, provider->declaration) != 0)
    qa_range_error("Saved grapple profile differs from its source declaration");
  qa_actor_id *owners =
      malloc((checkpoint->owner_count == 0 ? 1 : checkpoint->owner_count) * sizeof *owners);
  if (owners == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < checkpoint->owner_count; i++)
    owners[i] = provider->bridge.restore_actor(provider->bridge.ctx, checkpoint->owners[i]);
  for (size_t i = 0; i < checkpoint->owner_count; i++)
    for (size_t j = i + 1; j < checkpoint->owner_count; j++)
      if (qa_same_actor(owners[i], owners[j])) {
        free(owners);
        qa_range_error("Saved grapple has duplicate owners");
      }
  qa_qvm_module_restore(&provider->game->module, &checkpoint->module);
  validate_layout(provider);
  for (size_t i = 0; i < provider->owner_count; i++)
    provider->bridge.publish(provider->bridge.ctx, provider->owners[i], NULL);
  provider->owner_count = 0;
  for (size_t i = 0; i < checkpoint->owner_count; i++) {
    owners_add(provider, owners[i]);
    publish(provider, owners[i]);
  }
  free(owners);
}

void qa_qvm_grapple_provider_close(qa_qvm_grapple_provider *provider) {
  size_t count = provider->owner_count;
  qa_actor_id *snapshot = malloc((count == 0 ? 1 : count) * sizeof *snapshot);
  if (snapshot == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(snapshot, provider->owners, count * sizeof *snapshot);
  for (size_t i = 0; i < count; i++) qa_qvm_grapple_provider_release(provider, snapshot[i], true);
  free(snapshot);
}
