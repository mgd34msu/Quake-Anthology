/*
 * Port of src/content/q1/foundation/checkpoint.ts. GPL-2.0-or-later.
 * Capture storage is an arena owned by the checkpoint (TS retains
 * immutable strings/arrays by reference-copy). Saved actor ids resolve
 * through the shared actor registry (sourceOf/resolveSaved/referenceSaved).
 */
#include "qa/content/q1/foundation/checkpoint.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

typedef struct arena {
  void **ptrs;
  size_t count, capacity;
} arena;

static void *arena_alloc(arena *a, size_t size) {
  void *ptr = calloc(1, size ? size : 1);
  if (!ptr) return NULL;
  if (a->count == a->capacity) {
    const size_t grown = a->capacity ? a->capacity * 2 : 32;
    void **next = realloc(a->ptrs, grown * sizeof *next);
    if (!next) {
      free(ptr);
      return NULL;
    }
    a->ptrs = next;
    a->capacity = grown;
  }
  a->ptrs[a->count++] = ptr;
  return ptr;
}
static bool arena_track(arena *a, void *ptr) {
  if (!ptr) return true;
  if (a->count == a->capacity) {
    const size_t grown = a->capacity ? a->capacity * 2 : 32;
    void **next = realloc(a->ptrs, grown * sizeof *next);
    if (!next) return false;
    a->ptrs = next;
    a->capacity = grown;
  }
  a->ptrs[a->count++] = ptr;
  return true;
}
static const char *arena_dup(arena *a, const char *text) {
  if (text == NULL) return NULL;
  const size_t len = strlen(text) + 1;
  char *copy = arena_alloc(a, len);
  if (copy) memcpy(copy, text, len);
  return copy;
}
static void arena_free(arena *a) {
  if (!a) return;
  for (size_t i = 0; i < a->count; i++) free(a->ptrs[i]);
  free(a->ptrs);
  free(a);
}
static void set_string(char **slot, const char *value) {
  if (value == NULL) value = "";
  if (*slot != NULL && strcmp(*slot, value) == 0) return;
  const size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (!copy) return;
  memcpy(copy, value, len);
  free(*slot);
  *slot = copy;
}

qa_q1_entity_source_state qa_q1_capture_entity_source_state(const qa_q1_actor *entity) {
  qa_q1_entity_source_state state;
  memset(&state, 0, sizeof state);
  state.model = entity->model;
  state.frame = entity->frame;
  state.skin = entity->skin;
  state.effects = entity->effects;
  state.solid = entity->solid;
  state.movement = entity->movement;
  state.target = entity->target;
  state.targetname = entity->targetname;
  state.killtarget = entity->killtarget;
  state.message = entity->message;
  state.delay = entity->delay;
  state.spawnflags = entity->spawnflags;
  state.sounds = entity->sounds;
  state.wait = entity->wait;
  state.speed = entity->speed;
  state.damage = entity->damage;
  state.max_health = entity->max_health;
  state.aimed_damage = entity->aimed_damage;
  state.next_think = entity->next_think;
  state.original_model = entity->original_model;
  state.pos1 = entity->pos1;
  state.pos2 = entity->pos2;
  state.dest1 = entity->dest1;
  state.dest2 = entity->dest2;
  state.movedir = entity->movedir;
  state.mangle = entity->mangle;
  state.state = entity->state;
  state.has_trigger_bounds = entity->has_trigger_bounds;
  state.trigger_bounds = entity->trigger_bounds;
  state.attack_finished = entity->attack_finished;
  state.count = entity->count;
  state.activated = entity->activated;
  state.projectile = entity->projectile;
  state.projectile_weapon = entity->projectile_weapon;
  state.angular_velocity = entity->angular_velocity;
  state.water_level = entity->water_level;
  state.water_type = entity->water_type;
  state.movement_flags = entity->movement_flags;
  state.ideal_yaw = entity->ideal_yaw;
  state.yaw_speed = entity->yaw_speed;
  state.attack_state = entity->attack_state;
  return state;
}

