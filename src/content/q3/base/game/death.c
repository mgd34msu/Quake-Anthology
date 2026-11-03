/* Port of src/content/q3/base/game/death.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/death.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/rankings.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
#define CONTENTS_CORPSE_V (double)0x4000000
#define GIB_HEALTH_V (-40)
#define AWARD_MASK_V (0x8 | 0x40 | 0x800 | 0x8000 | 0x10000 | 0x20000)
static float f32(double v) { return (float)v; }
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
typedef qa_q3_death_runtime RT;
static const char *const COMMON_MOD_NAMES[] = {"MOD_UNKNOWN", "MOD_SHOTGUN", "MOD_GAUNTLET", "MOD_MACHINEGUN",
  "MOD_GRENADE", "MOD_GRENADE_SPLASH", "MOD_ROCKET", "MOD_ROCKET_SPLASH", "MOD_PLASMA",
  "MOD_PLASMA_SPLASH", "MOD_RAILGUN", "MOD_LIGHTNING", "MOD_BFG", "MOD_BFG_SPLASH",
  "MOD_WATER", "MOD_SLIME", "MOD_LAVA", "MOD_CRUSH", "MOD_TELEFRAG", "MOD_FALLING",
  "MOD_SUICIDE", "MOD_TARGET_LASER", "MOD_TRIGGER_HURT"};
static const char *const MP_MOD_NAMES[] = {"MOD_NAIL", "MOD_CHAINGUN", "MOD_PROXIMITY_MINE", "MOD_KAMIKAZE", "MOD_JUICED"};
static const char *mod_name(RT *r, double mod) {
  int i = qa_js_to_int32(mod);
  if (mod != i || i < 0) return "<bad obituary>";
  if (i < 23) return COMMON_MOD_NAMES[i];
  if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) { if (i < 28) return MP_MOD_NAMES[i - 23]; if (i == 28) return "MOD_GRAPPLE"; }
  else if (i == 23) return "MOD_GRAPPLE";
  return "<bad obituary>";
}
static qa_q3_game_client *client_of(qa_q3_game_entity *e) {
  if (!e->client) qa_range_error("Death operation requires a client entity");
  return e->client;
}
static double slot_get(qa_q3_base_player_state_slots *s, double i) { return qa_q3_base_player_state_slots_get(s, (size_t)i); }
static void slot_set(qa_q3_base_player_state_slots *s, double i, double v) { qa_q3_base_player_state_slots_set(s, (size_t)i, v); }
static const qa_q3_base_stat_schema *schema(RT *r) {
  return qa_q3_base_stat_schema_for(r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3);
}
void qa_q3_return_persistent_powerup(qa_q3_game_entity *powerup, qa_q3_base_server_world world) {
  powerup->r.sv_flags = qa_js_to_int32(powerup->r.sv_flags) & ~1;
  powerup->s.e_flags = qa_js_to_int32(powerup->s.e_flags) & ~0x80;
  powerup->r.contents = (double)(int32_t)0x40000000;
  world.link(world.ctx, powerup);
}
void qa_q3_toss_client_persistent_powerup(qa_q3_game_entity *entity, qa_q3_return_item_fn return_item, void *return_ctx) {
  qa_q3_game_client *client = entity->client;
  if (!client || !client->persistant_powerup) return;
  return_item(return_ctx, client->persistant_powerup);
  qa_q3_base_player_state_slots_set(&client->ps.stats, QA_Q3_BASE_MP_STAT_PERSISTANT_POWERUP, 0);
  client->persistant_powerup = NULL;
}
/* TS private dropContext: built fresh, so time/gameType are the current frame's. */
static qa_q3_drop_item_context drop_context(RT *r) {
  qa_q3_death_frame frame = r->host.frame(r->host.ctx);
  qa_q3_drop_item_context c; memset(&c, 0, sizeof c);
  c.launch.entities = r->host.pool; c.launch.product = r->host.product;
  c.launch.time = frame.time; c.launch.game_type = frame.game_type;
  c.launch.touch_item = r->host.touch_item; c.launch.dropped_flag_think = r->host.dropped_flag_think;
  c.launch.check_dropped_team_item = r->host.check_dropped_team_item;

  c.random_ctx = r->host.ctx; c.random = r->host.random_value;
  return c;
}
void qa_q3_death_runtime_init(RT *r, qa_q3_death_host host) {
  if (host.team_scores->length != 4) qa_range_error("Death scoring requires four shared team-score slots");
  if (host.pool->options.product != host.product) qa_range_error("Death product does not match its entity pool");
  r->host = host; qa_q3_death_bind_save_callbacks(r);
}
void qa_q3_death_score_plum(RT *r, qa_q3_game_entity *e, qa_vec3 origin, double score) {
  qa_q3_game_entity *plum = qa_q3_entity_pool_temp_entity(r->host.pool, origin, QA_Q3_EV_SCOREPLUM);
  plum->r.sv_flags = qa_js_to_int32(plum->r.sv_flags) | 256; /* SINGLECLIENT */
  plum->r.single_client = e->s.number; plum->s.other_entity_num = e->s.number; plum->s.time = score;
}
void qa_q3_death_add_score(RT *r, qa_q3_game_entity *e, qa_vec3 origin, double score) {
  qa_q3_game_client *client = e->client; qa_q3_death_frame frame = r->host.frame(r->host.ctx);
  if (!client || frame.warmup_time != 0) return;
  qa_q3_death_score_plum(r, e, origin, score);
  qa_q3_base_player_state_slots *p = &client->ps.persistant;
  slot_set(p, QA_Q3_BASE_PERS_SCORE, slot_get(p, QA_Q3_BASE_PERS_SCORE) + score);
  if (qa_js_to_int32(frame.game_type) == QA_Q3_BASE_GT_TEAM) {
    double team = slot_get(p, QA_Q3_BASE_PERS_TEAM);
    slot_set(r->host.team_scores, team, slot_get(r->host.team_scores, team) + score);
  }
  r->host.calculate_ranks(r->host.ctx);
}
void qa_q3_death_toss_client_items(RT *r, qa_q3_game_entity *e) {
  qa_q3_game_client *client = client_of(e); qa_q3_drop_item_context context = drop_context(r);
  double weapon = e->s.weapon;
  if (qa_js_to_int32(weapon) == QA_Q3_WP_MACHINEGUN || qa_js_to_int32(weapon) == QA_Q3_WP_GRAPPLING_HOOK) {
    if (qa_js_to_int32(client->ps.weapon_state) == QA_Q3_WEAPON_DROPPING) weapon = client->pers.cmd.weapon;
    if (!((qa_js_to_int32(slot_get(&client->ps.stats, schema(r)->weapons)) & (1 << qa_js_to_int32(weapon))) != 0)) weapon = QA_Q3_WP_NONE;
  }
  if (weapon > QA_Q3_WP_MACHINEGUN && qa_js_to_int32(weapon) != QA_Q3_WP_GRAPPLING_HOOK && slot_get(&client->ps.ammo, weapon) != 0)
    (void)qa_q3_drop_item(context, e, qa_q3_base_find_item_for_weapon(r->host.product, (qa_q3_weapon)weapon), 0);
  if (qa_js_to_int32(context.launch.game_type) == QA_Q3_BASE_GT_TEAM) return;
  double angle = 45;
  for (int powerup = 1; powerup < QA_Q3_PW_NUM_POWERUPS; powerup++) {
    double expires = slot_get(&client->ps.powerups, powerup);
    if (expires <= context.launch.time) continue;
    const qa_q3_base_item_definition *item = qa_q3_base_find_item_for_powerup(r->host.product, (qa_q3_powerup)powerup);
    if (!item) continue;
    qa_q3_game_entity *dropped = qa_q3_drop_item(context, e, item, angle);
    double remain = trunc((double)qa_int32(expires - context.launch.time) / 1000);
    dropped->count = remain > 1 ? remain : 1; angle = f32(angle + 45);
  }
}
void qa_q3_death_toss_client_cubes(RT *r, qa_q3_game_entity *e) {
  if (r->host.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) qa_range_error("TossClientCubes requires missionpack");
  qa_q3_game_client *client = client_of(e); double time = r->host.frame(r->host.ctx).time;
  client->ps.generic1 = 0;
  if (!qa_q3_entity_pool_entities_free(r->host.pool)) return;
  const qa_q3_base_item_definition *item = qa_q3_base_find_item(r->host.product, client->sess.session_team == 1 ? "Red Cube" : "Blue Cube");
  if (!item) qa_range_error("Missing source Harvester cube item");
  qa_vec3 forward = qa_scale3(qa_qvm_angle_vectors((qa_vec3){0, fmod(time, 360), 0}).forward, 150);
  qa_vec3 velocity = {forward.x, forward.y, f32(forward.z + f32(200 + f32(r->host.crandom(r->host.ctx) * 50)))};
  qa_q3_game_entity *obelisk = r->host.neutral_obelisk(r->host.ctx);
  qa_vec3 origin = obelisk ? (qa_vec3){obelisk->s.pos.base.x, obelisk->s.pos.base.y, obelisk->s.pos.base.z + 44} : (qa_vec3){0, 0, 0};
  qa_q3_game_entity *dropped = qa_q3_launch_item(drop_context(r).launch, item, origin, velocity);
  qa_q3_game_entity_set_nextthink(dropped, qa_int32(time + qa_js_to_int32((double)((uint32_t)qa_js_to_int32(r->host.cube_timeout_seconds(r->host.ctx)) * 1000u))) /* Math.imul: the cvar can be any number */);
  dropped->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.base.game.death.tossClientCubes.think");
  dropped->spawnflags = client->sess.session_team;
}
static void return_cb(void *ctx, qa_q3_game_entity *p) { RT *r = ctx; qa_q3_return_persistent_powerup(p, r->host.world); }
void qa_q3_death_toss_client_persistant_powerups(RT *r, qa_q3_game_entity *e) {
  if (r->host.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) qa_range_error("Persistent powerup tossing requires missionpack");
  qa_q3_toss_client_persistent_powerup(e, return_cb, r);
}
static bool part_origin(const qa_q3_damage_participant *p, qa_q3_game_entity *self, qa_vec3 *out) {
  if (!p || p->entity == self) return false;
  if (p->entity) { *out = p->entity->s.pos.base; return true; }
  return p->origin != NULL && p->origin(p->origin_ctx, out);
}
void qa_q3_death_look_at_killer(RT *r, qa_q3_game_entity *self, const qa_q3_damage_participant *inflictor, const qa_q3_damage_participant *attacker) {
  const qa_q3_damage_participant *target = NULL;
  if (attacker && attacker->entity != self && (attacker->entity || attacker->origin)) target = attacker;
  else if (inflictor && inflictor->entity != self && (inflictor->entity || inflictor->origin)) target = inflictor;
  qa_vec3 origin; bool have = part_origin(target, self, &origin);
  if (!have) have = part_origin(inflictor, self, &origin);
  double yaw = have ? qa_vector_to_angles(qa_sub3(origin, self->s.pos.base)).y : self->s.angles.y;
  double integer = yaw >= -2147483648.0 && yaw < 2147483648.0 ? trunc(yaw) : -2147483648.0;
  slot_set(&client_of(self)->ps.stats, schema(r)->dead_yaw, integer);
}
void qa_q3_death_gib_entity(RT *r, qa_q3_game_entity *self, double killer) {
  if ((qa_js_to_int32(self->s.e_flags) & 0x200) != 0) {
    for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) {
      qa_q3_game_entity *timer = qa_q3_entity_pool_at(r->host.pool, i);
      const char *cn = qa_q3_game_entity_classname(timer);
      if (!qa_q3_game_entity_inuse(timer) || timer->activator != self || !cn || strcmp(cn, "kamikaze timer") != 0) continue;
      qa_q3_entity_pool_free(r->host.pool, timer); break;
    }
  }
  qa_q3_entity_pool_add_event(r->host.pool, self, QA_Q3_EV_GIB_PLAYER, killer);
  qa_q3_game_entity_set_takedamage(self, false); self->s.e_type = 10; /* ET_INVISIBLE */ self->r.contents = 0;
}
void qa_q3_death_body_die(RT *r, qa_q3_game_entity *self) {
  if (qa_q3_game_entity_health(self) > GIB_HEALTH_V) return;
  if (!r->host.frame(r->host.ctx).blood) { qa_q3_game_entity_set_health(self, GIB_HEALTH_V + 1); return; }
  qa_q3_death_gib_entity(r, self, 0);
}
static void kamikaze_timer(RT *r, qa_q3_game_entity *self) {
  if (r->host.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) qa_range_error("Kamikaze death timer requires missionpack");
  qa_q3_game_entity *timer = qa_q3_entity_pool_spawn(r->host.pool);
  qa_q3_game_entity_set_classname(timer, "kamikaze timer");
  timer->s.pos.base = self->s.pos.base; timer->r.sv_flags = qa_js_to_int32(timer->r.sv_flags) | 1;
  timer->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.base.game.death.kamikazeDeathTimer.think");
  qa_q3_game_entity_set_nextthink(timer, qa_int32(r->host.frame(r->host.ctx).time + 5000));
  timer->activator = self;
}
static void almost_reward(RT *r, qa_q3_game_entity *self, const qa_q3_damage_participant *attacker) {
  qa_q3_base_player_state_slots *p = &client_of(self)->ps.persistant;
  slot_set(p, QA_Q3_BASE_PERS_PLAYEREVENTS, qa_js_to_int32(slot_get(p, QA_Q3_BASE_PERS_PLAYEREVENTS)) ^ 4);
  if (!attacker) qa_range_error("Source almost-score reward dereferences a null attacker");
  if (attacker->entity && attacker->entity->client) {
    qa_q3_base_player_state_slots *ap = &attacker->entity->client->ps.persistant;
    slot_set(ap, QA_Q3_BASE_PERS_PLAYEREVENTS, qa_js_to_int32(slot_get(ap, QA_Q3_BASE_PERS_PLAYEREVENTS)) ^ 4);
  }
  (void)r;
}
static void check_almost_capture(RT *r, qa_q3_game_entity *self, const qa_q3_damage_participant *attacker) {
  qa_q3_game_client *c = client_of(self);
  if (!(slot_get(&c->ps.powerups, QA_Q3_PW_REDFLAG) || slot_get(&c->ps.powerups, QA_Q3_PW_BLUEFLAG) || slot_get(&c->ps.powerups, QA_Q3_PW_NEUTRALFLAG))) return;
  bool blue = c->sess.session_team == 2, ctf = qa_js_to_int32(r->host.frame(r->host.ctx).game_type) == QA_Q3_BASE_GT_CTF;
  const char *cn = (ctf ? blue : !blue) ? "team_CTF_blueflag" : "team_CTF_redflag";
  qa_q3_game_entity *goal = NULL;
  do { goal = qa_q3_find_entity(r->host.pool, goal, QA_Q3_FIELD_CLASSNAME, cn); } while (goal && ((qa_js_to_int32(goal->flags) & QA_Q3_GAME_FLAG_DROPPED_ITEM) != 0));
  if (goal && ((qa_js_to_int32(goal->r.sv_flags) & 1) == 0) && qa_length3(qa_sub3(qa_q3_base_player_state_record_origin(&c->ps), goal->s.origin)) < 200) almost_reward(r, self, attacker);
}
static void check_almost_scored(RT *r, qa_q3_game_entity *self, const qa_q3_damage_participant *attacker) {
  qa_q3_game_client *c = client_of(self);
  if (c->ps.generic1 == 0) return;
  const char *cn = c->sess.session_team == 2 ? "team_redobelisk" : "team_blueobelisk";
  qa_q3_game_entity *goal = qa_q3_find_entity(r->host.pool, NULL, QA_Q3_FIELD_CLASSNAME, cn);
  if (goal && qa_length3(qa_sub3(qa_q3_base_player_state_record_origin(&c->ps), goal->s.origin)) < 200) almost_reward(r, self, attacker);
}
typedef struct { bool has; double team, powerup; } carried_flag;
static carried_flag carried(qa_q3_game_entity *self) {
  qa_q3_base_player_state_slots *p = &client_of(self)->ps.powerups;
  if (slot_get(p, QA_Q3_PW_NEUTRALFLAG)) { carried_flag o = {true, 0, QA_Q3_PW_NEUTRALFLAG}; return o; }
  if (slot_get(p, QA_Q3_PW_REDFLAG)) { carried_flag o = {true, 1, QA_Q3_PW_REDFLAG}; return o; }
  if (slot_get(p, QA_Q3_PW_BLUEFLAG)) { carried_flag o = {true, 2, QA_Q3_PW_BLUEFLAG}; return o; }
  carried_flag o = {false, 0, 0}; return o;
}
void qa_q3_death_player_die(RT *r, qa_q3_game_entity *self, const qa_q3_damage_participant *inflictor,
    const qa_q3_damage_participant *attacker, double damage, double mod) {
  double x2;
  double y2;
  (void)damage; qa_q3_game_client *client = client_of(self); qa_q3_death_frame frame = r->host.frame(r->host.ctx);
  if (qa_js_to_int32(client->ps.pm_type) == QA_Q3_PM_DEAD || frame.intermission_time != 0) return;
  check_almost_capture(r, self, attacker); check_almost_scored(r, self, attacker);
  if (client->hook) r->host.hook_free(r->host.ctx, client->hook);
  if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && ((qa_js_to_int32(client->ps.e_flags) & 0x2) != 0) && self->activator) {
    client->ps.e_flags = qa_js_to_int32(client->ps.e_flags) & ~0x2;
    self->activator->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.base.game.death.playerDie.think");
    qa_q3_game_entity_set_nextthink(self->activator, frame.time);
  }
  client->ps.pm_type = QA_Q3_PM_DEAD;
  qa_q3_game_entity *atk_e = attacker ? attacker->entity : NULL;
  self->enemy = atk_e;
  double killer = atk_e ? atk_e->s.number : QA_Q3_BASE_ENTITYNUM_WORLD;
  const char *killer_name = !attacker ? "<world>" : (atk_e && atk_e->client) ? (atk_e->client->pers.netname ? atk_e->client->pers.netname : "") : "<non-client>";
  if (killer < 0 || killer >= QA_Q3_MAX_CLIENTS) { killer = QA_Q3_BASE_ENTITYNUM_WORLD; killer_name = "<world>"; }
  double rank_mod = (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && mod >= 23) ? (mod == 28 ? 23 : 0) : mod;
  qa_q3_ranking_reports_player_die(&r->host.pool->rankings, self->slot, killer, rank_mod);
  qa_q3_game_format_arg args[6] = {qa_q3_game_format_number(killer), qa_q3_game_format_number(self->s.number),
    qa_q3_game_format_number(mod), qa_q3_game_format_string(killer_name),
    qa_q3_game_format_string(client->pers.netname ? client->pers.netname : ""), qa_q3_game_format_string(mod_name(r, mod))};
  char *log = qa_q3_game_format("Kill: %i %i %i: %s killed %s by %s\n", args, 6, 32000);
  r->host.log(r->host.ctx, log); free(log);
  qa_q3_game_entity *ob = qa_q3_entity_pool_temp_entity(r->host.pool, qa_q3_base_entity_shared_current_origin(&self->r), QA_Q3_EV_OBITUARY);
  ob->s.event_parm = mod; ob->s.other_entity_num = self->s.number; ob->s.other_entity_num2 = killer; ob->r.sv_flags = 32;
  slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_KILLED, slot_get(&client->ps.persistant, QA_Q3_BASE_PERS_KILLED) + 1);
  if (atk_e && atk_e->client) {
    qa_q3_game_client *kc = atk_e->client; kc->last_killed_client = self->s.number;
    bool same_team = frame.game_type >= QA_Q3_BASE_GT_TEAM && client->sess.session_team == kc->sess.session_team;
    if (atk_e == self || same_team) qa_q3_death_add_score(r, atk_e, qa_q3_base_entity_shared_current_origin(&self->r), -1);
    else {
      qa_q3_death_add_score(r, atk_e, qa_q3_base_entity_shared_current_origin(&self->r), 1);
      if (qa_js_to_int32(mod) == 2) {
        slot_set(&kc->ps.persistant, QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT, slot_get(&kc->ps.persistant, QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT) + 1);
        kc->ps.e_flags = (qa_js_to_int32(kc->ps.e_flags) & ~AWARD_MASK_V) | 0x40; kc->reward_time = qa_int32(frame.time + 2000);
        slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_PLAYEREVENTS, qa_js_to_int32(slot_get(&client->ps.persistant, QA_Q3_BASE_PERS_PLAYEREVENTS)) ^ 2);
      }
      if (qa_int32(frame.time - kc->last_kill_time) < 3000) {
        qa_q3_ranking_reports_reward(&r->host.pool->rankings, atk_e->slot, 0x8);
        slot_set(&kc->ps.persistant, QA_Q3_BASE_PERS_EXCELLENT_COUNT, slot_get(&kc->ps.persistant, QA_Q3_BASE_PERS_EXCELLENT_COUNT) + 1);
        kc->ps.e_flags = (qa_js_to_int32(kc->ps.e_flags) & ~AWARD_MASK_V) | 0x8; kc->reward_time = qa_int32(frame.time + 2000);
      }
      kc->last_kill_time = frame.time;
    }
  } else qa_q3_death_add_score(r, self, qa_q3_base_entity_shared_current_origin(&self->r), -1);
  if (!attacker || atk_e) r->host.team_frag_bonuses(r->host.ctx, self, atk_e);
  if (qa_js_to_int32(mod) == 20) {
    carried_flag flag = carried(self);
    if (flag.has) { r->host.return_flag(r->host.ctx, flag.team); slot_set(&client->ps.powerups, flag.powerup, 0); }
  }
  double contents = r->host.point_contents(r->host.ctx, qa_q3_base_entity_shared_current_origin(&self->r), -1);
  bool nodrop = (qa_int32(contents) & (int32_t)0x80000000u) != 0;
  if (!nodrop) qa_q3_death_toss_client_items(r, self);
  else { carried_flag flag = carried(self); if (flag.has) r->host.return_flag(r->host.ctx, flag.team); }
  if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    qa_q3_death_toss_client_persistant_powerups(r, self);
    if (qa_js_to_int32(frame.game_type) == QA_Q3_BASE_GT_HARVESTER) qa_q3_death_toss_client_cubes(r, self);
  }
  r->host.send_scoreboard(r->host.ctx, self);
  for (double i = 0; i < r->host.pool->max_clients; i++) {
    qa_q3_game_client *f = qa_q3_entity_pool_client_at(r->host.pool, i);
    if (!f) continue;
    if (qa_js_to_int32(f->pers.connected) == QA_Q3_CONN_CONNECTED && f->sess.session_team == 3 && f->sess.spectator_client == self->s.number)
      r->host.send_scoreboard(r->host.ctx, qa_q3_entity_pool_at(r->host.pool, i));
  }
  double health = qa_q3_game_entity_health(self);
  if (r->host.character_death_selected) {
    double x3;
    double y3;
    qa_q3_game_entity_set_takedamage(self, true); self->s.weapon = 0; self->s.powerups = 0;
    self->r.contents = health <= GIB_HEALTH_V ? 0 : CONTENTS_CORPSE_V;
    qa_q3_base_entity_shared_set_maxs(&self->r, (x3 = qa_q3_base_entity_shared_maxs(&self->r).x, y3 = qa_q3_base_entity_shared_maxs(&self->r).y, (qa_vec3){x3, y3, -8})); self->s.loop_sound = 0;
    client->respawn_time = qa_int32(frame.time + 1700);
    for (size_t i = 0; i < client->ps.powerups.length; i++) slot_set(&client->ps.powerups, (double)i, 0);
    if ((health <= GIB_HEALTH_V && !nodrop && frame.blood) || qa_js_to_int32(mod) == 20) qa_q3_death_gib_entity(r, self, killer);
    else {
      if (health <= GIB_HEALTH_V) qa_q3_game_entity_set_health(self, GIB_HEALTH_V + 1);
      self->die = qa_q3_callback_family_resolve(&r->host.pool->callbacks.die, "q3.death.body");
    }
    r->host.world.link(r->host.world.ctx, self); return;
  }
  qa_q3_game_entity_set_takedamage(self, true); self->s.weapon = 0; self->s.powerups = 0;
  self->r.contents = CONTENTS_CORPSE_V; self->s.angles = (qa_vec3){0, self->s.angles.y, 0};
  qa_q3_death_look_at_killer(r, self, inflictor, attacker);
  client->ps.viewangles = self->s.angles; self->s.loop_sound = 0;
  qa_q3_base_entity_shared_set_maxs(&self->r, (x2 = qa_q3_base_entity_shared_maxs(&self->r).x, y2 = qa_q3_base_entity_shared_maxs(&self->r).y, (qa_vec3){x2, y2, -8}));
  client->respawn_time = qa_int32(frame.time + 1700);
  for (size_t i = 0; i < client->ps.powerups.length; i++) slot_set(&client->ps.powerups, (double)i, 0);
  if ((health <= GIB_HEALTH_V && !nodrop && frame.blood) || qa_js_to_int32(mod) == 20) qa_q3_death_gib_entity(r, self, killer);
  else {
    qa_q3_death_animation_next nx = qa_q3_death_animation_sequence_next(r->host.death_animations);
    if (health <= GIB_HEALTH_V) qa_q3_game_entity_set_health(self, GIB_HEALTH_V + 1);
    client->ps.legs_anim = ((qa_js_to_int32(client->ps.legs_anim) & 128) ^ 128) | qa_js_to_int32(nx.animation);
    client->ps.torso_anim = ((qa_js_to_int32(client->ps.torso_anim) & 128) ^ 128) | qa_js_to_int32(nx.animation);
    qa_q3_entity_pool_add_event(r->host.pool, self, nx.event, killer);
    self->die = qa_q3_callback_family_resolve(&r->host.pool->callbacks.die, "q3.death.body");
    if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && ((qa_js_to_int32(self->s.e_flags) & 0x200) != 0)) kamikaze_timer(r, self);
  }
  r->host.world.link(r->host.world.ctx, self);
}
static void cb_free(void *ctx, qa_q3_game_entity *s) { RT *r = ctx; qa_q3_entity_pool_free(r->host.pool, s); }
static void cb_free_player(void *ctx, qa_q3_game_entity *s) { RT *r = ctx; qa_q3_entity_pool_free(r->host.pool, s); }
static void cb_kamikaze(void *ctx, qa_q3_game_entity *s) {
  RT *r = ctx;
  if (r->host.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) qa_range_error("Kamikaze callback requires missionpack");
  r->host.start_kamikaze(r->host.ctx, s); qa_q3_entity_pool_free(r->host.pool, s);
}
static void cb_body_die(void *ctx, qa_q3_game_entity *s, qa_q3_damage_inflictor i, qa_q3_damage_participant a, double d, double m) {
  (void)i; (void)a; (void)d; (void)m; qa_q3_death_body_die(ctx, s);
}
static void cb_player_die(void *ctx, qa_q3_game_entity *s, qa_q3_damage_inflictor i, qa_q3_damage_participant a, double d, double m) {
  qa_q3_death_player_die(ctx, s, &i, &a, d, m);
}
void qa_q3_death_bind_save_callbacks(RT *r) {
  qa_q3_callback_catalog *c = &r->host.pool->callbacks;
  qa_q3_entity_think_fn f1 = cb_free, f2 = cb_kamikaze, f3 = cb_free_player;
  qa_q3_entity_die_fn d1 = cb_body_die, d2 = cb_player_die;
  qa_q3_callback_family_intern(&c->think, "q3.base.game.death.tossClientCubes.think", mkcb(&f1, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.death.kamikazeDeathTimer.think", mkcb(&f2, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.death.playerDie.think", mkcb(&f3, r));
  qa_q3_callback_family_register(&c->die, "q3.death.body", mkcb(&d1, r));
  qa_q3_callback_family_register(&c->die, "q3.death.player", mkcb(&d2, r));
}
