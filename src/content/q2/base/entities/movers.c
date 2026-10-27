/* Port of src/content/q2/base/entities/movers.ts. Quake II g_func.c
 * platforms, secret doors and supporting brush entities.
 * GPL-2.0-or-later. */
#include "qa/content/q2/base/entities/movers.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/scenery.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

typedef struct plat_entry { qa_q2_entity *e; qa_q2_base_platform_state state; } plat_entry;
typedef struct secret_entry { qa_q2_entity *e; qa_q2_base_secret_state state; } secret_entry;

struct qa_q2_base_mover_entities {
  qa_q2_base_entity_hooks hooks;
  qa_q2_linear_motion *linear;
  plat_entry *platforms; size_t platform_count, platform_cap;
  secret_entry *secrets; size_t secret_count, secret_cap;
  qa_q2_named_think *thinks; size_t think_count;
  qa_q2_named_use uses[6];
  qa_q2_named_touch touches[3];
  qa_q2_named_die dies[1];
  qa_q2_named_blocked blockeds[2];
  qa_q2_callback_definitions defs;
};

#define TCB(m, fn) ((qa_q2_think){(m), (fn)})
#define UCB(m, fn) ((qa_q2_use){(m), (fn)})
#define XCB(m, fn) ((qa_q2_touch){(m), (fn)})
#define DCB(m, fn) ((qa_q2_die){(m), (fn)})
#define BCB(m, fn) ((qa_q2_blocked){(m), (fn)})

static void bottom_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void top_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void down_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void up_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void secret_done_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void secret_home_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void secret_return_pause_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void secret_return_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void secret_opened_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void secret_second_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void secret_pause_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void elevator_init_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void object_release_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void platform_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void secret_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void elevator_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void conveyor_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void killbox_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void object_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void platform_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void secret_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void object_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void secret_die_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_death_reaction);
static void platform_blocked_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_actor_id);
static void secret_blocked_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_actor_id);

static int64_t sf(const qa_q2_entity *e) { return (int64_t)e->spawnflags; }

static qa_q2_base_platform_state *find_platform(qa_q2_base_mover_entities *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->platform_count; i++) if (m->platforms[i].e == e) return &m->platforms[i].state;
  return NULL;
}
static qa_q2_base_secret_state *find_secret(qa_q2_base_mover_entities *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->secret_count; i++) if (m->secrets[i].e == e) return &m->secrets[i].state;
  return NULL;
}
static qa_q2_base_platform_state *platform_of(qa_q2_base_mover_entities *m, qa_q2_entity *e) {
  qa_q2_base_platform_state *s = find_platform(m, e);
  if (!s) qa_range_error("Platform callback without platform state");
  return s;
}
static qa_q2_base_secret_state *secret_of(qa_q2_base_mover_entities *m, qa_q2_entity *e) {
  qa_q2_base_secret_state *s = find_secret(m, e);
  if (!s) qa_range_error("Secret door callback without source state");
  return s;
}
static void set_platform(qa_q2_base_mover_entities *m, qa_q2_entity *e, qa_q2_base_platform_state state) {
  qa_q2_base_platform_state *existing = find_platform(m, e);
  if (existing) { *existing = state; return; }
  if (m->platform_count == m->platform_cap) {
    size_t cap = m->platform_cap ? m->platform_cap * 2 : 8;
    plat_entry *grown = realloc(m->platforms, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    m->platforms = grown; m->platform_cap = cap;
  }
  m->platforms[m->platform_count++] = (plat_entry){e, state};
}
static void set_secret(qa_q2_base_mover_entities *m, qa_q2_entity *e, qa_q2_base_secret_state state) {
  qa_q2_base_secret_state *existing = find_secret(m, e);
  if (existing) { *existing = state; return; }
  if (m->secret_count == m->secret_cap) {
    size_t cap = m->secret_cap ? m->secret_cap * 2 : 8;
    secret_entry *grown = realloc(m->secrets, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    m->secrets = grown; m->secret_cap = cap;
  }
  m->secrets[m->secret_count++] = (secret_entry){e, state};
}

static void loop_sound(qa_q2_entity *entity, qa_q2_game_services *game, const char *path, bool start) {
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = entity->actor.id;
  ev.origin = qa_q2_game_body(game, entity).origin; ev.path = path;
  ev.channel = 2; ev.volume = 1; ev.attenuation = 3; ev.reliable = false;
  ev.loop = start ? "start" : "stop";
  game->host->emit(game->host->ctx, ev);
}

static void portals(qa_q2_entity *entity, qa_q2_game_services *game, bool open) {
  size_t count = 0;
  qa_q2_entity *const *targets = game->targets(game->ctx, entity->target, &count);
  for (size_t i = 0; i < count; i++)
    if (strcmp(targets[i]->classname, "func_areaportal") == 0)
      game->host->set_area_portal(game->host->ctx, qa_q2_integer_field(&targets[i]->spawn, "style", 0), open);
}

/* Returns false after destroying a non-player obstruction, matching
 * source blocked callbacks. */
static bool crush(qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id other) {
  qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, other);
  if (!body.has_value) return false;
  qa_actor_id self = entity->actor.id;
  if (!game->host->is_player(game->host->ctx, other) && !game->host->is_monster(game->host->ctx, other)) {
    qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, other);
    if (combat.has_value && combat.value.can_take_damage)
      game->damage(game->ctx, other, entity, entity->actor.id, &self, 100000, 1, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 20, 0, NULL);
    qa_q2_entity *obstacle = game->entity(game->ctx, &other);
    if (obstacle) {
      qa_q2_presentation_event ev = {0};
      ev.kind = "effect"; ev.effect = "q2:explosion1"; ev.origin = body.value.origin;
      ev.direction = QA_Q2_ZERO; ev.count = 1; ev.color = 0;
      game->host->emit(game->host->ctx, ev);
      game->remove(game->ctx, obstacle);
    }
    return false;
  }
  game->damage(game->ctx, other, entity, entity->actor.id, &self, entity->damage, 1, QA_Q2_ZERO, body.value.origin, QA_Q2_ZERO, 20, 0, NULL);
  return true;
}

