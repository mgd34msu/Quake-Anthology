/* Port of src/content/q3/base/game/weapon.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/weapon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/hitscan.h"
#include "qa/content/q3/base/game/rankings.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/direction_byte.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#define AWARD_FLAGS_V (0x8 | 0x40 | 0x800 | 0x8000 | 0x10000 | 0x20000)
typedef qa_q3_weapon_runtime RT;
typedef qa_q3_bullet_attack Attack;
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
static qa_q3_game_client *client_of(qa_q3_game_entity *e) {
  if (!e->client) qa_range_error("Weapon attack requires a client entity");
  return e->client;
}
static void owned(RT *r, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(r->host.combat->entities, e->slot) != e)
    qa_range_error("Weapon entity does not belong to this pool");
}
double qa_q3_weapon_damage_factor(qa_q3_game_client *client, double quad_factor, qa_q3_game_product product) {
  double factor = qa_q3_game_slots_get_mirror(&client->ps.powerups, QA_Q3_PW_QUAD) != 0 ? (float)quad_factor : 1;
  if (product == QA_Q3_GAME_PRODUCT_MISSIONPACK && client->persistant_powerup && client->persistant_powerup->item && (int)client->persistant_powerup->item->tag == QA_Q3_PW_DOUBLER)
    factor = (float)(factor * 2);
  return factor;
}
qa_q3_sphere_intersections qa_q3_ray_sphere_intersections(qa_vec3 origin, double radius, qa_vec3 point, qa_q3_missile_direction *direction) {
  qa_vec3 dir = qa_normalize3((qa_vec3){direction->x, direction->y, direction->z});
  direction->x = dir.x; direction->y = dir.y; direction->z = dir.z;
  qa_vec3 offset = qa_sub3(point, origin);
  double b = (float)(2 * qa_dot3(dir, offset));
  double c = (float)(qa_dot3(offset, offset) - (float)(radius * radius));
  double disc = (float)((float)(b * b) - (float)(4 * c));
  qa_q3_sphere_intersections r; r.count = 0;
  if (disc > 0) {
    double root = (float)sqrt(disc);
    double first = (float)((float)(-b + root) / 2), second = (float)((float)(-b - root) / 2);
    r.points[0] = qa_add3(point, qa_scale3(dir, first)); r.points[1] = qa_add3(point, qa_scale3(dir, second)); r.count = 2;
  } else if (disc == 0) { r.points[0] = qa_add3(point, qa_scale3(dir, (float)(-b / 2))); r.count = 1; }
  return r;
}
qa_q3_weapon_invuln_impact qa_q3_invulnerability_effect(qa_q3_entity_pool *pool, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point) {
  qa_q3_weapon_invuln_impact miss = {QA_Q3_WEAPON_INVULN_MISS, {0, 0, 0}, {0, 0, 0}};
  if (pool->options.product != QA_Q3_GAME_PRODUCT_MISSIONPACK) qa_range_error("Invulnerability effects require missionpack");
  if (!target->client) return miss;
  qa_q3_missile_direction backwards = {-direction.x, -direction.y, -direction.z};
  qa_q3_sphere_intersections hits = qa_q3_ray_sphere_intersections(target->client->ps.origin, 42, point, &backwards);
  if (hits.count == 0) return miss;
  qa_vec3 impact_point = hits.points[0];
  qa_q3_game_entity *impact = qa_q3_entity_pool_temp_entity(pool, target->client->ps.origin, QA_Q3_EV_INVUL_IMPACT);
  qa_vec3 offset = qa_sub3(impact_point, target->client->ps.origin), angles = qa_vector_to_angles(offset);
  double pitch = (float)(angles.x + 90); if (pitch > 360) pitch = (float)(pitch - 360);
  impact->s.angles = (qa_vec3){pitch, angles.y, angles.z};
  qa_q3_weapon_invuln_impact hit = {QA_Q3_WEAPON_INVULN_HIT, impact_point, qa_normalize3(offset)}; return hit;
}
static qa_q3_accuracy_subject accuracy_subject(qa_q3_game_entity *e, qa_actor_id actor, char *team_buf) {
  qa_q3_accuracy_subject s; s.actor = actor; s.damageable = qa_q3_game_entity_takedamage(e);
  s.player = e->client != NULL; s.health = e->client ? e->client->ps.health : qa_q3_game_entity_health(e);
  if (!e->client) s.team = NULL; else { snprintf(team_buf, 64, "q3-team:%lld", (long long)e->client->sess.session_team); s.team = team_buf; }
  return s;
}
bool qa_q3_log_accuracy_hit(double game_type, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  if (!qa_q3_game_entity_takedamage(target) || target == attacker || !target->client || !attacker->client || target->client->ps.health <= 0) return false;
  char tb[64], ab[64];
  return qa_q3_accuracy_hit(game_type >= QA_Q3_BASE_GT_TEAM,
      accuracy_subject(target, qa_q3_game_entity_actor(target).id, tb),
      accuracy_subject(attacker, qa_q3_game_entity_actor(attacker).id, ab));
}
static double quad_of(RT *r, qa_q3_game_entity *e) {
  qa_q3_game_client *c = client_of(e);
  if (r->host.has_damage_factor) return r->host.damage_factor(r->host.ctx, e);
  return qa_q3_weapon_damage_factor(c, r->host.quad_factor, r->host.combat->product);
}
static Attack attack_of(qa_q3_game_entity *e, double quad) {
  qa_q3_game_client *c = client_of(e); qa_angle_vectors v = qa_qvm_angle_vectors(c->ps.viewangles);
  qa_vec3 eye = {e->s.pos.base.x, e->s.pos.base.y, (float)(e->s.pos.base.z + c->ps.viewheight)};
  Attack a; a.forward = v.forward; a.right = v.right; a.up = v.up;
  a.muzzle = qa_q3_snap_vector(qa_add3(eye, qa_scale3(v.forward, 14))); a.quad = quad; return a;
}
static double scaled(double amount, Attack *a) { return qa_qvm_float_to_int((float)((float)amount * a->quad)); }
static qa_q3_missile_direction dir_of(qa_vec3 v) { qa_q3_missile_direction d = {v.x, v.y, v.z}; return d; }
/* Hitscan adapter state (stack-lived per synchronous fire call). */
typedef struct {
  RT *runtime; qa_q3_game_entity *entity; qa_actor_id attacker; double method, firing_weapon;
  qa_q3_game_client *firing_client; qa_q3_game_entity *shotgun_event; double source_number, client_number;
} adapter;
static bool hit_target(adapter *ad, qa_actor_id actor, qa_q3_bullet_target *out) {
  RT *r = ad->runtime; double health; bool can; const char *team;
  if (!r->host.authority_read(r->host.ctx, actor, &health, &can, &team)) return false;
  qa_q3_damage_participant target = r->host.participant(r->host.ctx, actor);
  qa_q3_game_entity *src = r->host.native_by_actor(r->host.ctx, ad->attacker);
  char ab[64], tb[64];
  qa_q3_accuracy_subject atk = src ? accuracy_subject(src, ad->attacker, ab)
      : (qa_q3_accuracy_subject){ad->attacker, false, false, 0, NULL};
  qa_q3_accuracy_subject obs = target.entity ? accuracy_subject(target.entity, actor, tb)
      : (qa_q3_accuracy_subject){actor, can, r->host.is_player(r->host.ctx, actor), health, team};
  out->damageable = obs.damageable; out->player = obs.player;
  out->accuracy_eligible = qa_q3_accuracy_hit(r->host.combat->game_type >= QA_Q3_BASE_GT_TEAM, obs, atk);
  out->invulnerable = target.entity && target.entity->client && target.entity->client->invulnerability_time > r->host.combat->time;
  return true;
}
static qa_q3_actor_trace_result_mirror ad_trace(void *ctx, qa_vec3 s, qa_vec3 e, const qa_actor_id *pass) {
  adapter *ad = ctx; return ad->runtime->host.trace(ad->runtime->host.ctx, s, e, pass);
}
static bool ad_target(void *ctx, qa_actor_id actor, qa_q3_bullet_target *out) { return hit_target(ctx, actor, out); }
static void ad_damage_method(void *ctx, qa_actor_id target, qa_vec3 dir, qa_vec3 point, double amount) {
  adapter *ad = ctx; RT *r = ad->runtime;
  qa_q3_damage_participant self = qa_q3_participant_entity(ad->entity);
  qa_vec3 d = dir;
  qa_q3_combat_damage(r->host.combat, r->host.participant(r->host.ctx, target), &self, &self, &d, &point, amount, 0, ad->method);
}
static qa_q3_invuln_impact ad_invuln(void *ctx, qa_actor_id actor, qa_vec3 dir, qa_vec3 point) {
  adapter *ad = ctx; RT *r = ad->runtime;
  qa_q3_damage_participant target = r->host.participant(r->host.ctx, actor);
  if (!target.entity) qa_range_error("Q3 invulnerability requires its actual client behavior");
  qa_q3_weapon_invuln_impact w = qa_q3_invulnerability_effect(r->host.combat->entities, target.entity, dir, point);
  qa_q3_invuln_impact out = {w.kind == QA_Q3_WEAPON_INVULN_HIT ? QA_Q3_INVULN_HIT : QA_Q3_INVULN_MISS, w.impact_point, w.bounce_direction};
  return out;
}
static void ad_credit_contact(void *ctx) {
  adapter *ad = ctx; double h; bool can; const char *team;
  if (ad->runtime->host.authority_read(ad->runtime->host.ctx, ad->attacker, &h, &can, &team))
    ad->firing_client->accuracy_hits = qa_int32(ad->firing_client->accuracy_hits + 1);
}
static void ad_credit_bullet(void *ctx) {
  adapter *ad = ctx; ad->firing_client->accuracy_hits = qa_int32(ad->firing_client->accuracy_hits + 1);
}
static void ad_bullet_emit(void *ctx, qa_q3_bullet_event hit) {
  adapter *ad = ctx; RT *r = ad->runtime; qa_q3_entity_pool *pool = r->host.combat->entities;
  qa_q3_game_entity *ev = qa_q3_entity_pool_temp_entity(pool, hit.point, hit.flesh ? QA_Q3_EV_BULLET_HIT_FLESH : QA_Q3_EV_BULLET_HIT_WALL);
  if (hit.flesh && hit.has_target) {
    qa_q3_damage_participant target = r->host.participant(r->host.ctx, hit.target);
    if (!target.entity || !target.entity->client) qa_range_error("Admitted Q3 map player has no native client behavior record");
    ev->s.event_parm = target.entity->s.number;
  } else ev->s.event_parm = qa_q3_base_direction_to_byte(&hit.normal);
  ev->s.other_entity_num = ad->entity->s.number;
}
static void ad_contact_emit(void *ctx, qa_q3_contact_event hit) {
  adapter *ad = ctx; RT *r = ad->runtime; qa_q3_entity_pool *pool = r->host.combat->entities;
  switch (hit.kind) {
    case QA_Q3_CONTACT_GAUNTLET_QUAD: qa_q3_entity_pool_add_event(pool, ad->entity, QA_Q3_EV_POWERUP_QUAD, 0); return;
    case QA_Q3_CONTACT_LIGHTNING_REFLECTION:
      qa_q3_entity_pool_temp_entity(pool, hit.start, QA_Q3_EV_LIGHTNINGBOLT)->s.origin2 = hit.end; return;
    case QA_Q3_CONTACT_MISS:
      qa_q3_entity_pool_temp_entity(pool, hit.point, QA_Q3_EV_MISSILE_MISS)->s.event_parm = qa_q3_base_direction_to_byte(&hit.normal); return;
    case QA_Q3_CONTACT_HIT: {
      qa_q3_damage_participant target = r->host.participant(r->host.ctx, hit.target);
      if (!target.entity || !target.entity->client) qa_range_error("Admitted Q3 map player has no native client behavior record");
      qa_q3_game_entity *ev = qa_q3_entity_pool_temp_entity(pool, hit.point, QA_Q3_EV_MISSILE_HIT);
      ev->s.other_entity_num = target.entity->s.number;
      ev->s.event_parm = qa_q3_base_direction_to_byte(&hit.normal); ev->s.weapon = ad->firing_weapon; return;
    }
  }
}
static bool ad_alive(void *ctx) { adapter *ad = ctx; return ad->runtime->host.native_by_actor(ad->runtime->host.ctx, ad->attacker) == ad->entity; }
static void ad_sg_begin(void *ctx, qa_vec3 muzzle, qa_vec3 direction) {
  adapter *ad = ctx;
  qa_q3_game_entity *ev = qa_q3_entity_pool_temp_entity(ad->runtime->host.combat->entities, muzzle, QA_Q3_EV_SHOTGUN);
  ev->s.origin2 = direction; ad->shotgun_event = ev;
}
static void ad_sg_publish(void *ctx, double seed) {
  adapter *ad = ctx; ad->shotgun_event->s.event_parm = seed; ad->shotgun_event->s.other_entity_num = ad->source_number;
}
static void *ad_unlink(void *ctx, qa_actor_id actor) { adapter *ad = ctx; return ad->runtime->host.unlink(ad->runtime->host.ctx, actor); }
static void ad_restore(void *ctx, void *token) { adapter *ad = ctx; ad->runtime->host.restore(ad->runtime->host.ctx, token); }
static void ad_trail(void *ctx, qa_q3_rail_trail shot) {
  adapter *ad = ctx;
  qa_q3_game_entity *ev = qa_q3_entity_pool_temp_entity(ad->runtime->host.combat->entities, shot.end, QA_Q3_EV_RAILTRAIL);
  ev->s.client_num = ad->client_number; ev->s.origin2 = shot.start;
  ev->s.event_parm = shot.impact_kind == QA_Q3_RAIL_NONE ? 255 : qa_q3_base_direction_to_byte(&shot.impact_normal);
}
static adapter make_adapter(RT *r, qa_q3_game_entity *e, double method) {
  adapter ad; memset(&ad, 0, sizeof ad);
  ad.runtime = r; ad.entity = e; ad.attacker = qa_q3_game_entity_actor(e).id; ad.method = method;
  ad.firing_weapon = e->s.weapon; ad.firing_client = client_of(e);
  ad.source_number = e->s.number; ad.client_number = e->s.client_num; return ad;
}
static qa_q3_contact_host contact_host(adapter *ad) {
  qa_q3_contact_host h; memset(&h, 0, sizeof h);
  h.product = ad->runtime->host.combat->product; h.ctx = ad;
  h.trace = ad_trace; h.target = ad_target; h.emit = ad_contact_emit; h.damage = ad_damage_method;
  h.credit_accuracy_hit = ad_credit_contact;
  h.invulnerability_impact = h.product == QA_Q3_GAME_PRODUCT_MISSIONPACK ? ad_invuln : NULL;
  return h;
}
static void bullet(RT *r, qa_q3_game_entity *e, Attack *a, double spread, double amount) {
  adapter ad = make_adapter(r, e, 3);
  qa_q3_bullet_host h; memset(&h, 0, sizeof h);
  h.product = r->host.combat->product; h.random = qa_q3_game_random_pick_from(r->host.random); h.ctx = &ad;
  h.trace = ad_trace; h.target = ad_target; h.emit = ad_bullet_emit; h.damage = ad_damage_method;
  h.credit_accuracy_hit = ad_credit_bullet;
  h.invulnerability_impact = h.product == QA_Q3_GAME_PRODUCT_MISSIONPACK ? ad_invuln : NULL;
  qa_q3_bullet_fire(h, ad.attacker, a, spread, amount);
}
static void shotgun(RT *r, qa_q3_game_entity *e, Attack *a) {
  adapter ad = make_adapter(r, e, 1);
  qa_q3_shotgun_host h; memset(&h, 0, sizeof h);
  h.product = r->host.combat->product; h.ctx = &ad; h.random = r->host.random;
  h.trace = ad_trace; h.target = ad_target; h.damage = ad_damage_method; h.credit_accuracy_hit = ad_credit_contact;
  h.invulnerability_impact = h.product == QA_Q3_GAME_PRODUCT_MISSIONPACK ? ad_invuln : NULL;
  h.begin = ad_sg_begin; h.publish = ad_sg_publish; h.alive = ad_alive;
  qa_q3_shotgun_fire(h, ad.attacker, a);
}
static void railgun(RT *r, qa_q3_game_entity *e, Attack *a) {
  adapter ad = make_adapter(r, e, 10);
  qa_q3_rail_host h; memset(&h, 0, sizeof h);
  h.product = r->host.combat->product; h.ctx = &ad;
  h.trace = ad_trace; h.target = ad_target; h.damage = ad_damage_method;
  h.invulnerability_impact = h.product == QA_Q3_GAME_PRODUCT_MISSIONPACK ? ad_invuln : NULL;
  h.alive = ad_alive; h.unlink = ad_unlink; h.restore = ad_restore; h.trail = ad_trail;
  double hits = qa_q3_rail_fire(h, ad.attacker, a);
  if (!ad_alive(&ad)) return;
  qa_q3_game_client *c = client_of(e);
  qa_q3_rail_statistics st = {c->accurate_count, c->accuracy_hits,
    qa_q3_game_slots_get_mirror(&c->ps.persistant, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT), c->reward_time};
  qa_q3_rail_statistics_result res = qa_q3_update_rail_statistics(st, hits, r->host.combat->time);
  c->accurate_count = res.state.streak; c->accuracy_hits = res.state.hits;
  if (res.awarded) {
    qa_q3_ranking_reports_reward(&r->host.combat->entities->rankings, e->slot, 0x8000);
    qa_q3_game_slots_set_mirror(&c->ps.persistant, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT, res.state.impressive_count);
    c->ps.e_flags = ((int)c->ps.e_flags & ~AWARD_FLAGS_V) | 0x8000; c->reward_time = res.state.reward_until;
  }
}
bool qa_q3_weapon_check_gauntlet_attack(RT *r, qa_q3_game_entity *e) {
  owned(r, e); adapter ad = make_adapter(r, e, 2); Attack a = attack_of(e, quad_of(r, e));
  return qa_q3_gauntlet_attack(contact_host(&ad), ad.attacker, &a,
      qa_q3_game_slots_get_mirror(&client_of(e)->ps.powerups, QA_Q3_PW_QUAD) != 0);
}
static void multiply(Attack *a, qa_q3_game_entity *p) { p->damage = scaled(p->damage, a); p->splash_damage = scaled(p->splash_damage, a); }
static void raise_fwd(Attack *a) { a->forward = qa_normalize3((qa_vec3){a->forward.x, a->forward.y, (float)(a->forward.z + (float)0.2)}); }
void qa_q3_weapon_fire(RT *r, qa_q3_game_entity *e) {
  owned(r, e); qa_q3_game_client *c = client_of(e); qa_q3_game_combat_context *combat = r->host.combat;
  double quad = quad_of(r, e);
  qa_q3_ranking_reports_fire_weapon(&combat->entities->rankings, e->slot, e->s.weapon);
  if ((int)e->s.weapon != QA_Q3_WP_GRAPPLING_HOOK && (int)e->s.weapon != QA_Q3_WP_GAUNTLET)
    c->accuracy_shots = qa_int32(c->accuracy_shots + (combat->product == QA_Q3_GAME_PRODUCT_MISSIONPACK && (int)e->s.weapon == QA_Q3_WP_NAILGUN ? 15 : 1));
  Attack a = attack_of(e, quad);
  switch ((int)e->s.weapon) {
    case QA_Q3_WP_GAUNTLET: return;
    case QA_Q3_WP_LIGHTNING: { adapter ad = make_adapter(r, e, 11); qa_q3_lightning_fire(contact_host(&ad), ad.attacker, &a); return; }
    case QA_Q3_WP_SHOTGUN: shotgun(r, e, &a); return;
    case QA_Q3_WP_MACHINEGUN: bullet(r, e, &a, 200, (int)combat->game_type == QA_Q3_BASE_GT_TEAM ? 5 : 7); return;
    case QA_Q3_WP_GRENADE_LAUNCHER: raise_fwd(&a); multiply(&a, r->host.fire_grenade(r->host.ctx, e, a.muzzle, dir_of(a.forward))); return;
    case QA_Q3_WP_ROCKET_LAUNCHER: multiply(&a, r->host.fire_rocket(r->host.ctx, e, a.muzzle, dir_of(a.forward))); return;
    case QA_Q3_WP_PLASMAGUN: multiply(&a, r->host.fire_plasma(r->host.ctx, e, a.muzzle, dir_of(a.forward))); return;
    case QA_Q3_WP_RAILGUN: railgun(r, e, &a); return;
    case QA_Q3_WP_BFG: multiply(&a, r->host.fire_bfg(r->host.ctx, e, a.muzzle, dir_of(a.forward))); return;
    case QA_Q3_WP_GRAPPLING_HOOK:
      if (!c->fire_held && !c->hook) (void)r->host.fire_grapple(r->host.ctx, e, a.muzzle, dir_of(a.forward));
      c->fire_held = true; return;
    case QA_Q3_WP_NAILGUN:
      if (combat->product == QA_Q3_GAME_PRODUCT_MISSIONPACK)
        for (int i = 0; i < 15; i++) multiply(&a, r->host.fire_nail(r->host.ctx, e, a.muzzle, dir_of(a.forward), a.right, a.up));
      return;
    case QA_Q3_WP_PROX_LAUNCHER:
      if (combat->product == QA_Q3_GAME_PRODUCT_MISSIONPACK) { raise_fwd(&a); multiply(&a, r->host.fire_prox(r->host.ctx, e, a.muzzle, dir_of(a.forward))); }
      return;
    case QA_Q3_WP_CHAINGUN: if (combat->product == QA_Q3_GAME_PRODUCT_MISSIONPACK) bullet(r, e, &a, 600, 7); return;
    case QA_Q3_WP_NONE: return;
    default: return;
  }
}
qa_q3_game_entity *qa_q3_weapon_start_kamikaze(RT *r, qa_q3_game_entity *e) {
  owned(r, e); qa_q3_game_combat_context *combat = r->host.combat; qa_q3_entity_pool *pool = combat->entities;
  if (combat->product != QA_Q3_GAME_PRODUCT_MISSIONPACK) qa_range_error("Kamikaze requires missionpack");
  qa_q3_game_entity *ex = qa_q3_entity_pool_spawn(pool);
  ex->s.e_type = 13 + QA_Q3_EV_KAMIKAZE; /* ET_EVENTS + EV_KAMIKAZE */ ex->event_time = combat->time;
  qa_q3_game_entity *source = e->client ? e : e->activator;
  if (!source) qa_range_error("Kamikaze timer requires its activator");
  qa_vec3 position = qa_q3_snap_vector(source->s.pos.base);
  qa_q3_set_origin(ex, position); qa_q3_game_entity_set_classname(ex, "kamikaze");
  ex->kamikaze_time = combat->time;
  ex->think = qa_q3_callback_family_resolve(&pool->callbacks.think, "q3.weapon.kamikazeDamage");
  qa_q3_game_entity_set_nextthink(ex, qa_int32(combat->time + 100));
  ex->count = 0; ex->movedir = (qa_vec3){0, 0, 0}; r->host.world.link(r->host.world.ctx, ex);
  if (e->client) {
    ex->activator = e; e->s.e_flags = (int)e->s.e_flags & ~0x200;
    qa_q3_damage_participant self = qa_q3_participant_entity(e);
    qa_q3_combat_damage(combat, self, &self, &self, NULL, NULL, 100000, QA_Q3_DAMAGE_NO_PROTECTION, 26);
  } else {
    const char *cn = qa_q3_game_entity_classname(source);
    ex->activator = (cn && strcmp(cn, "bodyque") == 0) ? qa_q3_entity_pool_at(pool, source->r.owner_num) : source;
  }
  qa_q3_game_entity *ev = qa_q3_entity_pool_temp_entity(pool, position, QA_Q3_EV_GLOBAL_TEAM_SOUND);
  ev->r.sv_flags = (int)ev->r.sv_flags | 32; ev->s.event_parm = 13;
  return ex;
}
static void kamikaze_area(RT *r, qa_vec3 origin, qa_q3_game_entity *attacker, double amount, double radius, bool shock) {
  qa_q3_game_combat_context *combat = r->host.combat;
  radius = fmax(1, (float)radius);
  qa_bounds b = {qa_sub3(origin, (qa_vec3){radius, radius, radius}), qa_add3(origin, (qa_vec3){radius, radius, radius})};
  double *numbers = malloc(1024 * sizeof *numbers); if (!numbers) qa_range_error("out of memory");
  size_t n = r->host.area_entities(r->host.ctx, b, numbers, 1024);
  for (size_t i = 0; i < n; i++) {
    qa_q3_game_entity *target = qa_q3_entity_pool_at(combat->entities, numbers[i]);
    if (shock ? target->kamikaze_shock_time > combat->time
              : !qa_q3_game_entity_takedamage(target) || target->kamikaze_time > combat->time) continue;
    qa_bounds link;
    if (!r->host.link_bounds(r->host.ctx, numbers[i], &link)) qa_range_error("Kamikaze area query returned an unlinked entity");
    double ax = origin.x < link.min.x ? link.min.x - origin.x : origin.x > link.max.x ? origin.x - link.max.x : 0;
    double ay = origin.y < link.min.y ? link.min.y - origin.y : origin.y > link.max.y ? origin.y - link.max.y : 0;
    double az = origin.z < link.min.z ? link.min.z - origin.z : origin.z > link.max.z ? origin.z - link.max.z : 0;
    if (qa_length3((qa_vec3){ax, ay, az}) >= radius) continue;
    qa_vec3 offset = qa_sub3(target->r.current_origin, origin);
    qa_vec3 direction = {offset.x, offset.y, (float)(offset.z + 24)};
    qa_q3_damage_participant tp = qa_q3_participant_entity(target);
    qa_q3_damage_participant ap; const qa_q3_damage_participant *aptr = NULL;
    if (attacker) { ap = qa_q3_participant_entity(attacker); aptr = &ap; }
    qa_q3_combat_damage(combat, tp, NULL, aptr, &direction, &origin, amount, QA_Q3_DAMAGE_RADIUS | QA_Q3_DAMAGE_NO_TEAM_PROTECTION, 26);
    if (shock) {
      qa_vec3 hz = qa_normalize3((qa_vec3){direction.x, direction.y, 0});
      if (target->client) target->client->ps.velocity = (qa_vec3){(float)(hz.x * 400), (float)(hz.y * 400), 100};
      target->kamikaze_shock_time = qa_int32(combat->time + 3000);
    } else target->kamikaze_time = qa_int32(combat->time + 3000);
  }
  free(numbers);
}
static void kamikaze_damage(RT *r, qa_q3_game_entity *e) {
  qa_q3_game_combat_context *combat = r->host.combat; qa_game_random *random = r->host.random;
  e->count = qa_int32(e->count + 100);
  if (e->count >= 0) kamikaze_area(r, e->s.pos.base, e->activator, 25, trunc((double)((int32_t)e->count * 1320) / 2000), true);
  if (e->count >= 250) kamikaze_area(r, e->s.pos.base, e->activator, 400, trunc((double)((int32_t)(e->count - 250) * 720) / 1750), false);
  if (e->count >= 2000) { qa_q3_entity_pool_free(combat->entities, e); return; }
  qa_q3_game_entity_set_nextthink(e, qa_int32(combat->time + 100));
  qa_vec3 angles = {(float)(qa_game_random_crandom(random) * 2), (float)(qa_game_random_crandom(random) * 2), 0};
  for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) {
    qa_q3_game_entity *target = qa_q3_entity_pool_at(combat->entities, i);
    qa_q3_game_client *c = target->client;
    if (!qa_q3_game_entity_inuse(target) || !c) continue;
    if (target->binding.body.has_ground)
      c->ps.velocity = (qa_vec3){(float)(c->ps.velocity.x + (float)(qa_game_random_crandom(random) * 120)),
        (float)(c->ps.velocity.y + (float)(qa_game_random_crandom(random) * 120)),
        (float)(30 + (float)(qa_game_random_random(random) * 25))};
    qa_vec3 delta = qa_sub3(angles, e->movedir), old = c->ps.delta_angles_vec;
    double dx = qa_qvm_float_to_int((float)((float)(delta.x * 65536) / 360)) , dy = qa_qvm_float_to_int((float)((float)(delta.y * 65536) / 360)), dz = qa_qvm_float_to_int((float)((float)(delta.z * 65536) / 360));
    c->ps.delta_angles_vec = (qa_vec3){qa_int32(old.x + ((int)dx & 65535)), qa_int32(old.y + ((int)dy & 65535)), qa_int32(old.z + ((int)dz & 65535))};
  }
  e->movedir = angles;
}
static void cb_kamikaze(void *ctx, qa_q3_game_entity *s) { kamikaze_damage(ctx, s); }
void qa_q3_weapon_runtime_init(RT *r, qa_q3_weapon_host host) {
  r->host = host;
  qa_q3_entity_think_fn fn = cb_kamikaze;
  qa_q3_callback_family_register(&host.combat->entities->callbacks.think, "q3.weapon.kamikazeDamage", mkcb(&fn, r));
}
