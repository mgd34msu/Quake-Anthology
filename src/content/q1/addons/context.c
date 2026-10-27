/* Port of src/content/q1/addons/context.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/context.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

const char *const QA_Q1AD_WEAPONS[QA_Q1AD_WEAPONS_COUNT] = {
  "axe", "shotgun", "supershotgun", "nailgun", "supernailgun",
  "grenadelauncher", "rocketlauncher", "lightning"
};

const char *qa_q1ad_program_name(qa_q1ad_program program) {
  static const char *const NAMES[] = {"dopa", "mg1", "mg3", "ctf"};
  return NAMES[program];
}

const char *qa_q1ad_weapon_item(const char *weapon, char buffer[64]) {
  (void)snprintf(buffer, 64, "q1:weapon/%s", weapon);
  return buffer;
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

/* JS String(Math.fround(value)) for the values this slice stores. */
static void format_number(char *out, size_t size, double value) {
  (void)snprintf(out, size, "%.9g", (double)(float)value);
}

/* --- mirrored Q1Actor field access (foundation/entity.ts) --- */
const char *qa_q1ad_actor_text(const qa_q1ad_actor *entity, const char *name) {
  for (size_t i = 0; i < entity->field_count; i++)
    if (strcmp(entity->field_keys[i], name) == 0) return entity->field_values[i];
  return "";
}
bool qa_q1ad_actor_has(const qa_q1ad_actor *entity, const char *name) {
  for (size_t i = 0; i < entity->field_count; i++)
    if (strcmp(entity->field_keys[i], name) == 0) return true;
  return false;
}
void qa_q1ad_actor_set(qa_q1ad_actor *entity, const char *name, const char *value) {
  for (size_t i = 0; i < entity->field_count; i++)
    if (strcmp(entity->field_keys[i], name) == 0) {
      (void)snprintf(entity->field_values[i], sizeof entity->field_values[i], "%s", value);
      return;
    }
  if (entity->field_count >= QA_Q1AD_ACTOR_FIELDS) return;
  entity->field_keys[entity->field_count] = name;
  (void)snprintf(entity->field_values[entity->field_count],
                 sizeof entity->field_values[entity->field_count], "%s", value);
  entity->field_count++;
}
double qa_q1ad_actor_number_or(const qa_q1ad_actor *entity, const char *name, double fallback) {
  const char *text = qa_q1ad_actor_text(entity, name);
  if (text[0] == '\0') return fallback;
  double value = strtod(text, NULL);
  return isfinite(value) ? (double)(float)value : fallback;
}
double qa_q1ad_actor_number(const qa_q1ad_actor *entity, const char *name) {
  return qa_q1ad_actor_number_or(entity, name, 0);
}
qa_vec3 qa_q1ad_actor_vector(const qa_q1ad_actor *entity, const char *name) {
  const char *text = qa_q1ad_actor_text(entity, name);
  double values[3] = {0, 0, 0};
  const char *cursor = text;
  for (int i = 0; i < 3 && *cursor != '\0'; i++) {
    char *end = NULL;
    double value = strtod(cursor, &end);
    if (end == cursor) break;
    values[i] = isfinite(value) ? (double)(float)value : 0;
    cursor = end;
  }
  return (qa_vec3){values[0], values[1], values[2]};
}
bool qa_q1ad_actor_ref(const qa_q1ad_actor *entity, const char *name, qa_actor_id *out) {
  for (size_t i = 0; i < entity->ref_count; i++)
    if (strcmp(entity->ref_keys[i], name) == 0) {
      if (entity->ref_has[i] && out != NULL) *out = entity->ref_values[i];
      return entity->ref_has[i];
    }
  return false;
}
void qa_q1ad_actor_set_ref(qa_q1ad_actor *entity, const char *name, const qa_actor_id *target) {
  for (size_t i = 0; i < entity->ref_count; i++)
    if (strcmp(entity->ref_keys[i], name) == 0) {
      entity->ref_has[i] = target != NULL;
      if (target != NULL) entity->ref_values[i] = *target;
      return;
    }
  if (entity->ref_count >= QA_Q1AD_ACTOR_REFS) return;
  size_t i = entity->ref_count++;
  entity->ref_keys[i] = name;
  entity->ref_has[i] = target != NULL;
  if (target != NULL) entity->ref_values[i] = *target;
}

double qa_q1ad_player_powerup(const qa_q1ad_player *player, const char *kind) {
  for (size_t i = 0; i < player->powerup_count; i++)
    if (strcmp(player->powerup_names[i], kind) == 0) return player->powerup_expires[i];
  return 0;
}
void qa_q1ad_player_set_powerup(qa_q1ad_player *player, const char *kind, double expires) {
  for (size_t i = 0; i < player->powerup_count; i++)
    if (strcmp(player->powerup_names[i], kind) == 0) { player->powerup_expires[i] = expires; return; }
  if (player->powerup_count >= QA_Q1AD_POWERUPS) return;
  player->powerup_names[player->powerup_count] = kind;
  player->powerup_expires[player->powerup_count] = expires;
  player->powerup_count++;
}