qa_q2_base_mover_entities *qa_q2_base_mover_entities_create(qa_q2_base_entity_hooks hooks) {
  qa_q2_base_mover_entities *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->hooks = hooks;
  m->linear = qa_q2_linear_motion_create("q2:base/linear");
  if (!m->linear) { free(m); return NULL; }
  qa_q2_callback_definitions linear_defs = qa_q2_linear_motion_callbacks(m->linear);
  static const struct { const char *name; void (*fn)(void *, qa_q2_entity *, qa_q2_game_services *); } extra[] = {
      {"plat_hit_bottom", bottom_fn}, {"plat_hit_top", top_fn}, {"plat_go_down", down_fn}, {"plat_go_up", up_fn},
      {"door_secret_done", secret_done_fn}, {"door_secret_move6", secret_home_fn}, {"door_secret_move5", secret_return_pause_fn},
      {"door_secret_move4", secret_return_fn}, {"door_secret_move3", secret_opened_fn}, {"door_secret_move2", secret_second_fn},
      {"door_secret_move1", secret_pause_fn}, {"trigger_elevator_init", elevator_init_fn}, {"func_object_release", object_release_fn}};
  size_t extra_count = sizeof extra / sizeof extra[0];
  m->thinks = malloc((linear_defs.think_count + extra_count) * sizeof *m->thinks);
  if (!m->thinks) { qa_q2_linear_motion_destroy(m->linear); free(m); return NULL; }
  size_t n = 0;
  for (size_t i = 0; i < linear_defs.think_count; i++) m->thinks[n++] = linear_defs.think[i];
  for (size_t i = 0; i < extra_count; i++) m->thinks[n++] = (qa_q2_named_think){extra[i].name, {m, extra[i].fn}};
  m->think_count = n;
  m->uses[0] = (qa_q2_named_use){"plat_use", {m, platform_use_fn}};
  m->uses[1] = (qa_q2_named_use){"door_secret_use", {m, secret_use_fn}};
  m->uses[2] = (qa_q2_named_use){"trigger_elevator_use", {m, elevator_use_fn}};
  m->uses[3] = (qa_q2_named_use){"func_conveyor_use", {m, conveyor_use_fn}};
  m->uses[4] = (qa_q2_named_use){"func_killbox_use", {m, killbox_use_fn}};
  m->uses[5] = (qa_q2_named_use){"func_object_use", {m, object_use_fn}};
  m->touches[0] = (qa_q2_named_touch){"Touch_Plat_Center", {m, platform_touch_fn}};
  m->touches[1] = (qa_q2_named_touch){"door_secret_touch", {m, secret_touch_fn}};
  m->touches[2] = (qa_q2_named_touch){"func_object_touch", {m, object_touch_fn}};
  m->dies[0] = (qa_q2_named_die){"door_secret_die", {m, secret_die_fn}};
  m->blockeds[0] = (qa_q2_named_blocked){"plat_blocked", {m, platform_blocked_fn}};
  m->blockeds[1] = (qa_q2_named_blocked){"door_secret_blocked", {m, secret_blocked_fn}};
  m->defs = (qa_q2_callback_definitions){.trajectory = linear_defs.trajectory, .trajectory_count = linear_defs.trajectory_count,
      .think = m->thinks, .think_count = m->think_count, .use = m->uses, .use_count = 6,
      .touch = m->touches, .touch_count = 3, .die = m->dies, .die_count = 1, .blocked = m->blockeds, .blocked_count = 2};
  return m;
}
void qa_q2_base_mover_entities_destroy(qa_q2_base_mover_entities *m) {
  if (!m) return;
  qa_q2_linear_motion_destroy(m->linear);
  free(m->platforms); free(m->secrets); free(m->thinks); free(m);
}
qa_q2_linear_motion *qa_q2_base_mover_entities_linear(qa_q2_base_mover_entities *m) { return m->linear; }
qa_q2_callback_definitions qa_q2_base_mover_entities_callbacks(qa_q2_base_mover_entities *m) { return m->defs; }

