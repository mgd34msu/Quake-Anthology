/* Port of src/content/q1/missionpacks/world/rogue-plats.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_plats.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* TS `x || y` / `x ||= y` */
static int32_t bits(double value) { return qa_js_to_int32(value); }
static double number_of(const qa_q1_actor *entity, const char *key) { return qa_q1_actor_number(entity, key); }
static qa_q1_bound_action action(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  return qa_q1_callback_registry_action(game->named, entity, name);
}

static void move(qa_q1_services *game, qa_q1_actor *entity, bool up) {
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise"), "voice", 1, 1);
  entity->state = up ? "up" : "down";
  qa_q1_calc_move(game, entity, up ? entity->pos1 : entity->pos2, entity->speed, action(game, entity, up ? "rogue:plat_top" : "rogue:plat_bottom"));
}
static void elevator_go(qa_q1_services *game, qa_q1_actor *entity) {
  double left;
  double right;
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise"), "voice", 1, 1);
  entity->state = "up"; qa_q1mp_world_number(entity, "elevatorLastUse", game->time);
  qa_vec3 destination = entity->pos2;
  destination.z = entity->pos2.z + (left = number_of(entity, "height"), right = number_of(entity, "elevatorToFloor"), left * right);
  qa_q1_calc_move(game, entity, destination, entity->speed, action(game, entity, "rogue:elevator_stop"));
}
static void button_fire(qa_q1_services *game, qa_q1_actor *entity) {
  if (qa_q1mp_streq(entity->state, "up") || qa_q1mp_streq(entity->state, "top")) return;
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise"), "voice", 1, 1);
  entity->state = "up";
  qa_q1_calc_move(game, entity, entity->pos2, entity->speed, action(game, entity, "rogue:elvbutton_wait"));
}
static void plat_up(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; move(game, entity, true); }
static void plat_down(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; move(game, entity, false); }
static void plat_arrive(qa_q1_services *game, qa_q1_actor *entity, bool up) {
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise1"), "voice", 1, 1);
  entity->state = up ? "top" : "bottom";
  if ((bits(entity->spawnflags) & 1) != 0 && !up) {
    double left = number_of(entity, "ltime");
    double right = qa_q1_health(game, entity->actor.id);
    qa_q1_schedule_at(game, entity, qa_q1mp_fround(left + right), action(game, entity, "rogue:plat_up"));
    return;
  }
  if ((bits(entity->spawnflags) & 16) == 0) return;
  qa_q1mp_world_number(entity, "plat2LastMove", game->time);
  if (number_of(entity, "plat2Called") == 1) {
    qa_q1mp_world_number(entity, "plat2Called", 0); qa_q1mp_world_number(entity, "plat2LastMove", 0);
    double due_seconds2 = qa_q1mp_fround(number_of(entity, "ltime") + 1.5);
    qa_q1_bound_action callback2 = action(game, entity, up ? "rogue:plat_down" : "rogue:plat_up");
    qa_q1_schedule_at(game, entity, due_seconds2, callback2);
    return;
  }
  if (up != ((bits(entity->spawnflags) & 8) != 0)) {
    qa_q1mp_world_number(entity, "plat2Called", 0);
    double due_seconds = qa_q1mp_fround(number_of(entity, "ltime") + entity->delay);
    qa_q1_bound_action callback = action(game, entity, up ? "rogue:plat_down" : "rogue:plat_up");
    qa_q1_schedule_at(game, entity, due_seconds, callback);
  }
}
static void plat_top(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; plat_arrive(game, entity, true); }
static void plat_bottom(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; plat_arrive(game, entity, false); }
static void plat_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  (void)ctx;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 1, NULL, NULL, NULL, NULL);
  if (!qa_q1mp_streq(entity->state, "up") && !qa_q1mp_streq(entity->state, "down")) qa_range_error("plat_new_crush: bad self.state");
  move(game, entity, qa_q1mp_streq(entity->state, "down"));
}
static void plat_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  if ((bits(entity->spawnflags) & 1) != 0) { if (qa_q1mp_streq(entity->state, "top")) move(game, entity, false); return; }
  if (qa_q1mp_streq(entity->state, "top")) move(game, entity, false);
  else if (qa_q1mp_streq(entity->state, "bottom")) move(game, entity, true);
}
static void elevator_stop(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_world_number(entity, "elevatorOnFloor", number_of(entity, "elevatorToFloor"));
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise1"), "voice", 1, 1);
  entity->state = "bottom";
  qa_q1mp_world_number(entity, "elevatorLastUse", game->time);
}
static void elevator_blocked(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  (void)ctx; (void)other;
  qa_q1mp_world_number(entity, "elevatorToFloor", number_of(entity, "elevatorOnFloor"));
  elevator_go(game, entity);
}
static void elevator_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)activator;
  if (number_of(entity, "elevatorLastUse") + 2 > game->time) return;
  qa_q1mp_world_number(entity, "elevatorLastUse", game->time);
  const double direction = game->world != NULL ? number_of(game->world, "rogue:elvButnDir") : 0;
  if (direction == 0) return;
  qa_q1_actor *button = qa_q1_get_entity(game, other);
  if (button == NULL) button = game->world;
  if (button == NULL) return;
  const qa_q1_body body = qa_q1_get_body(game, entity), other_body = qa_q1_get_body(game, button);
  const double position = body.origin.z + (body.bounds.min.z + body.bounds.max.z) / 2;
  const double button_position = other_body.origin.z + (other_body.bounds.min.z + other_body.bounds.max.z) / 2;
  const double height = number_of(entity, "height"), floor_number = number_of(entity, "elevatorOnFloor");
  if (position > button_position) qa_q1mp_world_number(entity, "elevatorToFloor", floor_number - ceil((position - button_position) / height));
  else if (button_position - position > height) qa_q1mp_world_number(entity, "elevatorToFloor", floor_number + floor((button_position - position) / height));
  else if (direction == -1 && floor_number > 0) qa_q1mp_world_number(entity, "elevatorToFloor", floor_number - 1);
  else if (direction == 1 && floor_number < number_of(entity, "cnt") - 1) qa_q1mp_world_number(entity, "elevatorToFloor", floor_number + 1);
  else return;
  elevator_go(game, entity);
}
static void plat2_enable(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator;
  qa_q1mp_world_number(entity, "plat2Disabled", 0);
  entity->use = (qa_q1_bound_use){0};
}
static void plat2_center(void *ctx, qa_q1_services *game, qa_q1_actor *trigger, qa_actor_id other, const qa_vec3 *normal,
                         const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  qa_q1_actor *e = qa_q1_get_entity(game, trigger->has_owner ? &trigger->owner : NULL);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  if (e == NULL || !body.has_value || number_of(e, "plat2LastMove") + 2 > game->time || number_of(e, "plat2Disabled") != 0) return;
  const double pending = number_of(e, "plat2GoTo");
  if (pending > 0) {
    if (number_of(e, "plat2GoTime") < game->time) { move(game, e, pending == 1); qa_q1mp_world_number(e, "plat2GoTo", 0); }
    return;
  }
  if (qa_q1mp_streq(e->state, "up") || qa_q1mp_streq(e->state, "down")) return;
  const qa_q1_body platform = qa_q1_get_body(game, e);
  const double center = platform.origin.z + (platform.bounds.min.z + platform.bounds.max.z) / 2;
  const bool same_level = qa_q1mp_streq(e->state, "top") ? center <= body.value.origin.z
                                                         : body.value.origin.z - center <= number_of(e, "height");
  qa_q1mp_world_number(e, "plat2Called", same_level ? 0 : 1);
  qa_q1mp_world_number(e, "plat2GoTime", game->time + (same_level ? 0.5 : 0.1));
  qa_q1mp_world_number(e, "plat2GoTo", qa_q1mp_streq(e->state, "bottom") ? 1 : 2);
}
static void new_plat_spawn(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_brush(g, e);
  if (falsy(e->speed)) e->speed = 150;
  if (falsy(e->sounds)) e->sounds = 2;
  if (e->sounds == 1 || e->sounds == 2) {
    qa_q1mp_field_text(e, "noise", e->sounds == 1 ? "plats/plat1.wav" : "plats/medplat1.wav");
    qa_q1mp_field_text(e, "noise1", e->sounds == 1 ? "plats/plat2.wav" : "plats/medplat2.wav");
  }
  const qa_q1_body body = qa_q1_get_body(g, e);
  double height = number_of(e, "height");
  bool negative = height < 0;
  height = fabs(height);
  if (height == 0) { negative = true; height = body.bounds.max.z - body.bounds.min.z - 8; }
  qa_q1mp_world_number(e, "height", height);
  e->pos1 = body.origin; e->pos2 = body.origin; e->pos2.z = body.origin.z - height;
  if ((bits(e->spawnflags) & 3) != 0) {
    e->use = qa_q1_callback_registry_use(g->named, e, "rogue:plat_use");
    e->blocked = qa_q1_callback_registry_blocked(g->named, e, "rogue:plat_blocked");
    e->state = negative ? "bottom" : "top";
    if (negative) qa_q1_set_origin(g, e, e->pos2);
    if ((bits(e->spawnflags) & 1) != 0 && qa_q1_health(g, e->actor.id) == 0) qa_gameplay_authority_set_health(g->host.combat, e->actor, 5);
  } else if ((bits(e->spawnflags) & 4) != 0) {
    const double floor_number = (bits(e->spawnflags) & 8) != 0 ? number_of(e, "cnt") - 1 : 0;
    qa_q1mp_world_number(e, "elevatorOnFloor", floor_number);
    qa_q1mp_world_number(e, "elevatorToFloor", 0); qa_q1mp_world_number(e, "elevatorLastUse", 0);
    if ((bits(e->spawnflags) & 8) != 0) { e->pos2 = body.origin; e->pos2.z = body.origin.z - height * (number_of(e, "cnt") - 1); }
    else { e->pos1 = body.origin; e->pos1.z = body.origin.z + height * (number_of(e, "cnt") - 1); e->pos2 = body.origin; }
    e->use = qa_q1_callback_registry_use(g->named, e, "rogue:elevator_use");
    e->blocked = qa_q1_callback_registry_blocked(g->named, e, "rogue:elevator_blocked");
  } else if ((bits(e->spawnflags) & 16) != 0) {
    qa_q1_actor *t = qa_q1_create(g, "rogue_plat2_trigger", NULL, NULL);
    t->has_owner = true; t->owner = e->actor.id; t->solid = "trigger"; t->movement = "none";
    t->touch = qa_q1_callback_registry_touch(g->named, t, "rogue:plat2_center");
    qa_vec3 min = qa_q1_vadd(body.bounds.min, QA_Q1MP_V3(25, 25, 0)), max = qa_q1_vsub(body.bounds.max, QA_Q1MP_V3(25, 25, -8));
    min.z = max.z - height - 8;
    if ((bits(e->spawnflags) & 1) != 0) max.z = min.z + 8;
    if (body.bounds.max.x - body.bounds.min.x <= 50) { min.x = (body.bounds.min.x + body.bounds.max.x) / 2; max.x = min.x + 1; }
    if (body.bounds.max.y - body.bounds.min.y <= 50) { min.y = (body.bounds.min.y + body.bounds.max.y) / 2; max.y = min.y + 1; }
    qa_q1_set_bounds(g, t, (qa_bounds){min, max});
    static const char *const KEYS[] = {"plat2Called", "plat2LastMove", "plat2GoTo", "plat2GoTime"};
    for (size_t i = 0; i < 4; i++) qa_q1mp_world_number(e, KEYS[i], 0);
    e->blocked = qa_q1_callback_registry_blocked(g->named, e, "rogue:plat_blocked");
    if (falsy(e->delay)) e->delay = 3;
    if (negative) { e->state = "bottom"; e->spawnflags = 16; qa_q1_set_origin(g, e, e->pos2); }
    else { e->spawnflags = qa_js_bit_or(e->spawnflags, 8); e->state = "top"; }
    if (e->targetname[0] != '\0') {
      qa_q1mp_world_number(e, "plat2Disabled", 1);
      e->use = qa_q1_callback_registry_use(g->named, e, "rogue:plat2_enable");
    }
  }
}
static void button_wait(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (game->world != NULL) qa_q1mp_world_number(game->world, "rogue:elvButnDir", (bits(entity->spawnflags) & 1) != 0 ? -1 : 1);
  entity->state = "top";
  double due_seconds = qa_q1mp_fround(number_of(entity, "ltime") + entity->wait);
  qa_q1_bound_action callback = action(game, entity, "rogue:elvbutton_return");
  qa_q1_schedule_at(game, entity, due_seconds, callback);
  qa_q1_use_targets(game, entity, entity->has_activator ? &entity->activator : NULL);
  entity->frame = 1;
}
static void button_done(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { (void)ctx; (void)game; entity->state = "bottom"; }
static void button_return(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->state = "down"; entity->frame = 0;
  if (qa_q1_health(game, entity->actor.id) != 0) qa_q1_actor_set_damageable(entity, true);
  qa_q1_calc_move(game, entity, entity->pos1, entity->speed, action(game, entity, "rogue:elvbutton_done"));
}
static void set_activator(qa_q1_actor *entity, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator != NULL) entity->activator = *activator;
}
static void button_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other;
  set_activator(entity, activator); button_fire(game, entity);
}
static void button_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                         const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  set_activator(entity, &other); button_fire(game, entity);
}
static void button_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  set_activator(entity, attacker);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
  qa_q1_actor_set_damageable(entity, false);
  button_fire(game, entity);
}
static void button_spawn(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  static const char *const SOUNDS[] = {"buttons/airbut1.wav", "buttons/switch21.wav", "buttons/switch02.wav", "buttons/switch04.wav"};
  /* TS sounds[e.sounds]: only an exact array index selects an entry. */
  if (e->sounds >= 0 && e->sounds < 4 && floor(e->sounds) == e->sounds) qa_q1mp_field_text(e, "noise", SOUNDS[(size_t)e->sounds]);
  e->movedir = qa_q1_move_direction(qa_q1_get_body(g, e).angles, NULL);
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = QA_Q1_ZERO;
  qa_q1_set_body(g, e, &patch);
  e->movement = "push"; e->solid = "bsp";
  e->use = qa_q1_callback_registry_use(g->named, e, "rogue:elvbutton_fire");
  if (qa_q1_health(g, e->actor.id) != 0) {
    e->max_health = qa_q1_health(g, e->actor.id);
    e->die = qa_q1_callback_registry_die(g->named, e, "rogue:elvbutton_fire");
    qa_q1_actor_set_damageable(e, true);
  } else e->touch = qa_q1_callback_registry_touch(g->named, e, "rogue:elvbutton_fire");
  if (falsy(e->speed)) e->speed = 40;
  if (falsy(e->wait)) e->wait = 1;
  const qa_q1_body body = qa_q1_get_body(g, e);
  e->state = "bottom"; e->pos1 = body.origin;
  const double lip = number_of(e, "lip");
  e->pos2 = qa_q1_vadd(e->pos1, qa_q1_vscale(e->movedir, fabs(qa_q1_dot(e->movedir, qa_q1_vsub(body.bounds.max, body.bounds.min))) - (falsy(lip) ? 4 : lip)));
}
void qa_q1mp_register_rogue_plats(qa_q1_services *game) {
  qa_q1_callback_registry_register(game->named, "rogue:plat_up", (qa_q1_callback_handlers){.action = plat_up});
  qa_q1_callback_registry_register(game->named, "rogue:plat_top", (qa_q1_callback_handlers){.action = plat_top});
  qa_q1_callback_registry_register(game->named, "rogue:plat_down", (qa_q1_callback_handlers){.action = plat_down});
  qa_q1_callback_registry_register(game->named, "rogue:plat_bottom", (qa_q1_callback_handlers){.action = plat_bottom});
  qa_q1_callback_registry_register(game->named, "rogue:plat_blocked", (qa_q1_callback_handlers){.blocked = plat_blocked});
  qa_q1_callback_registry_register(game->named, "rogue:plat_use", (qa_q1_callback_handlers){.use = plat_use});
  qa_q1_callback_registry_register(game->named, "rogue:elevator_stop", (qa_q1_callback_handlers){.action = elevator_stop});
  qa_q1_callback_registry_register(game->named, "rogue:elevator_blocked", (qa_q1_callback_handlers){.blocked = elevator_blocked});
  qa_q1_callback_registry_register(game->named, "rogue:elevator_use", (qa_q1_callback_handlers){.use = elevator_use});
  qa_q1_callback_registry_register(game->named, "rogue:plat2_enable", (qa_q1_callback_handlers){.use = plat2_enable});
  qa_q1_callback_registry_register(game->named, "rogue:plat2_center", (qa_q1_callback_handlers){.touch = plat2_center});
  qa_q1_register_spawn(game, "func_new_plat", new_plat_spawn, NULL);
  qa_q1_callback_registry_register(game->named, "rogue:elvbutton_wait", (qa_q1_callback_handlers){.action = button_wait});
  qa_q1_callback_registry_register(game->named, "rogue:elvbutton_done", (qa_q1_callback_handlers){.action = button_done});
  qa_q1_callback_registry_register(game->named, "rogue:elvbutton_return", (qa_q1_callback_handlers){.action = button_return});
  qa_q1_callback_registry_register(game->named, "rogue:elvbutton_fire",
                                   (qa_q1_callback_handlers){.use = button_use, .touch = button_touch, .die = button_die});
  qa_q1_register_spawn(game, "func_elvtr_button", button_spawn, NULL);
}