/* --- thin host wrappers --- */
double qa_q1ad_cvar(qa_q1ad_context *context, const char *name) {
  return context->services.cvar != NULL ? context->services.cvar(context->services.ctx, name) : 0;
}
void qa_q1ad_emit(qa_q1ad_context *context, const qa_q1ad_event *event) {
  if (context->services.emit != NULL) context->services.emit(context->services.ctx, event);
}
void qa_q1ad_host_emit(qa_q1ad_context *context, const qa_q1ad_host_event *event) {
  if (context->game.host_emit != NULL) context->game.host_emit(context->game.ctx, event);
}
bool qa_q1ad_live(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  return context->game.live == NULL || context->game.live(context->game.ctx, entity);
}
void qa_q1ad_remove(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.remove != NULL) context->game.remove(context->game.ctx, entity);
}
void qa_q1ad_link(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.link != NULL) context->game.link(context->game.ctx, entity);
}
void qa_q1ad_cancel(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.cancel != NULL) context->game.cancel(context->game.ctx, entity);
}
void qa_q1ad_schedule(qa_q1ad_context *context, qa_q1ad_actor *entity, double delay, const char *name) {
  if (context->game.schedule != NULL) context->game.schedule(context->game.ctx, entity, delay, name);
}
void qa_q1ad_schedule_at(qa_q1ad_context *context, qa_q1ad_actor *entity, double time, const char *name) {
  if (context->game.schedule_at != NULL) context->game.schedule_at(context->game.ctx, entity, time, name);
}
void qa_q1ad_named_name(char out[QA_Q1AD_NAME], const char *prefix, const char *name) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s%s", prefix, name);
}
void qa_q1ad_schedule_named(qa_q1ad_context *context, qa_q1ad_actor *entity, double delay,
                            const char *prefix, const char *name) {
  char full[QA_Q1AD_NAME];
  qa_q1ad_named_name(full, prefix, name);
  qa_q1ad_schedule(context, entity, delay, full);
}
void qa_q1ad_bind(char slot[QA_Q1AD_NAME], const char *name) {
  (void)snprintf(slot, QA_Q1AD_NAME, "%s", name);
}
void qa_q1ad_bind_prefix(char slot[QA_Q1AD_NAME], const char *prefix, const char *name) {
  qa_q1ad_named_name(slot, prefix, name);
}
qa_q1ad_body qa_q1ad_body_of(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body body = {0};
  if (context->game.body != NULL) (void)context->game.body(context->game.ctx, entity, &body);
  return body;
}
bool qa_q1ad_body_by_id(qa_q1ad_context *context, qa_actor_id actor, qa_q1ad_body *out) {
  *out = (qa_q1ad_body){0};
  return context->game.body_of != NULL && context->game.body_of(context->game.ctx, actor, out);
}
void qa_q1ad_set_body(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_q1ad_body_patch *patch) {
  if (context->game.set_body != NULL) context->game.set_body(context->game.ctx, entity, patch);
}
void qa_q1ad_set_origin(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_vec3 origin) {
  if (context->game.set_origin != NULL) context->game.set_origin(context->game.ctx, entity, origin);
}
void qa_q1ad_set_bounds(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_bounds bounds) {
  if (context->game.set_bounds != NULL) context->game.set_bounds(context->game.ctx, entity, bounds);
}
double qa_q1ad_random(qa_q1ad_context *context) {
  return context->game.random != NULL ? context->game.random(context->game.ctx) : 0;
}
double qa_q1ad_health(qa_q1ad_context *context, qa_actor_id actor) {
  return context->game.health != NULL ? context->game.health(context->game.ctx, actor) : 0;
}
bool qa_q1ad_is_player(qa_q1ad_context *context, const qa_actor_id *actor) {
  return actor != NULL && context->game.is_player != NULL &&
         context->game.is_player(context->game.ctx, *actor);
}
qa_q1ad_player *qa_q1ad_player_state(qa_q1ad_context *context, qa_actor_id actor) {
  return context->game.player != NULL ? context->game.player(context->game.ctx, actor) : NULL;
}
qa_q1ad_actor *qa_q1ad_entity(qa_q1ad_context *context, const qa_actor_id *actor) {
  if (actor == NULL || context->game.entity_by_id == NULL) return NULL;
  return context->game.entity_by_id(context->game.ctx, *actor);
}
qa_q1ad_actor *qa_q1ad_world(qa_q1ad_context *context) {
  return context->game.world != NULL ? context->game.world(context->game.ctx) : NULL;
}
bool qa_q1ad_resolve_owned(qa_q1ad_context *context, qa_actor_id actor, qa_owned_actor *out) {
  return context->game.resolve_owned != NULL &&
         context->game.resolve_owned(context->game.ctx, actor, out);
}
void qa_q1ad_sound_actor(qa_q1ad_context *context, qa_actor_id actor, const char *path, const char *channel) {
  if (context->game.sound != NULL) context->game.sound(context->game.ctx, actor, path, channel);
}
void qa_q1ad_sound_entity(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *path, const char *channel) {
  qa_q1ad_sound_actor(context, entity->actor.id, path, channel);
}
void qa_q1ad_effect(qa_q1ad_context *context, const char *kind, qa_vec3 origin, const qa_actor_id *actor, double count) {
  if (context->game.effect != NULL) context->game.effect(context->game.ctx, kind, origin, actor, count);
}
void qa_q1ad_message(qa_q1ad_context *context, qa_actor_id player, const char *key, bool localized,
                     const qa_q1ad_msg_arg *args, size_t arg_count) {
  if (context->game.message != NULL)
    context->game.message(context->game.ctx, player, key, localized, args, arg_count);
}
void qa_q1ad_damage(qa_q1ad_context *context, qa_actor_id target, const qa_actor_id *inflictor,
                    const qa_actor_id *attacker, double amount, const char *weapon,
                    const char *mode, const char *cause) {
  if (context->game.damage != NULL)
    context->game.damage(context->game.ctx, target, inflictor, attacker, amount, weapon, mode, cause);
}
void qa_q1ad_use_targets(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *activator) {
  if (context->game.use_targets != NULL) context->game.use_targets(context->game.ctx, entity, activator);
}
size_t qa_q1ad_entities(qa_q1ad_context *context, qa_q1ad_actor **out, size_t capacity) {
  return context->game.entities != NULL ? context->game.entities(context->game.ctx, out, capacity) : 0;
}
size_t qa_q1ad_find(qa_q1ad_context *context, const char *targetname, qa_q1ad_actor **out, size_t capacity) {
  return context->game.find != NULL ? context->game.find(context->game.ctx, targetname, out, capacity) : 0;
}
size_t qa_q1ad_players(qa_q1ad_context *context, qa_actor_id *out, size_t capacity) {
  return context->game.players != NULL ? context->game.players(context->game.ctx, out, capacity) : 0;
}