qa_q2_base_movers_checkpoint qa_q2_base_mover_entities_capture(qa_q2_base_mover_entities *m, qa_q2_game_services *game) {
  qa_q2_base_movers_checkpoint out = {0};
  size_t count = 0;
  qa_q2_entity *const *entities = game->entities(game->ctx, &count);
  size_t pcap = 0, scap = 0;
  for (size_t i = 0; i < count; i++) {
    qa_q2_entity *entity = entities[i];
    qa_saved_actor_id actor = {(double)entity->actor.id.slot, (double)entity->actor.id.generation};
    qa_q2_base_platform_state *platform = find_platform(m, entity);
    qa_q2_base_secret_state *secret = find_secret(m, entity);
    if (platform) {
      if (out.platform_count == pcap) { pcap = pcap ? pcap * 2 : 8; out.platforms = realloc(out.platforms, pcap * sizeof *out.platforms); if (!out.platforms) qa_range_error("Out of memory"); }
      out.platforms[out.platform_count++] = (qa_q2_base_platform_entry){actor, *platform};
    }
    if (secret) {
      if (out.secret_count == scap) { scap = scap ? scap * 2 : 8; out.secrets = realloc(out.secrets, scap * sizeof *out.secrets); if (!out.secrets) qa_range_error("Out of memory"); }
      out.secrets[out.secret_count++] = (qa_q2_base_secret_entry){actor, *secret};
    }
  }
  out.linear = qa_q2_linear_motion_capture(m->linear, game);
  return out;
}

static qa_q2_entity *saved_entity(qa_q2_game_services *game, qa_saved_actor_id actor) {
  qa_owned_actor owner = qa_q2_restore_actor(game, actor);
  qa_q2_entity *entity = game->entity(game->ctx, &owner.id);
  if (!entity) qa_range_error("Missing saved Q2 base mover");
  return entity;
}
void qa_q2_base_mover_entities_restore(qa_q2_base_mover_entities *m, qa_q2_game_services *game, qa_q2_base_movers_checkpoint checkpoint) {
  m->platform_count = 0; m->secret_count = 0;
  for (size_t i = 0; i < checkpoint.platform_count; i++) set_platform(m, saved_entity(game, checkpoint.platforms[i].actor), checkpoint.platforms[i].state);
  for (size_t i = 0; i < checkpoint.secret_count; i++) set_secret(m, saved_entity(game, checkpoint.secrets[i].actor), checkpoint.secrets[i].state);
  qa_q2_linear_motion_restore(m->linear, game, checkpoint.linear);
}

