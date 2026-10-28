/* Port of src/content/q3/base/combat-bridge.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/combat_bridge.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/world/gameplay/policies.h"

#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/contracts/time.h"
#include "qa/foundation/support.h"

static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }

/* Spatial adapter for the landed combat context (ctx is the bridge). */
static qa_q3_radius_trace_result spatial_trace(void *ctx, qa_q3_radius_trace_query query) {
  qa_q3_base_combat_bridge *bridge = ctx;
  qa_q3_base_actor_trace_query input;
  memset(&input, 0, sizeof input);
  input.start = query.start;
  input.end = query.end;
  input.shape.kind = QA_Q3_BASE_SHAPE_POINT;
  input.mask = query.mask;
  input.pass_actor = query.has_pass_actor ? &query.pass_actor : NULL;
  qa_q3_base_actor_trace_result result =
      bridge->host.spatial->trace_actor(bridge->host.spatial->ctx, input);
  qa_q3_radius_trace_result out = {result.fraction,
      result.hit_kind == QA_Q3_BASE_HIT_ACTOR ? QA_Q3_RADIUS_HIT_ACTOR
      : result.hit_kind == QA_Q3_BASE_HIT_WORLD ? QA_Q3_RADIUS_HIT_WORLD : QA_Q3_RADIUS_HIT_NONE,
      result.hit_actor};
  return out;
}
static size_t spatial_area(void *ctx, qa_bounds bounds, double max_count, qa_actor_id *out, size_t out_cap) {
  qa_q3_base_combat_bridge *bridge = ctx;
  size_t count = 0;
  const qa_actor_id *actors = bridge->host.spatial->area_actors(bridge->host.spatial->ctx, bounds, max_count, &count);
  size_t take = count < out_cap ? count : out_cap;
  for (size_t i = 0; i < take; i++) out[i] = actors[i];
  return take;
}
static bool context_is_live(void *ctx, qa_actor_id actor) {
  qa_q3_base_combat_bridge *bridge = ctx;
  return bridge->host.is_live(bridge->host.ctx, actor);
}
static void context_apply_damage(void *ctx, qa_actor_id target, double amount, double knockback,
    qa_vec3 direction, qa_vec3 point, bool radius) {
  qa_q3_base_combat_bridge *bridge = ctx;
  bridge->host.apply_damage(bridge->host.ctx, target, amount, knockback, direction, point, radius);
}
static void context_debug_damage(void *ctx, qa_q3_damage_diagnostic diagnostic) {
  qa_q3_base_combat_bridge *bridge = ctx;
  if (bridge->host.debug_damage) bridge->host.debug_damage(bridge->host.ctx, diagnostic);
}
static void context_check_hurt_carrier(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  qa_q3_base_combat_bridge *bridge = ctx;
  bridge->host.check_hurt_carrier(bridge->host.ctx, target, attacker);
}
static bool context_log_accuracy(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  qa_q3_base_combat_bridge *bridge = ctx;
  return bridge->host.log_accuracy_hit(bridge->host.ctx, target, attacker);
}
static bool context_check_obelisk(void *ctx, qa_q3_game_entity *target, qa_q3_use_participant attacker) {
  qa_q3_base_combat_bridge *bridge = ctx;
  if (bridge->host.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) return false;
  qa_q3_game_entity *native = attacker.entity != NULL ? attacker.entity
      : qa_q3_base_entity_records_native_by_actor(bridge->host.records, true, attacker.actor);
  const qa_q3_base_record_host *records_host = qa_q3_base_entity_records_host(bridge->host.records);
  if (native == NULL && records_host->is_player(records_host->ctx, qa_q3_use_actor(attacker)))
    qa_range_error("Admitted Q3 map player has no native client behavior record");
  return native == NULL ? false : bridge->host.check_obelisk_attack(bridge->host.ctx, target, native);
}
static void context_invulnerability(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point) {
  qa_q3_base_combat_bridge *bridge = ctx;
  if (bridge->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && bridge->host.invulnerability_effect)
    bridge->host.invulnerability_effect(bridge->host.ctx, target, direction, point);
}

