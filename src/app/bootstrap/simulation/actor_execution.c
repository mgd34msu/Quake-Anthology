/* Port of src/app/bootstrap/simulation/actor-execution.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/actor_execution.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/pusher.h"
#include "qa/network/common/js_number.h"
#include "qa/world/actors/registry.h"

static const qa_vec3 down = {0, 0, -1};
static const qa_vec3 zero = {0, 0, 0};

static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* TS model(path): /^\*[0-9]+$/ ? Number(path.slice(1)) : null. */
static bool model_number(const char *path, double *out) {
  if (path == NULL || path[0] != '*' || path[1] == '\0') return false;
  double value = 0;
  for (const char *c = path + 1; *c != '\0'; c++) {
    if (*c < '0' || *c > '9') return false;
  }
  value = qa_js_string_to_number(path + 1);
  *out = value;
  return true;
}
static double seconds_of(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
/* TS add(a, b): Math.fround per component. */
static qa_vec3 add_fround(qa_vec3 a, double bx, double by, double bz) {
  qa_vec3 out;
  out.x = (double)(float)(a.x + bx);
  out.y = (double)(float)(a.y + by);
  out.z = (double)(float)(a.z + bz);
  return out;
}
static const char *q1_edition(const qa_q1_services *services) {
  return services->options.physics_edition != NULL ? services->options.physics_edition : services->options.edition;
}

void qa_app_sim_actor_visited_add(qa_app_sim_actor_visited *visited, qa_owned_actor actor) {
  if (qa_app_sim_actor_visited_has(visited, actor)) return;
  if (visited->count == visited->capacity) {
    size_t capacity = visited->capacity == 0 ? 8 : visited->capacity * 2;
    qa_owned_actor *next = realloc(visited->actors, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    visited->actors = next; visited->capacity = capacity;
  }
  visited->actors[visited->count++] = actor;
}
bool qa_app_sim_actor_visited_has(const qa_app_sim_actor_visited *visited, qa_owned_actor actor) {
  for (size_t i = 0; i < visited->count; i++)
    if (qa_same_owned_actor(visited->actors[i], actor)) return true;
  return false;
}

qa_q2_motion qa_app_sim_actor_motion(const qa_app_sim_actor_execution *entry, qa_body_state body) {
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE) return entry->motion(entry->ctx, body);
  qa_q2_motion motion;
  memset(&motion, 0, sizeof motion);
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) {
    if (!entry->quakec->motion(entry->quakec->ctx, entry->actor, body, &motion)) qa_raise("Error", 0, 0, "Missing live QC motion");
    return motion;
  }
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3) {
    motion.actor = entry->actor; motion.velocity = body.velocity; motion.angular_velocity = zero;
    motion.kind = "stationary"; motion.gravity = 1; motion.gravity_vector = down; motion.clip_mask = 0x6000001;
    motion.has_owner = true; motion.owner = entry->owner;
    return motion;
  }
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) {
    const qa_q2_entity *entity = entry->q2_entity;
    motion.actor = entity->actor; motion.velocity = body.velocity; motion.angular_velocity = entity->angular_velocity;
    motion.kind = entity->motion; motion.gravity = entity->gravity; motion.gravity_vector = entity->gravity_vector;
    motion.clip_mask = entity->clip_mask; motion.has_owner = entity->has_owner; motion.owner = entity->owner;
    return motion;
  }
  const qa_q1_actor *entity = entry->q1_entity;
  if (same(entity->movement, "gib") && !same(q1_edition(entry->q1_services), "rerelease"))
    qa_raise("Error", 0, 0, "Q1 gib movement requires rerelease engine behavior");
  motion.actor = entity->actor; motion.velocity = body.velocity; motion.angular_velocity = entity->angular_velocity;
  motion.kind = same(entity->movement, "gib") ? "bounce" : same(entity->movement, "flymissile") ? "fly-missile"
      : same(entity->movement, "none") || same(entity->movement, "noclip") ? "stationary" : entity->movement;
  double gravity = qa_q1_actor_number(entity, "gravity");
  motion.gravity = gravity != 0 && gravity == gravity ? gravity : 1;
  motion.gravity_vector = down; motion.clip_mask = 0x6000003;
  motion.has_owner = entity->has_owner; motion.owner = entity->owner;
  return motion;
}

