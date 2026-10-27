/* Port of src/content/q2/base/entities/targets.ts. Remaining Quake II
 * g_target.c targets. GPL-2.0-or-later. */
#include "qa/content/q2/base/entities/targets.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/scenery.h"
#include "qa/foundation/support.h"

struct qa_q2_base_targets {
  qa_q2_base_entity_hooks hooks;
  qa_q2_named_think thinks[7];
  qa_q2_named_use uses[7];
  qa_q2_callback_definitions defs;
};

static void laser_on_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void laser_off_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void laser_start_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void ramp_think_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void crosslevel_think_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void quake_think_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void laser_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void ramp_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void temp_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void spawner_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void blaster_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void crosslevel_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void quake_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);

static int64_t sf(const qa_q2_entity *e) { return (int64_t)e->spawnflags; }
#define SET_SF(e, bits) ((e)->spawnflags = (double)(bits))

const qa_q2_think QA_Q2_TARGET_LASER_THINK = {NULL, qa_q2_target_laser_think_fn};

void qa_q2_target_laser_think_fn(void *ctx, qa_q2_entity *self, qa_q2_game_services *services) {
  (void)ctx;
  double count = (sf(self) & (int64_t)0x80000000LL) != 0 ? 8 : 4;
  qa_vec3 origin = qa_q2_game_body(services, self).origin;
  qa_body_state_result target = self->has_enemy
      ? qa_shared_body_table_read(services->host->bodies, self->enemy) : (qa_body_state_result){0};
  if (target.has_value) {
    qa_vec3 center = qa_q2_add(target.value.origin, qa_q2_scale(qa_q2_add(target.value.bounds.min, target.value.bounds.max), 0.5));
    qa_vec3 direction = qa_q2_normalize(qa_q2_subtract(center, origin));
    if (direction.x != self->movedir.x || direction.y != self->movedir.y || direction.z != self->movedir.z)
      SET_SF(self, sf(self) | (int64_t)0x80000000LL);
    self->movedir = direction;
  }
  qa_vec3 end = qa_q2_add(origin, qa_q2_scale(self->movedir, 2048));
  qa_vec3 start = origin, terminal = end;
  qa_actor_id ignore = self->actor.id;
  /* Passed-through actors need growable storage; a file-scope scratch
   * is safe because execution is single-threaded and non-reentrant
   * between trace calls. */
  static qa_actor_id *passed = NULL;
  static size_t passed_cap = 0;
  size_t passed_count = 0;
  for (;;) {
    qa_q2_trace_request request = {0};
    request.start = start; request.end = end; request.bounds = NULL; request.ignore = &ignore;
    request.mask = 0x6000001; request.exclude = passed; request.exclude_count = passed_count;
    qa_trace_result trace = services->host->trace(services->host->ctx, request);
    terminal = trace.end;
    bool has_hit = trace.hit.kind == QA_TRACE_HIT_ACTOR;
    qa_actor_id hit = trace.hit.actor;
    if (has_hit) {
      qa_combat_state_result combat = qa_gameplay_authority_read(services->host->combat, hit);
      qa_q2_entity *hit_entity = services->entity(services->ctx, &hit);
      if (combat.has_value && combat.value.can_take_damage && !(hit_entity && hit_entity->laser_immune)) {
        services->damage(services->ctx, hit, self, self->actor.id,
                         self->has_activator ? &self->activator : NULL, self->damage, 1,
                         self->movedir, trace.end, QA_Q2_ZERO, 30, 4, NULL);
      }
    }
    if (!has_hit || (!services->host->is_monster(services->host->ctx, hit) && !services->host->is_player(services->host->ctx, hit))) {
      if (trace.fraction < 1 && (sf(self) & (int64_t)0x80000000LL) != 0) {
        SET_SF(self, sf(self) & ~(int64_t)0x80000000LL);
        qa_q2_presentation_event ev = {0};
        ev.kind = "effect"; ev.effect = "q2:laser-sparks"; ev.origin = trace.end;
        ev.direction = trace.contact.has_plane ? trace.contact.plane.normal : QA_Q2_ZERO;
        ev.count = count; ev.color = (double)((int64_t)self->skin & 255);
        services->host->emit(services->host->ctx, ev);
      }
      break;
    }
    if (passed_count == passed_cap) {
      passed_cap = passed_cap ? passed_cap * 2 : 8;
      passed = realloc(passed, passed_cap * sizeof *passed);
      if (!passed) qa_range_error("Out of memory");
    }
    passed[passed_count++] = hit;
    ignore = hit;
    start = trace.end;
  }
  qa_q2_presentation_event beam = {0};
  beam.kind = "beam"; beam.has_actor = true; beam.actor = self->actor.id;
  beam.start = origin; beam.end = terminal; beam.width = self->frame; beam.color = self->skin; beam.visible = true;
  services->host->emit(services->host->ctx, beam);
  services->schedule(services->ctx, self, services->host->frame_seconds(services->host->ctx), QA_Q2_TARGET_LASER_THINK);
}

