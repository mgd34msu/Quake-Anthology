/* Port of src/content/q2/base/entities/scenery.ts. Quake II g_misc.c
 * clocks, decorative models and teleporters. GPL-2.0-or-later. */
#include "qa/content/q2/base/entities/scenery.h"
#include "qa/network/common/js_number.h"

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
#include "qa/foundation/js.h"

typedef struct anim_entry { qa_q2_entity *e; double first, end; } anim_entry;
typedef struct clock_entry { qa_q2_entity *e; double value; } clock_entry;
/* TS clock messages are immutable strings held by both the clock and its
 * display. Each entity gets its own heap text so a pointer stays valid when
 * the clock table grows or is restored, and a former display keeps its text. */
typedef struct message_slot { qa_q2_entity *e; char text[16]; } message_slot;

struct qa_q2_base_scenery {
  qa_q2_base_entity_hooks hooks;
  anim_entry *animations; size_t animation_count, animation_cap;
  clock_entry *clocks; size_t clock_count, clock_cap;
  message_slot **messages; size_t message_count, message_cap;
  qa_q2_named_think thinks[6];
  qa_q2_named_use uses[5];
  qa_q2_named_touch touches[2];
  qa_q2_named_die dies[1];
  qa_q2_callback_definitions defs;
};

static void animate_step_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void clock_tick_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void bomb_prethink_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void commander_collapse_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void commander_release_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void remove_think_fn(void *, qa_q2_entity *, qa_q2_game_services *);
static void clock_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void bomb_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void commander_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void remove_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void string_use_fn(void *, qa_q2_entity *, qa_q2_game_services *, const qa_actor_id *, const qa_actor_id *);
static void teleporter_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void bomb_touch_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_touch_contact);
static void remove_die_fn(void *, qa_q2_entity *, qa_q2_game_services *, qa_death_reaction);

/* TS `spawnflags & x` applies ToInt32: the int32 view of the number. */
static int32_t sf(const qa_q2_entity *e) { return qa_js_to_int32(e->spawnflags); }

/* JS String(number) for the integral clock values used here. */
static void js_number(char *out, size_t size, double value) {
  qa_net_js_number(value, out, size);
}
static void pad_start(char *out, size_t size, const char *text, size_t width, char fill) {
  size_t length = strlen(text);
  if (length >= width) { (void)snprintf(out, size, "%s", text); return; }
  size_t pad = width - length;
  if (pad + length + 1 > size) pad = size > length + 1 ? size - length - 1 : 0;
  memset(out, fill, pad);
  (void)snprintf(out + pad, size - pad, "%s", text);
}

void qa_q2_clock_text(double seconds, double style, char *out, size_t out_size) {
  char sec_raw[64], sec[64];
  js_number(sec_raw, sizeof sec_raw, fmod(seconds, 60));
  pad_start(sec, sizeof sec, sec_raw, 2, '0');
  if (style == 0) {
    char raw[64];
    js_number(raw, sizeof raw, seconds);
    pad_start(out, out_size, raw, 2, ' ');
    return;
  }
  if (style == 1) {
    char raw[64], minutes[64];
    js_number(raw, sizeof raw, trunc(seconds / 60));
    pad_start(minutes, sizeof minutes, raw, 2, ' ');
    (void)snprintf(out, out_size, "%s:%s", minutes, sec);
    return;
  }
  double hours = trunc(seconds / 3600);
  double minutes_value = trunc((seconds - hours * 3600) / 60);
  char raw[64], hour_text[64], minute_text[64];
  js_number(raw, sizeof raw, hours);
  pad_start(hour_text, sizeof hour_text, raw, 2, ' ');
  js_number(raw, sizeof raw, minutes_value);
  pad_start(minute_text, sizeof minute_text, raw, 2, '0');
  (void)snprintf(out, out_size, "%s:%s:%s", hour_text, minute_text, sec);
}

