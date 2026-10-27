/* Port of src/content/q1/addons/ctf/maps.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/maps.h"

#include <string.h>

#include "qa/content/q1/addons/ctf/runes.h"
#include "qa/foundation/support.h"

static qa_q1ad_ctf_state *state_of(qa_q1ad_context *context) { return context->slice; }

static void vote_teleport(qa_q1ad_ctf_state *state, qa_q1ad_actor *trigger, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_actor *found[4];
  size_t count = qa_q1ad_find(context, trigger->target != NULL ? trigger->target : "", found, 4);
  if (count == 0)
    qa_range_error("CTF vote exit has missing target %s", trigger->target != NULL ? trigger->target : "");
  qa_q1ad_actor *target = found[0];
  qa_vec3 origin = qa_q1ad_body_of(context, target).origin;
  qa_angle_vectors basis = qa_math_angle_vectors_rule(target->mangle, QA_ANGLE_RULE_Q1);
  qa_q1ad_body body = qa_q1ad_ctf_body(state, actor);
  if (context->game.spawn_teleport_fog != NULL) {
    context->game.spawn_teleport_fog(context->game.ctx, body.origin);
    context->game.spawn_teleport_fog(context->game.ctx, qa_add3(origin, qa_scale3(basis.forward, 32)));
  }
  if (context->game.spawn_teledeath != NULL)
    context->game.spawn_teledeath(context->game.ctx, origin, actor);
  if (state->services.teleport != NULL)
    state->services.teleport(state->services.ctx, actor, origin, target->mangle,
        qa_scale3(basis.forward, 300), context->game.time + 0.7);
}
void qa_q1ad_ctf_vote_touch(qa_q1ad_ctf_state *state, qa_q1ad_actor *trigger, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  if (!qa_q1ad_is_player(context, &actor) || qa_q1ad_health(context, actor) <= 0 ||
      (state->services.observer != NULL && state->services.observer(state->services.ctx, actor))) return;
  if (qa_q1ad_ctf_number(state, actor, "voted") != 0) {
    if (qa_q1ad_ctf_number(state, actor, "voted") < context->game.time)
      qa_q1ad_message(context, actor, "$qc_ctf_already_voted", false, NULL, 0);
    qa_q1ad_ctf_set(state, actor, "voted", context->game.time + 1);
    vote_teleport(state, trigger, actor);
    return;
  }
  qa_q1ad_ctf_set(state, actor, "voted", context->game.time + 1);
  qa_q1ad_use_targets(context, trigger, &actor);
  qa_q1ad_ctf_announce(state, "$qc_ctf_has_voted", &actor, trigger->message != NULL ? trigger->message : "");
  trigger->count++;
  qa_q1ad_actor *leader = NULL;
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_actor *candidate = entities[i];
    if (candidate->classname != NULL && strcmp(candidate->classname, "trigger_voteexit") == 0 &&
        candidate != trigger && candidate->count > (leader != NULL ? leader->count : 0))
      leader = candidate;
  }
  double leader_count = leader != NULL ? leader->count : 0;
  if (trigger->count > leader_count || (trigger->count == leader_count && qa_q1ad_random(context) > 0.5))
    leader = trigger;
  qa_q1ad_actor *world = qa_q1ad_ctf_world(state);
  qa_q1ad_actor_set_ref(world, "ctf.voteLeader", leader != NULL ? &leader->actor.id : NULL);
  if (leader != NULL && qa_q1ad_actor_number(world, "ctf.voteExitTime") == 0)
    qa_q1ad_set_number(context, world, "ctf.voteExitTime", context->game.time + 60);
  vote_teleport(state, trigger, actor);
}
static void next_level(qa_q1ad_ctf_state *state, const char *map) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_set_number(context, qa_q1ad_ctf_world(state), "ctf.pregameOver", 1);
  if (context->game.create == NULL) return;
  qa_q1ad_actor *timer = context->game.create(context->game.ctx, "ctf_nextlevel");
  if (timer == NULL) return;
  qa_q1ad_actor_set(timer, "map", map);
  qa_q1ad_schedule(context, timer, 0.1, "ctf:nextlevel");
}
void qa_q1ad_ctf_map_frame(qa_q1ad_ctf_state *state) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_actor *world = qa_q1ad_ctf_world(state);
  if (context->game.intermission || qa_q1ad_actor_number(world, "ctf.pregameOver") != 0) return;
  if (qa_q1ad_ctf_start_map(state)) {
    qa_actor_id leader_id;
    qa_q1ad_actor *leader = qa_q1ad_actor_ref(world, "ctf.voteLeader", &leader_id)
        ? qa_q1ad_entity(context, &leader_id) : NULL;
    double time = qa_q1ad_actor_number(world, "ctf.voteExitTime");
    if (leader != NULL && time != 0 && context->game.time > time)
      next_level(state, qa_q1ad_actor_text(leader, "map"));
    return;
  }
  double time_limit = qa_q1ad_cvar(context, "timelimit") * 60;
  double capture_limit = qa_q1ad_cvar(context, "fraglimit");
  double red = state->services.captures != NULL ? state->services.captures(state->services.ctx, "red") : 0;
  double blue = state->services.captures != NULL ? state->services.captures(state->services.ctx, "blue") : 0;
  if (!(time_limit != 0 && context->game.time >= time_limit) &&
      !(capture_limit != 0 && (red >= capture_limit || blue >= capture_limit))) return;
  qa_actor_id players[64];
  size_t count = qa_q1ad_players(context, players, 64);
  for (size_t i = 0; i < count; i++) {
    if (red == blue) {
      qa_q1ad_msg_arg args[] = {{.is_number = true, .number = red}};
      qa_q1ad_message(context, players[i], "$qc_ks_match_tied", false, args, 1);
    } else {
      qa_q1ad_msg_arg red_args[] = {{.is_number = true, .number = red}};
      qa_q1ad_msg_arg blue_args[] = {{.is_number = true, .number = blue}};
      qa_q1ad_message(context, players[i], red > blue ? "$qc_ks_red_won" : "$qc_ks_red_lost", false, red_args, 1);
      qa_q1ad_message(context, players[i], blue > red ? "$qc_ks_blue_won" : "$qc_ks_blue_lost", false, blue_args, 1);
    }
  }
  static const char *const MAPS[] = {"ctf1", "ctf2", "ctf3", "ctf4", "ctf5", "ctf6", "ctf7", "ctf8", "ctf9"};
  const char *current = context->game.map_name != NULL ? context->game.map_name : "";
  int index = -1;
  for (int i = 0; i < 9; i++) if (strcmp(current, MAPS[i]) == 0) index = i;
  next_level(state, MAPS[(index + 1) % 9]);
}
static void nextlevel_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)state_of(context);
  if (context->game.level_begin != NULL)
    context->game.level_begin(context->game.ctx, qa_q1ad_actor_text(entity, "map"), NULL);
  qa_q1ad_remove(context, entity);
}
static void frame_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)entity;
  qa_q1ad_ctf_map_frame(state_of(context));
}
static void vote_touch_named(qa_q1ad_context *context, qa_q1ad_actor *entity,
                             const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other != NULL) qa_q1ad_ctf_vote_touch(state_of(context), entity, *other);
}
static void changelevel_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                              const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  qa_q1ad_ctf_state *state = state_of(context);
  double noexit = qa_q1ad_cvar(context, "noexit");
  if (!qa_q1ad_is_player(context, other) || noexit == 1 ||
      (noexit == 2 && !qa_q1ad_ctf_start_map(state))) return;
  qa_actor_id actor = *other;
  qa_q1ad_ctf_announce(state, "$qc_exited", &actor, "");
  qa_q1ad_use_targets(context, entity, &actor);
  if (((int32_t)entity->spawnflags & 1) != 0 && context->game.deathmatch == 0) {
    if (context->game.travel != NULL)
      context->game.travel(context->game.ctx, qa_q1ad_actor_text(entity, "map"), &actor);
    return;
  }
  entity->touch[0] = '\0';
  qa_q1ad_schedule(context, entity, 0.1, "ctf:nextlevel");
}
static void spawn_worldspawn(qa_q1ad_context *context, qa_q1ad_actor *world) {
  if (context->game.spawn_map_actor != NULL) context->game.spawn_map_actor(context->game.ctx, world);
  qa_q1ad_add_frame_tick(context, world, "ctf:frame");
}
static void spawn_vote_destination(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->targetname == NULL || entity->targetname[0] == '\0')
    qa_range_error("CTF vote destination has no targetname");
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  entity->mangle = body.angles;
  entity->model = "";
  qa_q1ad_body_patch patch = {.has_angles = true, .angles = {0, 0, 0}, .has_origin = true,
      .origin = qa_add3(body.origin, (qa_vec3){0, 0, 27})};
  qa_q1ad_set_body(context, entity, &patch);
}
static void spawn_voteexit(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  /* The released QC has an unconditional objerror here; retain its documented map behavior with the missing condition restored. */
  if (entity->target == NULL || entity->target[0] == '\0')
    qa_range_error("CTF vote exit has no target");
  qa_q1ad_init_trigger(context, entity);
  entity->count = 0;
  qa_q1ad_bind(entity->touch, "ctf:vote_touch");
  qa_q1ad_link(context, entity);
}
static void spawn_changelevel(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_text(entity, "map")[0] == '\0')
    qa_range_error("CTF changelevel trigger has no map");
  qa_q1ad_init_trigger(context, entity);
  qa_q1ad_bind(entity->touch, "ctf:changelevel_touch");
  qa_q1ad_link(context, entity);
}
static void spawn_ctf_wall(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->solid = "bsp";
  entity->movement = "push";
  qa_q1ad_body_patch patch = {.has_angles = true, .angles = {0, 0, 0}};
  qa_q1ad_set_body(context, entity, &patch);
  qa_q1ad_link(context, entity);
}
static void spawn_nothing(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)context; (void)entity;
}
static void spawn_deathmatch_point(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)entity;
  if (context->game.deathmatch != 0) qa_q1ad_ctf_start_runes(state_of(context));
}
static void spawn_remove(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_remove(context, entity);
}
void qa_q1ad_ctf_register_maps(qa_q1ad_ctf_state *state) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_register_named(context, "ctf:nextlevel", (qa_q1ad_handlers){.action = nextlevel_action});
  qa_q1ad_register_named(context, "ctf:frame", (qa_q1ad_handlers){.action = frame_action});
  qa_q1ad_register_named(context, "ctf:vote_touch", (qa_q1ad_handlers){.touch = vote_touch_named});
  qa_q1ad_register_named(context, "ctf:changelevel_touch", (qa_q1ad_handlers){.touch = changelevel_touch});
  qa_q1ad_register_spawn(context, "worldspawn", false, spawn_worldspawn);
  qa_q1ad_register_spawn(context, "info_vote_destination", false, spawn_vote_destination);
  qa_q1ad_register_spawn(context, "trigger_voteexit", false, spawn_voteexit);
  qa_q1ad_register_spawn(context, "trigger_changelevel", true, spawn_changelevel);
  qa_q1ad_register_spawn(context, "func_ctf_wall", false, spawn_ctf_wall);
  qa_q1ad_register_spawn(context, "info_player_team1", false, spawn_nothing);
  qa_q1ad_register_spawn(context, "info_player_team2", false, spawn_nothing);
  qa_q1ad_register_spawn(context, "info_player_deathmatch", false, spawn_deathmatch_point);
  static const char *const MONSTERS[] = {"monster_army", "monster_dog", "monster_ogre",
      "monster_ogre_marksman", "monster_knight", "monster_hell_knight", "monster_wizard",
      "monster_demon1", "monster_shambler", "monster_zombie", "monster_tarbaby", "monster_fish",
      "monster_enforcer", "monster_shalrath", "monster_boss", "monster_oldone"};
  /* Q1_BASE_CLASSNAMES membership decides replace vs register in TS;
   * the base list is unported, so all register as spawns (host notice
   * carries replace=false). */
  for (size_t i = 0; i < sizeof MONSTERS / sizeof MONSTERS[0]; i++)
    qa_q1ad_register_spawn(context, MONSTERS[i], false, spawn_remove);
}