bool qa_q1_save_actor(const qa_actor_id *actor, qa_saved_actor_id *out) {
  if (actor == NULL) return false;
  out->slot = (double)actor->slot;
  out->generation = (double)actor->generation;
  return true;
}

static qa_saved_actor_id saved_owned(qa_owned_actor actor) {
  const qa_saved_actor_id saved = {(double)actor.id.slot, (double)actor.id.generation};
  return saved;
}

static bool save_entity(qa_q1_services *game, arena *a, qa_q1_actor *entity, qa_q1_saved_entity *out) {
  memset(out, 0, sizeof *out);
  const qa_actor_source_result source = qa_session_actor_registry_source_of(game->host.actors, entity->actor.id);
  out->actor = saved_owned(entity->actor);
  out->has_source_slot = source.has_value;
  out->source_slot = source.has_value ? (double)source.slot : 0;
  out->actor_provider = entity->actor.owner;
  out->classname = arena_dup(a, entity->classname);
  out->has_source_ordinal = entity->has_source_ordinal;
  out->source_ordinal = entity->source_ordinal;
  out->state = qa_q1_capture_entity_source_state(entity);
  out->state.model = arena_dup(a, entity->model);
  out->state.target = arena_dup(a, entity->target);
  out->state.targetname = arena_dup(a, entity->targetname);
  out->state.killtarget = arena_dup(a, entity->killtarget);
  out->state.message = arena_dup(a, entity->message);
  out->state.original_model = arena_dup(a, entity->original_model);
  if (entity->field_count > 0) {
    qa_q1_saved_field *fields = arena_alloc(a, entity->field_count * sizeof *fields);
    if (!fields) return false;
    for (size_t i = 0; i < entity->field_count; i++) {
      fields[i].key = arena_dup(a, entity->fields[i].key);
      fields[i].value = arena_dup(a, entity->fields[i].value);
    }
    out->fields = fields;
    out->field_count = entity->field_count;
  }
  if (entity->reference_count > 0) {
    qa_q1_saved_reference *refs = arena_alloc(a, entity->reference_count * sizeof *refs);
    if (!refs) return false;
    for (size_t i = 0; i < entity->reference_count; i++) {
      refs[i].key = arena_dup(a, entity->references[i].key);
      refs[i].has_actor = entity->references[i].has_actor &&
                          qa_q1_save_actor(&entity->references[i].actor, &refs[i].actor);
    }
    out->references = refs;
    out->reference_count = entity->reference_count;
  }
  out->has_owner = entity->has_owner && qa_q1_save_actor(&entity->owner, &out->owner);
  out->has_activator = entity->has_activator && qa_q1_save_actor(&entity->activator, &out->activator);
  if (entity->door_group_count > 0) {
    qa_saved_actor_id *group = arena_alloc(a, entity->door_group_count * sizeof *group);
    if (!group) return false;
    for (size_t i = 0; i < entity->door_group_count; i++) group[i] = saved_owned(entity->door_group[i]->actor);
    out->door_group = group;
    out->door_group_count = entity->door_group_count;
  }
  if (entity->monster != NULL) {
    qa_q1_saved_monster *saved = arena_alloc(a, sizeof *saved);
    if (!saved) return false;
    const qa_q1_monster *monster = entity->monster;
    saved->species = arena_dup(a, monster->species);
    saved->mode = monster->mode;
    saved->has_enemy = monster->has_enemy && qa_q1_save_actor(&monster->enemy, &saved->enemy);
    saved->has_old_enemy = monster->has_old_enemy && qa_q1_save_actor(&monster->old_enemy, &saved->old_enemy);
    saved->path = arena_dup(a, monster->path);
    saved->pause_until = monster->pause_until;
    saved->attack_finished = monster->attack_finished;
    saved->pain_finished = monster->pain_finished;
    saved->search_until = monster->search_until;
    saved->refired = monster->refired;
    out->monster = saved;
  }
  if (entity->has_move) {
    const char *done = qa_q1_action_name(entity->move.done);
    if (done == NULL) qa_range_error("Q1 move has no named completion");
    out->has_move = true;
    out->move_destination = entity->move.destination;
    out->move_done = done;
  }
  out->callbacks.think = qa_q1_action_name(entity->think);
  out->callbacks.use = qa_q1_use_name(entity->use);
  out->callbacks.touch = qa_q1_touch_name(entity->touch);
  out->callbacks.pain = qa_q1_pain_name(entity->pain);
  out->callbacks.die = qa_q1_die_name(entity->die);
  out->callbacks.blocked = qa_q1_blocked_name(entity->blocked);
  out->callbacks.path_end = qa_q1_action_name(entity->path_end);
  return true;
}