static anim_entry *find_animation(qa_q2_base_scenery *s, qa_q2_entity *e) {
  for (size_t i = 0; i < s->animation_count; i++) if (s->animations[i].e == e) return &s->animations[i];
  return NULL;
}
static clock_entry *find_clock(qa_q2_base_scenery *s, qa_q2_entity *e) {
  for (size_t i = 0; i < s->clock_count; i++) if (s->clocks[i].e == e) return &s->clocks[i];
  return NULL;
}
static clock_entry *clock_of(qa_q2_base_scenery *s, qa_q2_entity *e) {
  clock_entry *entry = find_clock(s, e);
  if (!entry) qa_range_error("Missing saved Q2 clock state");
  return entry;
}
static void set_animation(qa_q2_base_scenery *s, qa_q2_entity *e, double first, double end) {
  anim_entry *existing = find_animation(s, e);
  if (existing) { existing->first = first; existing->end = end; return; }
  if (s->animation_count == s->animation_cap) {
    size_t cap = s->animation_cap ? s->animation_cap * 2 : 8;
    anim_entry *grown = realloc(s->animations, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    s->animations = grown; s->animation_cap = cap;
  }
  s->animations[s->animation_count++] = (anim_entry){e, first, end};
}
static clock_entry *set_clock(qa_q2_base_scenery *s, qa_q2_entity *e, double value) {
  clock_entry *existing = find_clock(s, e);
  if (existing) { existing->value = value; return existing; }
  if (s->clock_count == s->clock_cap) {
    size_t cap = s->clock_cap ? s->clock_cap * 2 : 8;
    clock_entry *grown = realloc(s->clocks, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    s->clocks = grown; s->clock_cap = cap;
  }
  clock_entry *entry = &s->clocks[s->clock_count++];
  *entry = (clock_entry){0};
  entry->e = e; entry->value = value;
  return entry;
}

static char *message_of(qa_q2_base_scenery *s, qa_q2_entity *e) {
  for (size_t i = 0; i < s->message_count; i++) if (s->messages[i]->e == e) return s->messages[i]->text;
  if (s->message_count == s->message_cap) {
    size_t cap = s->message_cap ? s->message_cap * 2 : 8;
    message_slot **grown = realloc(s->messages, cap * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    s->messages = grown; s->message_cap = cap;
  }
  message_slot *slot = calloc(1, sizeof *slot);
  if (!slot) qa_range_error("Out of memory");
  slot->e = e;
  s->messages[s->message_count++] = slot;
  return slot->text;
}

static void set_model(qa_q2_entity *entity, qa_q2_game_services *game, const char *path, qa_bounds bounds, const char *solid) {
  entity->model = path;
  qa_q2_body_changes changes = {0};
  changes.has_bounds = true; changes.bounds = bounds;
  game->move(game->ctx, entity, changes, false);
  game->solid(game->ctx, entity, solid);
  game->show(game->ctx, entity);
}

static void animate(qa_q2_base_scenery *s, qa_q2_entity *entity, qa_q2_game_services *game, double first, double end, double initial_delay) {
  set_animation(s, entity, first, end);
  entity->frame = first;
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, initial_delay, (qa_q2_think){s, animate_step_fn});
}
static void animate_step_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  qa_q2_base_scenery *s = c;
  anim_entry *state = find_animation(s, self);
  if (!state) qa_range_error("Missing saved scenery animation");
  self->frame++;
  if (self->frame >= state->end) self->frame = state->first;
  game->show(game->ctx, self);
  game->schedule(game->ctx, self, game->host->frame_seconds(game->host->ctx), (qa_q2_think){s, animate_step_fn});
}

static void reset_clock(qa_q2_base_scenery *s, qa_q2_entity *entity) {
  clock_entry *state = clock_of(s, entity);
  entity->has_activator = false;
  if ((sf(entity) & 1) != 0) { state->value = 0; entity->wait = entity->count; }
  else if ((sf(entity) & 2) != 0) { state->value = entity->count; entity->wait = 0; }
}

static void clock_tick_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services) {
  qa_q2_base_scenery *s = c;
  clock_entry *state = clock_of(s, self);
  qa_q2_entity *display = self->has_enemy ? services->entity(services->ctx, &self->enemy) : NULL;
  if (!display) {
    size_t count = 0;
    qa_q2_entity *const *targets = services->targets(services->ctx, self->target, &count);
    display = count > 0 ? targets[0] : NULL;
    self->has_enemy = display != NULL;
    if (display) self->enemy = display->actor.id;
  }
  if (!display) return;
  char text[256];
  if ((sf(self) & 1) != 0) { qa_q2_clock_text(state->value, qa_q2_integer_field(&self->spawn, "style", 0), text, sizeof text); state->value++; }
  else if ((sf(self) & 2) != 0) { qa_q2_clock_text(state->value, qa_q2_integer_field(&self->spawn, "style", 0), text, sizeof text); state->value--; }
  else {
    qa_q2_base_local_time time = s->hooks.local_time(s->hooks.ctx);
    char hour_raw[64], hour[64], minute_raw[64], minute[64], second_raw[64], second[64];
    js_number(hour_raw, sizeof hour_raw, time.hour); pad_start(hour, sizeof hour, hour_raw, 2, ' ');
    js_number(minute_raw, sizeof minute_raw, time.minute); pad_start(minute, sizeof minute, minute_raw, 2, '0');
    js_number(second_raw, sizeof second_raw, time.second); pad_start(second, sizeof second, second_raw, 2, '0');
    (void)snprintf(text, sizeof text, "%s:%s:%s", hour, minute, second);
  }
  /* TS slice(0, 15); the owned copy backs entity->message. */
  char *own = message_of(s, self);
  (void)snprintf(own, sizeof ((message_slot *)0)->text, "%.15s", text);
  self->message = own;
  if (display != self) {
    char *shown = message_of(s, display);
    memcpy(shown, own, sizeof ((message_slot *)0)->text);
    display->message = shown;
  }
  qa_actor_id self_id = self->actor.id;
  (void)qa_actor_callback_table_use(services->host->callbacks, display->actor, &self_id, &self_id);
  if (((sf(self) & 1) != 0 && state->value > self->wait) || ((sf(self) & 2) != 0 && state->value < self->wait)) {
    const char *path_target = qa_q2_spawn_get(&self->spawn, "pathtarget");
    if (!path_target) path_target = "";
    if (path_target[0] != '\0') {
      const char *previous_target = self->target, *previous_message = self->message;
      self->target = path_target;
      self->message = "";
      qa_q2_game_use_targets_entity(services, self, self->has_activator ? &self->activator : NULL, false);
      self->target = previous_target;
      self->message = previous_message;
    }
    if ((sf(self) & 8) == 0 || !qa_session_actor_registry_is_live(services->host->actors, self->actor.id)) return;
    reset_clock(s, self);
    if ((sf(self) & 4) != 0) return;
  }
  services->schedule(services->ctx, self, 1, (qa_q2_think){s, clock_tick_fn});
}

