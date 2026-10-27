/* Port of src/content/q2/base/entities/turrets.ts. Quake II
 * g_turret.c. Turret drivers reuse the permanent infantry AI/death
 * runner. GPL-2.0-or-later. */
#include "qa/content/q2/base/entities/turrets.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

typedef struct breach_entry { qa_q2_entity *e; qa_q2_base_turret_breach_state state; } breach_entry;
typedef struct driver_entry {
  qa_q2_entity *e;
  qa_q2_base_monster_context *context;
  qa_q2_die monster_die;
  bool has_breach;
  qa_actor_id breach;
  double radius, yaw_offset, height;
} driver_entry;

struct qa_q2_turret_entities {
  qa_q2_base_entity_hooks hooks;
  breach_entry *breaches; size_t breach_count, breach_cap;
  driver_entry *drivers; size_t driver_count, driver_cap;
  qa_q2_entity **team; size_t team_cap;
  qa_q2_named_think thinks[4];
  qa_q2_named_die dies[1];
  qa_q2_named_blocked blockeds[1];
  qa_q2_callback_definitions defs;
};

static void breach_think_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void breach_init_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void driver_think_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void driver_link_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void driver_die_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_death_reaction);
static void turret_blocked_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_actor_id);

double qa_q2_snap_turret_eighth(double value) { return trunc(value * 8 + (value > 0 ? 0.5 : -0.5)) * 0.125; }
static double normalize_angle(double value) { while (value > 360) value -= 360; while (value < 0) value += 360; return value; }
static double short_angle(double value) { return value < -180 ? value + 360 : value > 180 ? value - 360 : value; }

