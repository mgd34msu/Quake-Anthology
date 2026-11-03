/*
 * Port of src/content/q1/equipment/threewave-grapple.ts.
 * GPL-2.0-or-later. Bodies, combat, attachments and actor liveness are
 * the shared world tables of game->host. The checkpoint state extension
 * uses the persistence checkpoint-value envelope and resolves saved
 * owners through the actor registry (resolveSaved/referenceSaved).
 */
#include "qa/content/q1/equipment/threewave_grapple.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

static double fr(double value) { return (float)value; }
static qa_vec3 v3(double x, double y, double z) {
  const qa_vec3 result = {x, y, z};
  return result;
}
/* TS game.host.bodies.read(actor); false = null. */
static bool body_of(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (!body.has_value) return false;
  *out = qa_q1_body_from_state(body.value);
  return true;
}
/* TS game.host.actors.isLive(actor). */
static bool is_live(qa_q1_services *game, qa_actor_id actor) {
  return qa_session_actor_registry_is_live(game->host.actors, actor);
}
/* TS game.host.combat.read(actor)?.canTakeDamage. */
static bool can_take_damage(qa_q1_services *game, qa_actor_id actor) {
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, actor);
  return combat.has_value && combat.value.can_take_damage;
}
static qa_owned_actor owner_of(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  qa_owned_actor owner;
  if (qa_session_actor_registry_resolve_owned(grapple->game->host.actors, actor, &owner)) return owner;
  qa_range_error("Grapple owner is no longer admitted"); /* TS Error throw */
}
static qa_q1_body body_required(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  qa_q1_body body;
  if (!body_of(grapple->game, actor, &body)) qa_range_error("Grapple actor has no shared body");
  return body;
}
static bool edition_classic(qa_q1_threewave_grapple *grapple) {
  return grapple->game->options.edition != NULL && !strcmp(grapple->game->options.edition, "classic");
}
static void sound_weapon(qa_q1_threewave_grapple *grapple, qa_actor_id actor, const char *path) {
  qa_q1_sound(grapple->game, actor, path, "weapon", 1, 1);
}
static qa_q1_bound_action named_action(qa_q1_threewave_grapple *grapple, qa_q1_actor *entity, const char *name) {
  return qa_q1_callback_registry_action(grapple->game->named, entity, name);
}
static void emit_particles(qa_q1_threewave_grapple *grapple, qa_vec3 origin, qa_vec3 direction, double count) {
  qa_q1_event event = {0};
  event.kind = "particles";
  event.has_origin = true;
  event.origin = origin;
  event.direction = direction;
  event.color = 73;
  event.count = count;
  qa_q1_emit(grapple->game, event);
}

qa_q1_threewave_state *qa_q1_threewave_state_for(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  (void)owner_of(grapple, actor);
  for (size_t i = 0; i < grapple->state_count; i++)
    if (qa_same_actor(grapple->states[i].actor, actor)) return &grapple->states[i].state;
  if (grapple->state_count == grapple->state_capacity) {
    const size_t grown = grapple->state_capacity ? grapple->state_capacity * 2 : 8;
    void *next = realloc(grapple->states, grown * sizeof *grapple->states);
    if (!next) return NULL;
    grapple->states = next;
    grapple->state_capacity = grown;
  }
  grapple->states[grapple->state_count].actor = actor;
  memset(&grapple->states[grapple->state_count].state, 0, sizeof(qa_q1_threewave_state));
  return &grapple->states[grapple->state_count++].state;
}

static qa_q1_threewave_state *find_state(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  for (size_t i = 0; i < grapple->state_count; i++)
    if (qa_same_actor(grapple->states[i].actor, actor)) return &grapple->states[i].state;
  return NULL;
}

bool qa_q1_threewave_pulling(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  const qa_q1_threewave_state *state = find_state(grapple, actor);
  return state != NULL && state->pulling;
}

qa_q1_actor *qa_q1_threewave_hook(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  for (size_t i = 0; i < grapple->game->entity_count; i++) {
    qa_q1_actor *entity = grapple->game->entities[i];
    if (entity != NULL && entity->classname != NULL && !strcmp(entity->classname, "ctf_hook") &&
        entity->has_owner && qa_same_actor(entity->owner, actor))
      return entity;
  }
  return NULL;
}