qa_app_sim_shared_solid qa_app_sim_actor_collision(const qa_app_sim_actor_execution *entry) {
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE) return entry->collision(entry->ctx);
  qa_app_sim_shared_solid solid;
  memset(&solid, 0, sizeof solid);
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) {
    if (!entry->quakec->collision(entry->quakec->ctx, entry->actor, &solid)) qa_raise("Error", 0, 0, "Missing live QC collision");
    return solid;
  }
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3) {
    solid.family = "q3"; solid.solid = "none"; solid.has_model = false; solid.owner = &entry->owner;
    return solid;
  }
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    const qa_q1_actor *entity = entry->q1_entity;
    solid.family = "q1";
    solid.owner = entity->has_owner ? &entity->owner : NULL;
    if (same(entity->solid, "corpse")) {
      solid.has_model = false;
      if (same(q1_edition(entry->q1_services), "rerelease")) { solid.solid = "box"; solid.q1_corpse = true; }
      else solid.solid = "none";
      return solid;
    }
    solid.solid = same(entity->solid, "none") ? "none" : same(entity->solid, "trigger") ? "trigger"
        : same(entity->solid, "bsp") ? "brush" : "box";
    const char *path = entity->model != NULL && entity->model[0] != '\0' ? entity->model : entity->original_model;
    solid.has_model = model_number(path, &solid.model);
    solid.has_monster = true; solid.monster = entity->monster != NULL;
    solid.has_item = true; solid.item = (qa_js_to_int32(entity->movement_flags) & 256) != 0;
    return solid;
  }
  const qa_q2_entity *entity = entry->q2_entity;
  solid.family = "q2"; solid.solid = entity->solid;
  solid.has_model = model_number(entity->model, &solid.model);
  solid.owner = entity->has_owner ? &entity->owner : NULL;
  solid.has_monster = true; solid.monster = (qa_js_to_int32(entity->server_flags) & 4) != 0;
  solid.has_dead_monster = true; solid.dead_monster = (qa_js_to_int32(entity->server_flags) & 2) != 0;
  return solid;
}

qa_app_sim_shared_physics_flags qa_app_sim_actor_flags(const qa_app_sim_actor_execution *entry) {
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) return entry->quakec->flags(entry->quakec->ctx, entry->actor);
  qa_app_sim_shared_physics_flags flags;
  memset(&flags, 0, sizeof flags);
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3 || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE) return flags;
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    const qa_q1_actor *entity = entry->q1_entity;
    int32_t movement = qa_js_to_int32(entity->movement_flags);
    flags.has_fly = true; flags.fly = (movement & 1) != 0;
    flags.has_swim = true; flags.swim = (movement & 2) != 0;
    flags.has_partial_ground = true; flags.partial_ground = (movement & 1024) != 0;
    flags.has_water_level = true; flags.water_level = entity->water_level;
    flags.has_water_type = true; flags.water_type = entity->water_type;
    flags.has_enemy = true;
    flags.enemy = entity->monster != NULL && entity->monster->has_enemy ? &entity->monster->enemy : NULL;
    return flags;
  }
  const qa_q2_entity *entity = entry->q2_entity;
  const qa_q2m_state *monster = entry->read_monster(entry->ctx);
  int32_t bits = qa_js_to_int32(entity->flags);
  flags.has_team_slave = true; flags.team_slave = (bits & 1024) != 0;
  flags.has_always_touch = true; flags.always_touch = (bits & 0x10000000) != 0;
  if (monster != NULL) {
    flags.has_fly = true; flags.fly = monster->locomotion == QA_Q2M_LOCO_FLY;
    flags.has_swim = true; flags.swim = monster->locomotion == QA_Q2M_LOCO_SWIM;
    flags.has_dead = true; flags.dead = monster->dead;
    flags.has_water_level = true; flags.water_level = monster->water_level;
    flags.has_water_type = true; flags.water_type = monster->water_type;
  }
  return flags;
}

