/* Port of src/content/q1/missionpacks/player.ts. Damage source
 * effects are an opaque hook struct for register_damage_source_effects
 * (TS owner: src/world/gameplay Q1DamageSourceEffects, unlanded shape).
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/player.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/messages.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const struct { const char *base; const char *powered; const char *ammo; const char *message; } COMBOS[] = {
  {"nailgun","rogue:lava-nailgun","rogue:ammo/lava-nails","$qc_lava_enabled"},
  {"supernailgun","rogue:lava-supernailgun","rogue:ammo/lava-nails","$qc_super_lava_enabled"},
  {"grenadelauncher","rogue:multi-grenade","rogue:ammo/multi-rockets","$qc_multi_gl_enabled"},
  {"rocketlauncher","rogue:multi-rocket","rogue:ammo/multi-rockets","$qc_multi_rl_enabled"},
  {"lightning","rogue:plasma","rogue:ammo/plasma","$qc_plasma_enabled"},
};
static const struct { const char *id; const char *warn; const char *lost; const char *sound; } TIMERS[] = {
  {"hipnotic:wetsuit","$qc_wetsuit_fade","","items/suit2.wav"},
  {"hipnotic:empathy","$qc_empathy_fade","","items/suit2.wav"},
  {"rogue:shield","$qc_shield_failing","$qc_shield_lost","shield/fadeout.wav"},
  {"rogue:antigrav","$qc_antigrav_failing","$qc_antigrav_lost","belt/fadeout.wav"},
};
static qa_q1mp_actor *pstate(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player) {
  qa_q1b_game *g = players->game->base; qa_q1mp_actor *ents[256]; size_t n = g->entities(g, ents, 256);
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(ents[i]->classname,"missionpack_player_state") && ents[i]->owner.has && qa_same_actor(ents[i]->owner.id, player->actor.id)) return ents[i];
  qa_q1mp_actor *e = g->create(g, "missionpack_player_state"); e->owner = QA_Q1MP_SOME(player->actor.id); return e;
}
static void configure(qa_q1mp_game *game, qa_owned_actor actor, const char *item, double count, double cap) {
  qa_inventory_entry e = {0}; e.item = item; e.count = count; e.capacity = cap;
  game->base->host->inventory_configure(game->base->host->ctx, actor, &e);
}
void qa_q1mp_players_attach(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player) {
  qa_q1mp_game *game = players->game; char prefix[32], buf[96]; (void)snprintf(prefix, sizeof prefix, "%s:", players->pack);
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) if (qa_q1mp_starts(qa_q1mp_mission_weapons[i].id, prefix)) {
    const char *item = qa_q1mp_weapon_item(qa_q1mp_mission_weapons[i].id, buf, sizeof buf);
    /* buf reused: configure copies in host; count read first as TS */
    double count = qa_q1mp_count(game, player->actor.id, item); configure(game, player->actor, item, count, 1);
  }
  if (qa_q1mp_streq(players->pack, "rogue")) {
    static const struct { const char *item; double cap; } entries[] = {{"rogue:ammo/lava-nails",200},{"rogue:ammo/multi-rockets",100},{"rogue:ammo/plasma",100},{"rogue:artifact/vengeance",1}};
    for (size_t i = 0; i < 4; i++) configure(game, player->actor, entries[i].item, qa_q1mp_count(game, player->actor.id, entries[i].item), entries[i].cap);
  }
  (void)pstate(players, player);
}
void qa_q1mp_players_enable_combos(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player) {
  if (!qa_q1mp_streq(players->pack, "rogue")) return;
  qa_q1mp_game *game = players->game; char b1[96], b2[96];
  for (size_t i = 0; i < sizeof COMBOS / sizeof COMBOS[0]; i++) {
    const char *pw = qa_q1mp_weapon_item(COMBOS[i].powered, b1, sizeof b1);
    char pwcopy[96]; (void)snprintf(pwcopy, sizeof pwcopy, "%s", pw);
    const char *bs = qa_q1mp_weapon_item(COMBOS[i].base, b2, sizeof b2);
    if (qa_q1mp_count(game, player->actor.id, pwcopy) == 0 && qa_q1mp_count(game, player->actor.id, bs) > 0 && qa_q1mp_count(game, player->actor.id, COMBOS[i].ammo) > 0) {
      (void)qa_q1mp_give(game, player->actor, pwcopy, 1); qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), COMBOS[i].message);
    }
  }
}
void qa_q1mp_players_powerup(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player, const char *powerup, double seconds) {
  qa_q1mp_actor *state = pstate(players, player); char key[128];
  (void)snprintf(key, sizeof key, "%s:warned", powerup); qa_q1b_fields_set(state, key, "0");
  (void)snprintf(key, sizeof key, "%s:lost", powerup); qa_q1b_fields_set(state, key, "0");
  if (qa_q1mp_streq(powerup, "rogue:antigrav")) {
    if (players->game->set_gravity) players->game->set_gravity(players->game->ctx, player->actor.id, 0.25);
    else if (players->game->base->host->set_gravity) players->game->base->host->set_gravity(players->game->base->host->ctx, player->actor.id, 0.25);
  }
  if (players->game->give_powerup) players->game->give_powerup(players->game->ctx, player, powerup, true, seconds);
}
double qa_q1mp_players_gravity_scale(qa_q1mp_mission_pack_players *players, qa_actor_id actor) {
  qa_q1mp_player_state *p = players->game->base->player(players->game->base, actor);
  return (p ? qa_q1b_powerups_get(p, "rogue:antigrav") : 0) > players->game->base->time ? 0.25 : 1;
}
static void set_grav(qa_q1mp_game *game, qa_actor_id actor, double scale) {
  if (game->set_gravity) game->set_gravity(game->ctx, actor, scale);
  else if (game->base->host->set_gravity) game->base->host->set_gravity(game->base->host->ctx, actor, scale);
}
static void frame_impl(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player, double seconds) {
  qa_q1mp_game *game = players->game; qa_q1mp_actor *state = pstate(players, player);
  qa_q1mp_players_enable_combos(players, player);
  for (size_t i = 0; i < sizeof TIMERS / sizeof TIMERS[0]; i++) {
    if (!qa_q1b_powerups_has(player, TIMERS[i].id)) continue;
    double expires = qa_q1b_powerups_get(player, TIMERS[i].id); char key[128];
    (void)snprintf(key, sizeof key, "%s:warned", TIMERS[i].id);
    if (expires < seconds + 3 && qa_q1mp_number(state, key) == 0) {
      qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), TIMERS[i].warn);
      game->base->sound_owned(game->base, player->actor, TIMERS[i].sound, QA_Q1MP_CH("auto"), 1, 1);
      qa_q1mp_set_mission_number(state, key, 1);
    }
    (void)snprintf(key, sizeof key, "%s:flash", TIMERS[i].id);
    if (expires < seconds + 3 && qa_q1mp_number(state, key) < seconds) {
      qa_q1b_body body; if (qa_q1mp_body_read(game, player->actor.id, &body)) qa_q1mp_effect_a(game, "pickup", body.origin, QA_Q1MP_SOME(player->actor.id), 0);
      qa_q1mp_set_mission_number(state, key, seconds + 1);
    }
    (void)snprintf(key, sizeof key, "%s:lost", TIMERS[i].id);
    if (expires <= seconds && qa_q1mp_number(state, key) == 0) {
      qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), TIMERS[i].lost); qa_q1mp_set_mission_number(state, key, 1);
      if (qa_q1mp_streq(TIMERS[i].id, "rogue:antigrav")) set_grav(game, player->actor.id, 1);
    }
  }
  if (qa_q1b_powerups_get(player, "hipnotic:wetsuit") > seconds) {
    player->air_finished = seconds + 12;
    if (player->water_level >= 2) {
      if (qa_q1mp_number(state, "hipnotic:scuba") < seconds) { game->base->sound_owned(game->base, player->actor, "misc/wetsuit.wav", QA_Q1MP_CH("body"), 1, 1); qa_q1mp_set_mission_number(state, "hipnotic:scuba", seconds + 7); }
      qa_q1b_body body; char tbuf[32]; qa_q1b_format_number(tbuf, sizeof tbuf, seconds);
      if (qa_q1mp_body_read(game, player->actor.id, &body) && !qa_q1mp_streq(qa_q1mp_text(state, "hipnotic:scaled-time"), tbuf)) {
        body.velocity = qa_q1mp_vscale(body.velocity, player->water_level == 2 ? 1.25 : 1.5);
        game->base->host->bodies_write(game->base->host->ctx, player->actor, &body);
        qa_q1b_fields_set(state, "hipnotic:scaled-time", tbuf); qa_q1mp_set_mission_number(state, "hipnotic:scaled-level", player->water_level);
      }
    }
  }
  qa_q1mp_actor *entity = game->base->entity(game->base, QA_Q1MP_SOME(player->actor.id));
  if (entity) entity->effects = qa_q1b_powerups_get(player, "hipnotic:empathy") > seconds ? (entity->effects | 8) : (entity->effects & ~8);
}
static void after_physics_impl(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player, double seconds) {
  qa_q1mp_actor *state = pstate(players, player); char tbuf[32]; qa_q1b_format_number(tbuf, sizeof tbuf, seconds);
  if (qa_q1mp_streq(qa_q1mp_text(state, "hipnotic:scaled-time"), tbuf)) {
    qa_q1b_body body;
    if (qa_q1mp_body_read(players->game, player->actor.id, &body)) {
      body.velocity = qa_q1mp_vscale(body.velocity, qa_q1mp_number(state, "hipnotic:scaled-level") == 2 ? 0.8 : 0.66);
      players->game->base->host->bodies_write(players->game->base->host->ctx, player->actor, &body);
    }
    qa_q1b_fields_set(state, "hipnotic:scaled-time", "");
  }
}
static void shield_hit(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player) {
  qa_q1mp_game *game = players->game; qa_q1mp_actor *state = pstate(players, player); qa_q1b_body body;
  if (!qa_q1mp_body_read(game, player->actor.id, &body)) return;
  if (qa_q1mp_number(state, "rogue:shield-death") <= game->base->time) {
    qa_q1mp_actor *shield = game->base->create(game->base, "power_shield");
    shield->owner = QA_Q1MP_SOME(player->actor.id); qa_q1mp_set_model(shield, "progs/p_shield.mdl");
    shield->delay = qa_q1mp_fround(game->base->time + 0.3);
    game->base->set_bounds(game->base, shield, QA_Q1MP_POINT);
    qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = body.origin; p.has_angles = true; p.angles = body.angles; qa_q1mp_patch(game, shield, &p);
    game->base->link(game->base, shield); qa_q1mp_set_mission_number(state, "rogue:shield-death", shield->delay);
    game->base->schedule(game->base, shield, 0.1, game->base->named.action(game->base->named.ctx, shield, "rogue:shield-think"));
  }
  if (qa_q1mp_number(state, "rogue:shield-sound") < game->base->time) {
    game->base->sound_owned(game->base, player->actor, "shield/hit.wav", QA_Q1MP_CH("item"), 1, 1);
    qa_q1mp_set_mission_number(state, "rogue:shield-sound", game->base->time + 0.5);
  }
}
/* TS DamagePreparation: *cancelled true = {kind:"cancel"}. */
static double before_quad(qa_q1mp_mission_pack_players *players, qa_actor_id target, qa_q1mp_opt_actor inflictor, const char *death_type, bool cause_q1, double damage, bool *cancelled) {
  *cancelled = false; qa_q1mp_game *game = players->game;
  qa_q1mp_player_state *player = game->base->player(game->base, target); if (!player) return damage;
  if (qa_q1mp_streq(players->pack,"hipnotic") && cause_q1 && qa_q1mp_streq(death_type,"discharge") && qa_q1b_powerups_get(player,"hipnotic:wetsuit") != 0) { *cancelled = true; return damage; }
  if (qa_q1mp_streq(players->pack,"rogue") && qa_q1b_powerups_get(player,"rogue:shield") != 0 && inflictor.has) {
    qa_q1b_body body, incoming;
    if (qa_q1mp_body_read(game, target, &body) && qa_q1mp_body_read(game, inflictor.id, &incoming)) {
      double hit = qa_q1b_yaw_for(qa_q1mp_vsub(incoming.origin, body.origin)) - qa_q1b_yaw_for(qa_q1mp_vectors(game, body.angles).forward);
      if (!((hit > 90 && hit < 270) || (hit < -90 && hit > -270))) {
        shield_hit(players, player);
        return qa_q1mp_fround(damage * (qa_q1mp_streq(game->base->host->classname(game->base->host->ctx, inflictor.id), "lava_spike") ? 0.7 : 0.3));
      }
    }
  }
  return damage;
}
static double after_quad(qa_q1mp_mission_pack_players *players, qa_actor_id target, qa_q1mp_opt_actor inflictor, qa_q1mp_opt_actor attacker, double damage) {
  qa_q1mp_game *game = players->game; qa_q1mp_player_state *player = game->base->player(game->base, target); if (!player) return damage;
  if (attacker.has) qa_q1mp_set_mission_reference(pstate(players, player), "rogue:killer", attacker);
  if (qa_q1mp_streq(players->pack,"hipnotic") && attacker.has && !qa_same_actor(attacker.id, target) && qa_q1b_powerups_get(player,"hipnotic:empathy") != 0) {
    double inf_empathy = 0;
    if (inflictor.has) { qa_q1mp_player_state *ip = game->base->player(game->base, inflictor.id); if (ip) inf_empathy = qa_q1b_powerups_get(ip, "hipnotic:empathy"); }
    if (!inflictor.has || inf_empathy == 0) {
      double reflected = qa_q1mp_fround(damage / 2);
      qa_q1mp_damage_full(game, attacker.id, target, QA_Q1MP_SOME(target), reflected, NULL, "direct", "hipnotic:empathy", NULL);
      return reflected;
    }
  }
  return damage;
}
typedef struct fx { qa_q1mp_mission_pack_players *players; } fx;
bool qa_q1mp_players_sphere(qa_q1mp_mission_pack_players *players, qa_q1mp_actor *item, qa_q1mp_player_state *player) {
  qa_q1mp_game *game = players->game;
  if (qa_q1mp_give(game, player->actor, "rogue:artifact/vengeance", 1) == 0) return false;
  qa_q1mp_actor *sphere = game->base->create(game->base, "Vengeance");
  sphere->owner = QA_Q1MP_SOME(player->actor.id); qa_q1mp_set_model(sphere, "progs/sphere.mdl");
  sphere->movement = "flymissile"; sphere->solid = "none"; sphere->angular_velocity = QA_Q1MP_V3(40,40,40);
  sphere->delay = qa_q1mp_fround(game->base->time + 30);
  game->base->set_origin(game->base, sphere, game->base->body(game->base, item).origin);
  game->base->schedule(game->base, sphere, 0.1, game->base->named.action(game->base->named.ctx, sphere, "rogue:sphere-think"));
  return true;
}
static void sphere_attack(qa_q1mp_mission_pack_players *players, qa_q1mp_actor *sphere);
static void sphere_think(qa_q1mp_mission_pack_players *players, qa_q1mp_actor *sphere) {
  qa_q1mp_game *game = players->game; qa_q1b_game *g = game->base;
  qa_q1mp_player_state *player = sphere->owner.has ? g->player(g, sphere->owner.id) : NULL;
  qa_q1b_body body; bool has_body = sphere->owner.has && qa_q1mp_body_read(game, sphere->owner.id, &body);
  if (!player || !has_body) { g->remove(g, sphere); return; }
  if (sphere->attack_finished < g->time) { qa_q1mp_sound_ch(game, sphere, "sphere/sphere.wav", "voice"); sphere->attack_finished = qa_q1mp_fround(g->time + 4); }
  if (g->time > sphere->delay || g->health(g, player->actor.id) < 1) {
    (void)qa_q1mp_consume(game, player->actor, "rogue:artifact/vengeance", 1);
    if (g->time > sphere->delay) { qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), "$qc_vengeance_lost"); g->remove(g, sphere); return; }
    qa_q1mp_opt_actor killer = qa_q1mp_mission_reference(game, pstate(players, player), "rogue:killer");
    if (killer.has && !g->is_player(g, killer)) { qa_q1mp_actor *ke = g->entity(g, killer); killer = ke ? ke->owner : QA_Q1MP_NONE; }
    if (!killer.has || !g->is_player(g, killer)) { g->remove(g, sphere); return; }
    qa_q1mp_set_mission_reference(sphere, "rogue:enemy", killer); sphere_attack(players, sphere); return;
  }
  qa_vec3 center = qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,48)), source = g->body(g, sphere).origin;
  if (sphere->count < 0 || sphere->count > 3) sphere->count = 0;
  qa_q1b_trace trace = qa_q1mp_trace(game, source, center, QA_Q1MP_NONE, false, false);
  if (trace.fraction < 1) { g->set_origin(g, sphere, center); sphere->count++; }
  else {
    qa_vec3 offset = sphere->count == 0 ? QA_Q1MP_V3(16,0,0) : sphere->count == 1 ? QA_Q1MP_V3(0,16,0) : sphere->count == 2 ? QA_Q1MP_V3(-16,0,0) : QA_Q1MP_V3(0,-16,0);
    qa_vec3 direction = qa_q1mp_vsub(qa_q1mp_vadd(center, offset), source); double dist = qa_q1mp_length(direction);
    if (dist < 8) sphere->count++; else qa_q1mp_move_missile(game, sphere, qa_q1mp_vscale(qa_q1mp_normalize(direction), dist < 50 ? 150 : 500));
  }
  g->schedule(g, sphere, 0.1, g->named.action(g->named.ctx, sphere, "rogue:sphere-think"));
}
static void sphere_attack(qa_q1mp_mission_pack_players *players, qa_q1mp_actor *sphere) {
  qa_q1mp_game *game = players->game; qa_q1b_game *g = game->base;
  sphere->solid = "trigger"; sphere->touch = g->named.touch(g->named.ctx, sphere, "rogue:sphere-impact"); g->link(g, sphere);
  qa_q1mp_opt_actor target = qa_q1mp_mission_reference(game, sphere, "rogue:enemy"); qa_q1b_body body;
  bool has_body = target.has && qa_q1mp_body_read(game, target.id, &body);
  if (!target.has || !has_body || g->health(g, target.id) < 1) { qa_q1mp_mission_message(game, sphere->owner, "$qc_you_are_denied_vengeance"); g->remove(g, sphere); return; }
  qa_q1mp_move_missile(game, sphere, qa_q1mp_vscale(qa_q1mp_normalize(qa_q1mp_vsub(qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,22)), g->body(g, sphere).origin)), 650));
  g->schedule(g, sphere, 0.1, g->named.action(g->named.ctx, sphere, "rogue:sphere-attack"));
}
static void shield_think_cb(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *shield) {
  (void)vctx; qa_q1b_body body; bool has = shield->owner.has && runtime->host->bodies_read(runtime->host->ctx, shield->owner.id, &body);
  if (!has || shield->delay < runtime->time) { runtime->remove(runtime, shield); return; }
  if (shield->delay - 0.25 <= runtime->time) shield->model[0] = '\0';
  else { qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = body.origin; p.has_angles = true; p.angles = body.angles; runtime->set_body(runtime, shield, &p); }
  runtime->schedule(runtime, shield, 0.05, runtime->named.action(runtime->named.ctx, shield, "rogue:shield-think"));
}
static void sphere_think_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e) { (void)r; sphere_think(((fx *)vctx)->players, e); }
static void sphere_attack_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e) { (void)r; sphere_attack(((fx *)vctx)->players, e); }
static void sphere_impact_cb(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *sphere, qa_actor_id target, bool hn, qa_vec3 n, const void *s) {
  (void)vctx; (void)hn; (void)n; (void)s; qa_q1mp_game *game = NULL; (void)game;
  if (runtime->health(runtime, target) != 0) (void)runtime->damage(runtime, target, sphere->actor.id, QA_Q1MP_SOME(sphere->actor.id), 1000, NULL, "direct", "rogue:vengeance", NULL);
  runtime->radius_damage(runtime, sphere->actor.id, QA_Q1MP_SOME(sphere->actor.id), 300, QA_Q1MP_SOME(target), NULL, "rogue:vengeance");
  qa_q1b_body b = runtime->body(runtime, sphere);
  runtime->effect(runtime, "explosion", qa_q1mp_vsub(b.origin, qa_q1mp_vscale(qa_q1mp_normalize(b.velocity), 8)), QA_Q1MP_NONE, 0);
  runtime->remove(runtime, sphere);
}
static void ext_attach(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *p) { (void)g; qa_q1mp_players_attach(((fx *)vctx)->players, p); }
static void ext_frame(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *p) { frame_impl(((fx *)vctx)->players, p, g->base->time); }
static void ext_after(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *p, double s) { (void)g; after_physics_impl(((fx *)vctx)->players, p, s); }
void qa_q1mp_mission_pack_players_init(qa_q1mp_mission_pack_players *players, qa_q1mp_game *game, qa_q1mp_pack pack) {
  players->game = game; players->pack = pack;
  fx *hook = malloc(sizeof *hook); hook->players = players; players->source_effects = hook;
  /* Expose before/afterQuad through file-local fns referenced by hook consumers. */
  (void)before_quad; (void)after_quad;
  if (game->register_damage_source_effects) {
    char id[64]; (void)snprintf(id, sizeof id, "q1:%s:effects", pack);
    game->register_damage_source_effects(game->ctx, id, hook);
  }
  if (game->register_player_extension) {
    qa_q1mp_player_extension ext = {0}; static char id2[64]; (void)snprintf(id2, sizeof id2, "q1:%s:players", pack);
    ext.id = id2; ext.ctx = hook; ext.attach = ext_attach; ext.frame = ext_frame; ext.after_physics = ext_after;
    game->register_player_extension(game->ctx, &ext);
  }
  qa_q1b_handlers h = {0}; h.ctx = hook; h.action = shield_think_cb;
  game->base->named.register_named(game->base->named.ctx, "rogue:shield-think", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = hook; h2.action = sphere_think_cb;
  game->base->named.register_named(game->base->named.ctx, "rogue:sphere-think", &h2);
  qa_q1b_handlers h3 = {0}; h3.ctx = hook; h3.action = sphere_attack_cb;
  game->base->named.register_named(game->base->named.ctx, "rogue:sphere-attack", &h3);
  qa_q1b_handlers h4 = {0}; h4.ctx = hook; h4.touch = sphere_impact_cb;
  game->base->named.register_named(game->base->named.ctx, "rogue:sphere-impact", &h4);
}
