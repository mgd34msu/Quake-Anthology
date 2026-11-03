/* Port of src/content/q2/base/entities/triggers.ts. Quake II
 * g_trigger.c environmental triggers. GPL-2.0-or-later. */
#include "qa/content/q2/base/entities/triggers.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"

typedef struct wind_entry { qa_actor_id actor; double until; } wind_entry;

struct qa_q2_environmental_triggers {
  qa_q2_base_entity_hooks hooks;
  wind_entry *wind; size_t wind_count, wind_cap;
  bool connected;
  qa_session_actor_registry *registry;
  int64_t subscription;
  qa_q2_named_touch touches[4];
  qa_q2_named_use uses[1];
  qa_q2_callback_definitions defs;
};

static void push_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void hurt_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void gravity_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void jump_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void hurt_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);

/* TS `spawnflags & x` applies ToInt32: the int32 view of the number. */
static int32_t sf(const qa_q2_entity *e) { return qa_js_to_int32(e->spawnflags); }

static wind_entry *wind_find(qa_q2_environmental_triggers *t, qa_actor_id actor) {
  for (size_t i = 0; i < t->wind_count; i++) if (qa_same_actor(t->wind[i].actor, actor)) return &t->wind[i];
  return NULL;
}
static void wind_set(qa_q2_environmental_triggers *t, qa_actor_id actor, double until) {
  wind_entry *existing = wind_find(t, actor);
  if (existing) { existing->until = until; return; }
  if (t->wind_count == t->wind_cap) {
    size_t cap = t->wind_cap ? t->wind_cap * 2 : 8;
    wind_entry *grown = realloc(t->wind, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    t->wind = grown; t->wind_cap = cap;
  }
  t->wind[t->wind_count++] = (wind_entry){actor, until};
}
static void wind_release(void *ctx, qa_owned_actor actor) {
  qa_q2_environmental_triggers *t = ctx;
  for (size_t i = 0; i < t->wind_count; i++) {
    if (qa_same_actor(t->wind[i].actor, actor.id)) {
      t->wind[i] = t->wind[--t->wind_count];
      return;
    }
  }
}
static void connect(qa_q2_environmental_triggers *t, qa_q2_game_services *game) {
  if (!t->connected) {
    t->registry = game->host->actors;
    t->subscription = qa_session_actor_registry_on_release(game->host->actors, wind_release, t);
    t->connected = true;
  }
}

/* TS velocity(): has_ground false keeps the body's ground. */
static void set_velocity(qa_q2_game_services *game, qa_actor_id actor, qa_vec3 value, bool has_ground, const qa_actor_id *ground) {
  qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, actor);
  qa_owned_actor owner;
  if (!body.has_value || !qa_session_actor_registry_resolve_owned(game->host->actors, actor, &owner)) return;
  qa_body_state next = body.value;
  next.velocity = value;
  if (has_ground) next.ground = ground;
  qa_shared_body_table_write(game->host->bodies, owner, next);
  qa_q2_entity *entity = game->entity(game->ctx, &actor);
  if (entity) game->motion(game->ctx, entity, entity->motion);
}

static void init_trigger(qa_q2_entity *entity, qa_q2_game_services *game) {
  /* InitTrigger leaves movedir zero for an unrotated brush. */
  qa_vec3 body_angles = qa_q2_game_body(game, entity).angles;
  entity->visible = false;
  entity->movedir = body_angles.x == 0 && body_angles.y == 0 && body_angles.z == 0 ? QA_Q2_ZERO : qa_q2_movedir(body_angles);
  qa_q2_body_changes angles = {0};
  angles.has_angles = true; angles.angles = QA_Q2_ZERO;
  game->move(game->ctx, entity, angles, false);
  game->solid(game->ctx, entity, "trigger");
}

