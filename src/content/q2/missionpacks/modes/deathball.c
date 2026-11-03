/* Port of src/content/q2/missionpacks/modes/deathball.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/modes/deathball.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/types.h"

qa_q2mp_deathball_rules qa_q2mp_deathball_get_rules(double deathmatch_flags) {
  /* JS `|` yields a signed int32 (bit 31 set reads back negative). */
  uint32_t bits = q2mp_u32(deathmatch_flags) | 0x20000u | 0x80000u | 0x40000u | 256u | 64u;
  qa_q2mp_deathball_rules rules = {0, bits > 0x7fffffffu ? (double)bits - 4294967296.0 : (double)bits};
  return rules;
}
qa_q2mp_deathball_checkpoint qa_q2mp_deathball_capture(qa_q2mp_deathball *ball) {
  qa_q2mp_deathball_checkpoint cp;
  memset(&cp, 0, sizeof cp);
  cp.has_ball = qa_q2_save_actor(ball->has_ball ? &ball->ball : NULL, &cp.ball);
  cp.starts = ball->starts; cp.team1_score = ball->team1_score; cp.team2_score = ball->team2_score;
  return cp;
}
void qa_q2mp_deathball_restore(qa_q2mp_deathball *ball, qa_q2_game_services *game, qa_q2mp_deathball_checkpoint saved) {
  if (!saved.has_ball) ball->has_ball = false;
  else {
    qa_owned_actor resolved; bool has_resolved = qa_session_actor_registry_resolve_saved(game->host->actors, saved.ball, &resolved);
    ball->ball = has_resolved ? resolved.id : qa_session_actor_registry_reference_saved(game->host->actors, saved.ball, NULL);
    ball->has_ball = true;
  }
  ball->starts = saved.starts; ball->team1_score = saved.team1_score; ball->team2_score = saved.team2_score;
}
bool qa_q2mp_deathball_ball_actor(qa_q2mp_deathball *ball, qa_actor_id *out) {
  if (!ball->has_ball) return false;
  *out = ball->ball;
  return true;
}
void qa_q2mp_deathball_scores(qa_q2mp_deathball *ball, double out[2]) {
  out[0] = ball->team1_score;
  out[1] = ball->team2_score;
}
bool qa_q2mp_deathball_check_rules(qa_q2mp_deathball *ball, qa_q2_game_services *game) {
  double limit = ball->hooks.settings(ball->hooks.ctx).goal_limit;
  if (limit == 0) return false;
  int winner = ball->team1_score >= limit ? 1 : ball->team2_score >= limit ? 2 : 0;
  if (winner == 0) return false;
  char text[64];
  (void)snprintf(text, sizeof text, "Team %d Wins.\n", winner);
  qa_q2_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = "print"; event.level = "high"; event.text = text; /* actor: null */
  game->host->emit(game->host->ctx, event);
  ball->hooks.end_level(ball->hooks.ctx);
  return true;
}
void qa_q2mp_deathball_client_begin(qa_q2mp_deathball *ball, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_deathball_settings settings = ball->hooks.settings(ball->hooks.ctx);
  double one = 0, two = 0, unassigned = 0;
  size_t players = 0;
  const qa_actor_id *list = game->host->players(game->host->ctx, &players);
  for (size_t i = 0; i < players; i++) {
    if (qa_same_actor(list[i], entity->actor.id)) continue;
    const char *skin = ball->hooks.skin(ball->hooks.ctx, list[i]);
    if (strchr(skin, '/') != NULL && strcmp(skin, settings.team1_skin) == 0) one++;
    else if (strchr(skin, '/') != NULL && strcmp(skin, settings.team2_skin) == 0) two++;
    else unassigned++;
  }
  ball->hooks.set_skin(ball->hooks.ctx, entity->actor.id, one > two ? settings.team2_skin : settings.team1_skin);
  if (unassigned != 0) {
    char message[96];
    (void)snprintf(message, sizeof message, "%s unassigned players present!", QA_JS_NUMBER(unassigned));
    game->host->diagnostic(game->host->ctx, message);
  }
}
qa_q2mp_spawn_point qa_q2mp_deathball_select_spawn(qa_q2mp_deathball *ball, qa_q2_entity *entity, qa_q2_game_services *game) {
  const char *skin = ball->hooks.skin(ball->hooks.ctx, entity->actor.id);
  qa_q2mp_deathball_settings settings = ball->hooks.settings(ball->hooks.ctx);
  const char *classname = strcmp(skin, settings.team1_skin) == 0 ? "dm_dball_team1_start"
      : strcmp(skin, settings.team2_skin) == 0 ? "dm_dball_team2_start" : "info_player_deathmatch";
  qa_q2_entity *best = NULL;
  double distance = 0;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
    qa_q2_entity *spot = qa_q2_game_entity_at(game, i);
    if (strcmp(spot->classname, classname) != 0) continue;
    double candidate = ball->hooks.spawn_distance(ball->hooks.ctx, spot, game);
    if (candidate > distance) { best = spot; distance = candidate; }
  }
  if (best == NULL) return ball->hooks.select_spawn(ball->hooks.ctx, entity, game);
  qa_body_state body = q2mp_body(game, best);
  qa_q2mp_spawn_point out = {q2mp_add(body.origin, q2mp_v3(0, 0, 9)), body.angles};
  return out;
}
void qa_q2mp_deathball_post_spawn(qa_q2mp_deathball *ball, qa_q2_game_services *game) {
  ball->starts = 0;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
    qa_q2_entity *entity = qa_q2_game_entity_at(game, i);
    if (strcmp(entity->classname, "misc_teleporter_dest") == 0) game->solid(game->ctx, entity, "none");
    else if (strcmp(entity->classname, "dm_dball_ball_start") == 0) ball->starts++;
  }
  if (ball->starts == 0) game->host->diagnostic(game->host->ctx, "No Deathball start points!");
}
double qa_q2mp_deathball_change_damage(qa_q2mp_deathball *ball, qa_actor_id target, bool has_attacker,
                                       qa_actor_id attacker, double damage) {
  bool target_is_ball = ball->has_ball && qa_same_actor(target, ball->ball);
  bool attacker_is_ball = has_attacker && ball->has_ball && qa_same_actor(attacker, ball->ball);
  return target_is_ball ? 1 : !attacker_is_ball ? trunc(damage / 2) : damage;
}
double qa_q2mp_deathball_change_knockback(qa_q2mp_deathball *ball, qa_actor_id target, double knockback,
                                          double means_of_death, qa_q2_game_services *game) {
  if (!(ball->has_ball && qa_same_actor(target, ball->ball))) return knockback;
  if (knockback < 1) {
    if (means_of_death == 8) return 70;
    if (means_of_death == 14) return 90;
    char message[96];
    (void)snprintf(message, sizeof message, "zero knockback, mod %s", QA_JS_NUMBER(means_of_death));
    game->host->diagnostic(game->host->ctx, message);
    return knockback;
  }
  if (means_of_death == 1) return knockback * 3;
  if (means_of_death == 2) return trunc(knockback * 3 / 8);
  if (means_of_death == 3) return trunc(knockback / 3);
  if (means_of_death == 4 || means_of_death == 9) return trunc(knockback * 3 / 2);
  if (means_of_death == 10) return knockback * 4;
  if (means_of_death == 6 || means_of_death == 15 || means_of_death == 46 || means_of_death == 7 || means_of_death == 16 ||
      means_of_death == 24 || means_of_death == 51 || means_of_death == 41) return trunc(knockback / 2);
  if (means_of_death == 11 || means_of_death == 44) return trunc(knockback / 3);
  return knockback;
}

