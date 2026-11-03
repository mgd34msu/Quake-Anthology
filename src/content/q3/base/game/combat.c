/* Port of src/content/q3/base/game/combat.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/combat.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/q3/base/game/use_participant.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/gameplay/damage_modifier.h"
#include "qa/foundation/js.h"
enum { MOD_JUICED = 27, MOD_PROXIMITY_MINE = 25, MOD_GRAPPLE_RANK = 28, MOD_FALLING = 19, ET_MOVER = 4, ET_MISSILE = 3, ET_GENERAL = 0,
  GT_TEAM = 3, GT_CTF = 4, GT_1FCTF = 5, GT_OBELISK = 6, PMF_TIME_KNOCKBACK = 64 };
static qa_q3_base_product base_product(qa_q3_base_product p) { return p == QA_Q3_BASE_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3; }
static bool on_same_team(qa_q3_game_combat_context *c, qa_q3_game_entity *first, qa_q3_game_entity *second) {
  return first->client && second->client && c->game_type >= GT_TEAM && first->client->sess.session_team == second->client->sess.session_team;
}
static void require_services(qa_q3_game_combat_context *c) {
  if (!c->authority || !c->participant || !c->linked_bounds) qa_range_error("Q3 combat context has no bound actor services");
}
bool qa_q3_invulnerability_blocks(qa_q3_game_combat_context *c, qa_q3_game_entity *target, const qa_vec3 *dir, const qa_vec3 *point, double mod) {
  if (c->product != QA_Q3_BASE_PRODUCT_MISSIONPACK || !target->client || mod == MOD_JUICED || target->client->invulnerability_time <= c->time) return false;
  if (dir && point && c->invulnerability_effect) c->invulnerability_effect(c->ctx, target, *dir, *point);
  return true;
}
double qa_q3_check_armor(qa_q3_game_entity *target, double damage, double flags) {
  if (damage == 0 || !target->client || (qa_js_to_int32(flags) & QA_Q3_DAMAGE_NO_ARMOR) != 0) return 0;
  qa_q3_base_player_state *ps = &target->client->ps;
  size_t slot = (size_t)qa_q3_base_stat_schema_for(base_product(ps->state_product))->armor;
  double scaled = (float)((float)damage * (float)QA_Q3_BASE_ARMOR_PROTECTION), rounded = ceil(scaled);
  double armor = qa_q3_base_player_state_slots_get(&ps->stats, slot);
  double save = rounded >= armor ? armor : rounded; if (save == 0) return 0;
  qa_q3_base_player_state_slots_set(&ps->stats, slot, armor - save); return save;
}
qa_q3_game_admit_result qa_q3_admit_target_damage(double intermission, qa_q3_game_entity *target, const qa_q3_use_participant *source, const qa_q3_use_participant *owner) {
  if (!qa_q3_game_entity_takedamage(target) || intermission != 0) return QA_Q3_GAME_ADMIT_HANDLED;
  if (target->s.e_type != ET_MOVER) return QA_Q3_GAME_ADMIT_CONTINUE;
  if (target->use.fn && target->mover_state == QA_Q3_MOVER_POS1) {
    qa_q3_entity_use_fn fn; memcpy(&fn, &target->use.fn, sizeof fn); fn(target->use.ctx, target, source, owner);
  }
  return QA_Q3_GAME_ADMIT_HANDLED;
}
static qa_damage_outcome apply_request(qa_q3_game_combat_context *c, qa_damage_request request) {
  return qa_gameplay_authority_apply(c->authority, qa_apply_source_damage_modifier(request, c->source_damage_modifier, c->is_live, c->ctx), NULL, NULL);
}
typedef struct apply_op { qa_q3_game_combat_context *context; qa_damage_request request; } apply_op;
static qa_damage_outcome run_apply_op(void *ctx) { apply_op *op = ctx; return apply_request(op->context, op->request); }
static qa_damage_request build_request(qa_q3_game_combat_context *c, qa_actor_id target, qa_q3_damage_participant source, qa_q3_use_participant owner,
    double amount, double flags, double mod, qa_vec3 impulse, qa_vec3 point, const qa_actor_id *projectile) {
  qa_damage_request request; memset(&request, 0, sizeof request);
  if (c->attack) request.attack = c->attack(c->services_ctx, source, owner, NULL, mod, flags, projectile);
  request.target = target; request.amount = amount; request.knockback = amount; request.direction = impulse; request.point = point;
  request.delivery = (qa_js_to_int32(flags) & QA_Q3_DAMAGE_RADIUS) != 0 ? "radius" : "direct";
  return request;
}
void qa_q3_combat_damage_from(qa_q3_game_combat_context *c, qa_q3_damage_participant target, const qa_q3_damage_participant *inflictor,
    const qa_q3_use_participant *attacker, qa_q3_damage_direction *direction, const qa_vec3 *point, double amount, double flags, double mod, const qa_actor_id *projectile) {
  if (c->intermission_queued != 0) return;
  if (!target.entity) {
    qa_vec3 origin; bool has = point != NULL; if (has) origin = *point;
    else if (target.origin) has = target.origin(target.origin_ctx, &origin);
    if (!has) return;
    qa_q3_damage_participant world = qa_q3_participant_entity(qa_q3_entity_pool_at(c->entities, QA_Q3_BASE_ENTITYNUM_WORLD));
    qa_q3_damage_participant src = inflictor ? *inflictor : world;
    qa_q3_use_participant own = attacker ? *attacker : world;
    qa_vec3 impulse = direction ? *direction : (qa_vec3){0, 0, 0};
    if (!direction) flags = qa_js_to_int32(flags) | QA_Q3_DAMAGE_NO_KNOCKBACK;
    else { qa_vec3 n = qa_normalize3(*direction); direction->x = n.x; direction->y = n.y; direction->z = n.z; }
    if (c->authority) (void)apply_request(c, build_request(c, target.actor, src, own, amount, flags, mod, impulse, origin, projectile));
    else if (c->apply_damage) c->apply_damage(c->ctx, target.actor, amount, amount, impulse, origin, (qa_js_to_int32(flags) & QA_Q3_DAMAGE_RADIUS) != 0);
    return;
  }
  qa_q3_game_entity *e = target.entity;
  if (!qa_q3_game_entity_takedamage(e)) return;
  if (qa_q3_invulnerability_blocks(c, e, direction, point, mod)) return;
  qa_q3_use_participant src = inflictor ? *inflictor : qa_q3_participant_entity(qa_q3_entity_pool_at(c->entities, QA_Q3_BASE_ENTITYNUM_WORLD));
  qa_q3_use_participant own = attacker ? *attacker : qa_q3_participant_entity(qa_q3_entity_pool_at(c->entities, QA_Q3_BASE_ENTITYNUM_WORLD));
  if (qa_q3_admit_target_damage(c->intermission_queued, e, &src, &own) == QA_Q3_GAME_ADMIT_HANDLED) return;
  if (c->product == QA_Q3_BASE_PRODUCT_MISSIONPACK && c->game_type == GT_OBELISK && c->check_obelisk_attack && c->check_obelisk_attack(c->ctx, e, own)) return;
  if (e->client && e->client->noclip) return;
  qa_vec3 impulse = direction ? *direction : (qa_vec3){0, 0, 0};
  if (!direction) flags = qa_js_to_int32(flags) | QA_Q3_DAMAGE_NO_KNOCKBACK;
  else { qa_vec3 n = qa_normalize3(*direction); direction->x = n.x; direction->y = n.y; direction->z = n.z; }
  qa_vec3 pt = point ? *point : qa_q3_base_entity_shared_current_origin(&e->r);
  if (!c->authority) {
    if (c->apply_damage) c->apply_damage(c->ctx, qa_q3_game_entity_actor(e).id, amount, amount, impulse, pt, (qa_js_to_int32(flags) & QA_Q3_DAMAGE_RADIUS) != 0);
    return;
  }
  apply_op op = {c, build_request(c, qa_q3_game_entity_actor(e).id, src, own, amount, flags, mod, impulse, pt, projectile)};
  qa_q3_damage_call call = {e, src, own, direction, point, amount, flags, mod};
  if (c->dispatch) (void)c->dispatch(c->services_ctx, call, run_apply_op, &op);
  else (void)run_apply_op(&op);
}
void qa_q3_combat_damage(qa_q3_game_combat_context *c, qa_q3_damage_participant target, const qa_q3_damage_participant *inflictor,
    const qa_q3_use_participant *attacker, qa_q3_damage_direction *direction, const qa_vec3 *point, double amount, double flags, double mod) {
  qa_q3_combat_damage_from(c, target, inflictor, attacker, direction, point, amount, flags, mod, NULL);
}
static int32_t imul32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static void committed_feedback(qa_q3_game_combat_context *c, qa_q3_game_entity *target, qa_q3_game_entity *native_owner,
    const qa_vec3 *direction, double knockback, double mod, qa_damage_decision d) {
  qa_q3_game_client *owner_client = native_owner ? native_owner->client : NULL;
  double owner_number = native_owner ? native_owner->s.number : QA_Q3_BASE_ENTITYNUM_NONE;
  qa_q3_game_client *client = target->client;
  bool any_armor = false;
  for (size_t i = 0; i < d.mutation_count; i++) if (d.mutations[i].kind == QA_DAMAGE_MUTATION_ARMOR) any_armor = true;
  if (d.applied_damage == 0 && !any_armor) return;
  double armor = 0, previous_health = qa_q3_game_entity_health(target); bool found_health = false;
  for (size_t i = 0; i < d.mutation_count; i++) {
    const qa_damage_mutation *m = &d.mutations[i];
    if (m->kind == QA_DAMAGE_MUTATION_ARMOR && m->armor_before.regular.kind != QA_REGULAR_ARMOR_NONE && m->armor_after.regular.kind != QA_REGULAR_ARMOR_NONE)
      armor += m->armor_before.regular.points - m->armor_after.regular.points;
    if (m->kind == QA_DAMAGE_MUTATION_HEALTH && !found_health) { previous_health = m->health_before; found_health = true; }
  }
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(c->product));
  bool missionpack = c->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  bool same_team = native_owner && on_same_team(c, target, native_owner);
  if (client) qa_q3_ranking_reports_damage(&c->entities->rankings, target->slot, native_owner ? native_owner->slot : QA_Q3_BASE_ENTITYNUM_WORLD,
      d.applied_damage + armor, missionpack && mod >= 23 ? (mod == MOD_GRAPPLE_RANK ? 23 : 0) : mod, c->time, owner_client != NULL, same_team);
  if (owner_client && target != native_owner && previous_health > 0 && target->s.e_type != ET_MISSILE && target->s.e_type != ET_GENERAL) {
    qa_q3_base_player_state_slots *persistent = &owner_client->ps.persistant;
    qa_q3_base_player_state_slots_set(persistent, QA_Q3_BASE_PERS_HITS, qa_q3_base_player_state_slots_get(persistent, QA_Q3_BASE_PERS_HITS) + (same_team ? -1 : 1));
    double previous_armor = (client ? qa_q3_base_player_state_slots_get(&client->ps.stats, (size_t)schema->armor) : 0) + armor;
    qa_q3_base_player_state_slots_set(persistent, QA_Q3_BASE_PERS_ATTACKEE_ARMOR, (int32_t)((uint32_t)qa_int32(previous_health) << 8) | qa_int32(previous_armor));
  }
  if (c->debug_damage) { qa_q3_damage_diagnostic diagnostic = {c->time, target->s.number, previous_health, d.applied_damage, armor}; c->debug_damage(c->ctx, diagnostic); }
  if (client) {
    qa_q3_base_player_state_slots_set(&client->ps.persistant, QA_Q3_BASE_PERS_ATTACKER, owner_number);
    client->damage_armor = qa_int32(client->damage_armor + armor);
    client->damage_blood = qa_int32(client->damage_blood + d.applied_damage);
    client->damage_knockback = qa_int32(client->damage_knockback + knockback);
    client->damage_from = direction ? *direction : qa_q3_base_entity_shared_current_origin(&target->r);
    client->damage_from_world = direction == NULL;
    client->last_hurt_client = owner_number; client->last_hurt_mod = mod;
  }
  if (native_owner && (c->game_type == GT_CTF || (missionpack && c->game_type == GT_1FCTF)) && c->check_hurt_carrier) c->check_hurt_carrier(c->ctx, target, native_owner);
  if (d.reaction && strcmp(d.reaction, "death") == 0) target->enemy = native_owner;
  if (d.reaction && strcmp(d.reaction, "death") == 0 && client) target->flags = qa_js_to_int32(target->flags) | QA_Q3_GAME_FLAG_NO_KNOCKBACK;
}
void qa_q3_damage_feedback(qa_q3_game_combat_context *c, qa_q3_damage_call call, qa_damage_decision decision) {
  qa_q3_game_entity *target = call.target, *native_owner = call.owner.entity;
  qa_q3_game_client *owner_client = native_owner ? native_owner->client : NULL, *client = target->client;
  double flags = call.flags, mod = call.method_of_death; int iflags = qa_js_to_int32(flags);
  double incoming = trunc(call.amount);
  bool missionpack = c->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  if (owner_client && native_owner != target) {
    const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(base_product(c->product));
    double maximum = qa_q3_base_player_state_slots_get(&owner_client->ps.stats, (size_t)schema->max_health);
    if (schema->product == QA_Q3_BASE_PRODUCT_MISSIONPACK &&
        qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_MISSIONPACK, qa_q3_base_player_state_slots_get(&owner_client->ps.stats, (size_t)schema->persistent_powerup))->tag == QA_Q3_PW_GUARD)
      maximum = trunc(maximum / 2);
    incoming = trunc((double)imul32(qa_int32(incoming), qa_int32(maximum)) / 100);
  }
  double knockback = (iflags & QA_Q3_DAMAGE_NO_KNOCKBACK) != 0 || (qa_js_to_int32(target->flags) & QA_Q3_GAME_FLAG_NO_KNOCKBACK) != 0 ? 0 : (incoming < 200 ? incoming : 200);
  if (knockback != 0 && client && call.direction && client->ps.pm_time == 0) {
    int32_t doubled = imul32(qa_int32(knockback), 2);
    client->ps.pm_time = doubled < 50 ? 50 : doubled > 200 ? 200 : doubled; client->ps.pm_flags = qa_js_to_int32(client->ps.pm_flags) | PMF_TIME_KNOCKBACK;
  }
  if ((iflags & QA_Q3_DAMAGE_NO_PROTECTION) == 0) {
    bool check_team = !missionpack || (mod != MOD_JUICED && (iflags & QA_Q3_DAMAGE_NO_TEAM_PROTECTION) == 0);
    if (check_team && target != native_owner && native_owner && on_same_team(c, target, native_owner) && !c->friendly_fire) return;
    if (missionpack && mod == MOD_PROXIMITY_MINE) {
      qa_actor_id parent; qa_q3_game_entity *parent_entity = NULL;
      if (c->parent && c->parent(c->services_ctx, qa_q3_use_actor(call.source), &parent))
        parent_entity = c->entities->options.records.native_by_actor(c->entities->options.records.ctx, parent);
      if (target == native_owner || (parent_entity && on_same_team(c, target, parent_entity))) return;
    }
    if ((qa_js_to_int32(target->flags) & QA_Q3_GAME_FLAG_GODMODE) != 0) return;
    if (c->authority) { qa_combat_state_result state = qa_gameplay_authority_read(c->authority, qa_q3_game_entity_actor(target).id); if (state.has_value && state.value.invulnerable) return; }
  }
  if (client && qa_q3_base_player_state_slots_get(&client->ps.powerups, QA_Q3_PW_BATTLESUIT) != 0) {
    qa_q3_entity_pool_add_event(c->entities, target, QA_Q3_EV_POWERUP_BATTLESUIT, 0);
    if ((iflags & QA_Q3_DAMAGE_RADIUS) != 0 || mod == MOD_FALLING) return;
  }
  committed_feedback(c, target, native_owner, call.direction, knockback, mod, decision);
}
void qa_q3_foreign_damage_feedback(qa_q3_game_combat_context *c, qa_q3_game_entity *target, qa_q3_game_entity *owner, qa_damage_decision d) {
  if (!d.has_feedback || !d.feedback_kind || strcmp(d.feedback_kind, "q3") != 0) return;
  qa_vec3 incoming = d.request.direction, normalized; const qa_vec3 *direction = NULL;
  if (incoming.x != 0 || incoming.y != 0 || incoming.z != 0) { normalized = qa_normalize3(incoming); direction = &normalized; }
  qa_q3_game_client *client = target->client;
  if (d.feedback_knockback != 0 && client && direction && client->ps.pm_time == 0) {
    int32_t doubled = imul32(qa_int32(d.feedback_knockback), 2);
    client->ps.pm_time = doubled < 50 ? 50 : doubled > 200 ? 200 : doubled; client->ps.pm_flags = qa_js_to_int32(client->ps.pm_flags) | PMF_TIME_KNOCKBACK;
  }
  if (d.feedback_battlesuit) qa_q3_entity_pool_add_event(c->entities, target, QA_Q3_EV_POWERUP_BATTLESUIT, 0);
  committed_feedback(c, target, owner, direction, d.feedback_knockback, d.request.attack.cause.kind == QA_ATTACK_CAUSE_Q3 ? d.request.attack.cause.means_of_death : 0, d);
}
bool qa_q3_combat_can_damage(qa_q3_game_combat_context *c, qa_q3_damage_participant target, qa_vec3 origin) {
  require_services(c);
  qa_actor_id actor = qa_q3_use_actor(target); qa_bounds bounds;
  return c->linked_bounds(c->services_ctx, actor, &bounds) && qa_q3_can_damage(c->spatial, actor, bounds, origin);
}
typedef struct radius_ctx { qa_q3_game_combat_context *context; qa_actor_id owner; double method_of_death; const qa_actor_id *projectile; } radius_ctx;
static bool radius_target(void *ctx, qa_actor_id actor, qa_q3_radius_target *out) {
  radius_ctx *r = ctx; qa_q3_game_combat_context *c = r->context;
  qa_combat_state_result state = qa_gameplay_authority_read(c->authority, actor);
  if (!state.has_value || !state.value.can_take_damage) return false;
  qa_bounds bounds; if (!c->linked_bounds(c->services_ctx, actor, &bounds)) return false;
  qa_q3_damage_participant target = c->participant(c->services_ctx, actor); qa_vec3 position;
  if (target.entity) position = qa_q3_base_entity_shared_current_origin(&target.entity->r);
  else if (!target.origin || !target.origin(target.origin_ctx, &position)) return false;
  qa_q3_game_entity *owner = c->entities->options.records.native_by_actor(c->entities->options.records.ctx, r->owner);
  out->bounds = bounds; out->origin = position;
  out->accuracy_eligible = target.entity && owner && c->log_accuracy_hit && c->log_accuracy_hit(c->ctx, target.entity, owner);
  return true;
}
static void radius_hit(void *ctx, qa_actor_id actor, qa_vec3 direction, qa_vec3 point, double points) {
  radius_ctx *r = ctx; qa_q3_game_combat_context *c = r->context;
  qa_q3_damage_participant target = c->participant(c->services_ctx, actor), owner = c->participant(c->services_ctx, r->owner);
  qa_vec3 copy = direction;
  qa_q3_combat_damage_from(c, target, NULL, &owner, &copy, &point, points, QA_Q3_DAMAGE_RADIUS, r->method_of_death, r->projectile);
}
bool qa_q3_combat_radius_damage_from(qa_q3_game_combat_context *c, qa_vec3 origin, qa_q3_damage_participant attacker, double amount, double radius,
    const qa_q3_damage_participant *ignore, double mod, const qa_actor_id *projectile) {
  require_services(c);
  radius_ctx r = {c, qa_q3_use_actor(attacker), mod, projectile};
  qa_q3_radius_host host; memset(&host, 0, sizeof host);
  host.spatial = c->spatial; host.ctx = &r; host.target = radius_target; host.damage = radius_hit;
  qa_actor_id ignored; if (ignore) ignored = qa_q3_use_actor(*ignore);
  return qa_q3_radius_damage(host, origin, amount, radius, ignore ? &ignored : NULL);
}
bool qa_q3_combat_radius_damage(qa_q3_game_combat_context *c, qa_vec3 origin, qa_q3_damage_participant attacker, double amount, double radius, const qa_q3_damage_participant *ignore, double mod) {
  return qa_q3_combat_radius_damage_from(c, origin, attacker, amount, radius, ignore, mod, NULL);
}
/* TS Q3CombatBridge context closures. */
static bool bridge_is_live(void *ctx, qa_actor_id actor) {
  qa_q3_base_combat_bridge *b = ctx;
  return qa_session_actor_registry_is_live(qa_q3_base_entity_records_host(b->host.records)->actors, actor);
}
static qa_attack_provenance bridge_attack(void *ctx, qa_q3_damage_participant inflictor, qa_q3_use_participant attacker, qa_item_id weapon, double mod, double flags, const qa_actor_id *projectile) {
  qa_actor_id none; memset(&none, 0, sizeof none);
  return qa_q3_base_combat_bridge_attack(ctx, inflictor, attacker, weapon, mod, flags, projectile != NULL, projectile ? *projectile : none);
}
static qa_damage_outcome bridge_dispatch(void *ctx, qa_q3_damage_call call, qa_damage_outcome (*operation)(void *), void *op_ctx) {
  qa_q3_base_combat_bridge_begin_call(ctx, call);
  qa_damage_outcome outcome = operation(op_ctx);
  qa_q3_base_combat_bridge_end_call(ctx);
  return outcome;
}
static qa_q3_damage_participant bridge_participant(void *ctx, qa_actor_id actor) {
  qa_q3_base_combat_bridge *b = ctx; return qa_q3_base_entity_records_damage_inflictor(b->host.records, true, actor);
}
static bool bridge_parent(void *ctx, qa_actor_id actor, qa_actor_id *out) {
  qa_q3_base_combat_bridge *b = ctx;
  return b->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && b->host.projectile_parent && b->host.projectile_parent(b->host.ctx, actor, out);
}
static bool bridge_linked_bounds(void *ctx, qa_actor_id actor, qa_bounds *out) {
  qa_q3_base_combat_bridge *b = ctx;
  qa_linked_body_result linked = qa_shared_body_table_linked(qa_q3_base_entity_records_host(b->host.records)->bodies, actor);
  if (!linked.has_value) return false;
  *out = linked.value.absolute_bounds; return true;
}
static bool bridge_is_player(void *ctx, qa_actor_id actor) {
  qa_q3_base_combat_bridge *b = ctx;
  qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(b->host.records, true, actor);
  if (native) return native->client != NULL;
  const qa_q3_base_record_host *host = qa_q3_base_entity_records_host(b->host.records);
  return host->is_player(host->ctx, actor);
}
void qa_q3_base_combat_bridge_bind_game_services(struct qa_q3_base_combat_bridge *b) {
  qa_q3_game_combat_context *c = &b->context;
  c->authority = b->host.authority;
  c->source_damage_modifier = b->host.has_source_damage_modifier ? &b->host.source_damage_modifier : NULL;
  c->services_ctx = b; c->is_live = bridge_is_live; c->attack = bridge_attack; c->dispatch = bridge_dispatch;
  c->participant = bridge_participant; c->parent = bridge_parent; c->linked_bounds = bridge_linked_bounds; c->is_player = bridge_is_player;
}