static qa_q2_base_turret_breach_state *find_breach(qa_q2_turret_entities *t, qa_q2_entity *e) {
  for (size_t i = 0; i < t->breach_count; i++) if (t->breaches[i].e == e) return &t->breaches[i].state;
  return NULL;
}
static driver_entry *find_driver(qa_q2_turret_entities *t, qa_q2_entity *e) {
  for (size_t i = 0; i < t->driver_count; i++) if (t->drivers[i].e == e) return &t->drivers[i];
  return NULL;
}
static void set_breach(qa_q2_turret_entities *t, qa_q2_entity *e, qa_q2_base_turret_breach_state state) {
  qa_q2_base_turret_breach_state *existing = find_breach(t, e);
  if (existing) { *existing = state; return; }
  if (t->breach_count == t->breach_cap) {
    size_t cap = t->breach_cap ? t->breach_cap * 2 : 8;
    breach_entry *grown = realloc(t->breaches, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    t->breaches = grown; t->breach_cap = cap;
  }
  t->breaches[t->breach_count++] = (breach_entry){e, state};
}
static driver_entry *set_driver(qa_q2_turret_entities *t, qa_q2_entity *e, driver_entry value) {
  driver_entry *existing = find_driver(t, e);
  if (existing) { *existing = value; return existing; }
  if (t->driver_count == t->driver_cap) {
    size_t cap = t->driver_cap ? t->driver_cap * 2 : 8;
    driver_entry *grown = realloc(t->drivers, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    t->drivers = grown; t->driver_cap = cap;
  }
  driver_entry *entry = &t->drivers[t->driver_count++];
  *entry = value;
  return entry;
}

/* Borrowed scratch, valid until the next team() call. */
static qa_q2_entity **team_of(qa_q2_turret_entities *t, qa_q2_entity *entity, qa_q2_game_services *game, size_t *out_count) {
  size_t count = 0;
  qa_q2_entity *master = entity->has_team_master ? game->entity(game->ctx, &entity->team_master) : NULL;
  if (master) {
    for (qa_q2_entity *member = master; member; member = member->has_team_chain ? game->entity(game->ctx, &member->team_chain) : NULL) {
      if (count == t->team_cap) {
        size_t cap = t->team_cap ? t->team_cap * 2 : 8;
        qa_q2_entity **grown = realloc(t->team, cap * sizeof *grown);
        if (!grown) qa_range_error("Out of memory");
        t->team = grown; t->team_cap = cap;
      }
      t->team[count++] = member;
    }
  } else {
    const char *team = qa_q2_spawn_get(&entity->spawn, "team");
    if (!team) {
      if (t->team_cap == 0) { t->team = malloc(sizeof *t->team); if (!t->team) qa_range_error("Out of memory"); t->team_cap = 1; }
      t->team[count++] = entity;
    } else {
      size_t total = 0;
      qa_q2_entity *const *entities = game->entities(game->ctx, &total);
      for (size_t i = 0; i < total; i++) {
        const char *member_team = qa_q2_spawn_get(&entities[i]->spawn, "team");
        if (!member_team || strcmp(member_team, team) != 0) continue;
        if (count == t->team_cap) {
          size_t cap = t->team_cap ? t->team_cap * 2 : 8;
          qa_q2_entity **grown = realloc(t->team, cap * sizeof *grown);
          if (!grown) qa_range_error("Out of memory");
          t->team = grown; t->team_cap = cap;
        }
        t->team[count++] = entities[i];
      }
    }
  }
  *out_count = count;
  return t->team;
}

qa_q2_turret_entities *qa_q2_turret_entities_create(qa_q2_base_entity_hooks hooks) {
  qa_q2_turret_entities *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  t->hooks = hooks;
  t->thinks[0] = (qa_q2_named_think){"turret_breach_think", {t, breach_think_fn}};
  t->thinks[1] = (qa_q2_named_think){"turret_breach_finish_init", {t, breach_init_fn}};
  t->thinks[2] = (qa_q2_named_think){"turret_driver_think", {t, driver_think_fn}};
  t->thinks[3] = (qa_q2_named_think){"turret_driver_link", {t, driver_link_fn}};
  t->dies[0] = (qa_q2_named_die){"turret_driver_die", {t, driver_die_fn}};
  t->blockeds[0] = (qa_q2_named_blocked){"turret_blocked", {t, turret_blocked_fn}};
  t->defs = (qa_q2_callback_definitions){.think = t->thinks, .think_count = 4, .die = t->dies, .die_count = 1,
      .blocked = t->blockeds, .blocked_count = 1};
  return t;
}
void qa_q2_turret_entities_destroy(qa_q2_turret_entities *t) {
  if (!t) return;
  free(t->breaches); free(t->drivers); free(t->team); free(t);
}
qa_q2_callback_definitions qa_q2_turret_entities_callbacks(qa_q2_turret_entities *t) { return t->defs; }

qa_q2_turrets_checkpoint qa_q2_turret_entities_capture(qa_q2_turret_entities *t, qa_q2_game_services *game) {
  qa_q2_turrets_checkpoint out = {0};
  size_t count = 0, bcap = 0, dcap = 0;
  qa_q2_entity *const *entities = game->entities(game->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q2_entity *entity = entities[i];
    qa_saved_actor_id actor = {(double)entity->actor.id.slot, (double)entity->actor.id.generation};
    qa_q2_base_turret_breach_state *breach = find_breach(t, entity);
    driver_entry *driver = find_driver(t, entity);
    if (breach) {
      if (out.breach_count == bcap) { bcap = bcap ? bcap * 2 : 8; out.breaches = realloc(out.breaches, bcap * sizeof *out.breaches); if (!out.breaches) qa_range_error("Out of memory"); }
      out.breaches[out.breach_count++] = (qa_q2_base_turret_breach_entry){actor, *breach};
    }
    if (driver) {
      const char *monster_die = qa_q2_die_name(game->source_callbacks, driver->monster_die, driver->monster_die.fn != NULL);
      if (!monster_die) qa_range_error("Turret driver has no named monster death callback");
      if (out.driver_count == dcap) { dcap = dcap ? dcap * 2 : 8; out.drivers = realloc(out.drivers, dcap * sizeof *out.drivers); if (!out.drivers) qa_range_error("Out of memory"); }
      qa_q2_base_turret_driver_entry entry = {0};
      entry.actor = actor;
      entry.monster_die = monster_die;
      entry.radius = driver->radius; entry.yaw_offset = driver->yaw_offset; entry.height = driver->height;
      entry.has_breach = qa_q2_save_actor(driver->has_breach ? &driver->breach : NULL, &entry.breach);
      out.drivers[out.driver_count++] = entry;
    }
  }
  return out;
}
static qa_q2_entity *saved_turret(qa_q2_game_services *game, qa_saved_actor_id actor) {
  qa_owned_actor owner = qa_q2_restore_actor(game, actor);
  qa_q2_entity *entity = game->entity(game->ctx, &owner.id);
  if (!entity) qa_range_error("Missing saved Q2 turret entity");
  return entity;
}
void qa_q2_turret_entities_restore(qa_q2_turret_entities *t, qa_q2_game_services *game, qa_q2_turrets_checkpoint checkpoint) {
  t->breach_count = 0; t->driver_count = 0;
  for (size_t i = 0; i < checkpoint.breach_count; i++)
    set_breach(t, saved_turret(game, checkpoint.breaches[i].actor), checkpoint.breaches[i].state);
  for (size_t i = 0; i < checkpoint.driver_count; i++) {
    const qa_q2_base_turret_driver_entry *saved = &checkpoint.drivers[i];
    qa_q2_entity *entity = saved_turret(game, saved->actor);
    qa_q2_base_monster_context *context = t->hooks.monster_context(t->hooks.ctx, entity->actor.id);
    bool has_die = false;
    qa_q2_die monster_die = qa_q2_die_resolve(game->source_callbacks, saved->monster_die, &has_die);
    if (!context || !has_die) qa_range_error("Restore Q2 turret drivers after their monster state");
    driver_entry entry = {0};
    entry.e = entity; entry.context = context; entry.monster_die = monster_die;
    entry.has_breach = saved->has_breach;
    if (saved->has_breach) entry.breach = qa_session_actor_registry_reference_saved(game->host->actors, saved->breach, "checkpoint");
    entry.radius = saved->radius; entry.yaw_offset = saved->yaw_offset; entry.height = saved->height;
    (void)set_driver(t, entity, entry);
  }
}

static void turret_blocked_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, qa_actor_id other) {
  qa_q2_turret_entities *t = c;
  qa_body_state_result body = qa_shared_body_table_read(services->host->bodies, other);
  qa_combat_state_result combat = qa_gameplay_authority_read(services->host->combat, other);
  if (!body.has_value || !combat.has_value || !combat.value.can_take_damage) return;
  size_t count = 0;
  qa_q2_entity **members = team_of(t, self, services, &count);
  qa_q2_entity *master = count > 0 ? members[0] : self;
  qa_actor_id attacker = master->has_owner ? master->owner : master->actor.id;
  services->damage(services->ctx, other, self, self->actor.id, &attacker, master->damage, 10, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 20, 0, NULL);
}
static void spawn_blocked(qa_q2_turret_entities *t, qa_q2_entity *entity, qa_q2_game_services *game) {
  entity->blocked = (qa_q2_blocked){t, turret_blocked_fn};
  game->solid(game->ctx, entity, "brush");
  game->motion(game->ctx, entity, "push");
  game->show(game->ctx, entity);
}

static void fire(qa_q2_turret_entities *t, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_base_turret_breach_state *state, qa_q2_entity *driver) {
  qa_body_state body = qa_q2_game_body(game, entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 start = qa_q2_add(qa_q2_add(qa_q2_add(body.origin, qa_q2_scale(axes.forward, state->muzzle.x)),
                                      qa_q2_scale(axes.right, state->muzzle.y)), qa_q2_scale(axes.up, state->muzzle.z));
  double damage = trunc(100 + game->host->random(game->host->ctx) * 50);
  double speed = 550 + 50 * game->options.skill;
  (void)t->hooks.fire_rocket(t->hooks.weapons_ctx, driver, game, start, axes.forward, damage, speed, 150, damage);
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = entity->actor.id; ev.origin = start;
  ev.path = "weapons/rocklf1a.wav"; ev.channel = 1; ev.volume = 1; ev.attenuation = 1; ev.reliable = false; ev.loop = "once";
  game->host->emit(game->host->ctx, ev);
}

static void breach_think_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_turret_entities *t = c;
  qa_q2_base_turret_breach_state *state = find_breach(t, entity);
  if (!state) return;
  qa_body_state body = qa_q2_game_body(game, entity);
  double frame = game->host->frame_seconds(game->host->ctx);
  double pitch = normalize_angle(state->goal.x), yaw = normalize_angle(state->goal.y);
  if (pitch > 180) pitch -= 360;
  pitch = fmax(state->pitch_min, fmin(state->pitch_max, pitch));
  if (yaw < state->yaw_min || yaw > state->yaw_max) {
    double min_distance = fabs(short_angle(fabs(state->yaw_min - yaw)));
    double max_distance = fabs(short_angle(fabs(state->yaw_max - yaw)));
    yaw = min_distance < max_distance ? state->yaw_min : state->yaw_max;
  }
  state->goal.x = pitch; state->goal.y = yaw;
  double limit = entity->speed * frame;
  entity->angular_velocity = (qa_vec3){
      fmax(-limit, fmin(limit, short_angle(pitch - normalize_angle(body.angles.x)))) / frame,
      fmax(-limit, fmin(limit, short_angle(yaw - normalize_angle(body.angles.y)))) / frame, 0};
  game->motion(game->ctx, entity, "push");
  size_t count = 0;
  qa_q2_entity **members = team_of(t, entity, game, &count);
  /* Copy the scratch: motion callbacks cannot reenter team_of here. */
  for (size_t i = 0; i < count; i++) {
    members[i]->angular_velocity.y = entity->angular_velocity.y;
    game->motion(game->ctx, members[i], "push");
  }
  qa_q2_entity *owner = entity->has_owner ? game->entity(game->ctx, &entity->owner) : NULL;
  driver_entry *driver = owner ? find_driver(t, owner) : NULL;
  if (owner && driver) {
    qa_body_state driver_body = qa_q2_game_body(game, owner);
    double radians = (body.angles.y + driver->yaw_offset) * M_PI / 180;
    qa_vec3 target = {qa_q2_snap_turret_eighth(body.origin.x + cos(radians) * driver->radius),
                      qa_q2_snap_turret_eighth(body.origin.y + sin(radians) * driver->radius),
                      qa_q2_snap_turret_eighth(body.origin.z + driver->radius * tan(body.angles.x * M_PI / 180) + driver->height)};
    owner->angular_velocity.x = entity->angular_velocity.x;
    owner->angular_velocity.y = entity->angular_velocity.y;
    qa_q2_body_changes changes = {0};
    changes.has_velocity = true;
    changes.velocity = qa_q2_scale(qa_q2_subtract(target, driver_body.origin), 1 / frame);
    game->move(game->ctx, owner, changes, false);
    game->motion(game->ctx, owner, "push");
    if (((int64_t)entity->spawnflags & 65536) != 0) {
      fire(t, entity, game, state, owner);
      entity->spawnflags = (double)((int64_t)entity->spawnflags & ~65536LL);
    }
  }
  game->schedule(game->ctx, entity, frame, (qa_q2_think){t, breach_think_fn});
}

static void driver_think_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_turret_entities *t = c;
  driver_entry *driver = find_driver(t, entity);
  qa_q2_entity *turret = driver && driver->has_breach ? game->entity(game->ctx, &driver->breach) : NULL;
  if (!driver || !turret) return;
  game->schedule(game->ctx, entity, game->host->frame_seconds(game->host->ctx), (qa_q2_think){t, driver_think_fn});
  qa_q2_base_monster_context *context = driver->context;
  qa_q2_base_monster_state *state = context->state;
  if (entity->has_enemy) {
    qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, entity->enemy);
    if (!qa_session_actor_registry_is_live(game->host->actors, entity->enemy) || (combat.has_value ? combat.value.health : 0) <= 0)
      entity->has_enemy = false;
  }
  if (!entity->has_enemy) {
    if (!context->find_target(context)) return;
    state->trail_time = game->host->now(game->host->ctx);
    state->lost_sight = false;
  } else if (context->visible_enemy(context)) {
    if (state->lost_sight) { state->trail_time = game->host->now(game->host->ctx); state->lost_sight = false; }
  } else { state->lost_sight = true; return; }
  qa_body_state_result enemy = entity->has_enemy ? qa_shared_body_table_read(game->host->bodies, entity->enemy) : (qa_body_state_result){0};
  qa_q2_base_turret_breach_state *breach = find_breach(t, turret);
  if (!enemy.has_value || !breach) return;
  qa_q2_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  qa_vec3 target = qa_q2_add(enemy.value.origin, (qa_vec3){0, 0, enemy_entity ? enemy_entity->view_height : 22});
  breach->goal = qa_vector_to_angles_with_rule(qa_q2_subtract(target, qa_q2_game_body(game, turret).origin), QA_VECTOR_TO_ANGLES_NATIVE);
  if (game->host->now(game->host->ctx) < state->attack_finished) return;
  double reaction_time = 3 - game->options.skill;
  if (game->host->now(game->host->ctx) - state->trail_time < reaction_time) return;
  state->attack_finished = game->host->now(game->host->ctx) + reaction_time + 1;
  turret->spawnflags = (double)((int64_t)turret->spawnflags | 65536LL);
}