qa_q2_base_platform_state *qa_q2_base_mover_entities_platform_state(qa_q2_base_mover_entities *m, qa_q2_entity *entity) {
  return find_platform(m, entity);
}
qa_q2_base_traversal qa_q2_base_mover_entities_traversal(qa_q2_base_mover_entities *m, qa_q2_entity *entity) {
  qa_q2_base_platform_state *platform = find_platform(m, entity);
  qa_vec3 destination = QA_Q2_ZERO;
  bool has_destination = qa_q2_linear_motion_destination(m->linear, entity, &destination);
  if (!platform && !find_secret(m, entity)) return (qa_q2_base_traversal){0};
  qa_q2_base_traversal out = {0};
  out.has_value = true;
  out.locked = platform && strcmp(platform->phase, "up") == 0 && entity->targetname[0] != '\0' && !has_destination && entity->think.fn == NULL;
  out.has_destination = has_destination;
  out.destination = destination;
  return out;
}

static void platform_sound(qa_q2_entity *entity, qa_q2_game_services *game, bool start) {
  game->sound(game->ctx, entity->actor, start ? "plats/pt1_strt.wav" : "plats/pt1_end.wav", 2, 1, 3);
  loop_sound(entity, game, "plats/pt1_mid.wav", start);
}
static void bottom_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game) {
  platform_of(c, entity)->phase = "bottom";
  platform_sound(entity, game, false);
}
static void top_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game) {
  platform_of(c, entity)->phase = "top";
  platform_sound(entity, game, false);
  game->schedule(game->ctx, entity, 3, TCB(c, down_fn));
}
static void down_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_base_platform_state *state = platform_of(c, entity);
  state->phase = "down";
  platform_sound(entity, game, true);
  qa_q2_linear_motion_move_to(((qa_q2_base_mover_entities *)c)->linear, entity, game, state->bottom, TCB(c, bottom_fn));
}
static void up_fn(void *c, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_base_platform_state *state = platform_of(c, entity);
  state->phase = "up";
  platform_sound(entity, game, true);
  qa_q2_linear_motion_move_to(((qa_q2_base_mover_entities *)c)->linear, entity, game, state->top, TCB(c, top_fn));
}

static void platform_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  if (self->think.fn == NULL) down_fn(c, self, game);
}
static void platform_blocked_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, qa_actor_id other) {
  if (!crush(self, game, other)) return;
  const char *phase = platform_of(c, self)->phase;
  if (strcmp(phase, "up") == 0) down_fn(c, self, game);
  else if (strcmp(phase, "down") == 0) up_fn(c, self, game);
}
static void platform_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, qa_touch_contact contact) {
  if (!game->host->is_player(game->host->ctx, contact.other)) return;
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, contact.other);
  if ((combat.has_value ? combat.value.health : 0) <= 0) return;
  qa_q2_entity *platform = self->has_enemy ? game->entity(game->ctx, &self->enemy) : NULL;
  if (!platform) return;
  qa_q2_base_platform_state *state = platform_of(c, platform);
  if (strcmp(state->phase, "bottom") == 0) up_fn(c, platform, game);
  else if (strcmp(state->phase, "top") == 0) game->schedule(game->ctx, platform, 1, TCB(c, down_fn));
}

