/* Port of src/content/q2/base/player/character.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/character.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/base/player/environment.h"
#include "qa/content/q2/base/player/view.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

qa_q2_character_actor *qa_q2_character_actor_create(qa_owned_actor actor, qa_q2_character_host host, qa_q2_character_options options) {
  qa_q2_character_actor *character = calloc(1, sizeof *character);
  if (!character) return NULL;
  character->actor = actor;
  character->host = host;
  character->options = options;
  qa_q2_spawn_fields spawn = {0};
  spawn.ordinal = -1;
  spawn.classname = "player";
  qa_q2_entity_init(&character->entity, actor, spawn);
  character->entity.model = options.model;
  character->entity.skin = options.skin;
  character->entity.view_height = 22;
  character->entity.render_flags = options.has_edition && options.edition == QA_Q2_EDITION_RERELEASE ? 32768 : 0;
  qa_q2_base_player_state_init(&character->state, options.slot, host.now(host.ctx));
  character->state.air_finished = host.now(host.ctx) + 12;
  character->rules = qa_q2_create_player_rules(options.view_rules, options.view_rules_mask);
  return character;
}
void qa_q2_character_actor_destroy(qa_q2_character_actor *character) {
  if (!character) return;
  qa_q2_entity_destroy(&character->entity);
  qa_q2_base_player_state_destroy(&character->state);
  free(character);
}

qa_q2_base_character_checkpoint qa_q2_character_actor_capture(qa_q2_character_actor *character) {
  qa_q2_base_character_checkpoint out = {0};
  qa_q2_entity *entity = &character->entity;
  out.version = 1;
  out.pain_index = character->pain_index;
  out.death_index = character->death_index;
  qa_q2_base_player_state_checkpoint_capture(&character->state, &out.state);
  out.rules = character->rules;
  out.entity = (qa_q2_base_character_entity_checkpoint){entity->model, entity->model2, entity->model3, entity->model4,
      entity->skin, entity->frame, entity->old_frame, entity->scale, entity->effects, entity->render_flags,
      entity->flags, entity->server_flags, entity->view_height, entity->max_health, entity->sound, entity->visible};
  out.has_last_attack = entity->last_attack != NULL;
  if (entity->last_attack) out.last_attack = qa_q2_save_attack(*entity->last_attack);
  return out;
}
void qa_q2_character_actor_restore(qa_q2_character_actor *character, qa_q2_base_character_checkpoint checkpoint,
                                   qa_actor_id (*resolve)(void *ctx, qa_saved_actor_id saved), void *resolve_ctx) {
  character->pain_index = checkpoint.pain_index;
  character->death_index = checkpoint.death_index;
  qa_q2_base_player_state_checkpoint_restore(&character->state, &checkpoint.state, resolve, resolve_ctx);
  character->rules = checkpoint.rules;
  qa_q2_entity *entity = &character->entity;
  entity->model = checkpoint.entity.model; entity->model2 = checkpoint.entity.model2;
  entity->model3 = checkpoint.entity.model3; entity->model4 = checkpoint.entity.model4;
  entity->skin = checkpoint.entity.skin; entity->frame = checkpoint.entity.frame;
  entity->old_frame = checkpoint.entity.old_frame; entity->scale = checkpoint.entity.scale;
  entity->effects = checkpoint.entity.effects; entity->render_flags = checkpoint.entity.render_flags;
  entity->flags = checkpoint.entity.flags; entity->server_flags = checkpoint.entity.server_flags;
  entity->view_height = checkpoint.entity.view_height; entity->max_health = checkpoint.entity.max_health;
  entity->sound = checkpoint.entity.sound; entity->visible = checkpoint.entity.visible;
  if (!checkpoint.has_last_attack) entity->last_attack = NULL;
  else {
    character->last_attack_storage = qa_q2_restore_attack(checkpoint.last_attack, resolve, resolve_ctx,
        &character->attack_attacker_storage, &character->attack_inflictor_storage);
    entity->last_attack = &character->last_attack_storage;
  }
}

static qa_body_state body_of(qa_q2_character_actor *character) {
  qa_body_state_result body = qa_shared_body_table_read(character->host.bodies, character->actor.id);
  if (!body.has_value) qa_range_error("Q2 character actor has no shared body");
  return body.value;
}

static qa_body_state service_body(void *ctx, qa_owned_actor actor) {
  (void)actor;
  return body_of(ctx);
}
static void service_move(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) {
  (void)entity;
  qa_q2_character_actor *character = ctx;
  qa_body_state next = body_of(character);
  if (changes.has_origin) next.origin = changes.origin;
  if (changes.has_angles) next.angles = changes.angles;
  if (changes.has_velocity) next.velocity = changes.velocity;
  if (changes.has_bounds) next.bounds = changes.bounds;
  qa_actor_id ground = changes.ground;
  if (changes.has_ground) next.ground = changes.ground_null ? NULL : &ground;
  qa_shared_body_table_write(character->host.bodies, character->actor, next);
  if (link) qa_shared_body_table_link(character->host.bodies, character->actor, NULL);
}
static void service_sound(void *ctx, qa_q2_entity *entity, const char *path, double channel, double volume, double attenuation) {
  (void)entity;
  qa_q2_character_actor *character = ctx;
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = character->actor.id;
  ev.origin = body_of(character).origin; ev.path = path;
  ev.channel = channel; ev.volume = volume; ev.attenuation = attenuation; ev.reliable = false; ev.loop = "once";
  character->host.emit(character->host.ctx, ev);
}
static qa_q2_player_powerups character_powerups(void *ctx) {
  qa_q2_character_actor *character = ctx;
  return character->host.powerups(character->host.ctx, character->actor.id);
}
static bool character_weapon_state(void *ctx, qa_q2_character_weapon *out) {
  qa_q2_character_actor *character = ctx;
  return character->host.weapon(character->host.ctx, character->actor.id, out);
}
static void character_environment_damage(void *ctx, double amount, double means, double flags) {
  qa_q2_character_actor *character = ctx;
  character->host.environment_damage(character->host.ctx, character->actor, amount, means, flags);
}

static qa_q2_character_context make_context(qa_q2_character_actor *character) {
  qa_q2_character_context context = {0};
  context.entity = &character->entity;
  context.state = &character->state;
  context.rules = &character->rules;
  context.movement = character->host.movement(character->host.ctx, character->actor.id);
  context.noise_ctx = character->host.ctx;
  context.noise = character->host.noise;
  context.cb_ctx = character;
  context.powerups = character_powerups;
  context.weapon_state = character_weapon_state;
  context.environment_damage = character_environment_damage;
  qa_q2_character_services *game = &context.game;
  game->host_ctx = character->host.ctx;
  game->now = character->host.now;
  game->random = character->host.random;
  game->combat = character->host.combat;
  game->inventory = character->host.inventory;
  game->point_contents = character->host.point_contents;
  game->emit = character->host.emit;
  game->mode = character->options.mode;
  game->deathmatch_flags = character->options.deathmatch_flags;
  game->edition = character->options.has_edition ? character->options.edition : QA_Q2_EDITION_CLASSIC;
  game->ctx = character;
  game->body = service_body;
  game->move = service_move;
  game->sound = service_sound;
  return context;
}

void qa_q2_character_actor_pain(qa_q2_character_actor *character, qa_pain_reaction reaction) {
  (void)character; (void)reaction;
}

void qa_q2_character_actor_record_damage(qa_q2_character_actor *character, qa_damage_decision decision) {
  character->last_attack_storage = decision.request.attack;
  character->entity.last_attack = &character->last_attack_storage;
  if (decision.reaction && strcmp(decision.reaction, "death") == 0) return;
  bool feedback = decision.has_feedback && decision.feedback_kind && strcmp(decision.feedback_kind, "q2") == 0;
  qa_q2_base_player_state *state = &character->state;
  state->damage_blood += feedback ? decision.feedback_blood : decision.applied_damage;
  state->damage_armor += feedback ? decision.feedback_armor : 0;
  state->damage_power_armor += feedback ? decision.feedback_power_armor : 0;
  state->damage_knockback += feedback ? decision.feedback_knockback : decision.request.knockback;
  state->damage_from = decision.request.point;
  if ((feedback ? decision.feedback_power_armor : 0) > 0) state->power_armor_time = character->host.now(character->host.ctx) + 0.2;
}

static void play_sound(qa_q2_character_actor *character, const char *path, double channel) {
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = character->actor.id;
  ev.origin = body_of(character).origin; ev.path = path;
  ev.channel = channel; ev.volume = 1; ev.attenuation = 1; ev.reliable = false; ev.loop = "once";
  character->host.emit(character->host.ctx, ev);
}
static void show(qa_q2_character_actor *character) {
  qa_q2_entity *entity = &character->entity;
  qa_q2_presentation_event ev = {0};
  ev.kind = "model"; ev.has_actor = true; ev.actor = character->actor.id;
  ev.path = entity->model;
  ev.attached_models[0] = entity->model2; ev.attached_models[1] = entity->model3; ev.attached_models[2] = entity->model4;
  ev.frame = entity->frame; ev.old_frame = entity->old_frame; ev.scale = entity->scale; ev.alpha = entity->alpha;
  ev.skin = entity->skin; ev.effects = entity->effects; ev.render_flags = entity->render_flags;
  character->host.emit(character->host.ctx, ev);
}
static qa_vec3 gib_velocity(qa_q2_character_actor *character, double damage) {
  double factor = damage < 50 ? 0.7 : 1.2;
  return (qa_vec3){(character->host.random(character->host.ctx) * 2 - 1) * 100 * factor,
                   (character->host.random(character->host.ctx) * 2 - 1) * 100 * factor,
                   (200 + character->host.random(character->host.ctx) * 100) * factor};
}

void qa_q2_character_actor_die(qa_q2_character_actor *character, qa_death_reaction reaction) {
  qa_q2_character_host *host = &character->host;
  qa_owned_actor actor = character->actor;
  qa_q2_base_player_state *state = &character->state;
  qa_q2_entity *entity = &character->entity;
  qa_body_state body = body_of(character);
  bool first = !state->dead;
  qa_body_state lowered = body;
  lowered.angles = (qa_vec3){0, body.angles.y, 0};
  lowered.bounds.max.z = -8;
  qa_shared_body_table_write(host->bodies, actor, lowered);
  host->motion(host->ctx, actor, "toss", "box");
  if (first) {
    state->dead = true;
    state->respawn_time = host->now(host->ctx) + 1;
    const qa_actor_id *attacker = reaction.pain.attacker && qa_same_actor(*reaction.pain.attacker, actor.id) ? NULL : reaction.pain.attacker;
    qa_body_state_result killer = {0};
    if (attacker == NULL) {
      if (reaction.inflictor && !qa_same_actor(*reaction.inflictor, actor.id))
        killer = qa_shared_body_table_read(host->bodies, *reaction.inflictor);
    } else killer = qa_shared_body_table_read(host->bodies, *attacker);
    state->killer_yaw = !killer.has_value ? body.angles.y
        : fmod(atan2(killer.value.origin.y - body.origin.y, killer.value.origin.x - body.origin.x) * 180 / M_PI + 360, 360);
    host->died(host->ctx, actor, reaction);
  }
  qa_combat_state_result combat = qa_gameplay_authority_read(host->combat, actor.id);
  double health = combat.has_value ? combat.value.health : 0;
  if (health < -40 && !state->gibbed) {
    play_sound(character, "misc/udeath.wav", 4);
    qa_vec3 half = {(body.bounds.max.x - body.bounds.min.x) * 0.5, (body.bounds.max.y - body.bounds.min.y) * 0.5,
                    (body.bounds.max.z - body.bounds.min.z) * 0.5};
    qa_vec3 center = qa_q2_add(body.origin, qa_q2_add(body.bounds.min, half));
    for (int index = 0; index < 4; index++) {
      qa_vec3 origin = qa_q2_add(center, (qa_vec3){(host->random(host->ctx) * 2 - 1) * half.x,
          (host->random(host->ctx) * 2 - 1) * half.y, (host->random(host->ctx) * 2 - 1) * half.z});
      qa_vec3 impulse = gib_velocity(character, reaction.pain.damage);
      qa_vec3 velocity = qa_q2_add(body.velocity, (qa_vec3){impulse.x * 0.5, impulse.y * 0.5, impulse.z * 0.5});
      qa_q2_character_gib gib = {0};
      gib.model = "models/objects/gibs/sm_meat/tris.md2";
      gib.origin = origin;
      gib.velocity = (qa_vec3){fmax(-300, fmin(300, velocity.x)), fmax(-300, fmin(300, velocity.y)), fmax(200, fmin(500, velocity.z))};
      gib.angular_velocity = (qa_vec3){host->random(host->ctx) * 600, host->random(host->ctx) * 600, host->random(host->ctx) * 600};
      gib.expires_at = host->now(host->ctx) + 10 + host->random(host->ctx) * 10;
      host->spawn_gib(host->ctx, gib);
    }
    bool head = host->random(host->ctx) < 0.5;
    entity->model = head ? "models/objects/gibs/head2/tris.md2" : "models/objects/gibs/skull/tris.md2";
    entity->skin = head ? 1 : 0;
    entity->frame = 0;
    entity->effects = 2;
    qa_body_state current = body_of(character);
    qa_vec3 new_velocity = qa_q2_add(current.velocity, gib_velocity(character, reaction.pain.damage));
    current.origin = qa_q2_add(current.origin, (qa_vec3){0, 0, 32});
    current.velocity = new_velocity;
    current.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 16}};
    qa_shared_body_table_write(host->bodies, actor, current);
    host->motion(host->ctx, actor, "bounce", "none");
    qa_combat_trait_changes tc = {0};
    tc.has_can_take_damage = true; tc.can_take_damage = false;
    qa_gameplay_authority_set_traits(host->combat, actor, tc);
    state->gibbed = true;
  } else if (first) {
    character->death_index = fmod(character->death_index + 1, 3);
    state->animation_priority = 5;
    bool ducked = host->movement(host->ctx, actor.id).ducked;
    qa_q2_base_frame_range frames = qa_q2_death_animation_frames(ducked, character->death_index);
    entity->frame = frames.first;
    state->animation_end = frames.last;
    char path[64];
    (void)snprintf(path, sizeof path, "*death%d.wav", (int)floor(host->random(host->ctx) * 4) + 1);
    play_sound(character, path, 2);
  }
  qa_shared_body_table_link(host->bodies, actor, NULL);
  show(character);
}

void qa_q2_character_actor_after_client_think(qa_q2_character_actor *character) {
  double buttons = character->host.movement(character->host.ctx, character->actor.id).buttons;
  qa_q2_base_player_state *state = &character->state;
  state->latched_buttons = (double)((int64_t)state->latched_buttons | ((int64_t)buttons & ~(int64_t)state->buttons));
  state->buttons = buttons;
}

void qa_q2_character_actor_begin_frame(qa_q2_character_actor *character) {
  qa_q2_base_player_state *state = &character->state;
  int64_t mask = character->options.mode == QA_Q2_MODE_DEATHMATCH ? 1 : -1;
  if (state->dead && character->host.now(character->host.ctx) > state->respawn_time &&
      (((int64_t)state->latched_buttons & mask) != 0 ||
       (character->options.mode == QA_Q2_MODE_DEATHMATCH && ((int64_t)character->options.deathmatch_flags & 1024) != 0))) {
    state->latched_buttons = 0;
    character->host.request_respawn(character->host.ctx, character->actor);
    return;
  }
  if (!state->dead) state->latched_buttons = 0;
}

void qa_q2_character_actor_respawned(qa_q2_character_actor *character) {
  qa_q2_base_player_state *state = &character->state;
  double now = character->host.now(character->host.ctx);
  state->dead = false; state->gibbed = false; state->respawn_time = now; state->air_finished = now + 12;
  state->drown_damage = 2; state->old_water_level = 0; state->old_velocity = QA_Q2_ZERO;
  state->damage_alpha = 0; state->bonus_alpha = 0; state->fall_time = 0; state->damage_time = 0;
  state->damage_blood = 0; state->damage_armor = 0; state->damage_power_armor = 0; state->damage_knockback = 0;
  state->animation_priority = 0; state->animation_end = 39;
  character->entity.frame = 0;
  character->entity.model = character->options.model;
  character->entity.skin = character->options.skin;
  character->entity.effects = 0;
  character->entity.render_flags = character->options.has_edition && character->options.edition == QA_Q2_EDITION_RERELEASE ? 32768 : 0;
  character->entity.view_height = 22;
  state->event = "q2:player-teleport";
  show(character);
}

void qa_q2_character_actor_set_animation(qa_q2_character_actor *character, const char *priority, double first, double last) {
  character->state.animation_priority = strcmp(priority, "attack") == 0 ? 4 : strcmp(priority, "pain") == 0 ? 3 : 6;
  character->entity.frame = first;
  character->state.animation_end = last;
}

void qa_q2_character_actor_end_frame(qa_q2_character_actor *character, bool intermission) {
  qa_q2_character_context context = make_context(character);
  qa_q2_base_player_state *state = &character->state;
  qa_q2_character_host *host = &character->host;
  qa_owned_actor actor = character->actor;
  if (intermission) { host->view(host->ctx, actor.id, qa_q2_build_view(&context, 0, true)); return; }
  character->entity.view_height = state->gibbed ? 8 : state->dead || context.movement.ducked ? -2 : 22;
  if (character->options.environment) qa_q2_world_effects(&context);
  qa_body_state body = body_of(character);
  qa_angle_vectors vectors = qa_math_angle_vectors_rule(context.movement.view_angles, QA_ANGLE_RULE_NATIVE);
  double side = qa_q2_dot(body.velocity, vectors.right);
  double roll = (side < 0 ? -1 : 1) * fmin(fabs(side) * character->rules.roll_angle / character->rules.roll_speed, character->rules.roll_angle);
  qa_body_state angled = body;
  angled.angles = (qa_vec3){(context.movement.view_angles.x > 180 ? context.movement.view_angles.x - 360 : context.movement.view_angles.x) / 3,
                            context.movement.view_angles.y, roll * 4};
  qa_shared_body_table_write(host->bodies, actor, angled);
  double speed = hypot(body.velocity.x, body.velocity.y);
  if (speed < 5) { state->bob_move = 0; state->bob_time = 0; }
  else if (context.movement.grounded) state->bob_move = speed > 210 ? 0.25 : speed > 100 ? 0.125 : 0.0625;
  state->bob_time += state->bob_move;
  if (character->options.environment) qa_q2_falling_damage(&context);
  qa_q2_damage_feedback_result feedback = qa_q2_damage_feedback(&context, character->pain_index);
  character->pain_index = feedback.pain_index;
  qa_q2_base_player_view view = qa_q2_build_view(&context, feedback.flashes, false);
  host->view(host->ctx, actor.id, view);
  double cycle = trunc(context.movement.ducked ? state->bob_time * 4 : state->bob_time);
  if (state->event[0] == '\0' && context.movement.grounded && speed > 225 && trunc(state->bob_time + state->bob_move) != cycle)
    state->event = "q2:footstep";
  if (state->event[0] != '\0') {
    double event = strcmp(state->event, "q2:footstep") == 0 ? 2 : strcmp(state->event, "q2:fall-short") == 0 ? 3
        : strcmp(state->event, "q2:fall") == 0 ? 4 : strcmp(state->event, "q2:fall-far") == 0 ? 5
        : strcmp(state->event, "q2:player-teleport") == 0 ? 6 : -1;
    if (event != -1) {
      qa_q2_presentation_event ev = {0};
      ev.kind = "entity-event"; ev.has_actor = true; ev.actor = actor.id; ev.event = event;
      host->emit(host->ctx, ev);
    } else {
      qa_q2_presentation_event ev = {0};
      ev.kind = "effect"; ev.effect = state->event; ev.origin = body_of(character).origin;
      ev.direction = QA_Q2_ZERO; ev.count = 1; ev.color = 0;
      host->emit(host->ctx, ev);
    }
    state->event = "";
  }
  qa_q2_client_effects(&context);
  qa_q2_client_animation(&context);
  state->old_velocity = body_of(character).velocity;
  state->old_view_angles = view.angles;
  show(character);
}