static void clock_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, const qa_actor_id *o, const qa_actor_id *activator) {
  (void)o;
  if ((sf(self) & 8) == 0) self->use = (qa_q2_use){0};
  if (self->has_activator) return;
  self->has_activator = activator != NULL;
  if (activator) self->activator = *activator;
  clock_tick_fn(c, self, services);
}

static void teleporter_touch_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, qa_touch_contact contact) {
  qa_q2_base_scenery *s = c;
  if (!services->host->is_player(services->host->ctx, contact.other)) return;
  size_t count = 0;
  qa_q2_entity *const *targets = services->targets(services->ctx, self->target, &count);
  qa_q2_entity *destination = count > 0 ? targets[0] : NULL;
  qa_q2_entity *player = services->entity(services->ctx, &contact.other);
  qa_owned_actor owned;
  bool has_owned = qa_session_actor_registry_resolve_owned(services->host->actors, contact.other, &owned);
  qa_body_state_result body = qa_shared_body_table_read(services->host->bodies, contact.other);
  if (!destination) {
    char message[512];
    (void)snprintf(message, sizeof message, "Teleporter destination missing: %s", self->target);
    services->host->diagnostic(services->host->ctx, message);
    return;
  }
  if (!has_owned || !body.has_value) return;
  qa_body_state target = qa_q2_game_body(services, destination);
  qa_vec3 origin = qa_q2_add(target.origin, (qa_vec3){0, 0, 10});
  qa_shared_body_table_unlink(services->host->bodies, owned);
  qa_body_state next = body.value;
  next.origin = origin; next.velocity = QA_Q2_ZERO; next.angles = QA_Q2_ZERO;
  qa_shared_body_table_write(services->host->bodies, owned, next);
  s->hooks.teleport_player(s->hooks.ctx, contact.other, target.origin, target.angles);
  qa_vec3 effect_origin;
  if (!self->has_owner) effect_origin = qa_q2_game_body(services, self).origin;
  else {
    qa_body_state_result owner_body = qa_shared_body_table_read(services->host->bodies, self->owner);
    effect_origin = owner_body.has_value ? owner_body.value.origin : qa_q2_game_body(services, self).origin;
  }
  qa_q2_presentation_event first = {0};
  first.kind = "effect"; first.effect = "q2:player-teleport"; first.origin = effect_origin;
  first.direction = QA_Q2_ZERO; first.count = 1; first.color = 0;
  services->host->emit(services->host->ctx, first);
  qa_q2_presentation_event second = first;
  second.origin = origin;
  services->host->emit(services->host->ctx, second);
  if (player) (void)qa_q2_kill_box(player, services);
  else {
    for (;;) {
      qa_q2_trace_request request = {0};
      request.start = origin; request.end = origin; request.bounds = &body.value.bounds;
      request.ignore = &contact.other; request.mask = 0x2010003;
      qa_trace_result trace = services->host->trace(services->host->ctx, request);
      if (trace.hit.kind != QA_TRACE_HIT_ACTOR) break;
      qa_actor_id victim = trace.hit.actor;
      qa_actor_id attacker = contact.other, inflictor = contact.other;
      qa_attack_provenance attack = services->attack(services->ctx, self, self->actor.id, &attacker, 21, 32, NULL);
      attack.inflictor = &inflictor;
      qa_damage_request damage = {0};
      damage.attack = attack; damage.target = victim; damage.amount = 100000; damage.knockback = 0;
      damage.direction = QA_Q2_ZERO; damage.point = origin; damage.normal = QA_Q2_ZERO; damage.delivery = "direct";
      (void)qa_gameplay_authority_apply(services->host->combat, damage, NULL, NULL);
      qa_trace_result check = services->host->trace(services->host->ctx, request);
      if (check.hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(check.hit.actor, victim)) break;
    }
  }
  qa_shared_body_table_link(services->host->bodies, owned, NULL);
}