static void breach_init_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services) {
  qa_q2_turret_entities *t = c;
  qa_q2_base_turret_breach_state *state = find_breach(t, self);
  if (!state) qa_range_error("Turret breach initialization without source state");
  qa_q2_entity *muzzle = services->pick_target(services->ctx, self->target);
  if (!muzzle) {
    char message[512];
    (void)snprintf(message, sizeof message, "turret_breach missing muzzle target %s", self->target);
    services->host->diagnostic(services->host->ctx, message);
  } else {
    state->muzzle = qa_q2_subtract(qa_q2_game_body(services, muzzle).origin, qa_q2_game_body(services, self).origin);
    services->remove(services->ctx, muzzle);
  }
  size_t count = 0;
  qa_q2_entity **members = team_of(t, self, services, &count);
  qa_q2_entity *master = count > 0 ? members[0] : self;
  master->damage = self->damage;
  breach_think_fn(c, self, services);
}

static void driver_die_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, qa_death_reaction reaction) {
  qa_q2_turret_entities *t = c;
  driver_entry *state = find_driver(t, self);
  if (!state) qa_range_error("Turret driver death without source state");
  qa_q2_base_monster_context *context = state->context;
  qa_q2_die monster_die = state->monster_die;
  qa_q2_entity *turret = state->has_breach ? services->entity(services->ctx, &state->breach) : NULL;
  if (turret) {
    qa_q2_base_turret_breach_state *breach = find_breach(t, turret);
    if (breach) breach->goal.x = 0;
    turret->has_owner = false;
    size_t count = 0;
    qa_q2_entity **members = team_of(t, turret, services, &count);
    qa_q2_entity *master = count > 0 ? members[0] : turret;
    master->has_owner = false;
    /* Re-walk by chain: the scratch is reused below by callbacks. */
    for (size_t i = 0; i < count; i++) {
      if (members[i]->has_team_chain && qa_same_actor(members[i]->team_chain, self->actor.id)) {
        members[i]->has_team_chain = self->has_team_chain;
        if (self->has_team_chain) members[i]->team_chain = self->team_chain;
        break;
      }
    }
  }
  state->has_breach = false;
  self->has_team_master = false;
  self->has_team_chain = false;
  self->flags = (double)((int64_t)self->flags & ~1024LL);
  self->angular_velocity = QA_Q2_ZERO;
  services->motion(services->ctx, self, "step");
  monster_die.fn(monster_die.ctx, self, services, reaction);
  if (!context->state->gibbed && qa_session_actor_registry_is_live(services->host->actors, self->actor.id))
    t->hooks.resume_monster(t->hooks.ctx, self, services);
}