static void laser_on_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  (void)c;
  if (!self->has_activator) { self->has_activator = true; self->activator = self->actor.id; }
  SET_SF(self, sf(self) | (int64_t)0x80000001LL);
  self->visible = true;
  qa_q2_target_laser_think_fn(NULL, self, game);
}
static void laser_off_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  (void)c;
  SET_SF(self, sf(self) & ~1LL);
  self->visible = false;
  game->cancel(game->ctx, self);
  qa_q2_presentation_event visibility = {0};
  visibility.kind = "visibility"; visibility.has_actor = true; visibility.actor = self->actor.id; visibility.visible = false;
  game->host->emit(game->host->ctx, visibility);
  qa_q2_presentation_event beam = {0};
  beam.kind = "beam"; beam.has_actor = true; beam.actor = self->actor.id;
  beam.start = qa_q2_game_body(game, self).origin; beam.end = beam.start;
  beam.width = self->frame; beam.color = self->skin; beam.visible = false;
  game->host->emit(game->host->ctx, beam);
}
static void laser_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *activator) {
  (void)o;
  self->has_activator = activator != NULL;
  if (activator) self->activator = *activator;
  if ((sf(self) & 1) != 0) laser_off_fn(c, self, game);
  else laser_on_fn(c, self, game);
}
static void laser_start_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  self->frame = (sf(self) & 64) != 0 ? 16 : 4;
  self->render_flags = (double)((int64_t)self->render_flags | 0xa0);
  self->skin = (sf(self) & 2) != 0 ? (double)0xf2f2f0f0 : (sf(self) & 4) != 0 ? (double)0xd0d1d2d3
      : (sf(self) & 8) != 0 ? (double)0xf3f3f1f1 : (sf(self) & 16) != 0 ? (double)0xdcdddedf
      : (sf(self) & 32) != 0 ? (double)0xe0e1e2e3 : 0;
  if (!self->has_enemy) {
    if (self->target[0] != '\0') {
      size_t count = 0;
      qa_q2_entity *const *targets = game->targets(game->ctx, self->target, &count);
      if (count > 0) { self->has_enemy = true; self->enemy = targets[0]->actor.id; }
      if (!self->has_enemy) {
        char message[512];
        (void)snprintf(message, sizeof message, "target_laser has missing target %s", self->target);
        game->host->diagnostic(game->host->ctx, message);
      }
    } else {
      self->movedir = qa_q2_movedir(qa_q2_game_body(game, self).angles);
      qa_q2_body_changes changes = {0};
      changes.has_angles = true; changes.angles = QA_Q2_ZERO;
      game->move(game->ctx, self, changes, false);
    }
  }
  if (self->damage == 0) self->damage = 1;
  qa_q2_body_changes changes = {0};
  changes.has_bounds = true;
  changes.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
  game->move(game->ctx, self, changes, true);
  self->use = (qa_q2_use){c, laser_use_fn};
  if ((sf(self) & 1) != 0) laser_on_fn(c, self, game);
  else laser_off_fn(c, self, game);
}