qa_q2_environmental_triggers *qa_q2_environmental_triggers_create(qa_q2_base_entity_hooks hooks) {
  qa_q2_environmental_triggers *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  t->hooks = hooks;
  t->touches[0] = (qa_q2_named_touch){"trigger_push_touch", {t, push_touch_fn}};
  t->touches[1] = (qa_q2_named_touch){"hurt_touch", {t, hurt_touch_fn}};
  t->touches[2] = (qa_q2_named_touch){"trigger_gravity_touch", {t, gravity_touch_fn}};
  t->touches[3] = (qa_q2_named_touch){"trigger_monsterjump_touch", {t, jump_touch_fn}};
  t->uses[0] = (qa_q2_named_use){"hurt_use", {t, hurt_use_fn}};
  t->defs = (qa_q2_callback_definitions){.touch = t->touches, .touch_count = 4, .use = t->uses, .use_count = 1};
  return t;
}
void qa_q2_environmental_triggers_destroy(qa_q2_environmental_triggers *t) {
  if (!t) return;
  /* Unsubscribe first: the registry must not dispatch into freed
   * state during later actor releases. */
  if (t->connected) qa_session_actor_registry_off_release(t->registry, t->subscription);
  free(t->wind); free(t);
}
qa_q2_callback_definitions qa_q2_environmental_triggers_callbacks(qa_q2_environmental_triggers *t) { return t->defs; }

qa_q2_base_wind_time *qa_q2_environmental_triggers_capture(qa_q2_environmental_triggers *t, qa_q2_game_services *game, size_t *out_count) {
  qa_q2_base_wind_time *out = NULL;
  size_t count = 0, cap = 0;
  for (size_t i = 0; i < t->wind_count; i++) {
    if (!qa_session_actor_registry_is_live(game->host->actors, t->wind[i].actor)) continue;
    if (count == cap) { cap = cap ? cap * 2 : 8; out = realloc(out, cap * sizeof *out); if (!out) qa_range_error("Out of memory"); }
    out[count++] = (qa_q2_base_wind_time){{(double)t->wind[i].actor.slot, (double)t->wind[i].actor.generation}, t->wind[i].until};
  }
  *out_count = count;
  return out;
}
void qa_q2_environmental_triggers_restore(qa_q2_environmental_triggers *t, qa_q2_game_services *game, const qa_q2_base_wind_time *checkpoint, size_t count) {
  connect(t, game);
  t->wind_count = 0;
  for (size_t i = 0; i < count; i++) wind_set(t, qa_q2_restore_actor(game, checkpoint[i].actor).id, checkpoint[i].until);
}

static void push_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, qa_touch_contact contact) {
  qa_q2_environmental_triggers *t = c;
  qa_q2_entity *other = services->entity(services->ctx, &contact.other);
  qa_combat_state_result combat = qa_gameplay_authority_read(services->host->combat, contact.other);
  if ((other && strcmp(other->classname, "grenade") == 0) || (combat.has_value ? combat.value.health : 0) > 0) {
    qa_vec3 push = qa_q2_scale(self->movedir, self->speed * 10);
    set_velocity(services, contact.other, push, false, NULL);
    if (services->host->is_player(services->host->ctx, contact.other)) {
      t->hooks.player_push(t->hooks.ctx, contact.other, push);
      wind_entry *until = wind_find(t, contact.other);
      if ((until ? until->until : 0) < services->host->now(services->host->ctx)) {
        wind_set(t, contact.other, services->host->now(services->host->ctx) + 1.5);
        qa_body_state_result body = qa_shared_body_table_read(services->host->bodies, contact.other);
        if (body.has_value) {
          qa_q2_presentation_event ev = {0};
          ev.kind = "sound"; ev.has_actor = true; ev.actor = contact.other; ev.origin = body.value.origin;
          ev.path = "misc/windfly.wav"; ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.reliable = false; ev.loop = "once";
          services->host->emit(services->host->ctx, ev);
        }
      }
    }
  }
  if ((sf(self) & 1) != 0) services->remove(services->ctx, self);
}

static void hurt_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, qa_touch_contact contact) {
  double left2;
  double right2;
  (void)c;
  qa_body_state_result body = qa_shared_body_table_read(services->host->bodies, contact.other);
  qa_combat_state_result combat = qa_gameplay_authority_read(services->host->combat, contact.other);
  if (!body.has_value || !combat.has_value || !combat.value.can_take_damage || self->timestamp > services->host->now(services->host->ctx)) return;
  double left = services->host->now(services->host->ctx);
  double right = ((sf(self) & 16) != 0 ? 1 : services->host->frame_seconds(services->host->ctx));
  self->timestamp = left + right;
  if ((sf(self) & 4) == 0 && fmod(qa_js_round((left2 = services->host->now(services->host->ctx), right2 = services->host->frame_seconds(services->host->ctx), left2 / right2)), 10) == 0) {
    qa_q2_presentation_event ev = {0};
    ev.kind = "sound"; ev.has_actor = true; ev.actor = contact.other; ev.origin = body.value.origin;
    ev.path = "world/electro.wav"; ev.channel = 0; ev.volume = 1; ev.attenuation = 1; ev.reliable = false; ev.loop = "once";
    services->host->emit(services->host->ctx, ev);
  }
  qa_actor_id self_id = self->actor.id;
  services->damage(services->ctx, contact.other, self, self->actor.id, &self_id, self->damage, self->damage, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 31, (sf(self) & 8) != 0 ? 32 : 0, NULL);
}