void qa_q1_threewave_release(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  qa_q1_services *game = grapple->game;
  qa_q1_threewave_state *state = find_state(grapple, actor);
  qa_q1_actor *animation =
      state != NULL && state->has_animation ? qa_q1_get_entity(game, &state->animation) : NULL;
  if (state != NULL) {
    state->has_animation = false;
    state->pulling = false;
  }
  if (animation != NULL) qa_q1_remove(game, animation);
  qa_q1_actor *hook = qa_q1_threewave_hook(grapple, actor);
  if (hook == NULL) return;
  if (edition_classic(grapple) && is_live(game, actor))
    sound_weapon(grapple, owner_of(grapple, actor).id, "weapons/bounce2.wav");
  qa_shared_body_table_detach(game->host.bodies, hook->actor);
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *link = game->entities[i];
    if (link != NULL && link->classname != NULL && !strcmp(link->classname, "ctf_hook_link") &&
        link->has_owner && qa_same_actor(link->owner, hook->actor.id))
      qa_q1_remove(game, link);
  }
  qa_q1_remove(game, hook);
}

static void vanish(qa_q1_threewave_grapple *grapple, qa_q1_actor *hook) {
  if (hook->has_owner) {
    qa_q1_threewave_release(grapple, hook->owner);
    return;
  }
  qa_q1_remove(grapple->game, hook);
}

static void pull(qa_q1_threewave_grapple *grapple, qa_q1_actor *hook) {
  qa_q1_services *game = grapple->game;
  qa_actor_id enemy;
  if (!hook->has_owner || !is_live(game, hook->owner) ||
      !qa_q1_actor_reference_get(hook, "ctf.enemy", &enemy) || !is_live(game, enemy)) {
    vanish(grapple, hook);
    return;
  }
  const qa_actor_id owner = hook->owner;
  const qa_q1_threewave_input input = grapple->host.input(grapple->host.ctx, owner);
  const qa_q1_threewave_anchor target = grapple->host.anchor(grapple->host.ctx, enemy);
  qa_q1_threewave_state_for(grapple, owner)->pulling = true;
  if (input.release || input.teleport_until > game->time || qa_q1_health(game, owner) <= 0 || !target.solid) {
    vanish(grapple, hook);
    return;
  }
  const qa_q1_body enemy_body = body_required(grapple, enemy);
  if (can_take_damage(game, enemy) && grapple->host.can_pulse(grapple->host.ctx, owner, enemy)) {
    if (!grapple->host.can_damage(grapple->host.ctx, enemy, owner)) {
      vanish(grapple, hook);
      return;
    }
    sound_weapon(grapple, hook->actor.id, "blob/land1.wav");
    (void)qa_q1_damage(game, enemy, hook->actor.id, &owner, 1, NULL, "direct", "ctf:grapple", NULL);
    if (!qa_q1_live(game, hook) || !is_live(game, owner) || !is_live(game, enemy)) {
      vanish(grapple, hook);
      return;
    }
    double x2 = 100 * (qa_q1_host_random(game) * 2 - 1);
    double y2 = 100 * (qa_q1_host_random(game) * 2 - 1);
    double z2 = 100 * (qa_q1_host_random(game) * 2 - 1) + 50;
    const qa_vec3 spray = v3(x2, y2, z2);
    emit_particles(grapple, qa_q1_get_body(game, hook).origin, qa_q1_vscale(spray, 0.1), 40);
  }
  {
    qa_q1_body patch = qa_q1_get_body(game, hook);
    if (target.centered) {
      patch.velocity = QA_Q1_ZERO;
      patch.origin = qa_q1_vadd(enemy_body.origin,
                                    qa_q1_vscale(qa_q1_vadd(enemy_body.bounds.min, enemy_body.bounds.max), 0.5));
      qa_q1_set_body_patch(game, hook, &patch, true, false, true, false, false);
    } else {
      patch.velocity = enemy_body.velocity;
      qa_q1_set_body_patch(game, hook, &patch, false, false, true, false, false);
    }
  }
  const qa_q1_body body = body_required(grapple, owner);
  const qa_q1_basis basis = qa_q1_vectors(body.angles);
  const qa_vec3 relative = qa_q1_vsub(
      qa_q1_get_body(game, hook).origin,
      qa_q1_vadd(body.origin, qa_q1_vadd(qa_q1_vscale(basis.up, input.jump ? 0 : 16),
                                         qa_q1_vscale(basis.forward, 16))));
  const double distance = qa_q1_length(relative);
  const qa_vec3 velocity = qa_q1_vscale(qa_q1_normalize(relative), distance <= 100 ? distance * 10 : 1000);
  const double traveled = qa_q1_length(qa_q1_vsub(body.origin, qa_q1_actor_vector(hook, "ctf.lastOrigin")));
  if (traveled > 10 && qa_q1_actor_number(hook, "style") == 3) {
    if (edition_classic(grapple)) sound_weapon(grapple, owner_of(grapple, owner).id, "weapons/chain2.wav");
    (void)qa_q1_actor_field_set(hook, "style", "2");
  }
  if (traveled < 10 && qa_q1_actor_number(hook, "style") == 2) {
    if (edition_classic(grapple)) sound_weapon(grapple, owner_of(grapple, owner).id, "weapons/chain3.wav");
    (void)qa_q1_actor_field_set(hook, "style", "3");
  }
  {
    const qa_owned_actor actor = owner_of(grapple, owner);
    qa_q1_body patch = body;
    patch.velocity = velocity;
    qa_shared_body_table_write(game->host.bodies, actor, qa_q1_body_to_state(&patch));
    qa_shared_body_table_link(game->host.bodies, actor, NULL);
  }
  {
    char text[96];
    (void)snprintf(text, sizeof text, "%s %s %s", QA_JS_NUMBER(fr(body.origin.x)), QA_JS_NUMBER(fr(body.origin.y)), QA_JS_NUMBER(fr(body.origin.z)));
    (void)qa_q1_actor_field_set(hook, "ctf.lastOrigin", text);
  }
  qa_q1_link(game, hook);
  qa_q1_schedule(game, hook, 0.1, named_action(grapple, hook, "ctf:hook_pull"));
}