static void driver_link_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services) {
  qa_q2_turret_entities *t = c;
  driver_entry *state = find_driver(t, self);
  if (!state) qa_range_error("Turret driver link without source state");
  qa_q2_entity *breach = services->pick_target(services->ctx, self->target);
  if (!breach || !find_breach(t, breach)) {
    char message[512];
    (void)snprintf(message, sizeof message, "turret_driver has invalid breach %s", self->target);
    services->host->diagnostic(services->host->ctx, message);
    return;
  }
  state->has_breach = true; state->breach = breach->actor.id;
  breach->has_owner = true; breach->owner = self->actor.id;
  size_t count = 0;
  qa_q2_entity **members = team_of(t, breach, services, &count);
  qa_q2_entity *master = count > 0 ? members[0] : breach;
  qa_q2_entity *last = count > 0 ? members[count - 1] : breach;
  master->has_owner = true; master->owner = self->actor.id;
  master->has_team_master = true; master->team_master = master->actor.id;
  last->has_team_chain = true; last->team_chain = self->actor.id;
  self->has_team_master = true; self->team_master = master->actor.id;
  self->has_team_chain = false;
  qa_body_state body = qa_q2_game_body(services, self);
  qa_body_state target = qa_q2_game_body(services, breach);
  qa_vec3 delta = qa_q2_subtract(body.origin, target.origin);
  state->radius = qa_q2_length((qa_vec3){delta.x, delta.y, 0});
  state->yaw_offset = normalize_angle(qa_vector_to_angles_with_rule(delta, QA_VECTOR_TO_ANGLES_NATIVE).y);
  state->height = delta.z;
  qa_q2_body_changes changes = {0};
  changes.has_angles = true; changes.angles = target.angles;
  services->move(services->ctx, self, changes, false);
  self->flags = (double)((int64_t)self->flags | 1024LL);
  services->schedule(services->ctx, self, services->host->frame_seconds(services->host->ctx), (qa_q2_think){t, driver_think_fn});
}