static void bomb_prethink_fn(void *c, qa_q2_entity *current, qa_q2_game_services *host) {
  (void)c;
  double diff = qa_js_max(-1, current->timestamp - host->host->now(host->host->ctx));
  qa_vec3 direction = qa_q2_scale(current->movedir, 1 + diff);
  direction.z = diff;
  qa_vec3 angles = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE);
  angles.z = qa_q2_game_body(host, current).angles.z + 10;
  qa_q2_body_changes changes = {0};
  changes.has_ground = true; changes.ground_null = true;
  changes.has_angles = true; changes.angles = angles;
  host->move(host->ctx, current, changes, false);
}

static void bomb_touch_fn(void *c, qa_q2_entity *current, qa_q2_game_services *host, qa_touch_contact contact) {
  (void)c; (void)contact;
  qa_q2_game_use_targets_entity(host, current, current->has_activator ? &current->activator : NULL, false);
  if (!qa_session_actor_registry_is_live(host->host->actors, current->actor.id)) return;
  qa_body_state body = qa_q2_game_body(host, current);
  qa_q2_body_changes changes = {0};
  changes.has_origin = true;
  changes.origin = body.origin;
  changes.origin.z = body.origin.z + body.bounds.min.z + 1;
  host->move(host->ctx, current, changes, false);
  qa_actor_id attacker = current->actor.id;
  host->radius_damage(host->ctx, current, &attacker, current->damage, NULL, current->damage + 40, 27, 0, NULL);
  qa_q2_presentation_event ev = {0};
  ev.kind = "effect"; ev.effect = "q2:explosion2"; ev.origin = qa_q2_game_body(host, current).origin;
  ev.direction = QA_Q2_ZERO; ev.count = 1; ev.color = 0;
  host->host->emit(host->host->ctx, ev);
  host->remove(host->ctx, current);
}