void qa_q1_threewave_touch(qa_q1_threewave_grapple *grapple, qa_q1_actor *hook, qa_actor_id other,
                           const qa_touch_surface *surface) {
  qa_q1_services *game = grapple->game;
  if (!hook->has_owner || !is_live(game, hook->owner)) {
    vanish(grapple, hook);
    return;
  }
  const qa_actor_id owner = hook->owner;
  if (qa_same_actor(owner, other)) return;
  if ((surface != NULL && (qa_js_to_int32(surface->native_flags) & 4) != 0) ||
      !strcmp(game->host.contents(game->host.ctx, qa_q1_get_body(game, hook).origin), "sky")) {
    vanish(grapple, hook);
    return;
  }
  if (!grapple->host.can_attach(grapple->host.ctx, owner, other)) return;
  const qa_q1_threewave_anchor target = grapple->host.anchor(grapple->host.ctx, other);
  if (can_take_damage(game, other)) {
    if (!target.player) sound_weapon(grapple, hook->actor.id, "player/axhit2.wav");
    (void)qa_q1_damage(game, other, hook->actor.id, &owner, 10, "ctf:grapple", "direct", "ctf:grapple", NULL);
    if (!qa_q1_live(game, hook) || !is_live(game, owner) || !is_live(game, other)) {
      vanish(grapple, hook);
      return;
    }
    qa_vec3 origin2 = qa_q1_get_body(game, hook).origin;
    qa_vec3 direction = qa_q1_vscale(qa_q1_get_body(game, hook).velocity, 0.1);
    emit_particles(grapple, origin2,
                     direction, 20);
  } else {
    sound_weapon(grapple, hook->actor.id, "player/axhit2.wav");
    hook->angular_velocity = QA_Q1_ZERO;
  }
  if (!grapple->host.input(grapple->host.ctx, owner).held) {
    vanish(grapple, hook);
    return;
  }
  const qa_q1_body body = body_required(grapple, other);
  {
    qa_q1_body patch = qa_q1_get_body(game, hook);
    if (target.centered) {
      patch.origin = qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5));
      patch.velocity = QA_Q1_ZERO;
      qa_q1_set_body_patch(game, hook, &patch, true, false, true, false, false);
    } else {
      patch.velocity = body.velocity;
      qa_q1_set_body_patch(game, hook, &patch, false, false, true, false, false);
    }
  }
  {
    qa_body_attachment attachment;
    memset(&attachment, 0, sizeof attachment);
    attachment.anchor = other;
    attachment.follow_kind = target.centered ? QA_BODY_FOLLOW_CENTER : QA_BODY_FOLLOW_TRANSLATION;
    if (!target.centered) attachment.follow_offset = qa_q1_vsub(qa_q1_get_body(game, hook).origin, body.origin);
    qa_shared_body_table_attach(game->host.bodies, hook->actor, attachment);
  }
  if (edition_classic(grapple)) sound_weapon(grapple, owner_of(grapple, owner).id, "weapons/chain2.wav");
  (void)qa_q1_actor_reference_set(hook, "ctf.enemy", &other);
  (void)qa_q1_actor_field_set(hook, "style", "2");
  memset(&hook->touch, 0, sizeof hook->touch);
  qa_q1_link(game, hook);
  qa_q1_schedule(game, hook, 0.1, named_action(grapple, hook, "ctf:hook_pull"));
}