bool qa_q2_turret_entities_spawn(qa_q2_turret_entities *t, qa_q2_entity *entity, qa_q2_game_services *game) {
  const char *classname = entity->classname;
  if (strcmp(classname, "turret_base") == 0) { spawn_blocked(t, entity, game); return true; }
  if (strcmp(classname, "turret_breach") == 0) {
    spawn_blocked(t, entity, game);
    if (entity->speed == 0) entity->speed = 50;
    if (entity->damage == 0) entity->damage = 10;
    double minpitch = qa_q2_number_field(&entity->spawn, "minpitch", 0);
    double maxpitch = qa_q2_number_field(&entity->spawn, "maxpitch", 0);
    double maxyaw = qa_q2_number_field(&entity->spawn, "maxyaw", 0);
    qa_q2_base_turret_breach_state state = {0};
    state.goal = (qa_vec3){0, qa_q2_game_body(game, entity).angles.y, 0};
    state.muzzle = QA_Q2_ZERO;
    state.pitch_max = -(minpitch != 0 ? minpitch : -30);
    state.pitch_min = -(maxpitch != 0 ? maxpitch : 30);
    state.yaw_min = qa_q2_number_field(&entity->spawn, "minyaw", 0);
    state.yaw_max = maxyaw != 0 ? maxyaw : 360;
    set_breach(t, entity, state);
    game->schedule(game->ctx, entity, game->host->frame_seconds(game->host->ctx), (qa_q2_think){t, breach_init_fn});
    return true;
  }
  if (strcmp(classname, "turret_driver") == 0) {
    if (game->options.mode == QA_Q2_MODE_DEATHMATCH) { game->remove(game->ctx, entity); return true; }
    qa_q2_base_monster_context *context = t->hooks.turret_driver(t->hooks.ctx, entity, game);
    qa_q2_die monster_die = entity->die;
    if (monster_die.fn == NULL) qa_range_error("Turret driver admission did not bind infantry death");
    driver_entry entry = {0};
    entry.e = entity; entry.context = context; entry.monster_die = monster_die;
    (void)set_driver(t, entity, entry);
    context->state->gib_health = 0;
    context->state->stand_ground = true;
    context->state->ducked = true;
    entity->flags = (double)((int64_t)entity->flags | 2048LL);
    entity->server_flags = (double)((int64_t)entity->server_flags | 4LL);
    entity->render_flags = (double)((int64_t)entity->render_flags | 64LL);
    entity->view_height = 24;
    /* TS infantryStand(context) is context.setMove("infantry_move_stand"). */
    context->set_move(context, "infantry_move_stand");
    entity->frame = 0;
    entity->die = (qa_q2_die){t, driver_die_fn};
    game->motion(game->ctx, entity, "push");
    game->solid(game->ctx, entity, "box");
    game->show(game->ctx, entity);
    game->schedule(game->ctx, entity, game->host->frame_seconds(game->host->ctx), (qa_q2_think){t, driver_link_fn});
    return true;
  }
  return false;
}