static void commander_collapse_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services) {
  self->frame++;
  services->show(services->ctx, self);
  if (self->frame == 22) services->sound(services->ctx, self->actor, "tank/thud.wav", 4, 1, 1);
  if (self->frame < 24) services->schedule(services->ctx, self, services->host->frame_seconds(services->host->ctx), (qa_q2_think){c, commander_collapse_fn});
}
static void commander_release_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services) {
  (void)c;
  qa_q2_body_changes changes = {0};
  changes.has_origin = true;
  changes.origin = qa_q2_add(qa_q2_game_body(services, self).origin, (qa_vec3){0, 0, 2});
  services->move(services->ctx, self, changes, true);
  services->motion(services->ctx, self, "toss");
}
static void commander_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; (void)a;
  game->sound(game->ctx, self->actor, "tank/pain.wav", 4, 1, 1);
  game->schedule(game->ctx, self, game->host->frame_seconds(game->host->ctx), (qa_q2_think){c, commander_collapse_fn});
}
static void remove_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  game->remove(game->ctx, self);
}
static void remove_die_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game, qa_death_reaction reaction) {
  (void)c; (void)reaction;
  game->remove(game->ctx, self);
}
static void remove_think_fn(void *c, qa_q2_entity *self, qa_q2_game_services *game) {
  (void)c;
  game->remove(game->ctx, self);
}

static void bomb_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, const qa_actor_id *o, const qa_actor_id *activator) {
  (void)o;
  qa_q2_base_scenery *s = c;
  size_t count = 0;
  qa_q2_entity *const *entities = services->entities(services->ctx, &count);
  qa_q2_entity *viper = NULL;
  for (size_t i = 0; i < count; i++) if (strcmp(entities[i]->classname, "misc_viper") == 0) { viper = entities[i]; break; }
  if (!viper) { services->host->diagnostic(services->host->ctx, "misc_viper_bomb has no misc_viper"); return; }
  qa_vec3 direction = qa_q2_mover_module_train_direction(s->hooks.movers, viper, services);
  self->movedir = direction;
  self->timestamp = services->host->now(services->host->ctx);
  self->visible = true;
  self->use = (qa_q2_use){0};
  self->has_activator = activator != NULL;
  if (activator) self->activator = *activator;
  self->effects = (double)(qa_js_to_int32(self->effects) | 16);
  qa_q2_body_changes changes = {0};
  changes.has_velocity = true; changes.velocity = qa_q2_scale(direction, viper->speed);
  services->move(services->ctx, self, changes, false);
  services->solid(services->ctx, self, "box");
  services->motion(services->ctx, self, "toss");
  services->show(services->ctx, self);
  self->prethink = (qa_q2_think){s, bomb_prethink_fn};
  self->touch = (qa_q2_touch){s, bomb_touch_fn};
}

static void string_use_fn(void *c, qa_q2_entity *self, qa_q2_game_services *services, const qa_actor_id *o, const qa_actor_id *a) {
  (void)c; (void)o; (void)a;
  const char *team = qa_q2_spawn_get(&self->spawn, "team");
  size_t count = 0;
  qa_q2_entity *const *entities = services->entities(services->ctx, &count);
  size_t length = strlen(self->message);
  for (size_t i = 0; i < count; i++) {
    qa_q2_entity *member = entities[i];
    if (member->count == 0) continue;
    if (team == NULL) { if (member != self) continue; }
    else {
      const char *member_team = qa_q2_spawn_get(&member->spawn, "team");
      if (!member_team || strcmp(member_team, team) != 0) continue;
    }
    double index = member->count - 1;
    char character = index >= 0 && index < (double)length ? self->message[(size_t)index] : '\0';
    member->frame = character >= '0' && character <= '9' ? character - '0' : character == '-' ? 10 : character == ':' ? 11 : 12;
    services->show(services->ctx, member);
  }
}

