/* Port of src/content/q1/missionpacks/world/hipnotic-rotate.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_rotate.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/rotate_targets.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* JS `||` on a number */
static bool flag(double spawnflags, double bit) { return qa_js_bit_and(spawnflags, bit) != 0; }
static void set_angles(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 angles) {
  qa_q1_body_patch patch = {0}; patch.has_angles = true; patch.angles = angles; qa_q1_set_body(game, entity, &patch);
}
static void set_velocity(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 velocity) {
  qa_q1_body_patch patch = {0}; patch.has_velocity = true; patch.velocity = velocity; qa_q1_set_body(game, entity, &patch);
}
static qa_q1_actor *goal_of(qa_q1_services *game, qa_q1_actor *entity) {
  qa_actor_id goal;
  return qa_q1_actor_reference_get(entity, "goalentity", &goal) ? qa_q1_get_entity(game, &goal) : NULL;
}
/* g.named.action(e, name)() */
static void run_named(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, name));
}

static void continuous_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  double elapsed = game->time - qa_q1_actor_number(entity, "ltime");
  qa_q1mp_world_number(entity, "ltime", game->time);
  const double state = qa_q1_actor_number(entity, "rotate_state");
  if (state == 2) { entity->count = qa_js_min(1, entity->count + qa_q1_actor_number(entity, "cnt") * elapsed); elapsed *= entity->count; }
  else if (state == 3) {
    entity->count -= qa_q1_actor_number(entity, "cnt") * elapsed;
    if (entity->count < 0) {
      qa_q1mp_rotate_targets_final(game, entity); qa_q1mp_world_number(entity, "rotate_state", 1);
      qa_q1_cancel(game, entity); return;
    }
    elapsed *= entity->count;
  }
  qa_vec3 a = qa_q1_get_body(game, entity).angles;
  qa_vec3 b = qa_q1_vscale(qa_q1_actor_vector(entity, "rotate"), elapsed);
  set_angles(game, entity, qa_q1mp_normalize_angles(qa_q1_vadd(a, b)));
  qa_q1mp_rotate_targets(game, entity);
  qa_q1mp_world_later(game, entity, 0.02, "hip:rotate_entity");
}
static void continuous_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  entity->frame = 1 - entity->frame;
  const double state = qa_q1_actor_number(entity, "rotate_state");
  if (state == 0) {
    if (flag(entity->spawnflags, 1)) {
      if (entity->speed != 0) { entity->count = 1; qa_q1mp_world_number(entity, "rotate_state", 3); }
      else { qa_q1mp_world_number(entity, "rotate_state", 1); qa_q1_cancel(game, entity); }
    }
  } else if (state == 1) {
    qa_q1mp_world_number(entity, "ltime", game->time); entity->count = 0;
    qa_q1mp_world_number(entity, "rotate_state", entity->speed != 0 ? 2 : 0);
    qa_q1mp_world_later(game, entity, 0.02, "hip:rotate_entity");
  } else if (state == 2) { if (flag(entity->spawnflags, 1)) qa_q1mp_world_number(entity, "rotate_state", 3); }
  else qa_q1mp_world_number(entity, "rotate_state", 2);
}
static void reverse_door(qa_q1_services *game, qa_q1_actor *entity) {
  entity->frame = 1 - entity->frame;
  const bool closing = qa_q1_actor_number(entity, "rotate_state") == 7;
  const qa_vec3 start = closing ? entity->dest1 : entity->dest2, destination = closing ? entity->dest2 : entity->dest1;
  qa_q1mp_world_vector(entity, "dest", destination);
  qa_q1mp_world_number(entity, "rotate_state", closing ? 6 : 7);
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise2"), "voice", 1, 1);
  qa_q1mp_world_vector(entity, "rotate", qa_q1_vscale(qa_q1_vsub(destination, start), 1 / entity->speed));
  qa_q1mp_world_number(entity, "endtime", game->time + entity->speed - (qa_q1_actor_number(entity, "endtime") - game->time));
  qa_q1mp_world_number(entity, "ltime", game->time);
  qa_q1mp_world_later(game, entity, 0.02, "hip:rotate_door");
}
static void reverse_group(qa_q1_services *game, qa_q1_actor *entity) {
  const char *group = qa_q1_actor_text(entity, "group");
  if (group[0] == '\0') { reverse_door(game, entity); return; }
  size_t count; qa_q1_actor **entities = qa_q1mp_entities(game, &count);
  size_t members = 0;
  for (size_t i = 0; i < count; i++)
    if (qa_q1mp_streq(qa_q1_actor_text(entities[i], "group"), group) && qa_q1_live(game, entities[i])) entities[members++] = entities[i];
  for (size_t i = 0; i < members; i++) reverse_door(game, entities[i]);
  free(entities);
}
static void train_stop(qa_q1_services *game, qa_q1_actor *entity, bool wait) {
  qa_q1_actor *goal = goal_of(game, entity);
  if (goal == NULL) qa_range_error("rotate_train: missing goal");
  qa_q1mp_world_number(entity, "rotate_state", wait ? 0 : 2);
  const char *noise = qa_q1_actor_text(goal, "noise");
  qa_q1_sound(game, entity->actor.id, noise[0] != '\0' ? noise : qa_q1_actor_text(entity, "noise"), "voice", 1, 1);
  if (flag(goal->spawnflags, 2)) { qa_q1mp_world_vector(entity, "rotate", QA_Q1_ZERO); set_angles(game, entity, qa_q1_actor_vector(entity, "finalangle")); }
  if (flag(goal->spawnflags, 8)) qa_q1mp_world_vector(entity, "rotate", QA_Q1_ZERO);
  if (wait) qa_q1mp_world_number(entity, "endtime", qa_q1_actor_number(entity, "ltime") + goal->wait);
  else entity->damage = 0;
  qa_q1mp_field_text(entity, "think1", "hip:rotate_train_next");
}
static void train_next(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_world_number(entity, "rotate_state", 4);
  qa_q1_actor *current = goal_of(game, entity);
  qa_q1_actor *target = qa_q1mp_find_first(game, qa_q1_actor_text(entity, "path"));
  if (current == NULL || target == NULL || !qa_q1mp_streq(target->classname, "path_rotate"))
    qa_range_error("rotate_train_next: next target is not path_rotate");
  if (qa_q1_actor_text(current, "noise1")[0] != '\0') qa_q1mp_field_text(entity, "noise1", qa_q1_actor_text(current, "noise1"));
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise1"), "voice", 1, 1);
  if (!qa_q1_actor_reference_set(entity, "goalentity", &target->actor.id)) qa_range_error("Out of memory");
  qa_q1mp_field_text(entity, "path", target->target);
  if (target->target[0] == '\0') qa_range_error("rotate_train_next: no next target");
  qa_q1mp_field_text(entity, "think1", flag(target->spawnflags, 4) ? "hip:rotate_train_stop" : target->wait != 0 ? "hip:rotate_train_wait" : "hip:rotate_train_next");
  if (qa_q1_actor_text(current, "event")[0] != '\0') qa_q1mp_world_target_event(game, entity, qa_q1_actor_text(current, "event"), current->message);
  if (flag(current->spawnflags, 2)) { qa_q1mp_world_vector(entity, "rotate", QA_Q1_ZERO); set_angles(game, entity, qa_q1_actor_vector(entity, "finalangle")); }
  if (flag(current->spawnflags, 1)) qa_q1mp_world_vector(entity, "rotate", qa_q1_actor_vector(current, "rotate"));
  if (flag(current->spawnflags, 16)) entity->damage = current->damage;
  if (flag(current->spawnflags, 64)) qa_q1mp_damage_on_targets(game, entity, current->damage);
  const qa_q1_body target_body = qa_q1_get_body(game, target), body = qa_q1_get_body(game, entity);
  if (current->speed == -1) {
    qa_q1_set_origin(game, entity, target_body.origin);
    qa_q1mp_world_number(entity, "endtime", qa_q1_actor_number(entity, "ltime") + 0.01);
    qa_q1mp_set_target_origin(game, entity);
    if (flag(target->spawnflags, 2)) set_angles(game, entity, target_body.angles);
    qa_q1mp_world_number(entity, "duration", 1); qa_q1mp_world_number(entity, "cnt", game->time);
    entity->dest2 = QA_Q1_ZERO; entity->dest1 = target_body.origin;
    qa_q1mp_world_vector(entity, "finaldest", target_body.origin);
    return;
  }
  qa_q1mp_world_number(entity, "rotate_state", 1);
  qa_q1mp_world_vector(entity, "finaldest", target_body.origin);
  const qa_vec3 delta = qa_q1_vsub(target_body.origin, body.origin);
  const double distance = qa_q1_length(delta);
  if (distance == 0) {
    set_velocity(game, entity, QA_Q1_ZERO);
    qa_q1mp_world_number(entity, "endtime", qa_q1_actor_number(entity, "ltime") + 0.1);
    qa_q1mp_world_number(entity, "duration", 1); qa_q1mp_world_number(entity, "cnt", game->time);
    entity->dest2 = QA_Q1_ZERO; entity->dest1 = body.origin;
    return;
  }
  if (!flag(current->spawnflags, 32) && current->speed > 0) entity->speed = current->speed;
  if (!flag(current->spawnflags, 32) && entity->speed == 0) qa_range_error("rotate_train: no speed defined");
  const double travel = flag(current->spawnflags, 32) ? current->speed : distance / entity->speed;
  if (travel < 0.1) {
    qa_q1_body_patch patch = {0};
    patch.has_velocity = true; patch.velocity = QA_Q1_ZERO;
    if (flag(target->spawnflags, 2)) { patch.has_angles = true; patch.angles = target_body.angles; }
    qa_q1_set_body(game, entity, &patch);
    qa_q1mp_world_number(entity, "endtime", qa_q1_actor_number(entity, "ltime") + 0.1);
    return;
  }
  const double inverse = 1 / travel;
  if (flag(target->spawnflags, 2)) {
    qa_q1mp_world_vector(entity, "finalangle", qa_q1mp_normalize_angles(target_body.angles));
    qa_q1mp_world_vector(entity, "rotate", qa_q1_vscale(qa_q1_vsub(target_body.angles, qa_q1_get_body(game, entity).angles), inverse));
  }
  qa_q1mp_world_number(entity, "endtime", qa_q1_actor_number(entity, "ltime") + travel);
  set_velocity(game, entity, qa_q1_vscale(delta, inverse));
  qa_q1mp_world_number(entity, "duration", inverse); qa_q1mp_world_number(entity, "cnt", game->time);
  entity->dest2 = delta; entity->dest1 = body.origin;
}
static void info_rotate_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) { (void)ctx; qa_q1mp_world_later(game, e, 2, "SUB_Remove"); }
static void path_rotate_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) { (void)ctx; (void)game; (void)e; }
static void rotate_object_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) { (void)ctx; (void)game; e->solid = "none"; e->movement = "none"; }
static void rotate_first(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_link_rotate_targets(game, e);
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:rotate_entity");
  qa_q1mp_world_number(e, "rotate_state", flag(e->spawnflags, 2) ? 0 : 1);
  if (flag(e->spawnflags, 2)) { qa_q1mp_world_number(e, "ltime", game->time); qa_q1mp_world_later(game, e, 0.02, "hip:rotate_entity"); }
}
static void rotate_entity_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  e->solid = "none"; e->movement = "none";
  if (e->speed != 0) qa_q1mp_world_number(e, "cnt", 1 / e->speed);
  qa_q1mp_world_number(e, "ltime", game->time);
  qa_q1mp_world_later(game, e, 0.1, "hip:rotate_first");
}
static void rotate_door_think(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  qa_vec3 a;
  qa_vec3 b;
  (void)ctx;
  const double elapsed = game->time - qa_q1_actor_number(e, "ltime");
  qa_q1mp_world_number(e, "ltime", game->time);
  const bool moving = game->time < qa_q1_actor_number(e, "endtime");
  set_angles(game, e, moving ? (a = qa_q1_get_body(game, e).angles, b = qa_q1_vscale(qa_q1_actor_vector(e, "rotate"), elapsed), qa_q1_vadd(a, b)) : qa_q1_actor_vector(e, "dest"));
  qa_q1mp_rotate_targets(game, e);
  qa_q1mp_world_later(game, e, 0.01, moving ? "hip:rotate_door" : "hip:rotate_door_done");
}
static void rotate_door_use(void *ctx, qa_q1_services *game, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  const double state = qa_q1_actor_number(e, "rotate_state");
  if (state != 4 && state != 5) return;
  if (qa_q1_actor_number(e, "cnt") == 0) { qa_q1mp_world_number(e, "cnt", 1); qa_q1mp_link_rotate_targets(game, e); }
  e->frame = 1 - e->frame;
  const qa_vec3 destination = state == 4 ? e->dest2 : e->dest1, start = state == 4 ? e->dest1 : e->dest2;
  qa_q1mp_world_vector(e, "dest", destination);
  qa_q1mp_world_number(e, "rotate_state", state == 4 ? 6 : 7);
  qa_q1mp_world_vector(e, "rotate", qa_q1_vscale(qa_q1_vsub(destination, start), 1 / e->speed));
  qa_q1_sound(game, e->actor.id, qa_q1_actor_text(e, "noise2"), "voice", 1, 1);
  qa_q1mp_world_number(e, "endtime", game->time + e->speed);
  qa_q1mp_world_number(e, "ltime", game->time);
  qa_q1mp_world_later(game, e, 0.01, "hip:rotate_door");
}
static void rotate_door_done(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_number(e, "ltime", game->time);
  e->frame = 1 - e->frame;
  set_angles(game, e, qa_q1_actor_vector(e, "dest"));
  if (qa_q1_actor_number(e, "rotate_state") == 6) qa_q1mp_world_number(e, "rotate_state", 5);
  else if (flag(e->spawnflags, 1)) { reverse_group(game, e); return; }
  else qa_q1mp_world_number(e, "rotate_state", 4);
  qa_q1_sound(game, e->actor.id, qa_q1_actor_text(e, "noise3"), "voice", 1, 1);
  qa_q1mp_rotate_targets_final(game, e);
}
static void rotate_door_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  if (e->target[0] == '\0') qa_range_error("rotate_door without target");
  e->dest1 = QA_Q1_ZERO; e->dest2 = qa_q1_get_body(game, e).angles;
  set_angles(game, e, QA_Q1_ZERO);
  if (falsy(e->speed)) e->speed = 2;
  e->damage = e->damage == 0 ? 2 : qa_js_max(0, e->damage);
  qa_q1mp_world_number(e, "cnt", 0); qa_q1mp_world_number(e, "rotate_state", 4);
  if (falsy(e->sounds)) e->sounds = 1;
  qa_q1mp_field_text(e, "noise2", e->sounds == 2 ? "doors/airdoor1.wav" : e->sounds == 3 ? "doors/basesec1.wav" : "doors/winch2.wav");
  qa_q1mp_field_text(e, "noise3", e->sounds == 2 ? "doors/airdoor2.wav" : e->sounds == 3 ? "doors/basesec2.wav" : "doors/drclos4.wav");
  e->solid = "none"; e->movement = "none";
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:rotate_door");
}
static void movewall_think(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_number(e, "ltime", game->time);
  qa_q1mp_world_later(game, e, 0.02, "hip:movewall");
}
static void movewall_touch(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_actor *owner = qa_q1_get_entity(game, e->has_owner ? &e->owner : NULL);
  if (owner == NULL || game->time < owner->attack_finished) return;
  const double damage = falsy(e->damage) ? owner->damage : e->damage;
  if (damage != 0) {
    (void)qa_q1_damage(game, other, e->actor.id, &owner->actor.id, damage, NULL, "direct", "", NULL);
    owner->attack_finished = game->time + 0.5;
  }
}
static void movewall_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *e, qa_actor_id other) {
  (void)ctx;
  qa_q1_actor *owner = qa_q1_get_entity(game, e->has_owner ? &e->owner : NULL);
  if (owner == NULL || game->time < owner->attack_finished) return;
  owner->attack_finished = game->time + 0.5;
  if (qa_q1mp_streq(owner->classname, "func_rotate_door")) reverse_group(game, owner);
  const double damage = falsy(e->damage) ? owner->damage : e->damage;
  if (damage != 0) (void)qa_q1_damage(game, other, e->actor.id, &owner->actor.id, damage, NULL, "direct", "", NULL);
}
static void movewall_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  e->movement = "push"; e->solid = flag(e->spawnflags, 4) ? "none" : "bsp";
  set_angles(game, e, QA_Q1_ZERO);
  if (qa_q1mp_streq(e->solid, "bsp")) e->blocked = qa_q1_callback_registry_blocked(game->named, e, "hip:movewall");
  if (flag(e->spawnflags, 2)) e->touch = qa_q1_callback_registry_touch(game->named, e, "hip:movewall");
  if (!flag(e->spawnflags, 1)) qa_q1mp_set_string(&e->model, "");
  qa_q1mp_world_number(e, "ltime", game->time);
  qa_q1mp_world_later(game, e, 0.02, "hip:movewall");
}
static void train_wait(void *ctx, qa_q1_services *game, qa_q1_actor *e) { (void)ctx; train_stop(game, e, true); }
static void train_stop_action(void *ctx, qa_q1_services *game, qa_q1_actor *e) { (void)ctx; train_stop(game, e, false); }
static void train_find(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_number(e, "rotate_state", 3);
  qa_q1mp_link_rotate_targets(game, e);
  qa_q1_actor *target = qa_q1mp_find_first(game, qa_q1_actor_text(e, "path"));
  if (target == NULL || !qa_q1mp_streq(target->classname, "path_rotate")) qa_range_error("rotate_train_find: next target is not path_rotate");
  if (!qa_q1_actor_reference_set(e, "goalentity", &target->actor.id)) qa_range_error("Out of memory");
  if (flag(target->spawnflags, 2)) {
    set_angles(game, e, qa_q1_get_body(game, target).angles);
    qa_q1mp_world_vector(e, "finalangle", qa_q1mp_normalize_angles(qa_q1_get_body(game, target).angles));
  }
  qa_q1mp_field_text(e, "path", target->target);
  qa_q1_set_origin(game, e, qa_q1_get_body(game, target).origin);
  qa_q1mp_set_target_origin(game, e);
  qa_q1mp_rotate_targets_final(game, e);
  qa_q1mp_field_text(e, "think1", "hip:rotate_train_next");
  qa_q1mp_world_number(e, "endtime", e->targetname[0] == '\0' ? qa_q1_actor_number(e, "ltime") + 0.1 : 0);
  qa_q1mp_world_number(e, "duration", 1); qa_q1mp_world_number(e, "cnt", game->time);
  e->dest2 = QA_Q1_ZERO; e->dest1 = qa_q1_get_body(game, e).origin;
}
static void rotate_train_think(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  double left;
  double right;
  (void)ctx;
  const double elapsed = game->time - qa_q1_actor_number(e, "ltime");
  qa_q1mp_world_number(e, "ltime", game->time);
  if (qa_q1_actor_number(e, "endtime") != 0 && game->time >= qa_q1_actor_number(e, "endtime")) {
    qa_q1mp_world_number(e, "endtime", 0);
    if (qa_q1_actor_number(e, "rotate_state") == 1) {
      qa_q1_body_patch patch = {0};
      patch.has_origin = true; patch.origin = qa_q1_actor_vector(e, "finaldest");
      patch.has_velocity = true; patch.velocity = QA_Q1_ZERO;
      qa_q1_set_body(game, e, &patch);
    }
    if (qa_q1_actor_text(e, "think1")[0] != '\0') run_named(game, e, qa_q1_actor_text(e, "think1"));
  } else qa_q1_set_origin(game, e, qa_q1_vadd(e->dest1, qa_q1_vscale(e->dest2, qa_js_min(1, (left = (game->time - qa_q1_actor_number(e, "cnt")), right = qa_q1_actor_number(e, "duration"), left * right)))));
  qa_vec3 a = qa_q1_get_body(game, e).angles;
  qa_vec3 b = qa_q1_vscale(qa_q1_actor_vector(e, "rotate"), elapsed);
  set_angles(game, e, qa_q1mp_normalize_angles(qa_q1_vadd(a, b)));
  qa_q1mp_rotate_targets(game, e);
  qa_q1mp_world_later(game, e, 0.02, "hip:rotate_train");
}
static void rotate_train_use(void *ctx, qa_q1_services *game, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  const qa_vec3 velocity = qa_q1_get_body(game, e).velocity;
  if (qa_q1mp_streq(qa_q1_actor_text(e, "think1"), "hip:rotate_train_find") || velocity.x != 0 || velocity.y != 0 || velocity.z != 0) return;
  if (qa_q1_actor_text(e, "think1")[0] != '\0') run_named(game, e, qa_q1_actor_text(e, "think1"));
}
static void rotate_train_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  if (e->target[0] == '\0') qa_range_error("rotate_train without target");
  if (falsy(e->speed)) e->speed = 100;
  e->solid = "none"; e->movement = "step";
  if (qa_q1_actor_text(e, "noise")[0] == '\0') qa_q1mp_field_text(e, "noise", e->sounds == 1 ? "plats/train2.wav" : "misc/null.wav");
  if (qa_q1_actor_text(e, "noise1")[0] == '\0') qa_q1mp_field_text(e, "noise1", e->sounds == 1 ? "plats/train1.wav" : "misc/null.wav");
  e->use = qa_q1_callback_registry_use(game->named, e, "hip:rotate_train");
  qa_q1mp_field_text(e, "think1", "hip:rotate_train_find");
  qa_q1mp_world_number(e, "rotate_state", 3);
  qa_q1mp_world_number(e, "ltime", game->time); qa_q1mp_world_number(e, "endtime", game->time + 0.1);
  qa_q1mp_world_number(e, "duration", 1); qa_q1mp_world_number(e, "cnt", 0.1);
  e->dest2 = QA_Q1_ZERO; e->dest1 = qa_q1_get_body(game, e).origin;
  qa_q1mp_world_later(game, e, 0.1, "hip:rotate_train");
}
static void clock_think(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  const double pos = (game->time + qa_q1_actor_number(e, "cnt")) / e->count, angle = 360 * (pos - floor(pos));
  if (qa_q1_actor_text(e, "event")[0] != '\0' && qa_q1_actor_number(e, "ltime") > angle) qa_q1mp_world_target_event(game, e, qa_q1_actor_text(e, "event"), "");
  set_angles(game, e, qa_q1_vscale(e->movedir, angle));
  qa_q1mp_rotate_targets_final(game, e);
  qa_q1mp_world_number(e, "ltime", angle);
  qa_q1mp_world_later(game, e, 1, "hip:clock");
}
static void clock_first(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_link_rotate_targets(game, e);
  run_named(game, e, "hip:clock");
}
static void clock_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *e) {
  (void)ctx;
  const qa_vec3 direction = qa_q1_move_direction(qa_q1_get_body(game, e).angles, NULL);
  e->movedir = QA_Q1MP_V3(-direction.y, -direction.z, -direction.x);
  set_angles(game, e, QA_Q1_ZERO);
  if (falsy(e->count)) e->count = 60;
  qa_q1mp_world_number(e, "cnt", qa_q1_actor_number(e, "cnt") * e->count / 12);
  qa_q1mp_world_number(e, "ltime", game->time);
  qa_q1mp_world_later(game, e, 0.1, "hip:clock_first");
}
void qa_q1mp_register_hipnotic_rotation(qa_q1_services *game) {
  qa_q1_callback_registry *named = game->named;
  qa_q1_register_spawn(game, "info_rotate", info_rotate_spawn, NULL);
  qa_q1_register_spawn(game, "path_rotate", path_rotate_spawn, NULL);
  qa_q1_register_spawn(game, "rotate_object", rotate_object_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:rotate_entity", (qa_q1_callback_handlers){.action = continuous_think, .use = continuous_use});
  qa_q1_callback_registry_register(named, "hip:rotate_first", (qa_q1_callback_handlers){.action = rotate_first});
  qa_q1_register_spawn(game, "func_rotate_entity", rotate_entity_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:rotate_door", (qa_q1_callback_handlers){.action = rotate_door_think, .use = rotate_door_use});
  qa_q1_callback_registry_register(named, "hip:rotate_door_done", (qa_q1_callback_handlers){.action = rotate_door_done});
  qa_q1_register_spawn(game, "func_rotate_door", rotate_door_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:movewall", (qa_q1_callback_handlers){.action = movewall_think, .touch = movewall_touch, .blocked = movewall_blocked});
  qa_q1_register_spawn(game, "func_movewall", movewall_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:rotate_train_next", (qa_q1_callback_handlers){.action = train_next});
  qa_q1_callback_registry_register(named, "hip:rotate_train_wait", (qa_q1_callback_handlers){.action = train_wait});
  qa_q1_callback_registry_register(named, "hip:rotate_train_stop", (qa_q1_callback_handlers){.action = train_stop_action});
  qa_q1_callback_registry_register(named, "hip:rotate_train_find", (qa_q1_callback_handlers){.action = train_find});
  qa_q1_callback_registry_register(named, "hip:rotate_train", (qa_q1_callback_handlers){.action = rotate_train_think, .use = rotate_train_use});
  qa_q1_register_spawn(game, "func_rotate_train", rotate_train_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:clock", (qa_q1_callback_handlers){.action = clock_think});
  qa_q1_callback_registry_register(named, "hip:clock_first", (qa_q1_callback_handlers){.action = clock_first});
  qa_q1_register_spawn(game, "func_clock", clock_spawn, NULL);
}