static void goal_effect(qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "effect"; e.effect = "q2:dball_goal"; e.origin = q2mp_body(game, entity).origin;
  e.direction = q2mp_zero(); e.count = 0; e.color = 0;
  game->host->emit(game->host->ctx, e);
}
static void respawn_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_deathball *ball = ctx;
  goal_effect(entity, game);
  qa_q2_entity *first = NULL, *spot = NULL;
  double which = ceil(q2mp_rnd(game) * ball->starts);
  double index = 0;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
    qa_q2_entity *candidate = qa_q2_game_entity_at(game, i);
    if (strcmp(candidate->classname, "dm_dball_ball_start") != 0) continue;
    if (first == NULL) first = candidate;
    if (++index == which) spot = candidate;
  }
  if (spot == NULL) spot = first;
  if (spot == NULL) game->host->diagnostic(game->host->ctx, "No ball start points found!");
  entity->angular_velocity = q2mp_zero();
  entity->model = "models/objects/dball/tris.md2";
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = spot == NULL ? q2mp_body(game, entity).origin : q2mp_body(game, spot).origin;
  c.has_angles = true; c.angles = q2mp_zero();
  c.has_velocity = true; c.velocity = q2mp_zero();
  c.has_ground = true; c.ground_null = true;
  q2mp_move(game, entity, c, false, false);
  game->solid(game->ctx, entity, "box");
  game->motion(game->ctx, entity, entity->motion);
  game->show(game->ctx, entity);
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "entity-event"; e.has_actor = true; e.actor = entity->actor.id; e.event = 6;
  game->host->emit(game->host->ctx, e);
  (void)qa_q2_kill_box(entity, game);
  game->link(game->ctx, entity);
}
static void reset_ball(qa_q2mp_deathball *ball, qa_q2_entity *entity, qa_q2_game_services *game) {
  goal_effect(entity, game);
  entity->angular_velocity = q2mp_zero();
  qa_q2_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  c.has_velocity = true; c.velocity = q2mp_zero();
  q2mp_move(game, entity, c, false, false);
  game->motion(game->ctx, entity, entity->motion);
  game->solid(game->ctx, entity, "none");
  game->schedule(game->ctx, entity, 2, ball->respawn);
}
static void goal_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  qa_q2mp_deathball *ball = ctx;
  if (!(ball->has_ball && qa_same_actor(contact.other, ball->ball))) return;
  qa_q2_entity *ball_entity = q2mp_ent(game, true, ball->ball);
  if (ball_entity == NULL) return;
  int team = ((q2mp_u32(entity->spawnflags) & 1u) != 0) ? 1 : 2;
  qa_q2mp_deathball_settings settings = ball->hooks.settings(ball->hooks.ctx);
  if (team == 1) ball->team1_score = trunc(ball->team1_score + entity->wait);
  else ball->team2_score = trunc(ball->team2_score + entity->wait);
  size_t players = 0;
  const qa_actor_id *list = game->host->players(game->host->ctx, &players);
  for (size_t i = 0; i < players; i++) {
    qa_actor_id actor = list[i];
    const char *skin = ball->hooks.skin(ball->hooks.ctx, actor);
    double score = trunc(entity->wait + (ball_entity->has_enemy && qa_same_actor(actor, ball_entity->enemy) ? 5 : 0));
    if (strchr(skin, '/') == NULL) continue;
    int player_team = strcmp(skin, settings.team1_skin) == 0 ? 1 : strcmp(skin, settings.team2_skin) == 0 ? 2 : 0;
    if (player_team == 0) game->host->diagnostic(game->host->ctx, "unassigned player!!!!");
    else if (player_team == team) ball->hooks.add_score(ball->hooks.ctx, actor, score);
    else if (ball_entity->has_enemy && qa_same_actor(actor, ball_entity->enemy)) ball->hooks.add_score(ball->hooks.ctx, actor, -score);
  }
  reset_ball(ball, ball_entity, game);
  qa_q2_game_use_targets_entity(game, entity, &ball_entity->actor.id, false);
}
static void ball_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  (void)ctx;
  if (!game->host->is_player(game->host->ctx, contact.other) || !q2mp_can_hurt(game, contact.other)) return;
  qa_body_state body = q2mp_body(game, entity);
  qa_body_state_result other = q2mp_bread(game, contact.other);
  double speed = q2mp_len(body.velocity);
  if (other.has_value && speed != 0 && q2mp_dot(q2mp_sub(body.origin, other.value.origin), body.velocity) > 0.7)
    q2mp_damage(game, contact.other, entity, entity->actor.id, &entity->actor.id, trunc(speed / 10), trunc(speed / 10),
                q2mp_zero(), body.origin, q2mp_zero(), QA_Q2MP_MOD_DEATHBALL_CRUSH, false, 0, NULL);
}
static void pain_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_pain_reaction reaction) {
  (void)ctx;
  if (reaction.attacker != NULL) entity->enemy = *reaction.attacker;
  entity->has_enemy = reaction.attacker != NULL;
  qa_gameplay_authority_set_health(game->host->combat, entity->actor, entity->max_health);
}
static void die_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  (void)reaction;
  reset_ball(ctx, entity, game);
}
static void speed_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  qa_q2mp_deathball *ball = ctx;
  if (!(ball->has_ball && qa_same_actor(contact.other, ball->ball)) || entity->timestamp >= q2mp_now(game)) return;
  qa_q2_entity *ball_entity = q2mp_ent(game, true, ball->ball);
  if (ball_entity == NULL) return;
  qa_vec3 velocity = q2mp_body(game, ball_entity).velocity;
  if (q2mp_len(velocity) < 1 ||
      (((q2mp_u32(entity->spawnflags) & 1u) != 0) && q2mp_dot(q2mp_norm(velocity), entity->movedir) < 0.8)) return;
  entity->timestamp = q2mp_now(game) + entity->delay;
  qa_q2_body_changes c = q2mp_changes();
  c.has_velocity = true; c.velocity = q2mp_scale(velocity, entity->speed);
  q2mp_move(game, ball_entity, c, false, false);
  game->motion(game->ctx, ball_entity, ball_entity->motion);
}