qa_q2_base_scenery *qa_q2_base_scenery_create(qa_q2_base_entity_hooks hooks) {
  qa_q2_base_scenery *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->hooks = hooks;
  s->thinks[0] = (qa_q2_named_think){"q2_base_scenery_animate", {s, animate_step_fn}};
  s->thinks[1] = (qa_q2_named_think){"func_clock_think", {s, clock_tick_fn}};
  s->thinks[2] = (qa_q2_named_think){"misc_viper_bomb_prethink", {s, bomb_prethink_fn}};
  s->thinks[3] = (qa_q2_named_think){"commander_body_think", {s, commander_collapse_fn}};
  s->thinks[4] = (qa_q2_named_think){"commander_body_drop", {s, commander_release_fn}};
  s->thinks[5] = (qa_q2_named_think){"q2_base_scenery_remove", {s, remove_think_fn}};
  s->uses[0] = (qa_q2_named_use){"func_clock_use", {s, clock_use_fn}};
  s->uses[1] = (qa_q2_named_use){"misc_viper_bomb_use", {s, bomb_use_fn}};
  s->uses[2] = (qa_q2_named_use){"commander_body_use", {s, commander_use_fn}};
  s->uses[3] = (qa_q2_named_use){"misc_blackhole_use", {s, remove_use_fn}};
  s->uses[4] = (qa_q2_named_use){"target_string_use", {s, string_use_fn}};
  s->touches[0] = (qa_q2_named_touch){"teleporter_touch", {s, teleporter_touch_fn}};
  s->touches[1] = (qa_q2_named_touch){"misc_viper_bomb_touch", {s, bomb_touch_fn}};
  s->dies[0] = (qa_q2_named_die){"q2_base_scenery_die", {s, remove_die_fn}};
  s->defs = (qa_q2_callback_definitions){.think = s->thinks, .think_count = 6, .use = s->uses, .use_count = 5,
      .touch = s->touches, .touch_count = 2, .die = s->dies, .die_count = 1};
  return s;
}
void qa_q2_base_scenery_destroy(qa_q2_base_scenery *s) {
  if (!s) return;
  for (size_t i = 0; i < s->message_count; i++) free(s->messages[i]);
  free(s->messages); free(s->animations); free(s->clocks); free(s);
}
qa_q2_callback_definitions qa_q2_base_scenery_callbacks(qa_q2_base_scenery *s) { return s->defs; }

qa_q2_base_scenery_checkpoint qa_q2_base_scenery_capture(qa_q2_base_scenery *s, qa_q2_game_services *game) {
  qa_q2_base_scenery_checkpoint out = {0};
  size_t count = 0, acap = 0, ccap = 0;
  qa_q2_entity *const *entities = game->entities(game->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q2_entity *entity = entities[i];
    qa_saved_actor_id actor = {(double)entity->actor.id.slot, (double)entity->actor.id.generation};
    anim_entry *animation = find_animation(s, entity);
    clock_entry *clock = find_clock(s, entity);
    if (animation) {
      if (out.animation_count == acap) { acap = acap ? acap * 2 : 8; out.animations = realloc(out.animations, acap * sizeof *out.animations); if (!out.animations) qa_range_error("Out of memory"); }
      out.animations[out.animation_count++] = (qa_q2_base_scenery_animation_entry){actor, animation->first, animation->end};
    }
    if (clock) {
      if (out.clock_count == ccap) { ccap = ccap ? ccap * 2 : 8; out.clocks = realloc(out.clocks, ccap * sizeof *out.clocks); if (!out.clocks) qa_range_error("Out of memory"); }
      out.clocks[out.clock_count++] = (qa_q2_base_scenery_clock_entry){actor, clock->value};
    }
  }
  return out;
}
static qa_q2_entity *saved_scenery(qa_q2_game_services *game, qa_saved_actor_id actor) {
  qa_owned_actor owner = qa_q2_restore_actor(game, actor);
  qa_q2_entity *entity = game->entity(game->ctx, &owner.id);
  if (!entity) qa_range_error("Missing saved Q2 scenery");
  return entity;
}
void qa_q2_base_scenery_restore(qa_q2_base_scenery *s, qa_q2_game_services *game, qa_q2_base_scenery_checkpoint checkpoint) {
  s->animation_count = 0; s->clock_count = 0;
  for (size_t i = 0; i < checkpoint.animation_count; i++)
    set_animation(s, saved_scenery(game, checkpoint.animations[i].actor), checkpoint.animations[i].first, checkpoint.animations[i].end);
  for (size_t i = 0; i < checkpoint.clock_count; i++)
    (void)set_clock(s, saved_scenery(game, checkpoint.clocks[i].actor), checkpoint.clocks[i].value);
}