static void position_link(qa_q1_threewave_grapple *grapple, qa_q1_actor *link) {
  qa_q1_services *game = grapple->game;
  qa_q1_actor *head = link->has_owner ? qa_q1_get_entity(game, &link->owner) : NULL;
  qa_actor_id tail;
  if (head == NULL || !qa_q1_actor_reference_get(link, "ctf.tail", &tail) || !is_live(game, tail)) {
    qa_q1_remove(game, link);
    return;
  }
  const qa_q1_body body = body_required(grapple, tail);
  const qa_q1_basis basis = qa_q1_vectors(body.angles);
  const qa_vec3 end = qa_q1_vadd(body.origin,
                                 qa_q1_vadd(qa_q1_vscale(basis.up, grapple->host.input(grapple->host.ctx, tail).jump ? 0 : 16),
                                            qa_q1_vscale(basis.forward, 16)));
  const qa_vec3 origin = qa_q1_get_body(game, head).origin;
  qa_q1_set_origin(game, link, qa_q1_vadd(origin, qa_q1_vscale(qa_q1_vsub(end, origin),
                                                               qa_q1_actor_number(link, "weapon"))));
  qa_q1_schedule(game, link, 0.1, named_action(grapple, link, "ctf:hook_link"));
}

bool qa_q1_threewave_fire(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  qa_q1_services *game = grapple->game;
  if (qa_q1_threewave_hook(grapple, actor) != NULL || qa_q1_health(game, actor) <= 0) return false;
  const qa_owned_actor owner = owner_of(grapple, actor);
  const qa_q1_body body = body_required(grapple, actor);
  const qa_vec3 forward = qa_q1_vectors(grapple->host.input(grapple->host.ctx, actor).view_angles).forward;
  const qa_vec3 direction = grapple->host.aim(grapple->host.ctx, actor, forward);
  (void)qa_q1_threewave_state_for(grapple, actor);
  qa_q1_actor *hook = qa_q1_create(game, "ctf_hook", NULL, NULL);
  if (hook == NULL) return false;
  hook->has_owner = true;
  hook->owner = actor;
  hook->movement = "fly";
  hook->solid = "bbox";
  {
    const size_t len = strlen("progs/star.mdl") + 1;
    char *copy = malloc(len);
    if (copy) {
      memcpy(copy, "progs/star.mdl", len);
      free(hook->model);
      hook->model = copy;
    }
  }
  hook->angular_velocity = v3(0, 0, -500);
  hook->touch = qa_q1_callback_registry_touch(game->named, hook, "ctf:hook_touch");
  {
    char text[40];
    (void)snprintf(text, sizeof text, "%s", QA_JS_NUMBER(fr(game->time)));
    (void)qa_q1_actor_field_set(hook, "ctf.fired", text);
  }
  const double yaw = atan2(direction.y, direction.x) * 180 / 3.141592653589793;
  const double pitch = atan2(direction.z, hypot(direction.x, direction.y)) * 180 / 3.141592653589793;
  {
    qa_q1_body patch = qa_q1_get_body(game, hook);
    patch.origin = qa_q1_vadd(body.origin, qa_q1_vadd(qa_q1_vscale(forward, 16), v3(0, 0, 16)));
    patch.bounds = QA_Q1_POINT;
    patch.velocity = qa_q1_vscale(direction, 800);
    patch.angles = v3(pitch, yaw < 0 ? yaw + 360 : yaw, 0);
    qa_q1_set_body_patch(game, hook, &patch, true, true, true, true, false);
  }
  qa_q1_link(game, hook);
  sound_weapon(grapple, owner.id, "weapons/chain1.wav");
  qa_q1_schedule(game, hook, 0.1, named_action(grapple, hook, "ctf:hook_flying"));
  if (edition_classic(grapple)) {
    for (int number = 3; number > 0; number--) {
      qa_q1_actor *link = qa_q1_create(game, "ctf_hook_link", NULL, NULL);
      if (link == NULL) continue;
      link->has_owner = true;
      link->owner = hook->actor.id;
      link->movement = "noclip";
      link->solid = "none";
      {
        const size_t len = strlen("progs/bit.mdl") + 1;
        char *copy = malloc(len);
        if (copy) {
          memcpy(copy, "progs/bit.mdl", len);
          free(link->model);
          link->model = copy;
        }
      }
      (void)qa_q1_actor_reference_set(link, "ctf.tail", &actor);
      {
        char text[40];
        (void)snprintf(text, sizeof text, "%s", QA_JS_NUMBER(number / 4.0));
        (void)qa_q1_actor_field_set(link, "weapon", text);
      }
      link->angular_velocity = v3(310, 410, 510);
      {
        qa_q1_body patch = qa_q1_get_body(game, link);
        patch.bounds = QA_Q1_POINT;
        patch.angles = v3(31 * number, 41 * number, 51 * number);
        qa_q1_set_body_patch(game, link, &patch, false, true, false, true, false);
      }
      position_link(grapple, link);
    }
  }
  return true;
}