static void free_checkpoint_arg(void *checkpoint) { qa_q1_checkpoint_free(checkpoint); }
bool qa_q1_capture_foundation(qa_q1_services *game, double sequence, double next_dynamic_slot,
                              qa_q1_foundation_checkpoint *out) {
  memset(out, 0, sizeof *out);
  arena *a = calloc(1, sizeof *a);
  if (!a) return false;
  out->storage = a;
  /* An unnamed callback or extension capture raises out of the save. */
  qa_cleanup storage_cleanup;
  qa_cleanup_push(&storage_cleanup, free_checkpoint_arg, out);
  out->format = "q1-foundation";
  out->provider = qa_q1_services_provider(game);
  out->version = 6;
  size_t model_count = 0, sound_count = 0;
  const char *const *models = qa_q1_precache_models(game->precaches, &model_count);
  const char *const *sounds = qa_q1_precache_sounds(game->precaches, &sound_count);
  const char **model_copies = arena_alloc(a, (model_count ? model_count : 1) * sizeof *model_copies);
  const char **sound_copies = arena_alloc(a, (sound_count ? sound_count : 1) * sizeof *sound_copies);
  if (!model_copies || !sound_copies) goto oom;
  for (size_t i = 0; i < model_count; i++) model_copies[i] = arena_dup(a, models[i]);
  for (size_t i = 0; i < sound_count; i++) sound_copies[i] = arena_dup(a, sounds[i]);
  out->precaches.phase = qa_q1_precache_phase(game->precaches);
  out->precaches.models = model_copies;
  out->precaches.model_count = model_count;
  out->precaches.sounds = sound_copies;
  out->precaches.sound_count = sound_count;
  out->edition = game->options.edition;
  out->time = game->time;
  out->frame_seconds = game->frame_seconds;
  out->force_retouch = game->force_retouch;
  out->basis = game->basis;
  out->sequence = sequence;
  out->next_dynamic_slot = next_dynamic_slot;
  out->total_secrets = game->total_secrets;
  out->found_secrets = game->found_secrets;
  out->total_monsters = game->total_monsters;
  out->killed_monsters = game->killed_monsters;
  out->world_type = game->world_type;
  out->map_name = arena_dup(a, game->map_name ? game->map_name : "");
  if (game->world != NULL) {
    out->has_world = true;
    out->world = saved_owned(game->world->actor);
  }
  if (game->sight_entity != NULL) {
    out->has_sight_entity = true;
    out->sight_entity = saved_owned(game->sight_entity->actor);
  }
  out->sight_time = game->sight_time;
  out->has_intermission = game->has_intermission;
  out->intermission_map = arena_dup(a, game->intermission_map);
  out->has_intermission_cause = game->has_intermission_cause;
  if (game->has_intermission_cause) (void)qa_q1_save_actor(&game->intermission_cause, &out->intermission_cause);
  out->intermission_exit_after = game->intermission_exit_after;
  {
    size_t live = 0;
    for (size_t i = 0; i < game->entity_count; i++)
      if (game->entities[i] != NULL) live++;
    qa_q1_saved_entity *entities = arena_alloc(a, (live ? live : 1) * sizeof *entities);
    if (!entities) goto oom;
    size_t index = 0;
    for (size_t i = 0; i < game->entity_count; i++) {
      if (game->entities[i] == NULL) continue;
      if (!save_entity(game, a, game->entities[i], &entities[index])) goto oom;
      index++;
    }
    out->entities = entities;
    out->entity_count = index;
  }
  {
    qa_q1_saved_player *players = arena_alloc(a, (game->player_count ? game->player_count : 1) * sizeof *players);
    if (!players) goto oom;
    size_t index = 0;
    for (size_t i = 0; i < game->player_count; i++) {
      qa_q1_player_state *player = game->players[i];
      if (player == NULL) continue;
      qa_q1_saved_player *saved = &players[index++];
      memset(saved, 0, sizeof *saved);
      saved->actor_provider = player->actor.owner;
      saved->actor = saved_owned(player->actor);
      qa_q1_saved_player_state *state = &saved->state;
      state->weapon = player->weapon;
      state->primary_holstered = player->primary_holstered;
      state->attack_finished = player->attack_finished;
      state->attack_held = player->attack_held;
      state->jump_held = player->jump_held;
      state->teleport_until = player->teleport_until;
      state->weapon_frame = player->weapon_frame;
      state->weapon_animation_at = player->weapon_animation_at;
      state->weapon_animation_base = player->weapon_animation_base;
      state->continuous_firing = player->continuous_firing;
      state->next_weapon_frame = player->next_weapon_frame;
      state->lightning_sound_at = player->lightning_sound_at;
      state->punch_angles = player->punch_angles;
      state->view_angles = player->view_angles;
      state->nail_side = player->nail_side;
      state->max_health = player->max_health;
      state->mega_rot_at = player->mega_rot_at;
      state->hostile_until = player->hostile_until;
      state->water_level = player->water_level;
      state->air_finished = player->air_finished;
      state->drown_damage = player->drown_damage;
      state->drown_at = player->drown_at;
      state->hazard_at = player->hazard_at;
      state->auto_switch = player->auto_switch;
      state->has_alpha = true;
      state->alpha = player->alpha;
      state->has_scale = true;
      state->scale = player->scale;
      if (player->powerup_count > 0) {
        qa_q1_powerup_entry *entries = arena_alloc(a, player->powerup_count * sizeof *entries);
        if (!entries) goto oom;
        memcpy(entries, player->powerups, player->powerup_count * sizeof *entries);
        saved->powerups = entries;
        saved->powerup_count = player->powerup_count;
      }
    }
    out->players = players;
    out->player_count = index;
  }
  {
    const size_t count = game->state_extension_count;
    void *extensions = arena_alloc(a, (count ? count : 1) *
                                          sizeof(struct { const char *id; const uint8_t *bytes; size_t len; }));
    if (!extensions) goto oom;
    out->extensions = extensions;
    out->extension_count = 0;
    for (size_t i = 0; i < count; i++) {
      const qa_q1_state_extension *extension = game->state_extensions[i];
      if (extension == NULL || extension->capture == NULL) continue;
      uint8_t *bytes = NULL;
      size_t len = 0;
      if (!extension->capture(extension->ctx, &bytes, &len)) goto oom;
      if (!arena_track(a, bytes)) {
        free(bytes);
        goto oom;
      }
      out->extensions[out->extension_count].id = extension->id;
      out->extensions[out->extension_count].bytes = bytes;
      out->extensions[out->extension_count].len = len;
      out->extension_count++;
    }
  }
  qa_cleanup_pop(&storage_cleanup);
  return true;
oom:
  qa_cleanup_pop(&storage_cleanup);
  qa_q1_checkpoint_free(out);
  return false;
}