static void spawn_clock(qa_q2_base_scenery *s, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (entity->target[0] == '\0' || ((sf(entity) & 2) != 0 && entity->count == 0)) {
    char message[128];
    (void)snprintf(message, sizeof message, "func_clock without %s", (sf(entity) & 2) != 0 ? "count or target" : "target");
    game->host->diagnostic(game->host->ctx, message);
    game->remove(game->ctx, entity);
    return;
  }
  if ((sf(entity) & 1) != 0 && entity->count == 0) entity->count = 3600;
  (void)set_clock(s, entity, 0);
  reset_clock(s, entity);
  if ((sf(entity) & 4) != 0) entity->use = (qa_q2_use){s, clock_use_fn};
  else game->schedule(game->ctx, entity, 1, (qa_q2_think){s, clock_tick_fn});
}

static void spawn_teleporter(qa_q2_base_scenery *s, qa_q2_entity *entity, qa_q2_game_services *game) {
  if (entity->target[0] == '\0') {
    game->host->diagnostic(game->host->ctx, "teleporter without a target");
    game->remove(game->ctx, entity);
    return;
  }
  entity->skin = 1;
  entity->effects = 0x20000;
  set_model(entity, game, "models/objects/dmspot/tris.md2", (qa_bounds){{-32, -32, -24}, {32, 32, -16}}, "box");
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = entity->actor.id;
  ev.origin = qa_q2_game_body(game, entity).origin; ev.path = "world/amb10.wav";
  ev.channel = 0; ev.volume = 1;
  ev.attenuation = game->options.edition == QA_Q2_EDITION_RERELEASE ? 3 : 1;
  ev.reliable = false; ev.loop = "start";
  game->host->emit(game->host->ctx, ev);
  qa_q2_entity *trigger = game->create(game->ctx, "teleporter_trigger", NULL, 0);
  trigger->target = entity->target;
  trigger->has_owner = true; trigger->owner = entity->actor.id;
  trigger->visible = false;
  qa_q2_body_changes changes = {0};
  changes.has_origin = true; changes.origin = qa_q2_game_body(game, entity).origin;
  changes.has_bounds = true; changes.bounds = (qa_bounds){{-8, -8, 8}, {8, 8, 24}};
  game->move(game->ctx, trigger, changes, false);
  trigger->touch = (qa_q2_touch){s, teleporter_touch_fn};
  game->solid(game->ctx, trigger, "trigger");
}

static void combat_create(qa_q2_game_services *game, qa_q2_entity *entity, double health, double mass, bool invulnerable) {
  qa_combat_state state = {0};
  state.health = health;
  state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  state.armor.powered.kind = "none";
  state.mass = mass; state.can_take_damage = true; state.invulnerable = invulnerable; state.team = NULL;
  qa_gameplay_authority_create_state(game->host->combat, entity->actor, state, NULL, NULL);
}

