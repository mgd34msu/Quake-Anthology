/* Port of src/content/q1/missionpacks/world/rogue-ending.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_ending.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/content/q1/missionpacks/world/campaign.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"
#include "qa/content/q1/missionpacks/world/index.h"

static qa_q1_actor *reference_entity(qa_q1_services *game, const qa_q1_actor *entity, const char *key) {
  qa_actor_id actor;
  return entity != NULL && qa_q1_actor_reference_get(entity, key, &actor) ? qa_q1_get_entity(game, &actor) : NULL;
}
static void set_reference(qa_q1_actor *entity, const char *key, const qa_actor_id *actor) {
  if (!qa_q1_actor_reference_set(entity, key, actor)) qa_range_error("Out of memory");
}
static void emit_finale(qa_q1_services *game, const char *text, double stage) {
  qa_q1_event event = {0};
  event.kind = "finale"; event.text = text; event.stage = stage;
  qa_q1_emit(game, event);
}
static void emit_achievement(qa_q1_services *game, const char *id) {
  qa_q1_event event = {0};
  event.kind = "achievement"; event.id = id;
  qa_q1_emit(game, event);
}
static qa_q1_actor *machine(qa_q1_services *game) {
  qa_q1_actor *entity = reference_entity(game, game->world, "rogue:theMachine");
  if (entity == NULL) qa_range_error("End sequence time machine is missing");
  return entity;
}
static void remove_stuff(qa_q1_services *game) {
  size_t count;
  qa_q1_actor **entities = qa_q1mp_entities(game, &count);
  for (size_t i = 0; i < count; i++) if (qa_q1mp_streq(entities[i]->classname, "ltrail_start")) qa_q1_remove(game, entities[i]);
  free(entities);
  entities = qa_q1mp_entities(game, &count);
  qa_q1_actor *core = NULL;
  for (size_t i = 0; i < count && core == NULL; i++) if (qa_q1mp_streq(entities[i]->classname, "item_time_core")) core = entities[i];
  free(entities);
  qa_q1_event event = {0};
  event.kind = "colored-explosion"; event.has_origin = true;
  event.origin = core == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, core).origin;
  event.color_start = 230; event.color_length = 5;
  qa_q1_emit(game, event);
  if (core != NULL) qa_q1mp_world_later(game, core, 0.1, "SUB_Remove");
}
static void escape_lava(qa_q1_services *game, qa_q1_actor *actor) {
  if (!qa_q1mp_streq(game->host.contents(game->host.ctx, qa_q1_get_body(game, actor).origin), "lava")) return;
  qa_q1_actor *point = qa_q1mp_find_first(game, "point1");
  if (point != NULL) qa_q1_set_origin(game, actor, qa_q1_get_body(game, point).origin);
}
static void goal(qa_q1_services *game, qa_q1_actor *actor, const char *target) {
  qa_q1mp_set_string(&actor->target, target);
  qa_q1_actor *point = qa_q1mp_find_first(game, target);
  if (point == NULL) qa_range_error("End sequence %s placing screwed up!", target);
  set_reference(actor, "goalentity", &point->actor.id);
  set_reference(actor, "movetarget", &point->actor.id);
}
static void control(void *ctx, qa_q1_services *game, qa_q1_actor *actor) {
  (void)ctx;
  qa_q1_actor *world = game->world;
  if (world == NULL) qa_range_error("Ending requires worldspawn");
  const double stage = qa_q1_actor_number(world, "rogue:actorStage");
  if (stage == 0) {
    goal(game, actor, "point1"); actor->frame = 6; qa_q1mp_world_number(world, "rogue:actorStage", 1);
    qa_q1mp_world_later(game, actor, 0.1, "rogue:actor_run"); return;
  }
  if (stage == 2) {
    goal(game, actor, "machine"); qa_q1mp_world_number(world, "rogue:actorStage", 5);
    qa_q1mp_world_later(game, actor, 0.1, "rogue:actor_fire1"); return;
  }
  if (stage == 4) { actor->frame = 12; qa_q1mp_world_later(game, actor, 2, "rogue:actor_teleport"); return; }
  if (stage == 3) {
    qa_q1mp_set_string(&actor->target, "timepod");
    qa_q1_use_targets(game, actor, actor->has_activator ? &actor->activator : NULL);
    goal(game, actor, "point2"); actor->frame = 6;
    qa_q1mp_world_later(game, actor, 0.1, "rogue:actor_run");
  }
}
void qa_q1mp_start_rogue_ending(qa_q1_services *game, qa_actor_id player, const qa_q1mp_world_hooks *hooks) {
  qa_q1_actor *world = game->world;
  if (world == NULL || qa_q1_actor_number(world, "rogue:cutscene_running") == 0 || qa_q1_actor_number(world, "rogue:ending_started") != 0) return;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player);
  if (!body.has_value) return;
  qa_q1mp_world_number(world, "rogue:ending_started", 1);
  qa_q1_actor *camera = qa_q1mp_find_first(game, "cameraview");
  qa_q1_base *base = qa_q1_base_for(game);
  qa_q1_level_rules_begin_cutscene(qa_q1_base_level_rules(base), game->map_name, &player,
                                   game->time + (game->options.coop || camera == NULL ? 3 : 10000000));
  if (game->options.coop || camera == NULL) {
    qa_q1_control_player(game, player, qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 48)), body.value.angles, QA_Q1_ZERO);
    emit_finale(game, qa_q1mp_mission_finale_text(game->options.edition, "$qc_finale_coop"), 4);
    remove_stuff(game);
    qa_q1mp_world_later(game, machine(game), 0.1, "rogue:time_crash");
    return;
  }
  emit_finale(game, "", 1);
  qa_q1_actor *actor = qa_q1_create(game, "actor", NULL, NULL);
  actor->has_owner = true; actor->owner = player; actor->max_health = 100; actor->solid = "slidebox"; actor->movement = "step";
  actor->frame = hooks != NULL && hooks->player_frame != NULL ? hooks->player_frame(hooks->ctx, player) : 0;
  qa_q1mp_set_string(&actor->model, "progs/player.mdl");
  qa_gameplay_authority_set_health(game->host.combat, actor->actor, 100);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = body.value.origin;
  patch.has_angles = true; patch.angles = body.value.angles;
  patch.has_bounds = true; patch.bounds = QA_Q1_PLAYER_BOUNDS;
  qa_q1_set_body(game, actor, &patch);
  qa_q1mp_world_vector(actor, "view_ofs", QA_Q1MP_V3(0, 0, 25));
  actor->movement_flags = qa_js_bit_or(actor->movement_flags, 32);
  actor->ideal_yaw = body.value.angles.y; actor->yaw_speed = 20;
  set_reference(world, "rogue:theActor", &actor->actor.id);
  escape_lava(game, actor);
  qa_q1mp_world_later(game, actor, 0.1, "rogue:actor_control");
  qa_q1_link(game, actor);
  const qa_vec3 origin = qa_q1_get_body(game, camera).origin;
  const qa_vec3 angles = qa_q1mp_velocity_angles(qa_q1_vsub(qa_q1_get_body(game, actor).origin, origin));
  qa_q1_control_player(game, player, origin, angles, QA_Q1_ZERO);
  qa_q1_actor *tracker = qa_q1_create(game, "rogue_camera_tracker", NULL, NULL);
  tracker->has_owner = true; tracker->owner = player;
  qa_q1mp_world_later(game, tracker, 0.05, "rogue:track_camera");
}
static bool world_followers(void *ctx, qa_q1_actor *corner, qa_q1_actor *mover) {
  qa_q1_services *game = ctx;
  if (!qa_q1mp_streq(mover->classname, "actor") && !qa_q1mp_streq(mover->classname, "buzzsaw")) return false;
  qa_actor_id current;
  if (!qa_q1_actor_reference_get(mover, "movetarget", &current) || !qa_same_actor(current, corner->actor.id)) return false;
  qa_q1_actor *next = qa_q1mp_find_first(game, corner->target);
  set_reference(mover, "goalentity", next != NULL ? &next->actor.id : NULL);
  set_reference(mover, "movetarget", next != NULL ? &next->actor.id : NULL);
  qa_vec3 a = next == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, next).origin;
  qa_vec3 b = qa_q1_get_body(game, mover).origin;
  mover->ideal_yaw = qa_q1_yaw_for(qa_q1_vsub(a, b));
  if (next == NULL) {
    qa_q1mp_world_number(mover, "pausetime", game->time + 999999);
    if (qa_q1mp_streq(mover->classname, "buzzsaw")) qa_q1mp_world_later(game, mover, 0.1, "rogue:saw_stand");
  }
  return true;
}
static void track_camera(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  qa_q1_actor *actor = reference_entity(g, g->world, "rogue:theActor");
  const qa_body_state_result body = e->has_owner ? qa_shared_body_table_read(g->host.bodies, e->owner) : (qa_body_state_result){0};
  if (actor == NULL || !e->has_owner || !body.has_value) { qa_q1_remove(g, e); return; }
  const qa_vec3 delta = qa_q1_vsub(qa_q1_get_body(g, actor).origin, body.value.origin);
  qa_q1_control_player(g, e->owner, body.value.origin, qa_q1mp_velocity_angles(QA_Q1MP_V3(delta.x, delta.y, -delta.z)), QA_Q1_ZERO);
  qa_q1mp_world_later(g, e, 0.1, "rogue:track_camera");
}
static void actor_teleport(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  (void)qa_q1_spawn_teleport_fog(g, qa_q1_get_body(g, e).origin);
  qa_q1mp_set_string(&e->model, "");
  qa_q1mp_world_later(g, e, 999999, "SUB_Null");
}
static void actor_run(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  escape_lava(g, e);
  qa_q1_actor *point = reference_entity(g, e, "goalentity"), *world = g->world;
  if (point != NULL && (qa_q1mp_streq(point->targetname, "endpoint1") || qa_q1mp_streq(point->targetname, "endpoint2"))) {
    if (world != NULL) qa_q1mp_world_number(world, "rogue:actorStage", qa_q1mp_streq(point->targetname, "endpoint1") ? 2 : 4);
    qa_q1mp_world_later(g, e, 0.1, "rogue:actor_control"); return;
  }
  e->frame++;
  if (e->frame > 11) e->frame = 6;
  if (point != NULL) g->host.move_to_goal(g->host.ctx, e->actor, point->actor.id, 15, NULL);
  qa_q1mp_world_later(g, e, 0.1, "rogue:actor_run");
}
static const char *const FIRE_NAMES[22] = {
  NULL, "rogue:actor_fire1", "rogue:actor_fire2", "rogue:actor_fire3", "rogue:actor_fire4", "rogue:actor_fire5",
  "rogue:actor_fire6", "rogue:actor_fire7", "rogue:actor_fire8", "rogue:actor_fire9", "rogue:actor_fire10",
  "rogue:actor_fire11", "rogue:actor_fire12", "rogue:actor_fire13", "rogue:actor_fire14", "rogue:actor_fire15",
  "rogue:actor_fire16", "rogue:actor_fire17", "rogue:actor_fire18", "rogue:actor_fire19", "rogue:actor_fire20",
  "rogue:actor_fire21",
};
static void actor_fire(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  const int stage = (int)((const char *const *)ctx - FIRE_NAMES);
  e->frame = stage <= 6 ? 106 + stage : 12 + (stage - 7) % 5;
  if (stage == 1) {
    qa_q1_actor *target = machine(g);
    set_reference(e, "goalentity", &target->actor.id);
    target->pain = qa_q1_callback_registry_pain(g->named, target, "rogue:time_crash");
    target->die = qa_q1_callback_registry_die(g->named, target, "rogue:time_crash");
    qa_gameplay_authority_set_health(g->host.combat, target->actor, 1);
    qa_vec3 a = qa_q1_get_body(g, target).origin;
    qa_vec3 b = qa_q1_get_body(g, e).origin;
    const qa_vec3 angles = qa_q1mp_velocity_angles(qa_q1_vsub(a, b));
    qa_q1_body_patch patch = {0};
    patch.has_angles = true; patch.angles = angles;
    qa_q1_set_body(g, e, &patch);
    e->effects = 2;
    const qa_vec3 view = QA_Q1MP_V3(-angles.x, angles.y, angles.z);
    const qa_q1_basis basis = qa_q1_make_vectors(g, view);
    qa_q1mp_world_vector(e, "v_angle", view);
    qa_q1mp_world_number(e, "ammo_rockets1", qa_q1_actor_number(e, "ammo_rockets1") - 1);
    qa_q1mp_world_number(e, "currentammo", qa_q1_actor_number(e, "ammo_rockets1"));
    qa_q1_sound(g, e->actor.id, "weapons/sgun1.wav", "weapon", 1, 1);
    const qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_get_body(g, e).origin, qa_q1_vscale(basis.forward, 8)), QA_Q1MP_V3(0, 0, 16));
    const qa_vec3 velocity = qa_q1_vscale(qa_q1_aim(g, e->actor, basis.forward), 1000);
    qa_q1_actor *missile = qa_q1_create_missile_lifetime(g, &e->actor.id, "missile", "missile", origin, velocity, 5);
    missile->projectile = "rocket";
    missile->touch = qa_q1_callback_registry_touch(g->named, missile, "projectile_touch");
    emit_finale(g, qa_q1mp_mission_finale_text(g->options.edition, "$qc_finale_rogue_end"), 4);
    const qa_q1_base_options *options = qa_q1_base_options_of(qa_q1_base_for(g));
    const bool official = !(options->has_official_campaign && !options->official_campaign);
    if (qa_q1mp_streq(g->options.edition, "rerelease") && official && qa_q1mp_streq(g->map_name, "r2m8")) {
      emit_achievement(g, "ACH_COMPLETE_R2M8");
      if (qa_q1_option_skill(g) == 3) emit_achievement(g, "ACH_COMPLETE_R2M8_NIGHTMARE");
    }
    qa_q1mp_start_finale_timer(g);
  } else if (stage == 2) {
    qa_q1_body_patch patch = {0};
    patch.has_angles = true; patch.angles = qa_q1_get_body(g, e).angles; patch.angles.x = 0;
    qa_q1_set_body(g, e, &patch);
    qa_vec3 view = qa_q1_actor_vector(e, "v_angle"); view.x = 0;
    qa_q1mp_world_vector(e, "v_angle", view);
  } else if (stage == 5) {
    remove_stuff(g);
    qa_q1_actor *target = machine(g);
    if (qa_q1_health(g, target->actor.id) > 0) qa_q1mp_world_later(g, target, 0.1, "rogue:time_crash");
  } else if (stage == 6) e->effects = 0;
  else if (stage == 21 && g->world != NULL) qa_q1mp_world_number(g->world, "rogue:actorStage", 3);
  qa_q1mp_world_later(g, e, stage == 1 ? 0.1 : 0.15, stage == 21 ? "rogue:actor_control" : FIRE_NAMES[stage + 1]);
}
void qa_q1mp_register_rogue_ending(qa_q1_services *game) {
  qa_q1_register_path_touch(game, "rogue:world_followers", world_followers, game);
  qa_q1_callback_registry_register(game->named, "rogue:track_camera", (qa_q1_callback_handlers){.action = track_camera});
  qa_q1_callback_registry_register(game->named, "rogue:actor_control", (qa_q1_callback_handlers){.action = control});
  qa_q1_callback_registry_register(game->named, "rogue:actor_teleport", (qa_q1_callback_handlers){.action = actor_teleport});
  qa_q1_callback_registry_register(game->named, "rogue:actor_run", (qa_q1_callback_handlers){.action = actor_run});
  for (int stage = 1; stage <= 21; stage++)
    qa_q1_callback_registry_register(game->named, FIRE_NAMES[stage],
                                     (qa_q1_callback_handlers){.ctx = (void *)&FIRE_NAMES[stage], .action = actor_fire});
}