void qa_q1_threewave_trail(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  if (edition_classic(grapple)) return;
  qa_q1_actor *hook = qa_q1_threewave_hook(grapple, actor);
  if (hook == NULL) return;
  const qa_q1_body body = qa_q1_get_body(grapple->game, hook);
  qa_vec3 offset = qa_q1_vscale(qa_q1_vectors(body.angles).forward, -7);
  qa_q1_event event = {0};
  event.kind = "beam";
  event.beam_style = "grapple";
  event.has_actor = true;
  event.actor = hook->actor.id;
  offset.z = -offset.z;
  event.start = qa_q1_vadd(body.origin, offset);
  event.end = qa_q1_vadd(body_required(grapple, actor).origin, v3(0, 0, 16));
  qa_q1_emit(grapple->game, event);
}

void qa_q1_threewave_on_release(qa_q1_threewave_grapple *grapple, qa_actor_id actor) {
  qa_q1_services *game = grapple->game;
  qa_q1_threewave_release(grapple, actor);
  for (size_t i = 0; i < grapple->state_count; i++)
    if (qa_same_actor(grapple->states[i].actor, actor)) {
      memmove(&grapple->states[i], &grapple->states[i + 1],
              (grapple->state_count - i - 1) * sizeof *grapple->states);
      grapple->state_count--;
      break;
    }
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *link = game->entities[i];
    if (link != NULL && link->classname != NULL && !strcmp(link->classname, "ctf_hook_link") &&
        link->has_owner && qa_same_actor(link->owner, actor))
      qa_q1_remove(game, link);
  }
  for (size_t i = 0; i < grapple->state_count; i++) {
    qa_q1_threewave_state *state = &grapple->states[i].state;
    if (state->has_animation && qa_same_actor(state->animation, actor)) state->has_animation = false;
    if (qa_q1_threewave_hook(grapple, grapple->states[i].actor) == NULL) state->pulling = false;
  }
}

static void touch_cb(void *ctx, qa_q1_services *game, qa_q1_actor *hook, qa_actor_id other,
                     const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal;
  qa_q1_threewave_touch(ctx, hook, other, surface);
}
static void pull_cb(void *ctx, qa_q1_services *game, qa_q1_actor *hook) {
  (void)game;
  pull(ctx, hook);
}
static void link_cb(void *ctx, qa_q1_services *game, qa_q1_actor *link) {
  (void)game;
  position_link(ctx, link);
}
static void flying_cb(void *ctx, qa_q1_services *game, qa_q1_actor *hook) {
  qa_q1_threewave_grapple *grapple = ctx;
  if (!hook->has_owner || !is_live(game, hook->owner) ||
      game->time >= qa_q1_actor_number(hook, "ctf.fired") + 5) {
    vanish(grapple, hook);
    return;
  }
  if (grapple->host.input(grapple->host.ctx, hook->owner).release) {
    vanish(grapple, hook);
    return;
  }
  qa_q1_schedule(game, hook, 0.1, named_action(grapple, hook, "ctf:hook_flying"));
}