bool qa_q2_base_scenery_spawn(qa_q2_base_scenery *s, qa_q2_entity *entity, qa_q2_game_services *game) {
  const char *classname = entity->classname;
  if (strcmp(classname, "viewthing") == 0) {
    entity->render_flags = 64;
    set_model(entity, game, "models/objects/banner/tris.md2", (qa_bounds){{-16, -16, -24}, {16, 16, 32}}, "box");
    animate(s, entity, game, 0, 7, 0.5);
    return true;
  }
  if (strcmp(classname, "misc_blackhole") == 0) {
    entity->render_flags = 32;
    entity->use = (qa_q2_use){s, remove_use_fn};
    set_model(entity, game, "models/objects/black/tris.md2", (qa_bounds){{-64, -64, 0}, {64, 64, 8}}, "none");
    animate(s, entity, game, 0, 19, 0.2);
    return true;
  }
  if (strcmp(classname, "misc_eastertank") == 0) {
    set_model(entity, game, "models/monsters/tank/tris.md2", (qa_bounds){{-32, -32, -16}, {32, 32, 32}}, "box");
    animate(s, entity, game, 254, 293, 0.2);
    return true;
  }
  if (strcmp(classname, "misc_easterchick") == 0 || strcmp(classname, "misc_easterchick2") == 0) {
    bool first = strcmp(classname, "misc_easterchick") == 0;
    set_model(entity, game, "models/monsters/bitch/tris.md2", (qa_bounds){{-32, -32, 0}, {32, 32, 32}}, "box");
    animate(s, entity, game, first ? 208 : 248, first ? 247 : 287, 0.2);
    return true;
  }
  if (strcmp(classname, "monster_commander_body") == 0) {
    entity->render_flags = (double)(qa_js_to_int32(entity->render_flags) | 64);
    entity->flags = (double)(qa_js_to_int32(entity->flags) | 16);
    set_model(entity, game, "models/monsters/commandr/tris.md2", (qa_bounds){{-32, -32, 0}, {32, 32, 48}}, "box");
    combat_create(game, entity, 0, 200, true);
    entity->use = (qa_q2_use){s, commander_use_fn};
    game->schedule(game->ctx, entity, 5 * game->host->frame_seconds(game->host->ctx), (qa_q2_think){s, commander_release_fn});
    return true;
  }
  if (strcmp(classname, "misc_bigviper") == 0) {
    set_model(entity, game, "models/ships/bigviper/tris.md2", (qa_bounds){{-176, -120, -24}, {176, 120, 72}}, "box");
    return true;
  }
  if (strcmp(classname, "misc_viper_bomb") == 0) {
    entity->visible = false;
    if (entity->damage == 0) entity->damage = 1000;
    set_model(entity, game, "models/objects/bomb/tris.md2", (qa_bounds){{-8, -8, -8}, {8, 8, 8}}, "none");
    entity->use = (qa_q2_use){s, bomb_use_fn};
    return true;
  }
  if (strcmp(classname, "light_mine1") == 0 || strcmp(classname, "light_mine2") == 0) {
    entity->model = strcmp(classname, "light_mine1") == 0 ? "models/objects/minelite/light1/tris.md2" : "models/objects/minelite/light2/tris.md2";
    game->link(game->ctx, entity);
    game->show(game->ctx, entity);
    return true;
  }
  if (strcmp(classname, "misc_gib_arm") == 0 || strcmp(classname, "misc_gib_leg") == 0) {
    entity->model = strcmp(classname, "misc_gib_arm") == 0 ? "models/objects/gibs/arm/tris.md2" : "models/objects/gibs/leg/tris.md2";
    entity->effects = (double)(qa_js_to_int32(entity->effects) | 2);
    entity->server_flags = (double)(qa_js_to_int32(entity->server_flags) | 4);
    double x = game->host->random(game->host->ctx) * 200;
    double y = game->host->random(game->host->ctx) * 200;
    double z = game->host->random(game->host->ctx) * 200;
    entity->angular_velocity = (qa_vec3){x, y, z};
    combat_create(game, entity, 0, 0, false);
    entity->die = (qa_q2_die){s, remove_die_fn};
    game->motion(game->ctx, entity, "toss");
    game->solid(game->ctx, entity, "none");
    game->show(game->ctx, entity);
    game->schedule(game->ctx, entity, 30, (qa_q2_think){s, remove_think_fn});
    return true;
  }
  if (strcmp(classname, "target_character") == 0) {
    entity->frame = 12;
    game->motion(game->ctx, entity, "push");
    game->solid(game->ctx, entity, "brush");
    game->show(game->ctx, entity);
    return true;
  }
  if (strcmp(classname, "target_string") == 0) { entity->use = (qa_q2_use){s, string_use_fn}; return true; }
  if (strcmp(classname, "func_clock") == 0) { spawn_clock(s, entity, game); return true; }
  if (strcmp(classname, "misc_teleporter") == 0) { spawn_teleporter(s, entity, game); return true; }
  if (strcmp(classname, "misc_teleporter_dest") == 0) {
    entity->skin = 0;
    set_model(entity, game, "models/objects/dmspot/tris.md2", (qa_bounds){{-32, -32, -24}, {32, 32, -16}}, "box");
    return true;
  }
  return false;
}