static void secret_done_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  if (secret_of(c, self)->shootable) {
    qa_gameplay_authority_set_health(game->host->combat, self->actor, 0);
    qa_combat_trait_changes tc = {0};
    tc.has_can_take_damage = true; tc.can_take_damage = true;
    qa_gameplay_authority_set_traits(game->host->combat, self->actor, tc);
  }
  portals(self, game, false);
}
static void secret_home_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  qa_q2_linear_motion_move_to(((qa_q2_base_mover_entities *)c)->linear, self, game, secret_of(c, self)->home, TCB(c, secret_done_fn));
}
static void secret_return_pause_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  game->schedule(game->ctx, self, 1, TCB(c, secret_home_fn));
}
static void secret_return_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  qa_q2_linear_motion_move_to(((qa_q2_base_mover_entities *)c)->linear, self, game, secret_of(c, self)->first, TCB(c, secret_return_pause_fn));
}
static void secret_opened_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  if (self->wait != -1) game->schedule(game->ctx, self, self->wait, TCB(c, secret_return_fn));
}
static void secret_second_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  qa_q2_linear_motion_move_to(((qa_q2_base_mover_entities *)c)->linear, self, game, secret_of(c, self)->second, TCB(c, secret_opened_fn));
}
static void secret_pause_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  game->schedule(game->ctx, self, 1, TCB(c, secret_second_fn));
}
static void secret_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  qa_vec3 origin = qa_q2_game_body(game, self).origin;
  qa_q2_base_secret_state *state = secret_of(c, self);
  if (origin.x != state->home.x || origin.y != state->home.y || origin.z != state->home.z) return;
  qa_q2_linear_motion_move_to(((qa_q2_base_mover_entities *)c)->linear, self, game, state->first, TCB(c, secret_pause_fn));
  portals(self, game, true);
}
static void secret_die_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, qa_death_reaction reaction) {
  (void)c;
  qa_combat_trait_changes tc = {0};
  tc.has_can_take_damage = true; tc.can_take_damage = false;
  qa_gameplay_authority_set_traits(game->host->combat, self->actor, tc);
  if (self->use.fn) self->use.fn(self->use.ctx, self, game, reaction.pain.attacker, reaction.pain.attacker);
}
static void secret_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, qa_touch_contact contact) {
  qa_q2_base_secret_state *state = secret_of(c, self);
  if (!game->host->is_player(game->host->ctx, contact.other) || state->message_time > game->host->now(game->host->ctx)) return;
  state->message_time = game->host->now(game->host->ctx) + 5;
  qa_q2_presentation_event ev = {0};
  ev.kind = "centerprint"; ev.has_actor = true; ev.actor = contact.other; ev.text = self->message;
  game->host->emit(game->host->ctx, ev);
  game->sound(game->ctx, self->actor, "misc/talk1.wav", 0, 1, 1);
}
static void secret_blocked_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, qa_actor_id other) {
  if (!game->host->is_player(game->host->ctx, other) && !game->host->is_monster(game->host->ctx, other)) { crush(self, game, other); return; }
  qa_q2_base_secret_state *state = secret_of(c, self);
  if (state->blocked_time > game->host->now(game->host->ctx)) return;
  state->blocked_time = game->host->now(game->host->ctx) + 0.5;
  crush(self, game, other);
}

static void elevator_init_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  qa_q2_entity *train = game->pick_target(game->ctx, self->target);
  if (!train || strcmp(train->classname, "func_train") != 0) {
    char message[512];
    (void)snprintf(message, sizeof message, "trigger_elevator has invalid train %s", self->target);
    game->host->diagnostic(game->host->ctx, message);
    return;
  }
  self->has_enemy = true; self->enemy = train->actor.id;
  self->visible = false;
  self->use = UCB(c, elevator_use_fn);
}
static void elevator_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *other, const qa_actor_id *a) {
  (void)a;
  qa_q2_base_mover_entities *m = c;
  qa_q2_entity *train = self->has_enemy ? game->entity(game->ctx, &self->enemy) : NULL;
  if (!train || train->has_next_think) return;
  qa_q2_entity *other_entity = other ? game->entity(game->ctx, other) : NULL;
  const char *path = other_entity ? qa_q2_spawn_get(&other_entity->spawn, "pathtarget") : NULL;
  if (!path) path = "";
  qa_q2_entity *corner = game->pick_target(game->ctx, path);
  if (!corner) {
    char message[512];
    (void)snprintf(message, sizeof message, "trigger_elevator used with invalid pathtarget %s", path);
    game->host->diagnostic(game->host->ctx, message);
    return;
  }
  qa_q2_mover_module_resume_train_at(m->hooks.movers, train, game, corner);
}
static void conveyor_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)game; (void)o; (void)a;
  if ((sf(self) & 1) != 0) { self->speed = 0; self->spawnflags = (double)(sf(self) & ~1LL); }
  else { self->speed = self->count; self->spawnflags = (double)(sf(self) | 1LL); }
  if ((sf(self) & 2) == 0) self->count = 0;
}
static void killbox_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  (void)qa_q2_kill_box(self, game);
}
static void object_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, qa_touch_contact contact) {
  (void)c;
  if (!contact.plane || contact.plane->normal.z < 1) return;
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, contact.other);
  if (!combat.has_value || !combat.value.can_take_damage) return;
  qa_actor_id self_id = self->actor.id;
  game->damage(game->ctx, contact.other, self, self->actor.id, &self_id, self->damage, 1, QA_Q2_ZERO, qa_q2_game_body(game, self).origin, QA_Q2_ZERO, 20, 0, NULL);
}
static void object_release_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  game->motion(game->ctx, self, "toss");
  self->touch = XCB(c, object_touch_fn);
}
static void object_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  self->visible = true;
  self->use = (qa_q2_use){0};
  game->solid(game->ctx, self, "brush");
  qa_q2_body_changes changes = {0};
  changes.has_bounds = true; changes.bounds = qa_q2_game_body(game, self).bounds;
  game->move(game->ctx, self, changes, true);
  (void)qa_q2_kill_box(self, game);
  game->show(game->ctx, self);
  object_release_fn(c, self, game);
}

