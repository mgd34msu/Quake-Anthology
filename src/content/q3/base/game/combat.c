/* Port of src/content/q3/base/game/combat.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/combat.h"
#include <math.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
enum { MOD_JUICED = 27, ET_MOVER = 4 };
bool qa_q3_invulnerability_blocks(qa_q3_game_combat_context *c, qa_q3_game_entity *target, const qa_vec3 *dir, const qa_vec3 *point, double mod) {
  if (c->product != QA_Q3_PRODUCT_MISSIONPACK || !target->client || mod == MOD_JUICED || target->client->invulnerability_time <= c->time) return false;
  if (dir && point && c->invulnerability_effect) c->invulnerability_effect(c->ctx, target, *dir, *point);
  return true;
}
double qa_q3_check_armor(qa_q3_game_entity *target, double damage, double flags) {
  if (damage == 0 || !target->client || ((int)flags & QA_Q3_DAMAGE_NO_ARMOR) != 0) return 0;
  qa_q3_game_player_state_mirror *ps = &target->client->ps;
  size_t slot = ps->product == QA_Q3_PRODUCT_BASEQ3 ? 3 : 4;
  double scaled = (float)((float)damage * (float)0.66), rounded = ceil(scaled);
  double armor = qa_q3_game_slots_get_mirror(&ps->stats, slot);
  double save = rounded >= armor ? armor : rounded; if (save == 0) return 0;
  qa_q3_game_slots_set_mirror(&ps->stats, slot, armor - save); return save;
}
qa_q3_admit_result qa_q3_admit_target_damage(double intermission, qa_q3_game_entity *target, const qa_q3_use_participant *source, const qa_q3_use_participant *owner) {
  if (!qa_q3_game_entity_takedamage(target) || intermission != 0) return QA_Q3_ADMIT_HANDLED;
  if (target->s.e_type != ET_MOVER) return QA_Q3_ADMIT_CONTINUE;
  if (target->use.fn && target->mover_state == QA_Q3_MOVER_POS1) {
    qa_q3_entity_use_fn fn; memcpy(&fn, &target->use.fn, sizeof fn); fn(target->use.ctx, target, source, owner);
  }
  return QA_Q3_ADMIT_HANDLED;
}
void qa_q3_combat_damage(qa_q3_game_combat_context *c, qa_q3_damage_participant target, const qa_q3_damage_participant *inflictor,
    const qa_q3_use_participant *attacker, qa_q3_damage_direction *direction, const qa_vec3 *point, double amount, double flags, double mod) {
  (void)inflictor; (void)attacker; (void)mod;
  if (c->intermission_queued != 0) return;
  if (!target.entity) {
    qa_vec3 origin; bool has = point != NULL; if (has) origin = *point;
    else if (target.origin) has = target.origin(target.origin_ctx, &origin); else has = false;
    if (!has) return;
    qa_vec3 impulse = direction ? *direction : (qa_vec3){0, 0, 0};
    if (!direction) flags = (int)flags | QA_Q3_DAMAGE_NO_KNOCKBACK;
    else { qa_vec3 n = qa_normalize3(*direction); direction->x = n.x; direction->y = n.y; direction->z = n.z; }
    if (c->apply_damage) c->apply_damage(c->ctx, target.actor, amount, amount, impulse, origin, ((int)flags & QA_Q3_DAMAGE_RADIUS) != 0);
    return;
  }
  qa_q3_game_entity *e = target.entity;
  if (!qa_q3_game_entity_takedamage(e)) return;
  if (qa_q3_invulnerability_blocks(c, e, direction, point, mod)) return;
  qa_q3_use_participant src = inflictor ? *inflictor : qa_q3_participant_entity(qa_q3_entity_pool_at(c->entities, QA_Q3_ENTITYNUM_WORLD));
  qa_q3_use_participant own = attacker ? *attacker : qa_q3_participant_entity(qa_q3_entity_pool_at(c->entities, QA_Q3_ENTITYNUM_WORLD));
  if (qa_q3_admit_target_damage(c->intermission_queued, e, &src, &own) == QA_Q3_ADMIT_HANDLED) return;
  if (c->product == QA_Q3_PRODUCT_MISSIONPACK && c->game_type == 6 && c->check_obelisk_attack && c->check_obelisk_attack(c->ctx, e, own)) return;
  if (e->client && e->client->noclip) return;
  qa_vec3 impulse = direction ? *direction : (qa_vec3){0, 0, 0};
  if (!direction) flags = (int)flags | QA_Q3_DAMAGE_NO_KNOCKBACK;
  else { qa_vec3 n = qa_normalize3(*direction); direction->x = n.x; direction->y = n.y; direction->z = n.z; }
  qa_vec3 pt = point ? *point : e->r.current_origin;
  if (c->apply_damage) c->apply_damage(c->ctx, qa_q3_game_entity_actor(e).id, amount, amount, impulse, pt, ((int)flags & QA_Q3_DAMAGE_RADIUS) != 0);
}
bool qa_q3_combat_can_damage(qa_q3_game_combat_context *c, qa_q3_damage_participant target, qa_vec3 origin) {
  if (!target.entity) return false;
  qa_bounds b = {target.entity->r.mins, target.entity->r.maxs};
  /* TS uses linked bounds via actors.linkedBounds; mirror uses r bounds offset by origin. */
  qa_bounds world = {qa_add3(target.entity->r.current_origin, b.min), qa_add3(target.entity->r.current_origin, b.max)};
  return qa_q3_can_damage(c->spatial, qa_q3_game_entity_actor(target.entity).id, world, origin);
}
bool qa_q3_combat_radius_damage(qa_q3_game_combat_context *c, qa_vec3 origin, qa_q3_damage_participant attacker, double amount, double radius, const qa_q3_damage_participant *ignore, double mod) {
  (void)attacker; (void)mod;
  if (!c->has_radius_host) qa_range_error("Combat radius damage requires a radius host");
  const qa_actor_id *ig = NULL; qa_actor_id ig_id;
  if (ignore) { ig_id = ignore->entity ? qa_q3_game_entity_actor(ignore->entity).id : ignore->actor; ig = &ig_id; }
  return qa_q3_radius_damage(c->radius_host, origin, amount, radius, ig);
}