/* --- registry / dispatch --- */
static const qa_q1ad_handlers *find_named(const qa_q1ad_context *context, const char *name) {
  for (size_t i = 0; i < context->named_count; i++)
    if (strcmp(context->named[i].name, name) == 0) return &context->named[i].handlers;
  return NULL;
}
void qa_q1ad_register_named(qa_q1ad_context *context, const char *name, qa_q1ad_handlers handlers) {
  for (size_t i = 0; i < context->named_count; i++)
    if (strcmp(context->named[i].name, name) == 0) { context->named[i].handlers = handlers; return; }
  if (context->named_count == context->named_capacity) {
    size_t capacity = context->named_capacity == 0 ? 32 : context->named_capacity * 2;
    void *next = realloc(context->named, capacity * sizeof *context->named);
    if (next == NULL) return;
    context->named = next;
    context->named_capacity = capacity;
  }
  context->named[context->named_count].name = dup_text(name);
  context->named[context->named_count].handlers = handlers;
  context->named_count++;
  if (context->game.register_named != NULL) context->game.register_named(context->game.ctx, name);
}
void qa_q1ad_register_spawn(qa_q1ad_context *context, const char *classname, bool replace, qa_q1ad_spawn_fn fn) {
  if (context->spawn_count == context->spawn_capacity) {
    size_t capacity = context->spawn_capacity == 0 ? 32 : context->spawn_capacity * 2;
    void *next = realloc(context->spawns, capacity * sizeof *context->spawns);
    if (next == NULL) return;
    context->spawns = next;
    context->spawn_capacity = capacity;
  }
  context->spawns[context->spawn_count].classname = dup_text(classname);
  context->spawns[context->spawn_count].replace = replace;
  context->spawns[context->spawn_count].fn = fn;
  context->spawn_count++;
  if (context->game.register_spawn != NULL) context->game.register_spawn(context->game.ctx, classname, replace);
}
bool qa_q1ad_spawn_dispatch(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const char *classname = entity->classname != NULL ? entity->classname : "";
  for (size_t i = context->spawn_count; i-- > 0;)
    if (strcmp(context->spawns[i].classname, classname) == 0) {
      context->spawns[i].fn(context, entity);
      return true;
    }
  return false;
}
void qa_q1ad_do_action(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name) {
  if (name == NULL || name[0] == '\0') return;
  const qa_q1ad_handlers *handlers = find_named(context, name);
  if (handlers != NULL && handlers->action != NULL) { handlers->action(context, entity); return; }
  if (context->game.call_action != NULL) context->game.call_action(context->game.ctx, entity, name);
}
void qa_q1ad_do_touch(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name,
                      const qa_actor_id *other, const qa_vec3 *normal) {
  if (name == NULL || name[0] == '\0') return;
  const qa_q1ad_handlers *handlers = find_named(context, name);
  if (handlers != NULL && handlers->touch != NULL) { handlers->touch(context, entity, other, normal); return; }
  if (context->game.call_touch != NULL) context->game.call_touch(context->game.ctx, entity, name, other, normal);
}
void qa_q1ad_do_use(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name,
                    const qa_actor_id *other, const qa_actor_id *activator) {
  if (name == NULL || name[0] == '\0') return;
  const qa_q1ad_handlers *handlers = find_named(context, name);
  if (handlers != NULL && handlers->use != NULL) { handlers->use(context, entity, other, activator); return; }
  if (context->game.call_use != NULL) context->game.call_use(context->game.ctx, entity, name, other, activator);
}
void qa_q1ad_do_die(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name,
                    const qa_actor_id *attacker) {
  if (name == NULL || name[0] == '\0') return;
  const qa_q1ad_handlers *handlers = find_named(context, name);
  if (handlers != NULL && handlers->die != NULL) { handlers->die(context, entity, attacker); return; }
  if (context->game.call_die != NULL) context->game.call_die(context->game.ctx, entity, name, attacker);
}