bool qa_q2mp_deathball_spawn(qa_q2mp_deathball *ball, qa_q2_entity *entity, qa_q2_game_services *game) {
  static const char *const classnames[] = {"dm_dball_ball", "dm_dball_goal", "dm_dball_speed_change",
                                           "dm_dball_ball_start", "dm_dball_team1_start", "dm_dball_team2_start"};
  bool known = false;
  for (size_t i = 0; i < sizeof classnames / sizeof classnames[0]; i++)
    if (strcmp(entity->classname, classnames[i]) == 0) known = true;
  if (!known) return false;
  if (!q2mp_dm(game)) {
    game->remove(game->ctx, entity);
    return true;
  }
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_deathball_callbacks(ball));
  if (strcmp(entity->classname, "dm_dball_ball") == 0) {
    ball->ball = entity->actor.id; ball->has_ball = true;
    entity->model = "models/objects/dball/tris.md2";
    entity->max_health = 50000;
    entity->clip_mask = qa_q2m_monster_solid_mask(game);
    entity->pain = ball->pain;
    entity->die = ball->die;
    entity->touch = ball->ball_touch;
    qa_combat_state state;
    memset(&state, 0, sizeof state);
    state.health = 50000; state.mass = 50; state.can_take_damage = true;
    state.armor.regular.kind = QA_REGULAR_ARMOR_NONE; state.armor.powered.kind = "none";
    qa_gameplay_authority_create_state(game->host->combat, entity->actor, state, NULL, NULL);
    qa_q2_body_changes c = q2mp_changes();
    c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-32, -32, -32), q2mp_v3(32, 32, 32)};
    q2mp_move(game, entity, c, false, false);
    game->solid(game->ctx, entity, "box");
    game->motion(game->ctx, entity, "new-toss");
    game->show(game->ctx, entity);
  } else if (strcmp(entity->classname, "dm_dball_goal") == 0 || strcmp(entity->classname, "dm_dball_speed_change") == 0) {
    bool speed = strcmp(entity->classname, "dm_dball_speed_change") == 0;
    entity->touch = speed ? ball->speed_touch : ball->goal_touch;
    entity->visible = false;
    if (speed) {
      if (entity->speed == 0) entity->speed = 2;
      if (entity->delay == 0) entity->delay = 0.2;
    } else if (entity->wait == 0) entity->wait = 10;
    entity->movedir = qa_q2_movedir(q2mp_body(game, entity).angles);
    qa_q2_body_changes c = q2mp_changes();
    c.has_angles = true; c.angles = q2mp_zero();
    q2mp_move(game, entity, c, false, false);
    game->solid(game->ctx, entity, "trigger");
    game->motion(game->ctx, entity, "stationary");
    game->show(game->ctx, entity);
  }
  return true;
}