void qa_q1_checkpoint_free(qa_q1_foundation_checkpoint *checkpoint) {
  if (checkpoint == NULL) return;
  arena_free(checkpoint->storage);
  memset(checkpoint, 0, sizeof *checkpoint);
}

/* TS game.host.actors.referenceSaved(saved). */
static qa_actor_id reference_id(qa_q1_services *game, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(game->host.actors, saved, NULL);
}
/* TS owned(saved): resolveSaved, null throws. */
static qa_owned_actor owned_of(qa_q1_services *game, qa_saved_actor_id saved) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_saved(game->host.actors, saved, &owned))
    qa_range_error("Missing restored Q1 actor %s/%s", QA_JS_NUMBER(saved.slot), QA_JS_NUMBER(saved.generation));
  return owned;
}
/* TS sourceEntity(saved): entities.get(owned(saved)). */
static qa_q1_actor *source_entity(qa_q1_services *game, qa_saved_actor_id saved) {
  const qa_owned_actor owned = owned_of(game, saved);
  qa_q1_actor *entity = qa_q1_get_entity(game, &owned.id);
  if (entity == NULL || !qa_same_owned_actor(entity->actor, owned))
    qa_range_error("Saved Q1 source reference points outside source entities");
  return entity;
}

static void apply_source_state(qa_q1_actor *entity, const qa_q1_entity_source_state *state) {
  set_string(&entity->model, state->model);
  entity->frame = state->frame;
  entity->skin = state->skin;
  entity->effects = state->effects;
  entity->solid = state->solid;
  entity->movement = state->movement;
  set_string(&entity->target, state->target);
  set_string(&entity->targetname, state->targetname);
  set_string(&entity->killtarget, state->killtarget);
  set_string(&entity->message, state->message);
  entity->delay = state->delay;
  entity->spawnflags = state->spawnflags;
  entity->sounds = state->sounds;
  entity->wait = state->wait;
  entity->speed = state->speed;
  entity->damage = state->damage;
  entity->max_health = state->max_health;
  entity->aimed_damage = state->aimed_damage;
  entity->next_think = state->next_think;
  set_string(&entity->original_model, state->original_model);
  entity->pos1 = state->pos1;
  entity->pos2 = state->pos2;
  entity->dest1 = state->dest1;
  entity->dest2 = state->dest2;
  entity->movedir = state->movedir;
  entity->mangle = state->mangle;
  entity->state = state->state;
  entity->has_trigger_bounds = state->has_trigger_bounds;
  entity->trigger_bounds = state->trigger_bounds;
  entity->attack_finished = state->attack_finished;
  entity->count = state->count;
  entity->activated = state->activated;
  entity->projectile = state->projectile;
  entity->projectile_weapon = state->projectile_weapon;
  entity->angular_velocity = state->angular_velocity;
  entity->water_level = state->water_level;
  entity->water_type = state->water_type;
  entity->movement_flags = state->movement_flags;
  entity->ideal_yaw = state->ideal_yaw;
  entity->yaw_speed = state->yaw_speed;
  entity->attack_state = state->attack_state;
}