/* --- Q1AddonContext --- */
bool qa_q1ad_context_init(qa_q1ad_context *context, qa_q1ad_program program,
                          qa_q1ad_services services, qa_q1ad_game game) {
  *context = (qa_q1ad_context){.program = program, .services = services, .game = game};
  return true;
}
void qa_q1ad_context_destroy(qa_q1ad_context *context) {
  for (size_t i = 0; i < context->word_count; i++) free(context->words[i].name);
  for (size_t i = 0; i < context->ref_count; i++) free(context->refs[i].name);
  for (size_t i = 0; i < context->named_count; i++) free(context->named[i].name);
  for (size_t i = 0; i < context->spawn_count; i++) free(context->spawns[i].classname);
  free(context->words); free(context->refs); free(context->ticks);
  free(context->named); free(context->spawns);
  *context = (qa_q1ad_context){0};
}
static bool same_owned(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_actor(a.id, b.id);
}
void qa_q1ad_context_release(qa_q1ad_context *context, qa_owned_actor actor) {
  for (size_t i = context->word_count; i-- > 0;)
    if (same_owned(context->words[i].owner, actor)) {
      free(context->words[i].name);
      context->words[i] = context->words[--context->word_count];
    }
  for (size_t i = context->ref_count; i-- > 0;)
    if (same_owned(context->refs[i].owner, actor)) {
      free(context->refs[i].name);
      context->refs[i] = context->refs[--context->ref_count];
    }
  for (size_t i = 0; i < context->tick_count; i++)
    if (qa_same_actor(context->ticks[i]->actor.id, actor.id)) {
      memmove(&context->ticks[i], &context->ticks[i + 1],
              (context->tick_count - i - 1) * sizeof *context->ticks);
      context->tick_count--;
      break;
    }
}
bool qa_q1ad_player_word(qa_q1ad_context *context, qa_actor_id actor, const char *name, double *out) {
  qa_owned_actor owner;
  if (!qa_q1ad_resolve_owned(context, actor, &owner)) return false;
  for (size_t i = 0; i < context->word_count; i++)
    if (same_owned(context->words[i].owner, owner) && strcmp(context->words[i].name, name) == 0) {
      if (out != NULL) *out = context->words[i].value;
      return true;
    }
  return false;
}
double qa_q1ad_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name) {
  double value = 0;
  return qa_q1ad_player_word(context, actor, name, &value) ? value : 0;
}
void qa_q1ad_set_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name, double value) {
  qa_owned_actor owner;
  if (!qa_q1ad_resolve_owned(context, actor, &owner))
    qa_range_error("Addon player is no longer admitted");
  for (size_t i = 0; i < context->word_count; i++)
    if (same_owned(context->words[i].owner, owner) && strcmp(context->words[i].name, name) == 0) {
      context->words[i].value = (double)(float)value;
      return;
    }
  if (context->word_count == context->word_capacity) {
    size_t capacity = context->word_capacity == 0 ? 32 : context->word_capacity * 2;
    void *next = realloc(context->words, capacity * sizeof *context->words);
    if (next == NULL) return;
    context->words = next;
    context->word_capacity = capacity;
  }
  context->words[context->word_count].owner = owner;
  context->words[context->word_count].name = dup_text(name);
  context->words[context->word_count].value = (double)(float)value;
  context->word_count++;
}
bool qa_q1ad_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name, qa_actor_id *out) {
  qa_owned_actor owner;
  if (!qa_q1ad_resolve_owned(context, actor, &owner)) return false;
  for (size_t i = 0; i < context->ref_count; i++)
    if (same_owned(context->refs[i].owner, owner) && strcmp(context->refs[i].name, name) == 0) {
      if (context->refs[i].has_target && out != NULL) *out = context->refs[i].target;
      return context->refs[i].has_target;
    }
  return false;
}
void qa_q1ad_set_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name,
                                  const qa_actor_id *target) {
  qa_owned_actor owner;
  if (!qa_q1ad_resolve_owned(context, actor, &owner))
    qa_range_error("Addon player is no longer admitted");
  for (size_t i = 0; i < context->ref_count; i++)
    if (same_owned(context->refs[i].owner, owner) && strcmp(context->refs[i].name, name) == 0) {
      context->refs[i].has_target = target != NULL;
      if (target != NULL) context->refs[i].target = *target;
      return;
    }
  if (context->ref_count == context->ref_capacity) {
    size_t capacity = context->ref_capacity == 0 ? 16 : context->ref_capacity * 2;
    void *next = realloc(context->refs, capacity * sizeof *context->refs);
    if (next == NULL) return;
    context->refs = next;
    context->ref_capacity = capacity;
  }
  context->refs[context->ref_count].owner = owner;
  context->refs[context->ref_count].name = dup_text(name);
  context->refs[context->ref_count].has_target = target != NULL;
  if (target != NULL) context->refs[context->ref_count].target = *target;
  context->ref_count++;
}
void qa_q1ad_set_number(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, double value) {
  (void)context;
  char text[64];
  format_number(text, sizeof text, value);
  qa_q1ad_actor_set(entity, name, text);
}
void qa_q1ad_set_vector(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, qa_vec3 value) {
  (void)context;
  char x[48], y[48], z[48], text[160];
  format_number(x, sizeof x, value.x);
  format_number(y, sizeof y, value.y);
  format_number(z, sizeof z, value.z);
  (void)snprintf(text, sizeof text, "%s %s %s", x, y, z);
  qa_q1ad_actor_set(entity, name, text);
}
void qa_q1ad_alpha(qa_q1ad_context *context, qa_q1ad_actor *entity, double alpha) {
  qa_q1ad_set_number(context, entity, "alpha", alpha);
  qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_ALPHA, .actor = entity->actor.id,
                         .has_actor = true, .alpha = alpha};
  qa_q1ad_emit(context, &event);
}
void qa_q1ad_broadcast(qa_q1ad_context *context, const char *text) {
  qa_actor_id players[32];
  size_t count = qa_q1ad_players(context, players, 32);
  for (size_t i = 0; i < count; i++) qa_q1ad_message(context, players[i], text, false, NULL, 0);
}
static qa_vec3 move_direction(qa_q1ad_context *context, qa_vec3 angles) {
  if (angles.y == -1 && angles.x == 0 && angles.z == 0) return (qa_vec3){0, 0, 1};
  if (angles.y == -2 && angles.x == 0 && angles.z == 0) return (qa_vec3){0, 0, -1};
  qa_angle_vectors basis;
  if (context->game.make_vectors != NULL &&
      context->game.make_vectors(context->game.ctx, angles, &basis))
    return basis.forward;
  return qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_Q1).forward;
}
void qa_q1ad_init_trigger(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, false) ||
      qa_q1ad_removed_for_runes(context, entity)) return;
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_vec3 direction = qa_q1ad_actor_has(entity, "movedir")
      ? qa_q1ad_actor_vector(entity, "movedir") : entity->movedir;
  bool zero_angles = body.angles.x == 0 && body.angles.y == 0 && body.angles.z == 0;
  bool nonzero = direction.x != 0 || direction.y != 0 || direction.z != 0;
  entity->movedir = zero_angles || nonzero ? direction : move_direction(context, body.angles);
  entity->solid = "trigger";
  entity->movement = "none";
  entity->model = "";
  qa_q1ad_body_patch patch = {.has_angles = true, .angles = {0, 0, 0}};
  qa_q1ad_set_body(context, entity, &patch);
}
bool qa_q1ad_removed_outside_coop(qa_q1ad_context *context, qa_q1ad_actor *entity, bool inhibit_coop) {
  int64_t flags = (int64_t)entity->spawnflags;
  if (context->game.coop ? inhibit_coop && (flags & 131072) != 0 : (flags & 32768) != 0) {
    qa_q1ad_remove(context, entity);
    return true;
  }
  return false;
}
bool qa_q1ad_removed_for_runes(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->program != QA_Q1AD_MG3) return false;
  double flags = context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
  int64_t flag = (int64_t)262144 * ((int64_t)1 << qa_q1ad_mg3_rune_count(flags));
  if (((int64_t)entity->spawnflags & flag) == 0) return false;
  qa_q1ad_remove(context, entity);
  return true;
}
void qa_q1ad_add_frame_tick(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name) {
  qa_q1ad_actor_set(entity, "addon.frameTick", name);
  if (context->tick_count == context->tick_capacity) {
    size_t capacity = context->tick_capacity == 0 ? 16 : context->tick_capacity * 2;
    void *next = realloc(context->ticks, capacity * sizeof *context->ticks);
    if (next == NULL) return;
    context->ticks = next;
    context->tick_capacity = capacity;
  }
  memmove(&context->ticks[1], &context->ticks[0], context->tick_count * sizeof *context->ticks);
  context->ticks[0] = entity;
  context->tick_count++;
}
void qa_q1ad_remove_frame_tick(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  for (size_t i = 0; i < context->tick_count; i++)
    if (context->ticks[i] == entity) {
      memmove(&context->ticks[i], &context->ticks[i + 1],
              (context->tick_count - i - 1) * sizeof *context->ticks);
      context->tick_count--;
      return;
    }
}
void qa_q1ad_frame(qa_q1ad_context *context, double seconds) {
  context->frame_time = seconds;
  size_t count = context->tick_count;
  qa_q1ad_actor **copy = malloc((count == 0 ? 1 : count) * sizeof *copy);
  if (copy == NULL) return;
  memcpy(copy, context->ticks, count * sizeof *copy);
  for (size_t i = 0; i < count; i++) {
    const char *tick = qa_q1ad_actor_text(copy[i], "addon.frameTick");
    if (tick[0] != '\0' && qa_q1ad_live(context, copy[i]))
      qa_q1ad_do_action(context, copy[i], tick);
  }
  free(copy);
}