static bool capture_extension(void *ctx, uint8_t **out, size_t *out_len) {
  qa_q1_threewave_grapple *grapple = ctx;
  qa_save_value *list = qa_save_value_array();
  if (!list) return false;
  for (size_t i = 0; i < grapple->state_count; i++) {
    qa_save_value *object = qa_save_value_object();
    qa_save_value *actor = qa_save_value_object();
    const qa_q1_threewave_state *state = &grapple->states[i].state;
    bool ok = object && actor &&
              qa_save_value_set(actor, "slot", qa_save_value_number((double)grapple->states[i].actor.slot)) &&
              qa_save_value_set(actor, "generation",
                                qa_save_value_number((double)grapple->states[i].actor.generation)) &&
              qa_save_value_set(object, "actor", actor) &&
              qa_save_value_set(object, "pulling", qa_save_value_boolean(state->pulling)) &&
              qa_save_value_set(object, "weaponFrame", qa_save_value_number(state->weapon_frame)) &&
              qa_save_value_set(object, "attackFinished", qa_save_value_number(state->attack_finished)) &&
              qa_save_value_set(object, "releaseTime", qa_save_value_number(state->release_time));
    if (ok) {
      if (state->has_animation) {
        qa_save_value *animation = qa_save_value_object();
        ok = animation &&
             qa_save_value_set(animation, "slot", qa_save_value_number((double)state->animation.slot)) &&
             qa_save_value_set(animation, "generation",
                               qa_save_value_number((double)state->animation.generation)) &&
             qa_save_value_set(object, "animation", animation);
      } else {
        ok = qa_save_value_set(object, "animation", qa_save_value_null());
      }
    }
    if (!ok || !qa_save_value_push(list, object)) {
      qa_save_value_free(list);
      return false;
    }
  }
  *out = qa_encode_checkpoint_value(list, out_len);
  qa_save_value_free(list);
  return *out != NULL;
}

static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void restore_extension(void *ctx, const uint8_t *bytes, size_t len) {
  qa_q1_threewave_grapple *grapple = ctx;
  qa_q1_services *game = grapple->game;
  grapple->state_count = 0;
  qa_save_value *tree = qa_decode_checkpoint_value(bytes, len);
  if (!tree) return;
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, tree);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, tree, "threewave-grapple")) {
    qa_cleanup_pop(&decoded_cleanup);
    qa_save_value_free(tree);
    return;
  }
  const size_t count = qa_save_reader_list_count(&reader);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, actor_reader, field;
    if (!qa_save_reader_item(&reader, i, &item)) continue;
    if (!qa_save_reader_field(&item, "actor", &actor_reader)) {
      qa_save_reader_destroy(&item);
      continue;
    }
    qa_save_reader slot_reader, generation_reader;
    double slot = 0, generation = 0;
    if (qa_save_reader_field(&actor_reader, "slot", &slot_reader)) {
      slot = qa_save_reader_integer_min(&slot_reader, 0);
      qa_save_reader_destroy(&slot_reader);
    }
    if (qa_save_reader_field(&actor_reader, "generation", &generation_reader)) {
      generation = qa_save_reader_integer_min(&generation_reader, 0);
      qa_save_reader_destroy(&generation_reader);
    }
    qa_save_reader_destroy(&actor_reader);
    const qa_saved_actor_id saved = {slot, generation};
    qa_owned_actor owner;
    if (!qa_session_actor_registry_resolve_saved(game->host.actors, saved, &owner))
      qa_save_reader_fail(&item, "missing grapple owner");
    const qa_actor_id actor_id = owner.id;
    if (grapple->state_count == grapple->state_capacity) {
      const size_t grown = grapple->state_capacity ? grapple->state_capacity * 2 : 8;
      void *next = realloc(grapple->states, grown * sizeof *grapple->states);
      if (!next) {
        qa_save_reader_destroy(&item);
        continue;
      }
      grapple->states = next;
      grapple->state_capacity = grown;
    }
    qa_q1_threewave_state *state = &grapple->states[grapple->state_count].state;
    grapple->states[grapple->state_count].actor = actor_id;
    memset(state, 0, sizeof *state);
    grapple->state_count++;
    if (qa_save_reader_field(&item, "pulling", &field)) {
      state->pulling = qa_save_reader_boolean(&field);
      qa_save_reader_destroy(&field);
    }
    if (qa_save_reader_field(&item, "weaponFrame", &field)) {
      state->weapon_frame = qa_save_reader_number(&field);
      qa_save_reader_destroy(&field);
    }
    if (qa_save_reader_field(&item, "attackFinished", &field)) {
      state->attack_finished = qa_save_reader_number(&field);
      qa_save_reader_destroy(&field);
    }
    if (qa_save_reader_field(&item, "releaseTime", &field)) {
      state->release_time = qa_save_reader_number(&field);
      qa_save_reader_destroy(&field);
    }
    if (qa_save_reader_field(&item, "animation", &field)) {
      if (!qa_save_reader_is_null(&field) && !qa_save_reader_is_undefined(&field)) {
        qa_save_reader slot_r, gen_r;
        double a_slot = 0, a_gen = 0;
        if (qa_save_reader_field(&field, "slot", &slot_r)) {
          a_slot = qa_save_reader_integer_min(&slot_r, 0);
          qa_save_reader_destroy(&slot_r);
        }
        if (qa_save_reader_field(&field, "generation", &gen_r)) {
          a_gen = qa_save_reader_integer_min(&gen_r, 0);
          qa_save_reader_destroy(&gen_r);
        }
        const qa_saved_actor_id animation = {a_slot, a_gen};
        state->has_animation = true;
        state->animation = qa_session_actor_registry_reference_saved(game->host.actors, animation, NULL);
      }
      qa_save_reader_destroy(&field);
    }
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(tree);
}

