/* Port of src/content/q1/addons/ctf/maps.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/maps.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/ctf/runes.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static void vote_teleport(qa_q1ad_ctf_state *state, qa_q1_actor *trigger, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  qa_q1_actor *target = NULL;
  if (qa_q1_find(game, trigger->target, &target, 1) == 0) qa_range_error("CTF vote exit has missing target %s", trigger->target);
  qa_vec3 origin = qa_q1_get_body(game, target).origin;
  qa_vec3 forward = qa_q1_vectors(target->mangle).forward;
  qa_q1_body body = qa_q1ad_ctf_body(state, actor);
  (void)qa_q1_spawn_teleport_fog(game, body.origin);
  (void)qa_q1_spawn_teleport_fog(game, qa_q1_vadd(origin, qa_q1_vscale(forward, 32)));
  (void)qa_q1_spawn_teledeath(game, origin, actor);
  state->services.teleport(state->services.ctx, actor, origin, target->mangle, qa_q1_vscale(forward, 300), game->time + 0.7);
}
void qa_q1ad_ctf_vote_touch(qa_q1ad_ctf_state *state, qa_q1_actor *trigger, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  if (!qa_q1_is_player(game, &actor) || qa_q1_health(game, actor) <= 0 || state->services.observer(state->services.ctx, actor))
    return;
  if (qa_q1ad_ctf_number(state, actor, "voted") != 0) {
    if (qa_q1ad_ctf_number(state, actor, "voted") < game->time) qa_q1_message(game, &actor, "$qc_ctf_already_voted", true, NULL, 0);
    qa_q1ad_ctf_set(state, actor, "voted", game->time + 1);
    vote_teleport(state, trigger, actor);
    return;
  }
  qa_q1ad_ctf_set(state, actor, "voted", game->time + 1);
  qa_q1_use_targets(game, trigger, &actor);
  qa_q1ad_ctf_announce(state, "$qc_ctf_has_voted", &actor, trigger->message);
  trigger->count++;
  qa_q1_actor *leader = NULL;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *candidate = game->entities[i];
    if (strcmp(candidate->classname, "trigger_voteexit") == 0 && candidate != trigger &&
        candidate->count > (leader != NULL ? leader->count : 0))
      leader = candidate;
  }
  double leading = leader != NULL ? leader->count : 0;
  if (trigger->count > leading || (trigger->count == leading && qa_q1_host_random(game) > 0.5)) leader = trigger;
  qa_q1_actor *world = qa_q1ad_ctf_world(state);
  (void)qa_q1_actor_reference_set(world, "ctf.voteLeader", leader != NULL ? &leader->actor.id : NULL);
  if (leader != NULL && qa_q1_actor_number(qa_q1ad_ctf_world(state), "ctf.voteExitTime") == 0)
    qa_q1ad_set_number(state->context, qa_q1ad_ctf_world(state), "ctf.voteExitTime", game->time + 60);
  vote_teleport(state, trigger, actor);
}
/** Deliberate divergence: the released CTF GotoNextMap discards the voted map and a trigger's own map; the port honors both. */
static void next_level(qa_q1ad_ctf_state *state, const char *map) {
  qa_q1_services *game = state->context->game;
  qa_q1ad_set_number(state->context, qa_q1ad_ctf_world(state), "ctf.pregameOver", 1);
  qa_q1_actor *timer = qa_q1_create(game, "ctf_nextlevel", NULL, NULL);
  (void)qa_q1_actor_field_set(timer, "map", map);
  qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, "ctf:nextlevel"));
}
void qa_q1ad_ctf_map_frame(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  if (game->has_intermission || qa_q1_actor_number(qa_q1ad_ctf_world(state), "ctf.pregameOver") != 0) return;
  if (qa_q1ad_ctf_start_map(state)) {
    qa_actor_id leader_id;
    bool has_leader = qa_q1_actor_reference_get(qa_q1ad_ctf_world(state), "ctf.voteLeader", &leader_id);
    qa_q1_actor *leader = qa_q1_get_entity(game, has_leader ? &leader_id : NULL);
    double time = qa_q1_actor_number(qa_q1ad_ctf_world(state), "ctf.voteExitTime");
    if (leader != NULL && time != 0 && game->time > time) next_level(state, qa_q1_actor_text(leader, "map"));
    return;
  }
  qa_q1ad_services *services = &state->context->services;
  double time_limit = services->cvar(services->ctx, "timelimit") * 60;
  double capture_limit = services->cvar(services->ctx, "fraglimit");
  double red = state->services.captures(state->services.ctx, "red");
  double blue = state->services.captures(state->services.ctx, "blue");
  if (!(time_limit != 0 && game->time >= time_limit) &&
      !(capture_limit != 0 && (red >= capture_limit || blue >= capture_limit)))
    return;
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_msg_arg red_arg[1] = {{.is_number = true, .number = red}};
    qa_q1_msg_arg blue_arg[1] = {{.is_number = true, .number = blue}};
    if (red == blue) qa_q1_message(game, &players[i], "$qc_ks_match_tied", false, red_arg, 1);
    else {
      qa_q1_message(game, &players[i], red > blue ? "$qc_ks_red_won" : "$qc_ks_red_lost", false, red_arg, 1);
      qa_q1_message(game, &players[i], blue > red ? "$qc_ks_blue_won" : "$qc_ks_blue_lost", false, blue_arg, 1);
    }
  }
  static const char *const MAPS[9] = {"ctf1", "ctf2", "ctf3", "ctf4", "ctf5", "ctf6", "ctf7", "ctf8", "ctf9"};
  int index = -1;
  for (int i = 0; i < 9; i++)
    if (game->map_name != NULL && strcmp(MAPS[i], game->map_name) == 0) { index = i; break; }
  next_level(state, MAPS[(index + 1) % 9]);
}