void qa_q2mp_deathball_init(qa_q2mp_deathball *ball, qa_q2mp_deathball_hooks hooks) {
  memset(ball, 0, sizeof *ball);
  ball->hooks = hooks;
  ball->respawn = (qa_q2_think){ball, respawn_fn};
  ball->ball_touch = (qa_q2_touch){ball, ball_touch_fn};
  ball->goal_touch = (qa_q2_touch){ball, goal_touch_fn};
  ball->speed_touch = (qa_q2_touch){ball, speed_touch_fn};
  ball->pain = (qa_q2_pain){ball, pain_fn};
  ball->die = (qa_q2_die){ball, die_fn};
  ball->cb_think[0] = (qa_q2_named_think){"DBall_BallRespawn", ball->respawn};
  ball->cb_touch[0] = (qa_q2_named_touch){"DBall_BallTouch", ball->ball_touch};
  ball->cb_touch[1] = (qa_q2_named_touch){"DBall_GoalTouch", ball->goal_touch};
  ball->cb_touch[2] = (qa_q2_named_touch){"DBall_SpeedTouch", ball->speed_touch};
  ball->cb_pain[0] = (qa_q2_named_pain){"DBall_BallPain", ball->pain};
  ball->cb_die[0] = (qa_q2_named_die){"DBall_BallDie", ball->die};
}
qa_q2_callback_definitions qa_q2mp_deathball_callbacks(qa_q2mp_deathball *ball) {
  qa_q2_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = ball->cb_think; defs.think_count = 1;
  defs.touch = ball->cb_touch; defs.touch_count = 3;
  defs.pain = ball->cb_pain; defs.pain_count = 1;
  defs.die = ball->cb_die; defs.die_count = 1;
  return defs;
}