/* Extension registrations are borrowed by the services; keep the
 * structs alive until grapple_free. */
typedef struct extension_entry {
  qa_q1_threewave_grapple *grapple;
  qa_q1_state_extension *extension;
} extension_entry;
static extension_entry *extensions;
static size_t extension_count, extension_capacity;

bool qa_q1_threewave_grapple_init(qa_q1_threewave_grapple *grapple, qa_q1_services *game,
                                  qa_q1_threewave_host host) {
  memset(grapple, 0, sizeof *grapple);
  grapple->game = game;
  grapple->host = host;
  qa_q1_callback_handlers h = {0};
  h.ctx = grapple;
  h.touch = touch_cb;
  qa_q1_callback_registry_register(game->named, "ctf:hook_touch", h);
  h = (qa_q1_callback_handlers){0};
  h.ctx = grapple;
  h.action = pull_cb;
  qa_q1_callback_registry_register(game->named, "ctf:hook_pull", h);
  h = (qa_q1_callback_handlers){0};
  h.ctx = grapple;
  h.action = link_cb;
  qa_q1_callback_registry_register(game->named, "ctf:hook_link", h);
  h = (qa_q1_callback_handlers){0};
  h.ctx = grapple;
  h.action = flying_cb;
  qa_q1_callback_registry_register(game->named, "ctf:hook_flying", h);
  qa_q1_state_extension *extension = calloc(1, sizeof *extension);
  if (!extension) return false;
  extension->ctx = grapple;
  extension->id = "q1:equipment:threewave-grapple";
  extension->capture = capture_extension;
  extension->restore = restore_extension;
  if (extension_count == extension_capacity) {
    const size_t grown = extension_capacity ? extension_capacity * 2 : 4;
    extension_entry *next = realloc(extensions, grown * sizeof *next);
    if (!next) {
      free(extension);
      return false;
    }
    extensions = next;
    extension_capacity = grown;
  }
  extensions[extension_count].grapple = grapple;
  extensions[extension_count].extension = extension;
  extension_count++;
  qa_q1_register_state_extension(game, extension);
  return true;
}

void qa_q1_threewave_grapple_free(qa_q1_threewave_grapple *grapple) {
  if (grapple == NULL) return;
  for (size_t i = 0; i < extension_count; i++)
    if (extensions[i].grapple == grapple) {
      free(extensions[i].extension);
      memmove(&extensions[i], &extensions[i + 1], (extension_count - i - 1) * sizeof *extensions);
      extension_count--;
      break;
    }
  free(grapple->states);
  memset(grapple, 0, sizeof *grapple);
}