static void ramp_think_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  qa_q2_entity *target = self->has_enemy ? game->entity(game->ctx, &self->enemy) : NULL;
  if (!target) return;
  double elapsed = game->host->now(game->host->ctx) - self->timestamp;
  double from = self->movedir.x, to = self->movedir.y;
  double level = trunc(97 + from + elapsed / self->speed * (to - from));
  char pattern[2] = {(char)((int64_t)level & 255), '\0'};
  qa_q2_presentation_event ev = {0};
  ev.kind = "lightstyle"; ev.style = qa_q2_integer_field(&target->spawn, "style", 0); ev.pattern = pattern;
  game->host->emit(game->host->ctx, ev);
  if (elapsed < self->speed) game->schedule(game->ctx, self, game->host->frame_seconds(game->host->ctx), (qa_q2_think){c, ramp_think_fn});
  else if ((sf(self) & 1) != 0) { self->movedir.x = to; self->movedir.y = from; }
}
static void ramp_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  qa_q2_entity *existing = self->has_enemy ? game->entity(game->ctx, &self->enemy) : NULL;
  if (!existing) {
    size_t count = 0;
    qa_q2_entity *const *targets = game->targets(game->ctx, self->target, &count);
    for (size_t i = 0; i < count; i++) {
      if (strcmp(targets[i]->classname, "light") == 0) { self->has_enemy = true; self->enemy = targets[i]->actor.id; }
      else {
        char message[512];
        (void)snprintf(message, sizeof message, "target_lightramp target %s is not a light", targets[i]->classname);
        game->host->diagnostic(game->host->ctx, message);
      }
    }
    if (!self->has_enemy) {
      char message[512];
      (void)snprintf(message, sizeof message, "target_lightramp missing target %s", self->target);
      game->host->diagnostic(game->host->ctx, message);
      game->remove(game->ctx, self);
      return;
    }
  }
  self->timestamp = game->host->now(game->host->ctx);
  ramp_think_fn(c, self, game);
}

static void temp_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  char effect[64];
  (void)snprintf(effect, sizeof effect, "q2:temp-%d", (int)((int64_t)qa_q2_integer_field(&self->spawn, "style", 0) & 255));
  qa_q2_presentation_event ev = {0};
  ev.kind = "effect"; ev.effect = effect; ev.origin = qa_q2_game_body(game, self).origin;
  ev.direction = QA_Q2_ZERO; ev.count = 1; ev.color = 0;
  game->host->emit(game->host->ctx, ev);
}

static void spawner_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  qa_body_state body = qa_q2_game_body(game, self);
  char origin[128], angles[128];
  (void)snprintf(origin, sizeof origin, "%g %g %g", body.origin.x, body.origin.y, body.origin.z);
  (void)snprintf(angles, sizeof angles, "%g %g %g", body.angles.x, body.angles.y, body.angles.z);
  qa_q2_spawn_fields fields = {0};
  fields.ordinal = -1;
  fields.classname = self->target;
  if (!qa_q2_spawn_set(&fields, "classname", self->target) || !qa_q2_spawn_set(&fields, "origin", origin) ||
      !qa_q2_spawn_set(&fields, "angles", angles)) qa_range_error("Out of memory");
  qa_q2_entity *spawned = game->spawn(game->ctx, fields);
  if (!qa_session_actor_registry_is_live(game->host->actors, spawned->actor.id)) return;
  qa_shared_body_table_unlink(game->host->bodies, spawned->actor);
  (void)qa_q2_kill_box(spawned, game);
  game->link(game->ctx, spawned);
  if (self->speed != 0) {
    qa_q2_body_changes changes = {0};
    changes.has_velocity = true; changes.velocity = self->movedir;
    game->move(game->ctx, spawned, changes, false);
    game->motion(game->ctx, spawned, spawned->motion);
  }
}