/* --- checkpoint extension (registerStateExtension capture/restore) --- */
static qa_save_value *actor_ref_value(int64_t slot, int64_t generation) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *s = qa_save_value_number((double)slot);
  qa_save_value *g = qa_save_value_number((double)generation);
  if (s == NULL || g == NULL || !qa_save_value_set(object, "slot", s) ||
      !qa_save_value_set(object, "generation", g)) {
    qa_save_value_free(object); qa_save_value_free(s); qa_save_value_free(g);
    return NULL;
  }
  return object;
}
uint8_t *qa_q1ad_context_capture(const qa_q1ad_context *context, size_t *out_length) {
  qa_save_value *root = qa_save_value_object();
  qa_save_value *players = qa_save_value_array();
  qa_save_value *references = qa_save_value_array();
  qa_save_value *ticks = qa_save_value_array();
  if (root == NULL || players == NULL || references == NULL || ticks == NULL) goto oom;
  for (size_t i = 0; i < context->word_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < i; j++)
      if (same_owned(context->words[j].owner, context->words[i].owner)) { seen = true; break; }
    if (seen) continue;
    qa_save_value *entry = actor_ref_value(context->words[i].owner.id.slot,
                                           context->words[i].owner.id.generation);
    qa_save_value *words = qa_save_value_array();
    if (entry == NULL || words == NULL) { qa_save_value_free(entry); qa_save_value_free(words); goto oom; }
    for (size_t j = 0; j < context->word_count; j++)
      if (same_owned(context->words[j].owner, context->words[i].owner)) {
        qa_save_value *pair = qa_save_value_array();
        qa_save_value *key = qa_save_value_string(context->words[j].name);
        qa_save_value *value = qa_save_value_number(context->words[j].value);
        if (pair == NULL || key == NULL || value == NULL ||
            !qa_save_value_push(pair, key) || !qa_save_value_push(pair, value) ||
            !qa_save_value_push(words, pair)) {
          qa_save_value_free(pair); qa_save_value_free(key); qa_save_value_free(value);
          qa_save_value_free(entry); qa_save_value_free(words); goto oom;
        }
      }
    if (!qa_save_value_set(entry, "words", words) || !qa_save_value_push(players, entry)) {
      qa_save_value_free(entry); qa_save_value_free(words); goto oom;
    }
  }
  for (size_t i = 0; i < context->ref_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < i; j++)
      if (same_owned(context->refs[j].owner, context->refs[i].owner)) { seen = true; break; }
    if (seen) continue;
    qa_save_value *entry = actor_ref_value(context->refs[i].owner.id.slot,
                                           context->refs[i].owner.id.generation);
    qa_save_value *values = qa_save_value_array();
    if (entry == NULL || values == NULL) { qa_save_value_free(entry); qa_save_value_free(values); goto oom; }
    for (size_t j = 0; j < context->ref_count; j++)
      if (same_owned(context->refs[j].owner, context->refs[i].owner)) {
        qa_save_value *item = qa_save_value_object();
        qa_save_value *name = qa_save_value_string(context->refs[j].name);
        qa_save_value *target = context->refs[j].has_target
            ? actor_ref_value(context->refs[j].target.slot, context->refs[j].target.generation)
            : qa_save_value_null();
        if (item == NULL || name == NULL || target == NULL ||
            !qa_save_value_set(item, "name", name) || !qa_save_value_set(item, "target", target) ||
            !qa_save_value_push(values, item)) {
          qa_save_value_free(item); qa_save_value_free(name); qa_save_value_free(target);
          qa_save_value_free(entry); qa_save_value_free(values); goto oom;
        }
      }
    if (!qa_save_value_set(entry, "values", values) || !qa_save_value_push(references, entry)) {
      qa_save_value_free(entry); qa_save_value_free(values); goto oom;
    }
  }
  for (size_t i = 0; i < context->tick_count; i++) {
    qa_save_value *tick = actor_ref_value(context->ticks[i]->actor.id.slot,
                                          context->ticks[i]->actor.id.generation);
    if (tick == NULL || !qa_save_value_push(ticks, tick)) { qa_save_value_free(tick); goto oom; }
  }
  if (!qa_save_value_set(root, "players", players) ||
      !qa_save_value_set(root, "references", references) ||
      !qa_save_value_set(root, "ticks", ticks)) goto oom;
  {
    uint8_t *bytes = qa_encode_checkpoint_value(root, out_length);
    qa_save_value_free(root);
    return bytes;
  }