void qa_app_sim_write_actor_flags(const qa_app_sim_actor_execution *entry, qa_app_sim_shared_physics_flags changes) {
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) {
    entry->quakec->write_flags(entry->quakec->ctx, entry->actor, changes);
    return;
  }
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3 || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE) return;
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    if (changes.has_water_level) entry->q1_entity->water_level = changes.water_level;
    if (changes.has_water_type) {
      double water_type = changes.water_type;
      if (water_type == 0 || water_type == -1 || water_type == -2 || water_type == -3 || water_type == -4
          || water_type == -5 || water_type == -6) entry->q1_entity->water_type = water_type;
    }
    return;
  }
  qa_q2m_state *monster = entry->read_monster(entry->ctx);
  if (monster == NULL) return;
  if (changes.has_water_level) {
    double level = changes.water_level;
    if (level == 0 || level == 1 || level == 2 || level == 3) monster->water_level = level;
  }
  if (changes.has_water_type) monster->water_type = changes.water_type;
}

static void move_q1_noclip(qa_owned_actor actor, qa_vec3 angular_velocity, const qa_app_sim_actor_execution_frame *context) {
  double elapsed = context->elapsed;
  qa_body_state_result read = qa_shared_body_table_read(context->bodies, actor.id);
  if (!read.has_value) return;
  qa_body_state body = read.value;
  qa_actor_id ground;
  if (body.ground != NULL) { ground = *body.ground; body.ground = &ground; }
  body.origin = add_fround(read.value.origin, read.value.velocity.x * elapsed, read.value.velocity.y * elapsed,
                           read.value.velocity.z * elapsed);
  body.angles = add_fround(read.value.angles, angular_velocity.x * elapsed, angular_velocity.y * elapsed,
                           angular_velocity.z * elapsed);
  qa_shared_body_table_write(context->bodies, actor, body);
  qa_shared_body_table_link(context->bodies, actor, NULL);
}

void qa_app_sim_execute_quake_c_physics(const qa_app_sim_quakec_actor_source *source, qa_owned_actor actor,
                                       const qa_app_sim_actor_execution_frame *context) {
  double move = 0;
  if (!source->read_move_type(source->ctx, actor.id, &move)) return;
  if (move == 7) {
    qa_q1_pusher_input input = {actor.id, context->elapsed, "translate"};
    (void)qa_step_q1_pusher(input, source->pusher_services(source->ctx));
    return;
  }
  if (move == 4) {
    (void)qa_app_sim_shared_physics_step(context->physics, actor, context->elapsed);
    if (qa_session_actor_registry_is_live(context->actors, actor.id)) source->run_think(source->ctx, actor, context->frame);
    if (qa_session_actor_registry_is_live(context->actors, actor.id)) source->check_water_transition(source->ctx, actor);
    return;
  }
  if (move != 0 && move != 5 && move != 6 && move != 8 && move != 9 && move != 10)
    qa_raise("Error", 0, 0, "Unsupported nonclient QuakeC movetype %s", QA_JS_NUMBER(move));
  source->run_think(source->ctx, actor, context->frame);
  if (!qa_session_actor_registry_is_live(context->actors, actor.id) || move == 0) return;
  if (move == 8) {
    qa_body_state_result body = qa_shared_body_table_read(context->bodies, actor.id);
    qa_q2_motion motion;
    if (body.has_value && source->motion(source->ctx, actor, body.value, &motion))
      move_q1_noclip(actor, motion.angular_velocity, context);
  } else (void)qa_app_sim_shared_physics_step(context->physics, actor, context->elapsed);
}

static void execute_quake_c_actor(const qa_app_sim_actor_execution *entry, const qa_app_sim_actor_execution_frame *context) {
  const qa_app_sim_quakec_actor_source *source = entry->quakec;
  qa_owned_actor actor = entry->actor;
  if (source->is_reserved_client(source->ctx, actor.id)) return;
  if (!source->run_actor_once(source->ctx, actor.id, context->frame)) return;
  qa_app_sim_execute_quake_c_physics(source, actor, context);
}