static void nextlevel_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_ctf_state *state = ctx;
  qa_q1_level_rules_begin(qa_q1_base_level_rules(state->context->base), qa_q1_actor_text(entity, "map"), NULL);
  qa_q1_remove(game, entity);
}
static void frame_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; (void)entity; /* TS () => mapFrame(state) */
  qa_q1ad_ctf_map_frame(ctx);
}
static void vote_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id actor, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS touch reads (entity, actor) */
  qa_q1ad_ctf_vote_touch(ctx, entity, actor);
}
static void changelevel_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id actor, const qa_vec3 *normal,
                              const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS touch reads (entity, actor) */
  qa_q1ad_ctf_state *state = ctx;
  double noexit = state->context->services.cvar(state->context->services.ctx, "noexit");
  if (!qa_q1_is_player(game, &actor) || noexit == 1 || (noexit == 2 && !qa_q1ad_ctf_start_map(state))) return;
  if (game->options.coop || game->options.deathmatch != 0) qa_q1ad_ctf_announce(state, "$qc_exited", &actor, "");
  qa_q1_use_targets(game, entity, &actor);
  if ((qa_js_to_int32(entity->spawnflags) & 1) != 0 && game->options.deathmatch == 0) {
    qa_q1_travel(game, qa_q1_actor_text(entity, "map"), &actor);
    return;
  }
  entity->touch = (qa_q1_bound_touch){0};
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "ctf:nextlevel"));
}
static void spawn_worldspawn(void *ctx, qa_q1_services *game, qa_q1_actor *world) {
  qa_q1ad_ctf_state *state = ctx;
  qa_q1_spawn_map_actor(game, world);
  qa_q1ad_add_frame_tick(state->context, world, "ctf:frame");
}
static void spawn_vote_destination(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* TS closure uses game only */
  if (entity->targetname[0] == '\0') qa_range_error("CTF vote destination has no targetname");
  entity->mangle = qa_q1_get_body(game, entity).angles;
  qa_q1ad_set_text(&entity->model, "");
  qa_q1_body_patch patch = {.has_angles = true, .angles = QA_Q1_ZERO, .has_origin = true,
                            .origin = qa_q1_vadd(qa_q1_get_body(game, entity).origin, (qa_vec3){0, 0, 27})};
  qa_q1_set_body(game, entity, &patch);
}
static void spawn_vote_exit(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_ctf_state *state = ctx;
  /* The released QC has an unconditional objerror here; retain its documented map behavior with the missing condition restored. */
  if (entity->target[0] == '\0') qa_range_error("CTF vote exit has no target");
  qa_q1ad_init_trigger(state->context, entity);
  entity->count = 0;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "ctf:vote_touch");
  qa_q1_link(game, entity);
}
static void spawn_changelevel(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_ctf_state *state = ctx;
  if (qa_q1_actor_text(entity, "map")[0] == '\0') qa_range_error("CTF changelevel trigger has no map");
  qa_q1ad_init_trigger(state->context, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "ctf:changelevel_touch");
  qa_q1_link(game, entity);
}
static void spawn_ctf_wall(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* TS closure uses game only */
  entity->solid = "bsp";
  entity->movement = "push";
  qa_q1_body_patch patch = {.has_angles = true, .angles = QA_Q1_ZERO};
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
}
static void spawn_nothing(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game; (void)entity; /* TS () => undefined */
}
static void spawn_deathmatch_start(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)entity; /* TS () => ... ignores the spot */
  if (game->options.deathmatch != 0) qa_q1ad_ctf_start_runes(ctx);
}
static void spawn_removed(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* TS closure uses game only */
  qa_q1_remove(game, entity);
}