static void gravity_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, qa_touch_contact contact) {
  qa_q2_environmental_triggers *t = c;
  qa_q2_entity *other = services->entity(services->ctx, &contact.other);
  if (other) { other->gravity = self->gravity; services->motion(services->ctx, other, other->motion); }
  t->hooks.set_actor_gravity(t->hooks.ctx, contact.other, self->gravity);
}

static void jump_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, qa_touch_contact contact) {
  (void)c;
  qa_q2_entity *other = services->entity(services->ctx, &contact.other);
  qa_body_state_result body = qa_shared_body_table_read(services->host->bodies, contact.other);
  if (!body.has_value || !services->host->is_monster(services->host->ctx, contact.other)) return;
  if (other && ((qa_js_to_int32(other->flags) & 3) != 0 || (qa_js_to_int32(other->server_flags) & 2) != 0)) return;
  qa_vec3 value = {self->movedir.x * self->speed, self->movedir.y * self->speed,
                   body.value.ground == NULL ? body.value.velocity.z : self->movedir.z};
  set_velocity(services, contact.other, value, true, NULL);
}

static void hurt_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  services->solid(services->ctx, self, strcmp(self->solid, "none") == 0 ? "trigger" : "none");
  if ((sf(self) & 2) == 0) self->use = (qa_q2_use){0};
}

bool qa_q2_environmental_triggers_spawn(qa_q2_environmental_triggers *t, qa_q2_entity *entity, qa_q2_game_services *game) {
  connect(t, game);
  const char *classname = entity->classname;
  if (strcmp(classname, "trigger_push") == 0) {
    init_trigger(entity, game);
    if (entity->speed == 0) entity->speed = 1000;
    entity->touch = (qa_q2_touch){t, push_touch_fn};
    return true;
  }
  if (strcmp(classname, "trigger_hurt") == 0) {
    init_trigger(entity, game);
    if (entity->damage == 0) entity->damage = 5;
    entity->timestamp = 0;
    if ((sf(entity) & 1) != 0) game->solid(game->ctx, entity, "none");
    if ((sf(entity) & 2) != 0) entity->use = (qa_q2_use){t, hurt_use_fn};
    entity->touch = (qa_q2_touch){t, hurt_touch_fn};
    return true;
  }
  if (strcmp(classname, "trigger_gravity") == 0) {
    if (!qa_q2_spawn_has(&entity->spawn, "gravity")) {
      game->host->diagnostic(game->host->ctx, "trigger_gravity without gravity set");
      game->remove(game->ctx, entity);
      return true;
    }
    init_trigger(entity, game);
    entity->gravity = game->options.edition == QA_Q2_EDITION_CLASSIC
        ? qa_q2_integer_field(&entity->spawn, "gravity", 0) : qa_q2_number_field(&entity->spawn, "gravity", 0);
    entity->touch = (qa_q2_touch){t, gravity_touch_fn};
    return true;
  }
  if (strcmp(classname, "trigger_monsterjump") == 0) {
    if (entity->speed == 0) entity->speed = 200;
    qa_vec3 angles = qa_q2_game_body(game, entity).angles;
    if (angles.y == 0) {
      qa_q2_body_changes changes = {0};
      changes.has_angles = true; changes.angles = angles; changes.angles.y = 360;
      game->move(game->ctx, entity, changes, false);
    }
    init_trigger(entity, game);
    double height = qa_q2_number_field(&entity->spawn, "height", 0);
    entity->movedir.z = height != 0 ? height : 200;
    entity->touch = (qa_q2_touch){t, jump_touch_fn};
    return true;
  }
  return false;
}