static void blaster_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  qa_q2_base_targets *t = c;
  double effects = game->options.edition == QA_Q2_EDITION_CLASSIC ? 8 : (sf(self) & 2) != 0 ? 0 : (sf(self) & 1) != 0 ? 64 : 8;
  (void)t->hooks.fire_blaster(t->hooks.weapons_ctx, self, game, qa_q2_game_body(game, self).origin, self->movedir,
                              self->damage, self->speed, effects, false, true, 33);
  game->sound(game->ctx, self->actor, "weapons/laser2.wav", 2, 1, 1);
}

static void crosslevel_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  game->counters->server_flags = (double)((int64_t)game->counters->server_flags | sf(self));
  game->remove(game->ctx, self);
}
static void crosslevel_think_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  (void)c;
  if (self->spawnflags == (double)((int64_t)game->counters->server_flags & 255 & sf(self))) {
    qa_actor_id self_id = self->actor.id;
    qa_q2_game_use_targets_entity(game, self, &self_id, false);
    game->remove(game->ctx, self);
  }
}

static void quake_think_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  if (self->wait < game->host->now(game->host->ctx)) {
    game->sound(game->ctx, self->actor, "world/quake.wav", 0, 1, 0);
    self->wait = game->host->now(game->host->ctx) + 0.5;
  }
  size_t count = 0;
  const qa_actor_id *players = game->host->players(game->host->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, players[i]);
    qa_owned_actor actor;
    if (!body.has_value || !qa_session_actor_registry_resolve_owned(game->host->actors, players[i], &actor) || body.value.ground == NULL) continue;
    qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, players[i]);
    double mass = combat.has_value ? combat.value.mass : 200;
    qa_body_state next = body.value;
    next.ground = NULL;
    next.velocity = (qa_vec3){body.value.velocity.x + (game->host->random(game->host->ctx) * 2 - 1) * 150,
                              body.value.velocity.y + (game->host->random(game->host->ctx) * 2 - 1) * 150,
                              self->speed * (100 / mass)};
    qa_shared_body_table_write(game->host->bodies, actor, next);
  }
  if (game->host->now(game->host->ctx) < self->timestamp)
    game->schedule(game->ctx, self, game->host->frame_seconds(game->host->ctx), (qa_q2_think){c, quake_think_fn});
}
static void quake_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *activator) {
  (void)o;
  self->timestamp = game->host->now(game->host->ctx) + self->count;
  self->wait = 0;
  self->has_activator = activator != NULL;
  if (activator) self->activator = *activator;
  game->schedule(game->ctx, self, game->host->frame_seconds(game->host->ctx), (qa_q2_think){c, quake_think_fn});
}

qa_q2_base_targets *qa_q2_base_targets_create(qa_q2_base_entity_hooks hooks) {
  qa_q2_base_targets *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  t->hooks = hooks;
  t->thinks[0] = (qa_q2_named_think){"target_laser_think", QA_Q2_TARGET_LASER_THINK};
  t->thinks[1] = (qa_q2_named_think){"target_laser_start", {t, laser_start_fn}};
  t->thinks[2] = (qa_q2_named_think){"target_laser_on", {t, laser_on_fn}};
  t->thinks[3] = (qa_q2_named_think){"target_laser_off", {t, laser_off_fn}};
  t->thinks[4] = (qa_q2_named_think){"target_lightramp_think", {t, ramp_think_fn}};
  t->thinks[5] = (qa_q2_named_think){"target_crosslevel_target_think", {t, crosslevel_think_fn}};
  t->thinks[6] = (qa_q2_named_think){"target_earthquake_think", {t, quake_think_fn}};
  t->uses[0] = (qa_q2_named_use){"target_laser_use", {t, laser_use_fn}};
  t->uses[1] = (qa_q2_named_use){"target_lightramp_use", {t, ramp_use_fn}};
  t->uses[2] = (qa_q2_named_use){"Use_Target_Tent", {t, temp_use_fn}};
  t->uses[3] = (qa_q2_named_use){"use_target_spawner", {t, spawner_use_fn}};
  t->uses[4] = (qa_q2_named_use){"use_target_blaster", {t, blaster_use_fn}};
  t->uses[5] = (qa_q2_named_use){"target_crosslevel_trigger_use", {t, crosslevel_use_fn}};
  t->uses[6] = (qa_q2_named_use){"target_earthquake_use", {t, quake_use_fn}};
  t->defs = (qa_q2_callback_definitions){.think = t->thinks, .think_count = 7, .use = t->uses, .use_count = 7};
  return t;
}
void qa_q2_base_targets_destroy(qa_q2_base_targets *t) { free(t); }
qa_q2_callback_definitions qa_q2_base_targets_callbacks(qa_q2_base_targets *t) { return t->defs; }