static void spawn_platform(qa_q2_base_mover_entities *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_body_changes angles = {0};
  angles.has_angles = true; angles.angles = QA_Q2_ZERO;
  game->move(game->ctx, entity, angles, false);
  game->solid(game->ctx, entity, "brush");
  game->motion(game->ctx, entity, "push");
  entity->speed = entity->speed == 0 ? 20 : entity->speed * 0.1;
  entity->accel = entity->accel == 0 ? 5 : entity->accel * 0.1;
  entity->decel = entity->decel == 0 ? 5 : entity->decel * 0.1;
  if (entity->damage == 0) entity->damage = 2;
  qa_body_state body = qa_q2_game_body(game, entity);
  double lip = qa_q2_number_field(&entity->spawn, "lip", 0);
  if (lip == 0) lip = 8;
  double height = qa_q2_number_field(&entity->spawn, "height", 0);
  if (height == 0) height = body.bounds.max.z - body.bounds.min.z - lip;
  qa_vec3 top = body.origin, bottom = top;
  bottom.z = top.z - height;
  set_platform(m, entity, (qa_q2_base_platform_state){top, bottom, entity->targetname[0] != '\0' ? "up" : "bottom"});
  entity->use = UCB(m, platform_use_fn);
  entity->blocked = BCB(m, platform_blocked_fn);
  qa_q2_entity *trigger = game->create(game->ctx, "plat_trigger", NULL, 0);
  trigger->has_enemy = true; trigger->enemy = entity->actor.id;
  trigger->visible = false;
  double min_x = body.bounds.min.x + 25, max_x = body.bounds.max.x - 25;
  double min_y = body.bounds.min.y + 25, max_y = body.bounds.max.y - 25;
  double min_z = body.bounds.max.z + 8 - (height + lip);
  if (max_x - min_x <= 0) { min_x = (body.bounds.min.x + body.bounds.max.x) * 0.5; max_x = min_x + 1; }
  if (max_y - min_y <= 0) { min_y = (body.bounds.min.y + body.bounds.max.y) * 0.5; max_y = min_y + 1; }
  qa_q2_body_changes bounds = {0};
  bounds.has_bounds = true;
  bounds.bounds = (qa_bounds){{min_x, min_y, min_z}, {max_x, max_y, (sf(entity) & 1) != 0 ? min_z + 8 : body.bounds.max.z + 8}};
  game->move(game->ctx, trigger, bounds, false);
  trigger->touch = XCB(m, platform_touch_fn);
  game->solid(game->ctx, trigger, "trigger");
  if (entity->targetname[0] == '\0') {
    qa_q2_body_changes origin = {0};
    origin.has_origin = true; origin.origin = bottom;
    game->move(game->ctx, entity, origin, true);
  }
  game->show(game->ctx, entity);
}