void qa_q1ad_ctf_register_maps(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  qa_q1_callback_registry_register(game->named, "ctf:nextlevel", (qa_q1_callback_handlers){.ctx = state, .action = nextlevel_action});
  qa_q1_callback_registry_register(game->named, "ctf:frame", (qa_q1_callback_handlers){.ctx = state, .action = frame_action});
  qa_q1_callback_registry_register(game->named, "ctf:vote_touch", (qa_q1_callback_handlers){.ctx = state, .touch = vote_touch});
  qa_q1_callback_registry_register(game->named, "ctf:changelevel_touch",
                                   (qa_q1_callback_handlers){.ctx = state, .touch = changelevel_touch});
  qa_q1_register_spawn(game, "worldspawn", spawn_worldspawn, state);
  qa_q1_register_spawn(game, "info_vote_destination", spawn_vote_destination, state);
  qa_q1_register_spawn(game, "trigger_voteexit", spawn_vote_exit, state);
  qa_q1_replace_spawn(game, "trigger_changelevel", spawn_changelevel, state);
  qa_q1_register_spawn(game, "func_ctf_wall", spawn_ctf_wall, state);
  qa_q1_register_spawn(game, "info_player_team1", spawn_nothing, state);
  qa_q1_register_spawn(game, "info_player_team2", spawn_nothing, state);
  qa_q1_register_spawn(game, "info_player_deathmatch", spawn_deathmatch_start, state);
  static const char *const MONSTERS[] = {
    "monster_army", "monster_dog", "monster_ogre", "monster_ogre_marksman", "monster_knight", "monster_hell_knight",
    "monster_wizard", "monster_demon1", "monster_shambler", "monster_zombie", "monster_tarbaby", "monster_fish",
    "monster_enforcer", "monster_shalrath", "monster_boss", "monster_oldone"};
  const char *const *base = NULL;
  size_t base_count = qa_q1_base_classnames(&base);
  for (size_t i = 0; i < sizeof MONSTERS / sizeof MONSTERS[0]; i++) {
    bool replaced = false;
    for (size_t j = 0; j < base_count && !replaced; j++) replaced = strcmp(base[j], MONSTERS[i]) == 0;
    if (replaced) qa_q1_replace_spawn(game, MONSTERS[i], spawn_removed, state);
    else qa_q1_register_spawn(game, MONSTERS[i], spawn_removed, state);
  }
}
