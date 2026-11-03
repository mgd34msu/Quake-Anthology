/* Port of src/app/bootstrap/simulation/arsenal/q3-source-legacy.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q3_source_legacy.h"
#include "qa/app/bootstrap/simulation/arsenal/q3_source.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/composition/expansion_source_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/composition/q2_q3_supply.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/direction_byte.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#include "qa/content/q3/base/game/save_callbacks.h"
#include "qa/foundation/js.h"

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
typedef struct ref_ctx { qa_app_saved_reference_fn reference; void *ctx; } ref_ctx;
static qa_actor_id identity_of(ref_ctx *ctx, const qa_save_reader *reader) {
  return ctx->reference(ctx->ctx, qa_read_saved_actor(reader));
}

qa_app_q3_projectile_state *qa_app_read_q3_projectile_states(const qa_save_reader *reader,
    qa_app_saved_actor_fn actor, void *actor_ctx, qa_app_saved_reference_fn reference, void *reference_ctx,
    size_t *out_count) {
  ref_ctx refs = {reference, reference_ctx};
  size_t count = qa_save_reader_list_count(reader);
  qa_app_q3_projectile_state *states = calloc(count ? count : 1, sizeof *states);
  if (!states) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value;
    if (!qa_save_reader_item(reader, i, &value)) qa_range_error("Out of memory");
    qa_app_q3_projectile_state *state = &states[i];
    qa_save_reader trajectory = field(&value, "trajectory"), phase = field(&value, "phase");
    qa_save_reader r = field(&trajectory, "type");
    const double trajectory_choices[3] = {QA_Q3_BASE_TR_STATIONARY, QA_Q3_BASE_TR_LINEAR, QA_Q3_BASE_TR_GRAVITY};
    state->saved_trajectory.type = qa_save_reader_choice_number(&r, trajectory_choices, 3);
    qa_save_reader_destroy(&r);
    r = field(&phase, "kind");
    const char *kinds[3] = {"flight", "event", "attached"};
    const char *kind = qa_save_reader_choice_string(&r, kinds, 3);
    qa_save_reader_destroy(&r);
    if (strcmp(kind, "flight") == 0) state->phase = QA_APP_Q3_PROJECTILE_FLIGHT;
    else if (strcmp(kind, "attached") == 0) {
      state->phase = QA_APP_Q3_PROJECTILE_ATTACHED;
      r = field(&phase, "target");
      if (!qa_save_reader_is_null(&r)) { state->has_attached_target = true; state->attached_target = identity_of(&refs, &r); }
      qa_save_reader_destroy(&r);
      r = field(&phase, "nextThink"); state->next_think = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    } else {
      state->phase = QA_APP_Q3_PROJECTILE_EVENT;
      r = field(&phase, "time"); state->phase_time = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
      qa_save_reader impact = field(&phase, "impact");
      r = field(&impact, "kind"); (void)qa_save_reader_literal_string(&r, "impact"); qa_save_reader_destroy(&r);
      state->impact.kind = QA_Q3_IMPACT_IMPACT;
      r = field(&impact, "normal"); state->impact.normal = qa_read_vector(&r); qa_save_reader_destroy(&r);
      r = field(&impact, "target");
      if (!qa_save_reader_is_null(&r)) { state->impact.has_target = true; state->impact.target = identity_of(&refs, &r); }
      qa_save_reader_destroy(&r);
      r = field(&impact, "flesh"); state->impact.flesh = qa_save_reader_boolean(&r); qa_save_reader_destroy(&r);
      r = field(&impact, "surfaceFlags"); state->impact.surface_flags = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
      qa_save_reader_destroy(&impact);
    }
    r = field(&value, "actor"); state->base.actor = actor(actor_ctx, &r); qa_save_reader_destroy(&r);
    r = field(&value, "owner"); state->base.owner = identity_of(&refs, &r); qa_save_reader_destroy(&r);
    r = field(&value, "pass");
    if (!qa_save_reader_is_null(&r)) { state->base.has_pass = true; state->base.pass = identity_of(&refs, &r); }
    qa_save_reader_destroy(&r);
    r = field(&value, "damagePoint"); state->base.damage_point = qa_read_vector(&r); qa_save_reader_destroy(&r);
    r = field(&value, "flags"); state->base.flags = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field(&value, "weapon");
    const double weapons[6] = {4, 5, 8, 9, 10, 11};
    state->base.weapon = qa_save_reader_choice_number(&r, weapons, 6); qa_save_reader_destroy(&r);
    r = field(&value, "direct"); state->base.direct = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    r = field(&value, "splash"); state->base.splash = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    r = field(&value, "radius"); state->base.radius = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
    r = field(&value, "method"); state->base.method = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    r = field(&value, "splashMethod"); state->base.splash_method = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    r = field(&value, "expires"); state->expires = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
    r = field(&trajectory, "time"); state->saved_trajectory.time = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
    r = field(&trajectory, "duration"); state->saved_trajectory.duration = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
    r = field(&trajectory, "base"); state->saved_trajectory.base = qa_read_vector(&r); qa_save_reader_destroy(&r);
    r = field(&trajectory, "delta"); state->saved_trajectory.delta = qa_read_vector(&r); qa_save_reader_destroy(&r);
    qa_save_reader_destroy(&trajectory); qa_save_reader_destroy(&phase); qa_save_reader_destroy(&value);
  }
  *out_count = count;
  return states;
}

qa_app_q3_weapon_statistics *qa_app_read_q3_weapon_statistics(const qa_save_reader *reader,
    qa_app_saved_actor_fn actor, void *actor_ctx, size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  qa_app_q3_weapon_statistics *stats = calloc(count ? count : 1, sizeof *stats);
  if (!stats) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value;
    if (!qa_save_reader_item(reader, i, &value)) qa_range_error("Out of memory");
    qa_save_reader r = field(&value, "actor"); stats[i].actor = actor(actor_ctx, &r); qa_save_reader_destroy(&r);
    r = field(&value, "shots"); stats[i].shots = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field(&value, "hits"); stats[i].rail.hits = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field(&value, "streak"); stats[i].rail.streak = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field(&value, "impressiveCount"); stats[i].rail.impressive_count = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field(&value, "rewardUntil"); stats[i].rail.reward_until = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    qa_save_reader_destroy(&value);
  }
  *out_count = count;
  return stats;
}

qa_app_legacy_q3_source qa_app_read_legacy_q3_source(const qa_save_reader *reader,
    qa_app_saved_actor_fn owner, void *owner_ctx, qa_app_saved_reference_fn reference, void *reference_ctx) {
  qa_app_legacy_q3_source source; memset(&source, 0, sizeof source);
  qa_save_reader r = field(reader, "milliseconds"); source.milliseconds = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
  r = field(reader, "randomSeed"); source.random_seed = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = field(reader, "projectiles");
  source.projectiles = qa_app_read_q3_projectile_states(&r, owner, owner_ctx, reference, reference_ctx, &source.projectile_count);
  qa_save_reader_destroy(&r);
  r = field(reader, "weaponStatistics");
  source.statistics = qa_app_read_q3_weapon_statistics(&r, owner, owner_ctx, &source.statistics_count);
  qa_save_reader_destroy(&r);
  r = field(reader, "hookHeld");
  if (!qa_save_reader_is_undefined(&r)) {
    size_t count = qa_save_reader_list_count(&r);
    source.hook_held = calloc(count ? count : 1, sizeof *source.hook_held);
    if (!source.hook_held) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&r, i, &item)) qa_range_error("Out of memory");
      source.hook_held[i] = owner(owner_ctx, &item);
      qa_save_reader_destroy(&item);
    }
    source.hook_held_count = count;
  }
  qa_save_reader_destroy(&r);
  return source;
}
void qa_app_legacy_q3_source_destroy(qa_app_legacy_q3_source *source) {
  if (!source) return;
  free(source->projectiles); free(source->statistics); free(source->hook_held);
  memset(source, 0, sizeof *source);
}

qa_app_q3_selected_arsenal_checkpoint qa_app_migrate_legacy_q3_arsenal(qa_app_q3_selected_arsenal_checkpoint checkpoint,
    const char *map, qa_composition_owned_profile *out_profile) {
  const qa_pickup_supply_profile *profile = map && strcmp(map, "q1") == 0
      ? &QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE : &QA_COMPOSITION_Q2_Q3_SUPPLY_PROFILE;
  if (!checkpoint.runtime.product || strcmp(checkpoint.runtime.product, "baseq3") != 0
      || !checkpoint.supply_profile || strcmp(checkpoint.supply_profile, profile->id) != 0)
    qa_range_error("Legacy Q3 arsenal does not match its original base supply profile");
  if (!qa_composition_expansion_source_supply(profile, out_profile)) qa_range_error("Out of memory");
  checkpoint.supply_profile = out_profile->id;
  return checkpoint;
}

/* TS Set<ActorId> in insertion order. */
typedef struct actor_set { qa_actor_id *items; size_t count, capacity; } actor_set;
static bool set_has(const actor_set *set, qa_actor_id actor) {
  for (size_t i = 0; i < set->count; i++) if (qa_same_actor(set->items[i], actor)) return true;
  return false;
}
static void set_add(actor_set *set, qa_actor_id actor) {
  if (set_has(set, actor)) return;
  if (set->count == set->capacity) {
    size_t capacity = set->capacity == 0 ? 16 : set->capacity * 2;
    qa_actor_id *grown = realloc(set->items, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    set->items = grown; set->capacity = capacity;
  }
  set->items[set->count++] = actor;
}
static qa_save_value *saved_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *object = qa_save_value_object();
  if (object == NULL || !qa_save_value_set(object, "slot", qa_save_value_number(saved.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number(saved.generation))) qa_range_error("Out of memory");
  return object;
}
static void set_field(qa_save_value *object, const char *key, qa_save_value *value) {
  if (value == NULL || !qa_save_value_set(object, key, value)) qa_range_error("Out of memory");
}
static qa_actor_id reference_saved(void *ctx, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(((qa_app_q3_selected_source *)ctx)->host.actors, saved, "checkpoint");
}
static qa_q3_game_entity *native(qa_app_q3_selected_source *source, bool has_actor, qa_actor_id actor) {
  return qa_q3_base_entity_records_native_by_actor(source->records, has_actor, actor);
}
static bool has_player(qa_app_q3_selected_source *source, qa_actor_id actor) {
  qa_app_q3_selected_client_pose pose;
  return source->host.player(source->host.ctx, actor, &pose);
}
static bool has_body(qa_app_q3_selected_source *source, qa_actor_id actor) {
  return qa_shared_body_table_read(source->host.bodies, actor).has_value;
}

void qa_app_restore_legacy_q3_source(qa_app_q3_selected_source *source, const qa_app_legacy_q3_source *saved,
    qa_q3_game_entity *(*project)(void *ctx, qa_actor_id actor), void *project_ctx) {
  qa_session_actor_registry *actors = source->host.actors;
  qa_q3_entity_pool *pool = &source->pool;
  if (source->host.product != QA_Q3_BASE_PRODUCT_BASEQ3) qa_range_error("Legacy ballistics require the base Q3 source");
  actor_set hooks = {0}, references = {0};
  for (size_t i = 0; i < saved->projectile_count; i++) {
    const qa_app_q3_projectile_state *state = &saved->projectiles[i];
    qa_session_actor_registry_assert_owned(actors, state->base.actor);
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) if (qa_same_owned_actor(saved->projectiles[j].base.actor, state->base.actor)) duplicate = true;
    if (state->base.actor.owner == NULL || source->host.provider == NULL || strcmp(state->base.actor.owner, source->host.provider) != 0
        || duplicate || native(source, true, state->base.actor.id) != NULL || !has_body(source, state->base.actor.id))
      qa_range_error("Legacy projectile has no unique owned body");
    set_add(&references, state->base.owner);
    if (state->base.has_pass) set_add(&references, state->base.pass);
    if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED && state->base.weapon != 10)
      qa_range_error("Only a legacy grappling hook can be attached");
    if (state->base.weapon == 10) {
      if (set_has(&hooks, state->base.owner) || !has_player(source, state->base.owner))
        qa_range_error("Legacy grappling hook has no unique live player");
      set_add(&hooks, state->base.owner);
    }
    if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED && state->has_attached_target) set_add(&references, state->attached_target);
    if (state->phase == QA_APP_Q3_PROJECTILE_EVENT && state->impact.has_target) set_add(&references, state->impact.target);
  }
  for (int group = 0; group < 2; group++) {
    size_t count = group == 0 ? saved->statistics_count : saved->hook_held_count;
    for (size_t i = 0; i < count; i++) {
      qa_owned_actor actor = group == 0 ? saved->statistics[i].actor : saved->hook_held[i];
      for (size_t j = 0; j < i; j++) {
        qa_owned_actor other = group == 0 ? saved->statistics[j].actor : saved->hook_held[j];
        if (qa_same_owned_actor(other, actor)) qa_range_error("Legacy Q3 client continuation has no unique live player");
      }
    }
    for (size_t i = 0; i < count; i++) {
      qa_owned_actor actor = group == 0 ? saved->statistics[i].actor : saved->hook_held[i];
      if (!has_player(source, actor.id)) qa_range_error("Legacy Q3 client continuation has no unique live player");
    }
    for (size_t i = 0; i < count; i++) set_add(&references, group == 0 ? saved->statistics[i].actor.id : saved->hook_held[i].id);
  }
  size_t foreign_non_players = 0;
  for (size_t i = 0; i < references.count; i++) {
    qa_actor_id actor = references.items[i];
    bool owned_projectile = false;
    for (size_t j = 0; j < saved->projectile_count; j++) if (qa_same_actor(saved->projectiles[j].base.actor.id, actor)) owned_projectile = true;
    if (!qa_session_actor_registry_is_live(actors, actor) || native(source, true, actor) != NULL || owned_projectile) continue;
    if (!has_body(source, actor)) qa_range_error("Legacy Q3 reference has no body");
    if (!has_player(source, actor)) foreign_non_players++;
  }
  size_t slot_count = 0;
  double *slots = calloc(1022 - QA_Q3_MAX_CLIENTS, sizeof *slots);
  if (slots == NULL) qa_range_error("Out of memory");
  for (double slot = QA_Q3_MAX_CLIENTS; slot < 1022; slot++)
    if (!qa_q3_game_entity_inuse(qa_q3_entity_pool_at(pool, slot))) slots[slot_count++] = slot;
  if (slot_count < saved->projectile_count + foreign_non_players)
    qa_range_error("Legacy Q3 projectiles exceed original source entity capacity");
  qa_q3_game_entity **entities = calloc(saved->projectile_count ? saved->projectile_count : 1, sizeof *entities);
  if (entities == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < saved->projectile_count; i++) {
    if (i >= slot_count) qa_range_error("Legacy Q3 entity slot is missing");
    double slot = slots[i];
    entities[i] = qa_q3_base_entity_records_adopt(source->records, slot, saved->projectiles[i].base.actor);
    if (slot >= pool->num_entities) {
      qa_q3_pool_counts counts = {slot + 1, source->host.max_clients};
      qa_q3_entity_pool_restore_counts(pool, counts);
    }
  }
  free(slots);
  for (size_t i = 0; i < references.count; i++) (void)project(project_ctx, references.items[i]);
  qa_save_value *continuations = qa_save_value_array();
  if (continuations == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < saved->projectile_count; i++) {
    const qa_app_q3_projectile_state *state = &saved->projectiles[i];
    qa_q3_game_entity *entity = entities[i];
    if (entity == NULL) qa_range_error("Legacy Q3 entity is missing");
    qa_q3_game_entity *owner = native(source, true, state->base.owner);
    double weapon = state->base.weapon;
    qa_q3_game_entity_set_classname(entity, weapon == 4 ? "grenade" : weapon == 5 ? "rocket" : weapon == 8 ? "plasma"
        : weapon == 9 ? "bfg" : weapon == 10 ? "hook" : "nail");
    entity->s.weapon = weapon; entity->s.e_flags = state->base.flags;
    entity->s.e_type = QA_Q3_BASE_ET_MISSILE; entity->r.sv_flags = QA_Q3_BASE_SVF_USE_CURRENT_ORIGIN;
    entity->r.owner_num = owner != NULL ? owner->slot : QA_Q3_BASE_ENTITYNUM_NONE; entity->clipmask = 0x6000001;
    entity->damage = state->base.direct; entity->splash_damage = state->base.splash; entity->splash_radius = state->base.radius;
    entity->method_of_death = state->base.method; entity->splash_method_of_death = state->base.splash_method;
    entity->s.origin = state->base.damage_point;
    entity->s.pos = state->saved_trajectory;
    entity->think = qa_q3_callback_family_resolve(&pool->callbacks.think,
        weapon == 10 ? "q3.base.game.missile.fireGrapple.think" : "q3.base.game.missile.launch.think");
    qa_q3_game_entity_set_nextthink(entity, state->expires);
    if (weapon == 10) {
      if (owner == NULL || owner->client == NULL) qa_range_error("Legacy hook owner lost its source client");
      entity->parent = owner; entity->s.other_entity_num = owner->slot; owner->client->hook = entity;
      if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED) {
        entity->s.e_type = QA_Q3_BASE_ET_GRAPPLE;
        entity->think = qa_q3_callback_family_resolve(&pool->callbacks.think, "q3.base.game.missile.hookThink");
        qa_q3_game_entity_set_nextthink(entity, state->next_think);
        owner->client->ps.pm_flags = (double)(qa_js_to_int32(owner->client->ps.pm_flags) | QA_Q3_FLAG_GRAPPLE_PULL);
        owner->client->ps.grapple_point = qa_q3_base_entity_shared_current_origin(&entity->r);
      }
    }
    if (state->phase == QA_APP_Q3_PROJECTILE_EVENT) {
      entity->free_after_event = true; entity->s.e_type = QA_Q3_BASE_ET_GENERAL; entity->event_time = state->phase_time;
      entity->s.event = state->impact.flesh ? QA_Q3_EV_MISSILE_HIT
          : (qa_js_to_int32(state->impact.surface_flags) & 0x1000) != 0 ? QA_Q3_EV_MISSILE_MISS_METAL : QA_Q3_EV_MISSILE_MISS;
      entity->s.event_parm = qa_q3_base_direction_to_byte(&state->impact.normal);
      qa_q3_game_entity *target = native(source, state->impact.has_target, state->impact.target);
      entity->s.other_entity_num = target != NULL ? target->slot : QA_Q3_BASE_ENTITYNUM_NONE;
    }
    qa_save_value *row = qa_save_value_object(), *attachment = qa_save_value_object();
    if (row == NULL || attachment == NULL) qa_range_error("Out of memory");
    set_field(row, "entity", qa_save_value_number(entity->slot));
    set_field(row, "actor", saved_value(state->base.actor.id));
    set_field(row, "owner", saved_value(state->base.owner));
    set_field(row, "pass", state->base.has_pass ? saved_value(state->base.pass) : qa_save_value_null());
    set_field(row, "trigger", qa_save_value_null());
    if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED && state->has_attached_target) {
      set_field(attachment, "kind", qa_save_value_string("player"));
      set_field(attachment, "actor", saved_value(state->attached_target));
    } else {
      set_field(attachment, "kind", qa_save_value_string("none"));
    }
    set_field(row, "attachment", attachment);
    if (!qa_save_value_push(continuations, row)) qa_range_error("Out of memory");
  }
  free(entities);
  qa_q3_missile_runtime_restore_save_state(&source->missiles, continuations, reference_saved, source);
  qa_save_value_free(continuations);
  qa_game_random_reset(&source->random, (int64_t)saved->random_seed);
  for (size_t i = 0; i < saved->statistics_count; i++) {
    const qa_app_q3_weapon_statistics *state = &saved->statistics[i];
    qa_q3_game_entity *entity = native(source, true, state->actor.id);
    qa_q3_game_client *client = entity != NULL ? entity->client : NULL;
    if (client == NULL) qa_range_error("Legacy statistics lost their source client");
    client->accuracy_shots = state->shots; client->accuracy_hits = state->rail.hits; client->accurate_count = state->rail.streak;
    qa_q3_base_player_state_slots_set(&client->ps.persistant, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT, state->rail.impressive_count);
    client->reward_time = state->rail.reward_until;
    if (state->rail.reward_until > saved->milliseconds) client->ps.e_flags = (double)(qa_js_to_int32(client->ps.e_flags) | 0x8000);
  }
  for (size_t i = 0; i < saved->hook_held_count; i++) {
    qa_q3_game_entity *entity = native(source, true, saved->hook_held[i].id);
    qa_q3_game_client *client = entity != NULL ? entity->client : NULL;
    if (client == NULL) qa_range_error("Legacy hook latch lost its source client");
    client->fire_held = true;
  }
  free(hooks.items); free(references.items);
}