oom:
  qa_save_value_free(root); qa_save_value_free(players);
  qa_save_value_free(references); qa_save_value_free(ticks);
  return NULL;
}
static double reader_field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_save_reader_fail(reader, "out of memory");
  double value = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  return value;
}
static void insert_word(qa_q1ad_context *context, qa_owned_actor owner, const char *name, double value) {
  for (size_t i = 0; i < context->word_count; i++)
    if (same_owned(context->words[i].owner, owner) && strcmp(context->words[i].name, name) == 0) {
      context->words[i].value = (double)(float)value;
      return;
    }
  if (context->word_count == context->word_capacity) {
    size_t capacity = context->word_capacity == 0 ? 32 : context->word_capacity * 2;
    void *next = realloc(context->words, capacity * sizeof *context->words);
    if (next == NULL) return;
    context->words = next;
    context->word_capacity = capacity;
  }
  context->words[context->word_count].owner = owner;
  context->words[context->word_count].name = dup_text(name);
  context->words[context->word_count].value = (double)(float)value;
  context->word_count++;
}
static void insert_ref(qa_q1ad_context *context, qa_owned_actor owner, const char *name,
                       const qa_actor_id *target) {
  for (size_t i = 0; i < context->ref_count; i++)
    if (same_owned(context->refs[i].owner, owner) && strcmp(context->refs[i].name, name) == 0) {
      context->refs[i].has_target = target != NULL;
      if (target != NULL) context->refs[i].target = *target;
      return;
    }
  if (context->ref_count == context->ref_capacity) {
    size_t capacity = context->ref_capacity == 0 ? 16 : context->ref_capacity * 2;
    void *next = realloc(context->refs, capacity * sizeof *context->refs);
    if (next == NULL) return;
    context->refs = next;
    context->ref_capacity = capacity;
  }
  context->refs[context->ref_count].owner = owner;
  context->refs[context->ref_count].name = dup_text(name);
  context->refs[context->ref_count].has_target = target != NULL;
  if (target != NULL) context->refs[context->ref_count].target = *target;
  context->ref_count++;
}
void qa_q1ad_context_restore(qa_q1ad_context *context, const uint8_t *bytes, size_t length) {
  for (size_t i = 0; i < context->word_count; i++) free(context->words[i].name);
  for (size_t i = 0; i < context->ref_count; i++) free(context->refs[i].name);
  context->word_count = 0;
  context->ref_count = 0;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  char path[64];
  (void)snprintf(path, sizeof path, "q1:%s:addons", qa_q1ad_program_name(context->program));
  qa_save_reader root;
  if (!qa_save_reader_init(&root, decoded, path)) qa_save_reader_fail(&root, "out of memory");
  qa_save_reader players;
  if (!qa_save_reader_field(&root, "players", &players)) qa_save_reader_fail(&root, "out of memory");
  size_t player_count = qa_save_reader_list_count(&players);
  for (size_t i = 0; i < player_count; i++) {
    qa_save_reader reader;
    if (!qa_save_reader_item(&players, i, &reader)) qa_save_reader_fail(&players, "out of memory");
    qa_owned_actor actor;
    bool resolved = context->game.resolve_saved != NULL && context->game.resolve_saved(
        context->game.ctx, (int64_t)reader_field_integer(&reader, "slot"),
        (int64_t)reader_field_integer(&reader, "generation"), &actor);
    if (!resolved) qa_save_reader_fail(&reader, "missing addon player");
    qa_save_reader words;
    if (!qa_save_reader_field(&reader, "words", &words)) qa_save_reader_fail(&reader, "out of memory");
    size_t word_count = qa_save_reader_list_count(&words);
    for (size_t j = 0; j < word_count; j++) {
      qa_save_reader pair;
      if (!qa_save_reader_item(&words, j, &pair)) qa_save_reader_fail(&words, "out of memory");
      if (qa_save_reader_list_count(&pair) != 2) qa_save_reader_fail(&pair, "invalid addon player word");
      qa_save_reader key, value;
      if (!qa_save_reader_item(&pair, 0, &key) || !qa_save_reader_item(&pair, 1, &value))
        qa_save_reader_fail(&pair, "out of memory");
      const char *name = qa_save_reader_string(&key);
      double number = qa_save_reader_number(&value);
      insert_word(context, actor, name, number);
      qa_save_reader_destroy(&key); qa_save_reader_destroy(&value);
      qa_save_reader_destroy(&pair);
    }
    qa_save_reader_destroy(&words);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&players);
  qa_save_reader references;
  if (!qa_save_reader_field(&root, "references", &references)) qa_save_reader_fail(&root, "out of memory");
  size_t ref_count = qa_save_reader_list_count(&references);
  for (size_t i = 0; i < ref_count; i++) {
    qa_save_reader reader;
    if (!qa_save_reader_item(&references, i, &reader)) qa_save_reader_fail(&references, "out of memory");
    qa_owned_actor actor;
    bool resolved = context->game.resolve_saved != NULL && context->game.resolve_saved(
        context->game.ctx, (int64_t)reader_field_integer(&reader, "slot"),
        (int64_t)reader_field_integer(&reader, "generation"), &actor);
    if (!resolved) qa_save_reader_fail(&reader, "missing addon player references");
    qa_save_reader values;
    if (!qa_save_reader_field(&reader, "values", &values)) qa_save_reader_fail(&reader, "out of memory");
    size_t value_count = qa_save_reader_list_count(&values);
    for (size_t j = 0; j < value_count; j++) {
      qa_save_reader item, name_reader, target_reader;
      if (!qa_save_reader_item(&values, j, &item)) qa_save_reader_fail(&values, "out of memory");
      if (!qa_save_reader_field(&item, "name", &name_reader) ||
          !qa_save_reader_field(&item, "target", &target_reader))
        qa_save_reader_fail(&item, "out of memory");
      const char *name = qa_save_reader_string(&name_reader);
      qa_actor_id target;
      const qa_actor_id *target_ptr = NULL;
      if (!qa_save_reader_is_null(&target_reader)) {
        bool ok = context->game.reference_saved != NULL && context->game.reference_saved(
            context->game.ctx, (int64_t)reader_field_integer(&target_reader, "slot"),
            (int64_t)reader_field_integer(&target_reader, "generation"), &target);
        if (ok) target_ptr = &target;
      }
      insert_ref(context, actor, name, target_ptr);
      qa_save_reader_destroy(&name_reader); qa_save_reader_destroy(&target_reader);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&values);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&references);
  context->tick_count = 0;
  qa_save_reader ticks;
  if (!qa_save_reader_field(&root, "ticks", &ticks)) qa_save_reader_fail(&root, "out of memory");
  size_t tick_count = qa_save_reader_list_count(&ticks);
  for (size_t i = 0; i < tick_count; i++) {
    qa_save_reader reader;
    if (!qa_save_reader_item(&ticks, i, &reader)) qa_save_reader_fail(&ticks, "out of memory");
    qa_owned_actor actor;
    bool resolved = context->game.resolve_saved != NULL && context->game.resolve_saved(
        context->game.ctx, (int64_t)reader_field_integer(&reader, "slot"),
        (int64_t)reader_field_integer(&reader, "generation"), &actor);
    qa_q1ad_actor *entity = resolved ? qa_q1ad_entity(context, &actor.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing addon frame tick actor");
    if (context->tick_count == context->tick_capacity) {
      size_t capacity = context->tick_capacity == 0 ? 16 : context->tick_capacity * 2;
      void *next = realloc(context->ticks, capacity * sizeof *context->ticks);
      if (next == NULL) qa_save_reader_fail(&reader, "out of memory");
      context->ticks = next;
      context->tick_capacity = capacity;
    }
    context->ticks[context->tick_count++] = entity;
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&ticks);
  qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
}