static void execute_q1_actor(const qa_app_sim_actor_execution *entry, const qa_app_sim_actor_execution_frame *context) {
  qa_q1_actor *entity = entry->q1_entity;
  qa_q1_services *services = entry->q1_services;
  qa_owned_actor actor = entity->actor;
  bool pusher = same(entity->movement, "push"), step = same(entity->movement, "step");
  qa_frame_context frame = context->frame;
  frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
  if (!pusher && !step) (void)qa_frame_scheduler_run(context->scheduler, actor.id, frame, QA_THINK_DURING_PHYSICS);
  if (qa_session_actor_registry_is_live(context->actors, actor.id)) {
    if (same(entity->movement, "noclip")) {
      move_q1_noclip(actor, entity->angular_velocity, context);
    } else if (step) {
      (void)qa_app_sim_shared_physics_step(context->physics, actor, context->elapsed);
      qa_body_state_result body = qa_shared_body_table_read(context->bodies, actor.id);
      int32_t grounded = !body.has_value || body.value.ground == NULL ? 0 : 512;
      entity->movement_flags = (double)((qa_js_to_int32(entity->movement_flags) & ~512) | grounded);
    } else if (!entity->has_move && (same(entity->movement, "toss") || same(entity->movement, "bounce")
                                     || same(entity->movement, "fly") || same(entity->movement, "flymissile"))) {
      qa_q1_apply_projectile_behavior(services, actor, context->time_seconds);
      (void)qa_app_sim_shared_physics_step(context->physics, actor, context->elapsed);
    } else qa_q1_services_physics_entity(services, actor, context->time_seconds, context->elapsed);
  }
  if (step && qa_session_actor_registry_is_live(context->actors, actor.id))
    (void)qa_frame_scheduler_run(context->scheduler, actor.id, frame, QA_THINK_DURING_PHYSICS);
  if (step && qa_session_actor_registry_is_live(context->actors, actor.id)) qa_q1_check_water_transition(services, entity);
}

typedef struct q2_turn {
  const qa_app_sim_actor_execution *entry;
  const qa_app_sim_actor_execution_frame *context;
  qa_frame_context frame;
} q2_turn;

static qa_q2_entity *q2_entity(qa_q2_entity_services *services, qa_actor_id actor) {
  qa_q2_game_services *game = qa_q2_entity_services_game(services);
  return game->entity(game->ctx, &actor);
}

