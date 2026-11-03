/* Port of src/content/q1/missionpacks/world/rogue-hazards.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_hazards.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* TS `x || y` / `x ||= y` */
static int32_t bits(double value) { return qa_js_to_int32(value); }
static double random_of(qa_q1_services *game) { return game->host.random(game->host.ctx); }
static const qa_actor_id *activator_of(const qa_q1_actor *entity) { return entity->has_activator ? &entity->activator : NULL; }
static qa_q1_actor *reference_entity(qa_q1_services *game, const qa_q1_actor *entity, const char *key) {
  qa_actor_id actor;
  return qa_q1_actor_reference_get(entity, key, &actor) ? qa_q1_get_entity(game, &actor) : NULL;
}
static void emit_sound(qa_q1_services *game, qa_actor_id actor, const char *path, qa_q1_sound_channel channel,
                       double volume, double attenuation) {
  qa_q1_event event = {0};
  event.kind = "sound"; event.has_actor = true; event.actor = actor; event.path = path;
  event.sound_channel = channel; event.volume = volume; event.attenuation = attenuation;
  qa_q1_emit(game, event);
}

void qa_q1mp_rogue_earthquake(qa_q1_services *game, qa_actor_id actor, double intensity) {
  qa_owned_actor owner;
  const bool resolved = qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner);
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (!resolved || !body.has_value || body.value.ground == NULL) return;
  qa_vec3 shake;
  shake.x = random_of(game) * intensity * 2 - intensity;
  shake.y = random_of(game) * intensity * 2 - intensity;
  shake.z = random_of(game) * intensity * 2 - intensity;
  qa_body_state next = body.value;
  next.velocity = qa_q1_vadd(body.value.velocity, shake);
  qa_shared_body_table_write(game->host.bodies, owner, next);
}
static void quake_stop(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (game->world != NULL) qa_q1mp_world_number(game->world, "rogue:earthquake_active", 0);
  const double delay = (bits(entity->spawnflags) & 1) != 0 ? random_of(game) * entity->wait : entity->wait;
  qa_q1mp_world_later(game, entity, delay, "rogue:quake_start");
}
static void quake_rumble(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  if (entity->attack_finished < game->time) { quake_stop(ctx, game, entity); return; }
  qa_q1_sound(game, entity->actor.id, "equake/rumble.wav", "voice", 0, 1);
  qa_q1mp_world_later(game, entity, 1, "rogue:quake_rumble");
}
static void quake_start(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  if (game->world != NULL) qa_q1mp_world_number(game->world, "rogue:earthquake_active", 1);
  entity->attack_finished = game->time + ((bits(entity->spawnflags) & 1) != 0 ? random_of(game) * entity->delay : entity->delay);
  quake_rumble(ctx, game, entity);
}
static void saw_start(qa_q1_services *game, qa_q1_actor *entity) {
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:saw_touch");
  entity->use = (qa_q1_bound_use){0};
  qa_q1_actor *found = qa_q1mp_find_first(game, entity->target);
  const qa_actor_id *target = found != NULL ? &found->actor.id : NULL;
  if (!qa_q1_actor_reference_set(entity, "goalentity", target) || !qa_q1_actor_reference_set(entity, "movetarget", target))
    qa_range_error("Out of memory");
  qa_q1mp_world_later(game, entity, 0.1, entity->target[0] == '\0' ? "rogue:saw_stand" : "rogue:saw_fly");
}
static void saw_start_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; saw_start(game, entity); }
static void saw_start_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; saw_start(game, entity);
}
static void trail_fire(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (!qa_q1mp_streq(entity->classname, "ltrail_end")) {
    qa_q1_sound(game, entity->actor.id, "weapons/lhit.wav", "voice", 1, 1);
    qa_q1_actor *target = qa_q1mp_find_first(game, entity->target);
    if (target == NULL) target = game->world;
    if (target == NULL) qa_range_error("Lightning trail requires worldspawn");
    const qa_vec3 start = qa_q1_get_body(game, entity).origin, end = qa_q1_get_body(game, target).origin;
    qa_q1_event beam = {0};
    beam.kind = "beam"; beam.beam_style = "lightning2"; beam.has_actor = true; beam.actor = entity->actor.id;
    beam.start = start; beam.end = end;
    game->host.emit(game->host.ctx, beam);
    const qa_vec3 side = {-(end.y - start.y) * 16, -(end.y - start.y) * 16, 0};
    const qa_vec3 offsets[3] = {QA_Q1_ZERO, side, qa_q1_vscale(side, -1)};
    qa_actor_id hit[3]; size_t hit_count = 0;
    for (size_t i = 0; i < 3; i++) {
      const qa_q1_trace trace = qa_q1mp_trace(game, qa_q1_vadd(start, offsets[i]), qa_q1_vadd(end, offsets[i]), &entity->actor.id, true);
      if (!trace.has_actor) continue;
      bool seen = false;
      for (size_t h = 0; h < hit_count; h++) if (qa_same_actor(hit[h], trace.actor)) seen = true;
      if (seen) continue;
      hit[hit_count++] = trace.actor;
      const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, trace.actor);
      if (combat.has_value && combat.value.can_take_damage) {
        qa_q1_event particles = {0};
        particles.kind = "particles"; particles.has_origin = true; particles.origin = trace.end;
        particles.direction = QA_Q1MP_V3(0, 0, 100); particles.color = 225;
        particles.count = qa_q1_actor_number(entity, "currentammo") * 4;
        game->host.emit(game->host.ctx, particles);
        (void)qa_q1_damage(game, trace.actor, entity->actor.id, &entity->actor.id, qa_q1_actor_number(entity, "currentammo"), NULL, NULL, NULL, NULL);
      }
    }
  }
  if (qa_q1_actor_number(entity, "items") < game->time) qa_q1mp_world_later(game, entity, qa_q1_actor_number(entity, "frags"), "rogue:ltrail_chain");
  else qa_q1mp_world_later(game, entity, 0.05, "rogue:ltrail_fire");
}
static void earthquake_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (falsy(entity->delay)) entity->delay = 20;
  if (falsy(entity->wait)) entity->wait = 60;
  if (qa_q1_actor_number(entity, "weapon") == 0) qa_q1mp_world_number(entity, "weapon", 40);
  if (game->world != NULL) {
    qa_q1mp_world_number(game->world, "rogue:earthquake_active", 0);
    qa_q1mp_world_number(game->world, "rogue:earthquake_intensity", qa_q1_actor_number(entity, "weapon") * 0.5);
  }
  qa_q1_set_bounds(game, entity, QA_Q1_POINT);
  qa_q1mp_world_later(game, entity, 1, "rogue:quake_stop");
}
static void field_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator;
  entity->delay = entity->delay == 0 ? 1 : 0;
}
static void field_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                        const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (entity->delay == 0) return;
  if (entity->attack_finished < game->time) {
    qa_q1_sound(game, entity->actor.id, "equake/rumble.wav", "voice", 1, 1);
    entity->attack_finished = game->time + 1;
  }
  if (qa_q1_is_player(game, &other)) qa_q1mp_rogue_earthquake(game, other, qa_q1_actor_number(entity, "weapon"));
}
static void trigger_earthquake_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const double weapon = qa_q1_actor_number(entity, "weapon");
  qa_q1mp_world_number(entity, "weapon", (falsy(weapon) ? 40 : weapon) * 0.5);
  entity->delay = entity->targetname[0] == '\0' ? 1 : 0;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:earthquake_field");
  if (entity->targetname[0] != '\0') entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:earthquake_field");
  qa_q1mp_world_trigger(game, entity);
}
static void earthquake_kill(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                            const qa_touch_surface *surface) {
  (void)ctx; (void)entity; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  qa_q1_actor *quake = NULL;
  for (size_t i = 0; i < game->entity_count && quake == NULL; i++)
    if (qa_q1mp_streq(game->entities[i]->classname, "earthquake")) quake = game->entities[i];
  if (quake != NULL) {
    if (game->world != NULL) qa_q1mp_world_number(game->world, "rogue:earthquake_active", 0);
    qa_q1_remove(game, quake);
  }
}
static void earthquake_kill_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "rogue:earthquake_kill");
  qa_q1mp_world_trigger(game, entity);
}
static void saw_move(qa_q1_services *game, qa_q1_actor *entity, bool flying) {
  entity->frame = flying ? 1 : 0;
  if (qa_q1_actor_number(entity, "pain_finished") < game->time) {
    emit_sound(game, entity->actor.id, "buzz/buzz1.wav", "voice", 0.2, 1);
    qa_q1mp_world_number(entity, "pain_finished", game->time + 1);
  }
  const qa_q1_body body = qa_q1_get_body(game, entity);
  if (flying) {
    qa_q1_actor *goal = reference_entity(game, entity, "goalentity");
    if (goal == NULL) goal = game->world;
    if (goal == NULL) qa_range_error("Buzzsaw requires worldspawn");
    qa_q1_set_origin(game, entity, qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(qa_q1_get_body(game, goal).origin, body.origin)), entity->speed)));
  }
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = body.angles; patch.angles.x = body.angles.x - 60;
  qa_q1_set_body(game, entity, &patch);
  entity->angular_velocity.x = 60;
  qa_q1mp_world_later(game, entity, 0.1, flying ? "rogue:saw_fly" : "rogue:saw_stand");
}
static void saw_fly(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; saw_move(game, entity, true); }
static void saw_stand(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; saw_move(game, entity, false); }
static void saw_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                      const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_actor *target = qa_q1_get_entity(game, &other);
  if (!qa_q1_is_player(game, &other) && (bits(target != NULL ? target->movement_flags : 0) & 32) == 0) return;
  if (entity->attack_finished < game->time) {
    qa_q1_sound(game, entity->actor.id, "buzz/buzz.wav", "weapon", 1, 1);
    entity->attack_finished = game->time + 2;
  }
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, qa_q1_actor_number(entity, "currentammo"), NULL, NULL, NULL, NULL);
  qa_q1_actor *goal = reference_entity(game, entity, "goalentity");
  if (goal == NULL) goal = game->world;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  qa_owned_actor owner;
  const bool resolved = qa_session_actor_registry_resolve_owned(game->host.actors, other, &owner);
  if (goal == NULL || !body.has_value || !resolved) return;
  qa_vec3 a = qa_q1_get_body(game, goal).origin;
  qa_vec3 b = qa_q1_get_body(game, entity).origin;
  const qa_vec3 direction = qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(a, b)), 200);
  qa_q1_effect(game, "meat-spray", qa_q1_get_body(game, entity).origin, &other, 1);
  qa_body_state next = body.value;
  next.velocity = QA_Q1MP_V3(direction.x, direction.y, 200);
  qa_shared_body_table_write(game->host.bodies, owner, next);
}
static void buzzsaw_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_set_string(&entity->model, "progs/buzzsaw.mdl");
  qa_q1_actor_set_damageable(entity, false); entity->solid = "trigger"; entity->movement = "fly";
  const double yaw = qa_q1_get_body(game, entity).angles.y;
  if (yaw == 0 || yaw == 180) qa_q1_set_bounds(game, entity, (qa_bounds){{-18, 0, -18}, {18, 0, 18}});
  else if (yaw == 90 || yaw == 270) qa_q1_set_bounds(game, entity, (qa_bounds){{0, -18, -18}, {0, 18, 18}});
  else qa_range_error("Buzzsaw: Not at 90 degree angle!");
  if (falsy(entity->speed)) entity->speed = 10;
  if (qa_q1_actor_number(entity, "currentammo") == 0) qa_q1mp_world_number(entity, "currentammo", 10);
  qa_q1mp_world_number(entity, "pain_finished", game->time + random_of(game) * 2);
  if (entity->targetname[0] == '\0') { qa_q1mp_world_later(game, entity, 0.2, "rogue:saw_start"); return; }
  entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:saw_start");
}
static void ltrail_chain(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_use_targets(game, entity, activator_of(entity));
  entity->think = qa_q1_callback_registry_action(game->named, entity, "SUB_Null");
}
static void ltrail_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator != NULL) entity->activator = *activator;
  if ((bits(entity->spawnflags) & 1) != 0) {
    qa_q1_actor *source = qa_q1_get_entity(game, other);
    if (source == NULL || !qa_q1mp_streq(source->classname, "ltrail_end")) {
      if ((bits(entity->spawnflags) & 2) != 0) { entity->spawnflags -= 2; return; }
      entity->spawnflags += 2;
    } else if ((bits(entity->spawnflags) & 2) == 0) return;
  }
  if (qa_q1mp_streq(entity->classname, "ltrail_end")) { qa_q1mp_world_later(game, entity, qa_q1_actor_number(entity, "frags"), "rogue:ltrail_chain"); return; }
  qa_q1mp_world_number(entity, "items", game->time + qa_q1_actor_number(entity, "weapon"));
  trail_fire(ctx, game, entity);
  if (qa_q1mp_streq(entity->classname, "ltrail_start")) qa_q1mp_world_number(entity, "ltrailLastUsed", game->time);
}
static void ltrail_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const char *classname = ctx;
  entity->movement = "none"; entity->solid = "bbox";
  entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:ltrail_use");
  if (qa_q1_actor_number(entity, "currentammo") == 0) qa_q1mp_world_number(entity, "currentammo", 25);
  if (qa_q1_actor_number(entity, "weapon") == 0) qa_q1mp_world_number(entity, "weapon", 0.3);
  if (qa_q1_actor_number(entity, "frags") == 0) qa_q1mp_world_number(entity, "frags", 0.3);
  if (qa_q1mp_streq(classname, "ltrail_start")) {
    qa_q1mp_world_number(entity, "ltrailLastUsed", game->time);
    if ((bits(entity->spawnflags) & 2) != 0) {
      qa_q1mp_world_number(entity, "items", game->time + 99999999);
      qa_q1mp_world_later(game, entity, 0.1, "rogue:ltrail_fire");
    }
  }
}
void qa_q1mp_register_rogue_hazards(qa_q1_services *game) {
  qa_q1_callback_registry_register(game->named, "rogue:quake_stop", (qa_q1_callback_handlers){.action = quake_stop});
  qa_q1_callback_registry_register(game->named, "rogue:quake_rumble", (qa_q1_callback_handlers){.action = quake_rumble});
  qa_q1_callback_registry_register(game->named, "rogue:quake_start", (qa_q1_callback_handlers){.action = quake_start});
  qa_q1_register_spawn(game, "earthquake", earthquake_spawn, NULL);
  qa_q1_callback_registry_register(game->named, "rogue:earthquake_field", (qa_q1_callback_handlers){.use = field_use, .touch = field_touch});
  qa_q1_register_spawn(game, "trigger_earthquake", trigger_earthquake_spawn, NULL);
  qa_q1_callback_registry_register(game->named, "rogue:earthquake_kill", (qa_q1_callback_handlers){.touch = earthquake_kill});
  qa_q1_register_spawn(game, "trigger_earthquake_kill", earthquake_kill_spawn, NULL);
  qa_q1_callback_registry_register(game->named, "rogue:saw_stand", (qa_q1_callback_handlers){.action = saw_stand});
  qa_q1_callback_registry_register(game->named, "rogue:saw_fly", (qa_q1_callback_handlers){.action = saw_fly});
  qa_q1_callback_registry_register(game->named, "rogue:saw_start", (qa_q1_callback_handlers){.action = saw_start_action, .use = saw_start_use});
  qa_q1_callback_registry_register(game->named, "rogue:saw_touch", (qa_q1_callback_handlers){.touch = saw_touch});
  qa_q1_register_spawn(game, "buzzsaw", buzzsaw_spawn, NULL);
  qa_q1_callback_registry_register(game->named, "rogue:ltrail_chain", (qa_q1_callback_handlers){.action = ltrail_chain});
  qa_q1_callback_registry_register(game->named, "rogue:ltrail_fire", (qa_q1_callback_handlers){.action = trail_fire});
  qa_q1_callback_registry_register(game->named, "rogue:ltrail_use", (qa_q1_callback_handlers){.use = ltrail_use});
  static const char *const TRAILS[] = {"ltrail_start", "ltrail_relay", "ltrail_end"};
  for (size_t i = 0; i < 3; i++) qa_q1_register_spawn(game, TRAILS[i], ltrail_spawn, (void *)TRAILS[i]);
}