void qa_q1_restore_foundation(qa_q1_services *game, const qa_q1_foundation_checkpoint *checkpoint) {
  const bool edition_ok = (checkpoint->edition == NULL && game->options.edition == NULL) ||
                          (checkpoint->edition != NULL && game->options.edition != NULL &&
                           !strcmp(checkpoint->edition, game->options.edition));
  const bool provider_ok = (checkpoint->provider == NULL && qa_q1_services_provider(game) == NULL) ||
                           (checkpoint->provider != NULL && qa_q1_services_provider(game) != NULL &&
                            !strcmp(checkpoint->provider, qa_q1_services_provider(game)));
  if (checkpoint->format == NULL || strcmp(checkpoint->format, "q1-foundation") != 0 ||
      (checkpoint->version != 5 && checkpoint->version != 6) || !edition_ok || !provider_ok)
    qa_range_error("Incompatible Q1 source checkpoint");
  if (game->entity_count != 0 || game->player_count != 0)
    qa_range_error("Restore Q1 source state into a fresh provider");
  qa_q1_precache_restore(game->precaches, checkpoint->precaches);
  for (size_t i = 0; i < checkpoint->entity_count; i++) {
    const qa_q1_saved_entity *saved = &checkpoint->entities[i];
    const qa_owned_actor actor = owned_of(game, saved->actor);
    const qa_actor_source_result source = qa_session_actor_registry_source_of(game->host.actors, actor.id);
    const bool provider_same = actor.owner != NULL && saved->actor_provider != NULL &&
                               !strcmp(actor.owner, saved->actor_provider);
    const bool slot_same = source.has_value == saved->has_source_slot &&
                           (!source.has_value || (double)source.slot == saved->source_slot);
    const bool source_provider_same = !source.has_value ||
                                      (source.provider != NULL && actor.owner != NULL && !strcmp(source.provider, actor.owner));
    qa_q1_actor *attached = qa_q1_get_entity(game, &actor.id);
    if (!provider_same || !slot_same || !source_provider_same ||
        (attached != NULL && qa_same_owned_actor(attached->actor, actor)))
      qa_range_error("Q1 restored source-slot mismatch");
    if (!qa_shared_body_table_read(game->host.bodies, actor.id).has_value ||
        !qa_gameplay_authority_read(game->host.combat, actor.id).has_value)
      qa_range_error("Restore shared Q1 body and combat stores before source state");
    qa_q1_actor *entity = qa_q1_attach_existing(game, actor, saved->classname, NULL,
                                                saved->has_source_ordinal ? &saved->source_ordinal : NULL);
    apply_source_state(entity, &saved->state);
    for (size_t f = 0; f < saved->field_count; f++)
      (void)qa_q1_actor_field_set(entity, saved->fields[f].key, saved->fields[f].value);
  }
  for (size_t i = 0; i < checkpoint->entity_count; i++) {
    const qa_q1_saved_entity *saved = &checkpoint->entities[i];
    qa_q1_actor *entity = source_entity(game, saved->actor);
    const qa_q1_saved_callbacks *callbacks = &saved->callbacks;
    for (size_t r = 0; r < saved->reference_count; r++) {
      qa_actor_id ref;
      const qa_actor_id *ref_ptr = NULL;
      if (saved->references[r].has_actor) {
        ref = reference_id(game, saved->references[r].actor);
        ref_ptr = &ref;
      }
      (void)qa_q1_actor_reference_set(entity, saved->references[r].key, ref_ptr);
    }
    entity->has_owner = saved->has_owner;
    if (saved->has_owner) entity->owner = reference_id(game, saved->owner);
    entity->has_activator = saved->has_activator;
    if (saved->has_activator) entity->activator = reference_id(game, saved->activator);
    free(entity->door_group);
    entity->door_group = NULL;
    entity->door_group_count = 0;
    if (saved->door_group_count > 0) {
      qa_q1_actor **group = malloc(saved->door_group_count * sizeof *group);
      if (group != NULL) {
        /* The entity owns the array before source_entity can raise, so a
         * bad saved reference does not leak it. */
        entity->door_group = group;
        for (size_t d = 0; d < saved->door_group_count; d++) {
          group[d] = source_entity(game, saved->door_group[d]);
          entity->door_group_count = d + 1;
        }
      }
    }
    if (entity->monster != NULL) {
      free(entity->monster->path);
      free(entity->monster);
      entity->monster = NULL;
    }
    if (saved->monster != NULL) {
      qa_q1_monster *monster = calloc(1, sizeof *monster);
      if (monster != NULL) {
        monster->species = saved->monster->species;
        monster->mode = saved->monster->mode;
        monster->has_enemy = saved->monster->has_enemy;
        if (saved->monster->has_enemy) monster->enemy = reference_id(game, saved->monster->enemy);
        monster->has_old_enemy = saved->monster->has_old_enemy;
        if (saved->monster->has_old_enemy) monster->old_enemy = reference_id(game, saved->monster->old_enemy);
        set_string(&monster->path, saved->monster->path ? saved->monster->path : "");
        monster->pause_until = saved->monster->pause_until;
        monster->attack_finished = saved->monster->attack_finished;
        monster->pain_finished = saved->monster->pain_finished;
        monster->search_until = saved->monster->search_until;
        monster->refired = saved->monster->refired;
        entity->monster = monster;
        if (saved->monster->has_legacy_animation)
          qa_q1_legacy_animations_set(game, entity->actor, saved->monster->legacy_animation);
      }
    }
    entity->has_move = saved->has_move;
    if (saved->has_move) {
      entity->move.destination = saved->move_destination;
      entity->move.done = qa_q1_callback_registry_action(game->named, entity, saved->move_done);
    }
    memset(&entity->think, 0, sizeof entity->think);
    memset(&entity->use, 0, sizeof entity->use);
    memset(&entity->touch, 0, sizeof entity->touch);
    memset(&entity->pain, 0, sizeof entity->pain);
    memset(&entity->die, 0, sizeof entity->die);
    memset(&entity->blocked, 0, sizeof entity->blocked);
    memset(&entity->path_end, 0, sizeof entity->path_end);
    if (callbacks->think) entity->think = qa_q1_callback_registry_action(game->named, entity, callbacks->think);
    if (callbacks->use) entity->use = qa_q1_callback_registry_use(game->named, entity, callbacks->use);
    if (callbacks->touch) entity->touch = qa_q1_callback_registry_touch(game->named, entity, callbacks->touch);
    if (callbacks->pain) entity->pain = qa_q1_callback_registry_pain(game->named, entity, callbacks->pain);
    if (callbacks->die) entity->die = qa_q1_callback_registry_die(game->named, entity, callbacks->die);
    if (callbacks->blocked) entity->blocked = qa_q1_callback_registry_blocked(game->named, entity, callbacks->blocked);
    if (callbacks->path_end) entity->path_end = qa_q1_callback_registry_action(game->named, entity, callbacks->path_end);
  }
  for (size_t i = 0; i < checkpoint->player_count; i++) {
    const qa_q1_saved_player *saved = &checkpoint->players[i];
    const qa_owned_actor actor = owned_of(game, saved->actor);
    if (actor.owner == NULL || saved->actor_provider == NULL || strcmp(actor.owner, saved->actor_provider) != 0)
      qa_range_error("Q1 restored player owner mismatch");
    for (size_t p = 0; p < game->player_count; p++)
      if (qa_same_owned_actor(game->players[p]->actor, actor))
        qa_range_error("Duplicate saved Q1 player");
    qa_q1_player_state *player = calloc(1, sizeof *player);
    if (player == NULL) continue;
    player->actor = actor;
    const qa_q1_saved_player_state *state = &saved->state;
    player->weapon = state->weapon;
    player->primary_holstered = state->primary_holstered;
    player->attack_finished = state->attack_finished;
    player->attack_held = state->attack_held;
    player->jump_held = state->jump_held;
    player->teleport_until = state->teleport_until;
    player->weapon_frame = state->weapon_frame;
    player->weapon_animation_at = state->weapon_animation_at;
    player->weapon_animation_base = state->weapon_animation_base;
    player->continuous_firing = state->continuous_firing;
    player->next_weapon_frame = state->next_weapon_frame;
    player->lightning_sound_at = state->lightning_sound_at;
    player->punch_angles = state->punch_angles;
    player->view_angles = state->view_angles;
    player->nail_side = state->nail_side;
    player->max_health = state->max_health;
    player->mega_rot_at = state->mega_rot_at;
    player->hostile_until = state->hostile_until;
    player->water_level = state->water_level;
    player->air_finished = state->air_finished;
    player->drown_damage = state->drown_damage;
    player->drown_at = state->drown_at;
    player->hazard_at = state->hazard_at;
    player->auto_switch = state->auto_switch;
    player->alpha = state->has_alpha ? state->alpha : 0;
    player->scale = state->has_scale ? state->scale : 0;
    for (size_t p = 0; p < saved->powerup_count; p++)
      qa_q1_player_powerup_set(player, saved->powerups[p].kind, saved->powerups[p].expires);
    if (game->player_count == game->player_capacity) {
      const size_t grown = game->player_capacity ? game->player_capacity * 2 : 8;
      qa_q1_player_state **next = realloc(game->players, grown * sizeof *next);
      if (next == NULL) {
        free(player);
        continue;
      }
      game->players = next;
      game->player_capacity = grown;
    }
    game->players[game->player_count++] = player;
  }
  qa_q1_services_set_time(game, checkpoint->time); /* TS setter: Math.fround */
  game->frame_seconds = checkpoint->frame_seconds;
  game->force_retouch = checkpoint->force_retouch;
  game->basis = checkpoint->basis;
  game->total_secrets = checkpoint->total_secrets;
  game->found_secrets = checkpoint->found_secrets;
  game->total_monsters = checkpoint->total_monsters;
  game->killed_monsters = checkpoint->killed_monsters;
  game->world_type = checkpoint->world_type;
  set_string(&game->map_name, checkpoint->map_name ? checkpoint->map_name : "");
  game->world = checkpoint->has_world ? source_entity(game, checkpoint->world) : NULL;
  game->sight_entity = NULL;
  if (checkpoint->has_sight_entity) {
    const qa_actor_id sight = reference_id(game, checkpoint->sight_entity);
    game->sight_entity = qa_q1_get_entity(game, &sight);
  }
  game->sight_time = checkpoint->sight_time;
  game->has_intermission = checkpoint->has_intermission;
  free(game->intermission_map);
  game->intermission_map = NULL;
  if (checkpoint->has_intermission && checkpoint->intermission_map != NULL)
    set_string(&game->intermission_map, checkpoint->intermission_map);
  game->has_intermission_cause = checkpoint->has_intermission && checkpoint->has_intermission_cause;
  if (game->has_intermission_cause) game->intermission_cause = reference_id(game, checkpoint->intermission_cause);
  game->intermission_exit_after = checkpoint->intermission_exit_after;
  {
    bool *restored = calloc(game->state_extension_count ? game->state_extension_count : 1, sizeof *restored);
    /* An extension restore that raises on bad saved bytes unwinds through here. */
    qa_cleanup restored_cleanup;
    qa_cleanup_push(&restored_cleanup, free, restored);
    for (size_t i = 0; i < checkpoint->extension_count; i++) {
      const char *id = checkpoint->extensions[i].id;
      const qa_q1_state_extension *extension = NULL;
      size_t index = 0;
      for (size_t e = 0; e < game->state_extension_count; e++)
        if (game->state_extensions[e] != NULL && game->state_extensions[e]->id != NULL &&
            id != NULL && !strcmp(game->state_extensions[e]->id, id)) {
          extension = game->state_extensions[e];
          index = e;
        }
      if (extension == NULL || (restored != NULL && restored[index]))
        qa_range_error("Unknown or duplicate Q1 saved extension: %s", id ? id : "");
      if (extension->restore)
        extension->restore(extension->ctx, checkpoint->extensions[i].bytes, checkpoint->extensions[i].len);
      if (restored != NULL) restored[index] = true;
    }
    for (size_t e = 0; e < game->state_extension_count; e++)
      if (restored != NULL && !restored[e]) {
        const qa_q1_state_extension *extension = game->state_extensions[e];
        void (*restore_absent)(void *ctx) =
            extension != NULL ? qa_q1_state_extension_restore_absent(game, extension) : NULL;
        if (restore_absent != NULL) {
          restore_absent(extension->ctx);
          continue;
        }
        const char *id = extension ? extension->id : NULL;
        qa_range_error("Missing Q1 saved extension: %s", id ? id : "");
      }
    qa_cleanup_pop(&restored_cleanup);
    free(restored);
  }
}

bool qa_q1_services_capture(qa_q1_services *game, qa_q1_foundation_checkpoint *out) {
  return qa_q1_capture_foundation(game, game->sequence, game->next_dynamic_slot, out);
}

void qa_q1_services_restore(qa_q1_services *game, const qa_q1_foundation_checkpoint *checkpoint,
                            bool schedule_thinks) {
  qa_q1_restore_foundation(game, checkpoint);
  game->sequence = checkpoint->sequence;
  game->next_dynamic_slot = checkpoint->next_dynamic_slot;
  if (schedule_thinks) qa_q1_resume_thinks(game);
}