bool qa_q2_base_targets_spawn(qa_q2_base_targets *t, qa_q2_entity *entity, qa_q2_game_services *game) {
  const char *classname = entity->classname;
  if (strcmp(classname, "target_temp_entity") == 0) { entity->use = (qa_q2_use){t, temp_use_fn}; return true; }
  if (strcmp(classname, "target_spawner") == 0) {
    entity->visible = false;
    if (entity->speed != 0) {
      entity->movedir = qa_q2_scale(qa_q2_movedir(qa_q2_game_body(game, entity).angles), entity->speed);
      qa_q2_body_changes changes = {0};
      changes.has_angles = true; changes.angles = QA_Q2_ZERO;
      game->move(game->ctx, entity, changes, false);
    }
    entity->use = (qa_q2_use){t, spawner_use_fn};
    return true;
  }
  if (strcmp(classname, "target_blaster") == 0) {
    entity->visible = false;
    if (entity->damage == 0) entity->damage = 15;
    if (entity->speed == 0) entity->speed = 1000;
    entity->movedir = qa_q2_movedir(qa_q2_game_body(game, entity).angles);
    qa_q2_body_changes changes = {0};
    changes.has_angles = true; changes.angles = QA_Q2_ZERO;
    game->move(game->ctx, entity, changes, false);
    entity->use = (qa_q2_use){t, blaster_use_fn};
    return true;
  }
  if (strcmp(classname, "target_crosslevel_trigger") == 0) {
    entity->visible = false;
    entity->use = (qa_q2_use){t, crosslevel_use_fn};
    return true;
  }
  if (strcmp(classname, "target_crosslevel_target") == 0) {
    entity->visible = false;
    if (entity->delay == 0) entity->delay = 1;
    game->schedule(game->ctx, entity, entity->delay, (qa_q2_think){t, crosslevel_think_fn});
    return true;
  }
  if (strcmp(classname, "target_laser") == 0) {
    game->schedule(game->ctx, entity, 1, (qa_q2_think){t, laser_start_fn});
    return true;
  }
  if (strcmp(classname, "target_lightramp") == 0) {
    const char *message = entity->message;
    bool valid = message[0] >= 'a' && message[0] <= 'z' && message[1] >= 'a' && message[1] <= 'z' && message[2] == '\0';
    if (!valid || message[0] == message[1] || entity->target[0] == '\0' || game->options.mode == QA_Q2_MODE_DEATHMATCH) {
      if (game->options.mode != QA_Q2_MODE_DEATHMATCH) {
        char diagnostic[512];
        (void)snprintf(diagnostic, sizeof diagnostic, "Invalid target_lightramp %s", message);
        game->host->diagnostic(game->host->ctx, diagnostic);
      }
      game->remove(game->ctx, entity);
      return true;
    }
    entity->movedir = (qa_vec3){message[0] - 97, message[1] - 97, 0};
    entity->timestamp = 0;
    entity->use = (qa_q2_use){t, ramp_use_fn};
    return true;
  }
  if (strcmp(classname, "target_earthquake") == 0) {
    entity->visible = false;
    if (entity->count == 0) entity->count = 5;
    if (entity->speed == 0) entity->speed = 200;
    entity->timestamp = 0;
    entity->wait = 0;
    entity->use = (qa_q2_use){t, quake_use_fn};
    return true;
  }
  return false;
}