static void spawn_secret(qa_q2_base_mover_entities *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_angle_vectors axes = qa_math_angle_vectors_rule(qa_q2_game_body(game, entity).angles, QA_ANGLE_RULE_NATIVE);
  qa_q2_body_changes angles = {0};
  angles.has_angles = true; angles.angles = QA_Q2_ZERO;
  game->move(game->ctx, entity, angles, false);
  game->solid(game->ctx, entity, "brush");
  game->motion(game->ctx, entity, "push");
  qa_body_state body = qa_q2_game_body(game, entity);
  qa_vec3 size = qa_q2_subtract(body.bounds.max, body.bounds.min);
  qa_vec3 side = (sf(entity) & 4) != 0 ? axes.up : axes.right;
  double width = fabs(qa_q2_dot(side, size));
  double length = fabs(qa_q2_dot(axes.forward, size));
  double along = (sf(entity) & 4) != 0 ? -width : (1 - (double)(sf(entity) & 2)) * width;
  qa_vec3 first = qa_q2_add(body.origin, qa_q2_scale(side, along));
  qa_vec3 second = qa_q2_add(first, qa_q2_scale(axes.forward, length));
  if (entity->damage == 0) entity->damage = 2;
  if (entity->wait == 0) entity->wait = 5;
  entity->speed = 50; entity->accel = 50; entity->decel = 50;
  bool shootable = entity->targetname[0] == '\0' || (sf(entity) & 1) != 0;
  if (shootable || entity->max_health != 0) {
    qa_combat_state state = {0};
    state.health = shootable ? 0 : entity->max_health;
    state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    state.armor.powered.kind = "none";
    state.mass = 0; state.can_take_damage = true; state.invulnerable = false; state.team = NULL;
    qa_gameplay_authority_create_state(game->host->combat, entity->actor, state, NULL, NULL);
  }
  set_secret(m, entity, (qa_q2_base_secret_state){first, second, QA_Q2_ZERO, shootable, 0, 0});
  entity->use = UCB(m, secret_use_fn);
  if (shootable || entity->max_health != 0) entity->die = DCB(m, secret_die_fn);
  else if (entity->targetname[0] != '\0' && entity->message[0] != '\0') entity->touch = XCB(m, secret_touch_fn);
  entity->blocked = BCB(m, secret_blocked_fn);
  game->show(game->ctx, entity);
}

bool qa_q2_base_mover_entities_spawn(qa_q2_base_mover_entities *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  const char *classname = entity->classname;
  if (strcmp(classname, "func_plat") == 0) { spawn_platform(m, entity, game); return true; }
  if (strcmp(classname, "func_door_secret") == 0) { spawn_secret(m, entity, game); return true; }
  if (strcmp(classname, "trigger_elevator") == 0) {
    game->schedule(game->ctx, entity, game->host->frame_seconds(game->host->ctx), TCB(m, elevator_init_fn));
    return true;
  }
  if (strcmp(classname, "func_conveyor") == 0) {
    if (entity->speed == 0) entity->speed = 100;
    if ((sf(entity) & 1) == 0) { entity->count = entity->speed; entity->speed = 0; }
    game->solid(game->ctx, entity, "brush");
    game->show(game->ctx, entity);
    entity->use = UCB(m, conveyor_use_fn);
    return true;
  }
  if (strcmp(classname, "func_killbox") == 0) {
    entity->visible = false;
    game->solid(game->ctx, entity, "none");
    entity->use = UCB(m, killbox_use_fn);
    return true;
  }
  if (strcmp(classname, "func_object") == 0) {
    game->solid(game->ctx, entity, "brush");
    game->motion(game->ctx, entity, "push");
    if (entity->damage == 0) entity->damage = 100;
    qa_bounds original = qa_q2_game_body(game, entity).bounds;
    qa_bounds bounds = {qa_q2_add(original.min, (qa_vec3){1, 1, 1}), qa_q2_add(original.max, (qa_vec3){-1, -1, -1})};
    if (entity->spawnflags == 0) game->schedule(game->ctx, entity, 2 * game->host->frame_seconds(game->host->ctx), TCB(m, object_release_fn));
    else { entity->visible = false; game->solid(game->ctx, entity, "none"); entity->use = UCB(m, object_use_fn); }
    if ((sf(entity) & 2) != 0) entity->effects = (double)((int64_t)entity->effects | 0x1000);
    if ((sf(entity) & 4) != 0) entity->effects = (double)((int64_t)entity->effects | 0x2000);
    entity->clip_mask = 0x2010003;
    qa_q2_body_changes changes = {0};
    changes.has_bounds = true; changes.bounds = bounds;
    game->move(game->ctx, entity, changes, true);
    game->show(game->ctx, entity);
    return true;
  }
  return false;
}