static void q2_push_turn(q2_turn *turn, qa_owned_actor actor) {
  const qa_app_sim_actor_execution_frame *context = turn->context;
  qa_q2_entity_services *services = turn->entry->q2_services;
  qa_q2_game_services *game = qa_q2_entity_services_game(services);
  size_t count = 0;
  const qa_owned_actor *shared = game->push_team(game->ctx, actor.id, &count);
  qa_owned_actor *team = count == 0 ? NULL : malloc(count * sizeof *team);
  if (count != 0 && team == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(team, shared, count * sizeof *team);
  if (count != 0 && !qa_same_actor(team[0].id, actor.id)) { free(team); return; }
  for (size_t i = 0; i < count; i++) qa_app_sim_actor_visited_add(context->visited, team[i]);
  qa_actor_id obstacle;
  bool blocked = qa_app_sim_shared_physics_push_team(context->physics, team, count, context->elapsed, &obstacle);
  if (blocked) {
    for (size_t i = 0; i < count; i++) {
      qa_scheduled_think_result pending = qa_frame_scheduler_pending(context->scheduler, team[i].id);
      if (pending.has_value && qa_session_actor_registry_is_live(context->actors, team[i].id)) {
        qa_think_timing timing = pending.value.timing;
        timing.due.value = pending.value.timing.due.value
            + (pending.value.timing.due.kind == QA_SOURCE_TIME_SECONDS ? context->elapsed : context->elapsed * 1000);
        qa_frame_scheduler_schedule(context->scheduler, team[i], pending.value.callback, timing);
        qa_q2_entity *part = q2_entity(services, team[i].id);
        if (part != NULL) { part->has_next_think = true; part->next_think = seconds_of(timing.due); }
      }
    }
  } else {
    for (size_t i = 0; i < count; i++)
      if (qa_session_actor_registry_is_live(context->actors, team[i].id))
        (void)qa_frame_scheduler_run(context->scheduler, team[i].id, turn->frame, QA_THINK_DURING_PHYSICS);
  }
  for (size_t i = 0; i < count; i++)
    if (qa_session_actor_registry_is_live(context->actors, team[i].id))
      qa_q2_entity_services_post_physics(services, team[i].id);
  free(team);
}

static void q2_turn_run(void *ctx) {
  q2_turn *turn = ctx;
  const qa_app_sim_actor_execution_frame *context = turn->context;
  qa_q2_entity_services *services = turn->entry->q2_services;
  qa_q2_entity *entity = turn->entry->q2_entity;
  qa_owned_actor actor = entity->actor;
  qa_q2_entity_services_pre_physics(services, actor.id);
  if (!qa_session_actor_registry_is_live(context->actors, actor.id)) return;
  if (same(entity->motion, "push") || same(entity->motion, "stop")) { q2_push_turn(turn, actor); return; }
  bool after = same(entity->motion, "step");
  if (!after) (void)qa_frame_scheduler_run(context->scheduler, actor.id, turn->frame, QA_THINK_DURING_PHYSICS);
  if (qa_session_actor_registry_is_live(context->actors, actor.id))
    qa_q2_entity_services_apply_projectile_behavior(services, actor.id, context->time_seconds);
  const char *moved = qa_session_actor_registry_is_live(context->actors, actor.id)
      ? qa_app_sim_shared_physics_step(context->physics, actor, context->elapsed) : NULL;
  if (qa_session_actor_registry_is_live(context->actors, actor.id)
      && ((same(entity->motion, "new-toss") && same(moved, "moved")) || same(entity->motion, "toss")
          || same(entity->motion, "bounce") || same(entity->motion, "fly") || same(entity->motion, "fly-missile")
          || same(entity->motion, "wall-bounce"))) {
    qa_body_state_result body = qa_shared_body_table_read(context->bodies, actor.id);
    bool has_next = entity->has_team_chain;
    qa_actor_id next = entity->team_chain;
    while (body.has_value && has_next) {
      qa_q2_entity *follower = q2_entity(services, next);
      if (follower == NULL) break;
      qa_body_state_result current = qa_shared_body_table_read(context->bodies, follower->actor.id);
      if (current.has_value) {
        qa_body_state state = current.value;
        qa_actor_id ground;
        if (state.ground != NULL) { ground = *state.ground; state.ground = &ground; }
        state.origin = body.value.origin;
        qa_shared_body_table_write(context->bodies, follower->actor, state);
        qa_shared_body_table_link(context->bodies, follower->actor, NULL);
      }
      has_next = follower->has_team_chain;
      next = follower->team_chain;
    }
  }
  if (after && qa_session_actor_registry_is_live(context->actors, actor.id))
    (void)qa_frame_scheduler_run(context->scheduler, actor.id, turn->frame, QA_THINK_DURING_PHYSICS);
  if (qa_session_actor_registry_is_live(context->actors, actor.id)) qa_q2_entity_services_post_physics(services, actor.id);
}

static void execute_q2_actor(const qa_app_sim_actor_execution *entry, const qa_app_sim_actor_execution_frame *context) {
  q2_turn turn;
  turn.entry = entry;
  turn.context = context;
  turn.frame = context->frame;
  turn.frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
  qa_q2_entity_services_run_actor(entry->q2_services, entry->q2_entity->actor.id, q2_turn_run, &turn);
}

void qa_app_sim_execute_actor(const qa_app_sim_actor_execution *entry, const qa_app_sim_actor_execution_frame *context) {
  if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) execute_quake_c_actor(entry, context);
  else if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) execute_q1_actor(entry, context);
  else if (entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) execute_q2_actor(entry, context);
  else qa_range_error("executeActor requires a QuakeC, Q1 or Q2 actor");
}
