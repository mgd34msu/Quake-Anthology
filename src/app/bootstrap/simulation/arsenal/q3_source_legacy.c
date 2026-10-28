/* Port of src/app/bootstrap/simulation/arsenal/q3-source-legacy.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q3_source_legacy.h"
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

static bool same_id(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
void qa_app_restore_legacy_q3_source(qa_app_legacy_q3_source_host host, qa_app_legacy_q3_source saved,
    void (*project)(void *ctx, qa_actor_id actor), void *project_ctx) {
  if (!host.product || strcmp(host.product, "baseq3") != 0) qa_range_error("Legacy ballistics require the base Q3 source");
  qa_actor_id references[256]; size_t reference_count = 0;
  qa_actor_id hooks[64]; size_t hook_count = 0;
  for (size_t i = 0; i < saved.projectile_count; i++) {
    qa_app_q3_projectile_state *state = &saved.projectiles[i];
    host.assert_owned(host.ctx, state->base.actor);
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) if (qa_same_actor(saved.projectiles[j].base.actor.id, state->base.actor.id) && saved.projectiles[j].base.actor.token == state->base.actor.token) duplicate = true;
    double slot; bool has_client;
    bool native = host.native_slot(host.ctx, state->base.actor.id, &slot, &has_client);
    if (!state->base.actor.owner || !host.provider || strcmp(state->base.actor.owner, host.provider) != 0
        || duplicate || native || !host.body_exists(host.ctx, state->base.actor.id))
      qa_range_error("Legacy projectile has no unique owned body");
    references[reference_count++] = state->base.owner;
    if (state->base.has_pass) references[reference_count++] = state->base.pass;
    if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED && state->base.weapon != 10)
      qa_range_error("Only a legacy grappling hook can be attached");
    if (state->base.weapon == 10) {
      bool seen = false;
      for (size_t j = 0; j < hook_count; j++) if (same_id(hooks[j], state->base.owner)) seen = true;
      if (seen || !host.has_player(host.ctx, state->base.owner)) qa_range_error("Legacy grappling hook has no unique live player");
      hooks[hook_count++] = state->base.owner;
    }
    if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED && state->has_attached_target) references[reference_count++] = state->attached_target;
    if (state->phase == QA_APP_Q3_PROJECTILE_EVENT && state->impact.has_target) references[reference_count++] = state->impact.target;
  }
  for (int group = 0; group < 2; group++) {
    size_t count = group == 0 ? saved.statistics_count : saved.hook_held_count;
    for (size_t i = 0; i < count; i++) {
      qa_owned_actor actor = group == 0 ? saved.statistics[i].actor : saved.hook_held[i];
      for (size_t j = 0; j < i; j++) {
        qa_owned_actor other = group == 0 ? saved.statistics[j].actor : saved.hook_held[j];
        if (qa_same_actor(other.id, actor.id) && other.token == actor.token)
          qa_range_error("Legacy Q3 client continuation has no unique live player");
      }
      if (!host.has_player(host.ctx, actor.id)) qa_range_error("Legacy Q3 client continuation has no unique live player");
      references[reference_count++] = actor.id;
    }
  }
  qa_actor_id foreign[256]; size_t foreign_count = 0;
  for (size_t i = 0; i < reference_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < foreign_count; j++) if (same_id(foreign[j], references[i])) seen = true;
    if (seen) continue;
    double slot; bool has_client;
    bool owned_projectile = false;
    for (size_t j = 0; j < saved.projectile_count; j++) if (same_id(saved.projectiles[j].base.actor.id, references[i])) owned_projectile = true;
    if (host.is_live(host.ctx, references[i]) && !host.native_slot(host.ctx, references[i], &slot, &has_client) && !owned_projectile)
      foreign[foreign_count++] = references[i];
  }
  for (size_t i = 0; i < foreign_count; i++)
    if (!host.body_exists(host.ctx, foreign[i])) qa_range_error("Legacy Q3 reference has no body");
  double slots[1024]; size_t slot_count = 0;
  for (double slot = 64; slot < 1022; slot++) if (!host.pool_inuse(host.ctx, slot)) slots[slot_count++] = slot;
  size_t foreign_non_players = 0;
  for (size_t i = 0; i < foreign_count; i++) if (!host.has_player(host.ctx, foreign[i])) foreign_non_players++;
  if (slot_count < saved.projectile_count + foreign_non_players)
    qa_range_error("Legacy Q3 projectiles exceed original source entity capacity");
  double entity_slots[256];
  for (size_t i = 0; i < saved.projectile_count; i++) {
    if (i >= slot_count) qa_range_error("Legacy Q3 entity slot is missing");
    entity_slots[i] = slots[i];
    host.adopt(host.ctx, slots[i], saved.projectiles[i].base.actor);
    if (slots[i] >= host.pool_num_entities(host.ctx)) host.pool_restore_counts(host.ctx, slots[i] + 1, host.max_clients);
  }
  for (size_t i = 0; i < reference_count; i++) project(project_ctx, references[i]);
  qa_app_legacy_continuation *continuations = calloc(saved.projectile_count ? saved.projectile_count : 1, sizeof *continuations);
  if (!continuations) qa_range_error("Out of memory");
  for (size_t i = 0; i < saved.projectile_count; i++) {
    qa_app_q3_projectile_state *state = &saved.projectiles[i];
    double slot = entity_slots[i];
    double owner_slot; bool owner_client;
    bool has_owner = host.native_slot(host.ctx, state->base.owner, &owner_slot, &owner_client);
    qa_app_legacy_entity_write write; memset(&write, 0, sizeof write);
    write.slot = slot;
    write.classname = state->base.weapon == 4 ? "grenade" : state->base.weapon == 5 ? "rocket" : state->base.weapon == 8 ? "plasma"
        : state->base.weapon == 9 ? "bfg" : state->base.weapon == 10 ? "hook" : "nail";
    write.weapon = state->base.weapon; write.e_flags = state->base.flags;
    write.e_type = QA_Q3_BASE_ET_MISSILE; write.sv_flags = QA_Q3_BASE_SVF_USE_CURRENT_ORIGIN;
    write.owner_num = has_owner ? owner_slot : 1023;
    write.clipmask = 0x6000001;
    write.damage = state->base.direct; write.splash_damage = state->base.splash; write.splash_radius = state->base.radius;
    write.method_of_death = state->base.method; write.splash_method_of_death = state->base.splash_method;
    write.origin = state->base.damage_point;
    write.trajectory = state->saved_trajectory;
    write.think = state->base.weapon == 10 ? "q3.base.game.missile.fireGrapple.think" : "q3.base.game.missile.launch.think";
    write.nextthink = state->expires;
    if (state->base.weapon == 10) {
      if (!has_owner || !owner_client) { free(continuations); qa_range_error("Legacy hook owner lost its source client"); }
      write.has_parent = true; write.parent_slot = owner_slot; write.other_entity_num = owner_slot;
      qa_app_legacy_client_write client; memset(&client, 0, sizeof client);
      client.slot = owner_slot; client.set_hook = true; client.hook_slot = slot;
      if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED) {
        write.e_type = QA_Q3_BASE_ET_GRAPPLE;
        write.think = "q3.base.game.missile.hookThink"; write.nextthink = state->next_think;
        client.grapple_pull = true;
        qa_vec3 origin;
        client.grapple_point = host.entity_origin && host.entity_origin(host.ctx, slot, &origin) ? origin : state->base.damage_point;
      }
      host.client_write(host.ctx, client);
    }
    if (state->phase == QA_APP_Q3_PROJECTILE_EVENT) {
      write.free_after_event = true; write.e_type = QA_Q3_BASE_ET_GENERAL; write.event_time = state->phase_time;
      write.event = state->impact.flesh ? QA_Q3_EV_MISSILE_HIT
          : (((long long)state->impact.surface_flags) & 0x1000) != 0 ? QA_Q3_EV_MISSILE_MISS_METAL : QA_Q3_EV_MISSILE_MISS;
      write.event_parm = qa_q3_base_direction_to_byte(&state->impact.normal);
      double target_slot; bool target_client;
      write.other_entity_num = state->impact.has_target && host.native_slot(host.ctx, state->impact.target, &target_slot, &target_client) ? target_slot : 1023;
    }
    host.entity_write(host.ctx, write);
    continuations[i].entity = slot;
    continuations[i].actor = qa_saved_actor_id_of(state->base.actor.id);
    continuations[i].owner = qa_saved_actor_id_of(state->base.owner);
    continuations[i].has_pass = state->base.has_pass;
    if (state->base.has_pass) continuations[i].pass = qa_saved_actor_id_of(state->base.pass);
    if (state->phase == QA_APP_Q3_PROJECTILE_ATTACHED && state->has_attached_target) {
      continuations[i].has_attachment = true;
      continuations[i].attachment_actor = qa_saved_actor_id_of(state->attached_target);
    }
  }
  host.missiles_restore(host.ctx, continuations, saved.projectile_count);
  free(continuations);
  host.random_reset(host.ctx, saved.random_seed);
  for (size_t i = 0; i < saved.statistics_count; i++) {
    double slot; bool has_client;
    if (!host.native_slot(host.ctx, saved.statistics[i].actor.id, &slot, &has_client) || !has_client)
      qa_range_error("Legacy statistics lost their source client");
    qa_app_legacy_client_write client; memset(&client, 0, sizeof client);
    client.slot = slot; client.set_accuracy = true;
    client.accuracy_shots = saved.statistics[i].shots;
    client.accuracy_hits = saved.statistics[i].rail.hits;
    client.accurate_count = saved.statistics[i].rail.streak;
    client.impressive_count = saved.statistics[i].rail.impressive_count;
    client.reward_until = saved.statistics[i].rail.reward_until;
    client.reward_flag = saved.statistics[i].rail.reward_until > saved.milliseconds;
    host.client_write(host.ctx, client);
  }
  for (size_t i = 0; i < saved.hook_held_count; i++) {
    double slot; bool has_client;
    if (!host.native_slot(host.ctx, saved.hook_held[i].id, &slot, &has_client) || !has_client)
      qa_range_error("Legacy hook latch lost its source client");
    qa_app_legacy_client_write client; memset(&client, 0, sizeof client);
    client.slot = slot; client.fire_held = true;
    host.client_write(host.ctx, client);
  }
}