void qa_q3_base_combat_bridge_init(qa_q3_base_combat_bridge *bridge, qa_q3_base_combat_bridge_host host) {
  memset(bridge, 0, sizeof *bridge);
  bridge->host = host;
  qa_q3_game_combat_context *context = &bridge->context;
  memset(context, 0, sizeof *context);
  context->product = host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? QA_Q3_PRODUCT_MISSIONPACK : QA_Q3_PRODUCT_BASEQ3;
  context->entities = host.entities;
  context->spatial.ctx = bridge;
  context->spatial.trace_actor = spatial_trace;
  context->spatial.area_actors = spatial_area;
  context->ctx = bridge;
  context->is_live = context_is_live;
  context->apply_damage = context_apply_damage;
  context->debug_damage = context_debug_damage;
  context->check_hurt_carrier = context_check_hurt_carrier;
  context->log_accuracy_hit = context_log_accuracy;
  context->check_obelisk_attack = context_check_obelisk;
  context->invulnerability_effect = context_invulnerability;
  (void)qa_q3_base_combat_bridge_context(bridge);
}
void qa_q3_base_combat_bridge_destroy(qa_q3_base_combat_bridge *bridge) {
  free(bridge->calls);
  bridge->calls = NULL;
  bridge->call_count = bridge->call_capacity = 0;
}
qa_q3_game_combat_context *qa_q3_base_combat_bridge_context(qa_q3_base_combat_bridge *bridge) {
  bridge->context.time = bridge->host.time(bridge->host.ctx);
  bridge->context.intermission_queued = bridge->host.intermission_queued(bridge->host.ctx);
  bridge->context.game_type = bridge->host.game_type(bridge->host.ctx);
  bridge->context.friendly_fire = bridge->host.friendly_fire(bridge->host.ctx);
  bridge->context.knockback = bridge->host.knockback(bridge->host.ctx);
  return &bridge->context;
}
qa_q3_base_combat_bridge_save_state qa_q3_base_combat_bridge_capture_save_state(qa_q3_base_combat_bridge *bridge) {
  if (bridge->call_count != 0) qa_range_error("Cannot save Q3 during a combat call");
  qa_q3_base_combat_bridge_save_state out = {bridge->sequence};
  return out;
}
void qa_q3_base_combat_bridge_restore_save_state(qa_q3_base_combat_bridge *bridge, const qa_save_value *value) {
  if (bridge->call_count != 0) qa_range_error("Cannot restore Q3 during a combat call");
  qa_save_reader reader, field;
  if (!qa_save_reader_init(&reader, value, "q3.combatBridge")) qa_range_error("out of memory");
  if (!qa_save_reader_field(&reader, "sequence", &field)) qa_range_error("out of memory");
  bridge->sequence = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
}
bool qa_q3_base_combat_bridge_current_call(qa_q3_base_combat_bridge *bridge, qa_q3_damage_call *out) {
  if (bridge->call_count == 0) return false;
  *out = bridge->calls[bridge->call_count - 1];
  return true;
}
void qa_q3_base_combat_bridge_begin_call(qa_q3_base_combat_bridge *bridge, qa_q3_damage_call call) {
  if (bridge->call_count == bridge->call_capacity) {
    size_t capacity = bridge->call_capacity ? bridge->call_capacity * 2 : 4;
    qa_q3_damage_call *grown = realloc(bridge->calls, capacity * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    bridge->calls = grown;
    bridge->call_capacity = capacity;
  }
  bridge->calls[bridge->call_count++] = call;
}
void qa_q3_base_combat_bridge_end_call(qa_q3_base_combat_bridge *bridge) {
  if (bridge->call_count > 0) bridge->call_count--;
}

qa_attack_provenance qa_q3_base_combat_bridge_attack(qa_q3_base_combat_bridge *bridge,
    qa_q3_damage_participant inflictor, qa_q3_use_participant attacker, qa_item_id weapon,
    double means_of_death, double flags, bool has_projectile, qa_actor_id originating_projectile) {
  qa_attack_provenance out;
  memset(&out, 0, sizeof out);
  out.sequence = bridge->sequence++;
  out.time.kind = QA_SOURCE_TIME_MILLISECONDS;
  out.time.value = bridge->host.time(bridge->host.ctx);
  size_t base = bridge->provenance_index;
  bridge->provenance_actors[base % 16] = qa_q3_use_actor(attacker);
  bridge->provenance_actors[(base + 1) % 16] = qa_q3_use_actor(inflictor);
  bridge->provenance_index = base + 2;
  out.attacker = &bridge->provenance_actors[base % 16];
  out.inflictor = &bridge->provenance_actors[(base + 1) % 16];
  out.has_originating_projectile = has_projectile;
  out.originating_projectile = originating_projectile;
  if (weapon != NULL) out.weapon = weapon;
  else {
    double inflictor_weapon = inflictor.entity != NULL ? inflictor.entity->s.weapon : 0;
    double attacker_weapon = attacker.entity != NULL ? attacker.entity->s.weapon : 0;
    double picked = inflictor_weapon != 0 ? inflictor_weapon : attacker_weapon;
    const qa_q3_weapon_item *item = qa_q3_get_weapon_item(picked);
    out.weapon = item != NULL ? item->item : NULL;
  }
  out.weapon_provider = bridge->host.weapon_provider;
  out.combat_provider = bridge->host.combat_provider;
  out.inventory_provider = bridge->host.inventory_provider;
  out.movement_provider = bridge->host.movement_provider;
  out.has_damage_powerup_owner = true;
  out.damage_powerup_owner = bridge->host.has_damage_powerup_owner
      ? bridge->host.damage_powerup_owner : bridge->host.weapon_provider;
  out.cause.kind = QA_ATTACK_CAUSE_Q3;
  out.cause.means_of_death = means_of_death;
  out.cause.damage_flags = flags;
  return out;
}

void qa_q3_base_combat_bridge_before_reaction(qa_q3_base_combat_bridge *bridge, qa_damage_decision decision) {
  qa_q3_damage_call call;
  bool has_call = qa_q3_base_combat_bridge_current_call(bridge, &call);
  if (decision.request.attack.cause.kind == QA_ATTACK_CAUSE_Q3 && has_call &&
      call.target != NULL && qa_same_actor(qa_q3_game_entity_actor(call.target).id, decision.request.target)) {
    bridge->host.damage_feedback(bridge->host.ctx, call, decision);
    return;
  }
  qa_q3_game_entity *target = qa_q3_base_entity_records_native_by_actor(bridge->host.records, true, decision.request.target);
  const qa_actor_id *attacker = decision.request.attack.attacker;
  if (target != NULL)
    bridge->host.foreign_damage_feedback(bridge->host.ctx, target,
        attacker == NULL ? NULL : qa_q3_base_entity_records_native_by_actor(bridge->host.records, true, *attacker),
        decision);
}

/* Policy wrapper. */
static bool same_team(qa_q3_base_combat_bridge *bridge, qa_q3_game_entity *first, qa_q3_game_entity *second) {
  return first != NULL && first->client != NULL && second != NULL && second->client != NULL &&
      bridge->host.game_type(bridge->host.ctx) >= QA_Q3_BASE_GT_TEAM &&
      first->client->sess.session_team == second->client->sess.session_team;
}
static qa_q3_combat_context policy_context(void *ctx, qa_damage_request request,
    qa_combat_state target_state, const qa_combat_state *attacker_state) {
  (void)target_state; (void)attacker_state;
  qa_q3_base_combat_bridge *bridge = ctx;
  qa_q3_base_combat_bridge_host *host = &bridge->host;
  qa_q3_game_entity *target = qa_q3_base_entity_records_native_by_actor(host->records, true, request.target);
  qa_q3_game_entity *owner = request.attack.attacker == NULL ? NULL
      : qa_q3_base_entity_records_native_by_actor(host->records, true, *request.attack.attacker);
  qa_q3_game_client *target_client = target ? target->client : NULL;
  qa_q3_game_client *owner_client = owner ? owner->client : NULL;
  double method = request.attack.cause.kind == QA_ATTACK_CAUSE_Q3 ? request.attack.cause.means_of_death : -1;
  qa_q3_game_entity *parent = NULL;
  if (host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && method == 25 && request.attack.inflictor != NULL) {
    qa_actor_id parent_actor;
    if (host->projectile_parent(host->ctx, *request.attack.inflictor, &parent_actor))
      parent = qa_q3_base_entity_records_native_by_actor(host->records, true, parent_actor);
  }
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(host->product);
  bool guard = owner_client != NULL && schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_MISSIONPACK,
          slot_get(&owner_client->ps.stats, schema->persistent_powerup))->tag == QA_Q3_PW_GUARD;
  qa_q3_combat_context out;
  memset(&out, 0, sizeof out);
  out.player = target_client != NULL;
  out.attacker_player = owner_client != NULL;
  out.attacker_max_health = owner_client != NULL ? slot_get(&owner_client->ps.stats, schema->max_health) : 100;
  out.attacker_guard = guard;
  out.intermission = host->intermission_queued(host->ctx) != 0;
  out.noclip = target_client != NULL && target_client->noclip;
  out.missionpack_invulnerability = host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
      target_client != NULL && target_client->invulnerability_time > host->time(host->ctx);
  out.no_knockback = target != NULL && ((int32_t)target->flags & QA_Q3_GAME_FLAG_NO_KNOCKBACK) != 0;
  out.knockback_scale = host->knockback(host->ctx);
  out.friendly_fire = host->friendly_fire(host->ctx);
  out.battlesuit = target_client != NULL && slot_get(&target_client->ps.powerups, QA_Q3_PW_BATTLESUIT) != 0;
  out.falling = method == 19;
  out.juiced = method == 27;
  out.proximity_protected = host->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && method == 25 &&
      ((target != NULL && target == owner) || (parent != NULL && same_team(bridge, target, parent)));
  out.product = qa_q3_base_product_name(host->product);
  return out;
}
static qa_victim_armor_context armor_context_of(void *ctx, qa_damage_request request) {
  qa_q3_base_combat_bridge *bridge = ctx;
  return bridge->host.armor_context(bridge->host.ctx, request);
}
static const char *const team_strings[] = {"q3-team:0", "q3-team:1", "q3-team:2", "q3-team:3"};
static qa_combat_state source_state(qa_q3_base_combat_bridge *bridge, qa_damage_request request,
    qa_combat_state state, bool attacker) {
  const qa_actor_id *id = attacker ? request.attack.attacker : &request.target;
  qa_q3_game_entity *entity = id == NULL ? NULL
      : qa_q3_base_entity_records_native_by_actor(bridge->host.records, true, *id);
  qa_combat_state out = state;
  out.invulnerable = state.invulnerable ||
      (entity != NULL && ((int32_t)entity->flags & QA_Q3_GAME_FLAG_GODMODE) != 0);
  if (entity != NULL && entity->client != NULL &&
      bridge->host.game_type(bridge->host.ctx) >= QA_Q3_BASE_GT_TEAM) {
    double team = entity->client->sess.session_team;
    out.team = (team >= 0 && team < 4 && team == (int)team) ? team_strings[(int)team] : state.team;
  }
  return out;
}
typedef struct project_ctx {
  qa_q3_base_combat_bridge *bridge;
  qa_damage_request request;
  qa_current_combat_state inner;
} project_ctx;
static qa_combat_state_result project_target(void *ctx) {
  project_ctx *project = ctx;
  qa_combat_state_result value = project->inner.target(project->inner.ctx);
  if (value.has_value) value.value = source_state(project->bridge, project->request, value.value, false);
  return value;
}
static qa_combat_state_result project_attacker(void *ctx) {
  project_ctx *project = ctx;
  qa_combat_state_result value = project->inner.attacker(project->inner.ctx);
  if (value.has_value) value.value = source_state(project->bridge, project->request, value.value, true);
  return value;
}
static qa_current_combat_state project_current(qa_q3_base_combat_bridge *bridge,
    qa_damage_request request, qa_current_combat_state inner) {
  project_ctx *project = malloc(sizeof *project);
  if (!project) qa_range_error("out of memory");
  project->bridge = bridge;
  project->request = request;
  project->inner = inner;
  qa_current_combat_state out = {project, project_target, project_attacker};
  return out;
}
typedef struct progress_ctx {
  qa_q3_base_combat_bridge *bridge;
  qa_combat_progress inner;
} progress_ctx;
static void wrap_progress(qa_q3_base_combat_bridge *bridge, qa_combat_progress *value);
static void resume_source_adapter(void *ctx, qa_current_combat_state current, qa_combat_progress *out) {
  progress_ctx *adapter = ctx;
  adapter->inner.resume_source(adapter->inner.ctx,
      project_current(adapter->bridge, adapter->inner.request, current), out);
  wrap_progress(adapter->bridge, out);
}
static void resume_armor_adapter(void *ctx, qa_armor_stage_result result,
    qa_current_combat_state current, qa_combat_progress *out) {
  progress_ctx *adapter = ctx;
  adapter->inner.resume_armor(adapter->inner.ctx, result,
      project_current(adapter->bridge, adapter->inner.request, current), out);
  wrap_progress(adapter->bridge, out);
}
static void wrap_progress(qa_q3_base_combat_bridge *bridge, qa_combat_progress *value) {
  if (strcmp(value->kind, "complete") == 0) return;
  progress_ctx *adapter = malloc(sizeof *adapter);
  if (!adapter) qa_range_error("out of memory");
  adapter->bridge = bridge;
  adapter->inner = *value;
  if (strcmp(value->kind, "source-continuation") == 0) {
    value->ctx = adapter;
    value->resume_source = resume_source_adapter;
  } else if (strcmp(value->kind, "armor-stage") == 0) {
    value->ctx = adapter;
    value->resume_armor = resume_armor_adapter;
  } else free(adapter);
}
typedef struct policy_ctx {
  qa_q3_base_combat_bridge *bridge;
  qa_combat_policy inner;
} policy_ctx;
static void policy_decide(void *ctx, qa_damage_request request, qa_combat_state target,
    const qa_combat_state *attacker, bool has_prepared, double prepared, qa_combat_progress *out) {
  policy_ctx *policy = ctx;
  qa_combat_state projected_target = source_state(policy->bridge, request, target, false);
  qa_combat_state projected_attacker;
  const qa_combat_state *attacker_ptr = NULL;
  if (attacker != NULL) {
    projected_attacker = source_state(policy->bridge, request, *attacker, true);
    attacker_ptr = &projected_attacker;
  }
  policy->inner.decide(policy->inner.ctx, request, projected_target, attacker_ptr, has_prepared, prepared, out);
  wrap_progress(policy->bridge, out);
}
qa_combat_policy qa_q3_base_combat_bridge_policy(qa_q3_base_combat_bridge *bridge) {
  qa_q3_combat_policy_options options;
  memset(&options, 0, sizeof options);
  options.id = bridge->host.combat_provider;
  options.ctx = bridge;
  options.context = policy_context;
  options.armor = qa_native_victim_armor(armor_context_of, bridge);
  qa_combat_policy inner = qa_create_q3_combat_policy(options);
  policy_ctx *policy = malloc(sizeof *policy);
  if (!policy) qa_range_error("out of memory");
  policy->bridge = bridge;
  policy->inner = inner;
  qa_combat_policy out;
  memset(&out, 0, sizeof out);
  out.ctx = policy;
  out.id = inner.id;
  out.decide = policy_decide;
  return out;
}
void qa_q3_base_combat_bridge_policy_destroy(qa_combat_policy policy) { free(policy.ctx); }
