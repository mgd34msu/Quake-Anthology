/* Port of src/content/q1/addons/effects.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/effects.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

static double fr(double value) { return (double)(float)value; }
static void fx_prefix(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:fx:", qa_q1ad_program_name(context->program));
}
static void fog_prefix(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:fog:", qa_q1ad_program_name(context->program));
}
static double random_signed_f(qa_q1ad_context *context) { return fr(qa_q1ad_random(context) * 2 - 1); }
static void schedule_fx(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, double delay) {
  char prefix[QA_Q1AD_NAME];
  fx_prefix(context, prefix);
  qa_q1ad_schedule_named(context, entity, delay, prefix, name);
}
static void particles(qa_q1ad_context *context, qa_vec3 origin, qa_vec3 direction, double color, double count) {
  qa_q1ad_host_event event = {.kind = "particles", .origin = origin, .direction = direction,
                              .color = color, .count = count};
  qa_q1ad_host_emit(context, &event);
}

static void shake_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.intermission) return;
  double end = qa_q1ad_actor_number(entity, "storednextthink");
  double start = end - entity->wait;
  qa_actor_id players[32];
  size_t count = qa_q1ad_players(context, players, 32);
  if (context->game.time > end) {
    if (((int32_t)entity->spawnflags & 1) == 0)
      qa_q1ad_sound_entity(context, entity, qa_q1ad_actor_text(entity, "noise1"), NULL);
    for (size_t i = 0; i < count; i++) {
      qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_VIEW_ROLL, .player = players[i],
                             .has_player = true, .roll = 0};
      qa_q1ad_emit(context, &event);
    }
    return;
  }
  double intensity = fr(entity->damage * (context->game.time < entity->delay
      ? (context->game.time - start) / (entity->wait / 3) : 1));
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_PUNCH_ANGLE, .player = players[i],
        .has_player = true,
        .angles = {fr(qa_q1ad_random(context) * intensity),
                   fr(random_signed_f(context) * intensity),
                   fr(qa_q1ad_random(context) * intensity)}};
    qa_q1ad_emit(context, &event);
  }
  schedule_fx(context, entity, "shake", 0.05);
}
static void shake_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_set_number(context, entity, "storednextthink", context->game.time + entity->wait);
  entity->delay = fr(context->game.time + entity->wait / 3);
  if (((int32_t)entity->spawnflags & 1) == 0)
    qa_q1ad_sound_entity(context, entity, qa_q1ad_actor_text(entity, "noise"), NULL);
  schedule_fx(context, entity, "shake", 0.05);
}
static void spawn_shake(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->wait == 0) entity->wait = 2;
  if (entity->damage == 0) entity->damage = 3;
  qa_q1ad_actor_set(entity, "noise", "misc/quake.wav");
  qa_q1ad_actor_set(entity, "noise1", "misc/quakeend.wav");
  char prefix[QA_Q1AD_NAME];
  fx_prefix(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "shake");
}
static void sound_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  if (qa_q1ad_actor_text(entity, "noise")[0] != '\0')
    qa_q1ad_sound_entity(context, entity, qa_q1ad_actor_text(entity, "noise"), NULL);
}
static void spawn_sound(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_text(entity, "noise")[0] == '\0') { qa_q1ad_remove(context, entity); return; }
  char prefix[QA_Q1AD_NAME];
  fx_prefix(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "sound");
}
static bool trace_point(qa_q1ad_context *context, qa_vec3 start, qa_vec3 end,
                        const qa_actor_id *ignore, bool monsters, qa_q1ad_trace *out) {
  *out = (qa_q1ad_trace){.fraction = 1, .end = end};
  return context->game.trace != NULL && context->game.trace(context->game.ctx, start, end,
      (qa_bounds){{0, 0, 0}, {0, 0, 0}}, ignore, monsters, out);
}
static void damage_lightning(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_vec3 start, qa_vec3 end) {
  /* QC discards normalize's return, then assigns y from the already changed x. */
  qa_vec3 difference = qa_sub3(end, start);
  qa_vec3 side = {fr(-difference.y * 16), fr(-difference.y * 16), 0};
  qa_actor_id hits[8];
  size_t hit_count = 0;
  qa_vec3 offsets[3] = {{0, 0, 0}, side, qa_scale3(side, -1)};
  qa_actor_id self = entity->actor.id;
  for (int i = 0; i < 3; i++) {
    qa_q1ad_trace trace;
    (void)trace_point(context, qa_add3(start, offsets[i]), qa_add3(end, offsets[i]), &self, true, &trace);
    if (!trace.has_actor) continue;
    bool seen = false;
    for (size_t j = 0; j < hit_count; j++) if (qa_same_actor(hits[j], trace.actor)) seen = true;
    if (seen) continue;
    if (hit_count < 8) hits[hit_count++] = trace.actor;
    bool can_take = false;
    bool known = context->game.combat_can_take_damage != NULL &&
        context->game.combat_can_take_damage(context->game.ctx, trace.actor, &can_take);
    if (!known || !can_take) continue;
    particles(context, trace.end, (qa_vec3){0, 0, 100}, 225, entity->damage * 4);
    qa_q1ad_damage(context, trace.actor, &self, &self, entity->damage, NULL, NULL, NULL);
  }
}
static void lightning_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_actor *targets[64];
  size_t count = qa_q1ad_find(context, entity->target != NULL ? entity->target : "", targets, 64);
  int64_t chosen = ((int32_t)entity->spawnflags & 1) != 0
      ? (int64_t)floor((double)count * qa_q1ad_random(context)) : -1;
  qa_actor_id self = entity->actor.id;
  for (size_t i = 0; i < count; i++) {
    if (chosen >= 0 && chosen != (int64_t)i) continue;
    qa_q1ad_actor *target = targets[i];
    bool reverse = ((int32_t)entity->spawnflags & 2) != 0;
    qa_vec3 start = qa_q1ad_body_of(context, reverse ? target : entity).origin;
    qa_q1ad_trace trace;
    (void)trace_point(context, start, qa_q1ad_body_of(context, reverse ? entity : target).origin,
                      &self, false, &trace);
    if (context->program != QA_Q1AD_MG3 || ((int32_t)entity->spawnflags & 16) == 0) {
      qa_q1ad_host_event event = {.kind = "sound", .actor = target->actor.id, .has_actor = true,
          .path = qa_q1ad_actor_text(entity, "noise"), .channel = "auto",
          .volume = qa_q1ad_actor_number(entity, "volume"), .attenuation = 1};
      qa_q1ad_host_emit(context, &event);
    }
    double style = qa_q1ad_actor_number(entity, "style");
    qa_q1ad_event beam = {.kind = QA_Q1AD_EVENT_LIGHTNING, .actor = target->actor.id,
        .has_actor = true, .start = start, .end = trace.end,
        .style = style == 1 ? 1 : style == 2 ? 2 : 3};
    qa_q1ad_emit(context, &beam);
    if (entity->damage != 0) damage_lightning(context, entity, start, trace.end);
    if (((int32_t)entity->spawnflags & 4) != 0 ||
        target->target == NULL || target->target[0] == '\0') continue;
    qa_q1ad_use_targets(context, target, activator);
    if (((int32_t)entity->spawnflags & 8) == 0 && qa_q1ad_live(context, target)) {
      double delay = target->delay;
      target->delay = fr(delay + 0.2);
      qa_q1ad_use_targets(context, target, activator);
      target->delay = delay;
    }
  }
}
static void spawn_lightning(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_text(entity, "noise")[0] == '\0') qa_q1ad_actor_set(entity, "noise", "misc/power.wav");
  if (qa_q1ad_actor_number(entity, "volume") == 0) qa_q1ad_set_number(context, entity, "volume", 1);
  if (context->program == QA_Q1AD_MG3 && ((int32_t)entity->spawnflags & 16) != 0)
    qa_q1ad_set_number(context, entity, "volume", 0);
  char prefix[QA_Q1AD_NAME];
  fx_prefix(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "lightning");
}
static void fade_targets(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_actor *targets[64];
  size_t count = qa_q1ad_find(context, entity->target != NULL ? entity->target : "", targets, 64);
  int remaining = 0;
  if (!entity->activated) {
    for (size_t i = 0; i < count; i++) qa_q1ad_alpha(context, targets[i], 1);
    entity->activated = true;
  }
  for (size_t i = 0; i < count; i++)
    if (qa_q1ad_health(context, targets[i]->actor.id) <= 0) {
      if (qa_q1ad_actor_number(targets[i], "alpha") > 0) {
        qa_q1ad_alpha(context, targets[i],
            fr(qa_q1ad_actor_number(targets[i], "alpha") - context->frame_time / entity->delay));
        remaining++;
      } else qa_q1ad_remove(context, targets[i]);
    }
  if (remaining > 0) schedule_fx(context, entity, "fade_targets", 0);
  else qa_q1ad_remove(context, entity);
}
static void fade_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  if (context->game.create == NULL) return;
  qa_q1ad_actor *manager = context->game.create(context->game.ctx, "fade_manager");
  if (manager == NULL) return;
  manager->target = entity->target;
  manager->delay = entity->delay;
  schedule_fx(context, manager, "fade_targets", 0);
}
static void spawn_fade(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->delay == 0) entity->delay = 1;
  char prefix[QA_Q1AD_NAME];
  fx_prefix(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "fade");
}
static void freeze_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_actor *targets[64];
  size_t count = qa_q1ad_find(context, entity->target != NULL ? entity->target : "", targets, 64);
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_actor *target = targets[i];
    if (qa_q1ad_actor_number(target, "is_frozen") == 0) {
      qa_q1ad_set_number(context, target, "storednextthink", target->next_think);
      qa_q1ad_actor_set(target, "addon.storedthink", target->think);
      qa_q1ad_set_number(context, target, "addon.frozenDamageable", target->damageable ? 1 : 0);
      target->damageable = false;
      qa_q1ad_cancel(context, target);
      qa_q1ad_bind(target->think, "SUB_Null");
      qa_q1ad_set_number(context, target, "is_frozen", 1);
    } else {
      const char *name = qa_q1ad_actor_text(target, "addon.storedthink");
      double due = qa_q1ad_actor_number(target, "storednextthink");
      if (name[0] == '\0') target->think[0] = '\0';
      else qa_q1ad_bind(target->think, name);
      if (target->think[0] != '\0' && due >= 0)
        qa_q1ad_schedule(context, target, due - context->game.time, target->think);
      qa_q1ad_set_number(context, target, "storednextthink", -1);
      qa_q1ad_set_number(context, target, "is_frozen", 0);
      target->damageable = qa_q1ad_actor_number(target, "addon.frozenDamageable") != 0;
    }
  }
}
static void spawn_freeze(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  char prefix[QA_Q1AD_NAME];
  fx_prefix(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "freeze");
}
static void embers_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_vec3 size = qa_q1ad_actor_vector(entity, "size");
  double up = fr(qa_q1ad_random(context) * 2 + 2);
  qa_vec3 direction = {fr(random_signed_f(context) * body.velocity.x),
                       fr(random_signed_f(context) * body.velocity.y),
                       fr(up * body.velocity.z)};
  qa_vec3 origin = {body.origin.x + size.x * random_signed_f(context),
                    body.origin.y + size.y * random_signed_f(context), body.origin.z};
  particles(context, origin, direction, 234, 2);
  schedule_fx(context, entity, "embers", entity->wait + entity->delay * qa_q1ad_random(context));
}
static void spawn_embers(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool tall = entity->classname != NULL && strcmp(entity->classname, "particle_embers_tall") == 0;
  if (qa_length3(qa_q1ad_actor_vector(entity, "size")) == 0)
    qa_q1ad_set_vector(context, entity, "size", tall ? (qa_vec3){40, 40, 0} : (qa_vec3){128, 128, 0});
  if (entity->wait == 0) entity->wait = 0.05;
  if (entity->delay == 0) entity->delay = 0.1;
  if (qa_length3(qa_q1ad_body_of(context, entity).velocity) == 0) {
    qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = {1, 1, tall ? 2 : 1}};
    qa_q1ad_set_body(context, entity, &patch);
  }
  schedule_fx(context, entity, "embers", entity->wait + entity->delay * qa_q1ad_random(context));
}
static void tele_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_vec3 direction = qa_normalize3_or_zero((qa_vec3){
      random_signed_f(context) * 10, random_signed_f(context) * 10, random_signed_f(context) * 5});
  double distance = context->program == QA_Q1AD_MG3 ? qa_q1ad_actor_number(entity, "distance") : 64;
  particles(context, qa_add3(qa_q1ad_body_of(context, entity).origin, qa_scale3(direction, distance)),
            qa_scale3(direction, distance * -0.125), 3, 3);
  schedule_fx(context, entity, "tele", entity->wait + entity->delay * qa_q1ad_random(context));
}
static void spawn_tele(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->delay == 0) entity->delay = 0.1;
  if (qa_q1ad_actor_number(entity, "distance") == 0) qa_q1ad_set_number(context, entity, "distance", 64);
  schedule_fx(context, entity, "tele", entity->wait + entity->delay * qa_q1ad_random(context));
}
static void fountain_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  particles(context, body.origin, (qa_vec3){random_signed_f(context) * body.velocity.x,
      random_signed_f(context) * body.velocity.y, body.velocity.z}, 13, 2);
  schedule_fx(context, entity, "fountain", entity->wait + entity->delay * qa_q1ad_random(context));
}
static void fountain_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  schedule_fx(context, entity, "fountain", 0.1);
}
static void spawn_fountain(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->wait == 0) entity->wait = 0.05;
  if (entity->delay == 0) entity->delay = 0.1;
  if (qa_length3(qa_q1ad_body_of(context, entity).velocity) == 0) {
    qa_q1ad_body_patch patch = {.has_velocity = true, .velocity = {1, 1, 6}};
    qa_q1ad_set_body(context, entity, &patch);
  }
  if (((int32_t)entity->spawnflags & 1) != 0) {
    char prefix[QA_Q1AD_NAME];
    fx_prefix(context, prefix);
    qa_q1ad_bind_prefix(entity->use, prefix, "fountain");
    return;
  }
  schedule_fx(context, entity, "fountain", entity->wait + entity->delay * qa_q1ad_random(context));
}

void qa_q1ad_register_effects(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  fx_prefix(context, prefix);
#define REG(name, handlers) do { qa_q1ad_named_name(full, prefix, name); \
  qa_q1ad_register_named(context, full, handlers); } while (0)
  REG("shake", ((qa_q1ad_handlers){.action = shake_action, .use = shake_use}));
  REG("sound", ((qa_q1ad_handlers){.use = sound_use}));
  REG("lightning", ((qa_q1ad_handlers){.use = lightning_use}));
  REG("fade_targets", ((qa_q1ad_handlers){.action = fade_targets}));
  REG("fade", ((qa_q1ad_handlers){.use = fade_use}));
  REG("freeze", ((qa_q1ad_handlers){.use = freeze_use}));
  REG("embers", ((qa_q1ad_handlers){.action = embers_action}));
  REG("tele", ((qa_q1ad_handlers){.action = tele_action}));
  REG("fountain", ((qa_q1ad_handlers){.action = fountain_action, .use = fountain_use}));
#undef REG
  qa_q1ad_register_spawn(context, "trigger_screenshake", false, spawn_shake);
  qa_q1ad_register_spawn(context, "trigger_sound", false, spawn_sound);
  qa_q1ad_register_spawn(context, "trigger_lightning", false, spawn_lightning);
  qa_q1ad_register_spawn(context, "trigger_fade", false, spawn_fade);
  qa_q1ad_register_spawn(context, "trigger_freeze", false, spawn_freeze);
  qa_q1ad_register_spawn(context, "particle_embers", false, spawn_embers);
  qa_q1ad_register_spawn(context, "particle_embers_tall", false, spawn_embers);
  qa_q1ad_register_spawn(context, "particle_tele", false, spawn_tele);
  qa_q1ad_register_spawn(context, "particle_tele_fountain", false, spawn_fountain);
}

/* --- fog (registerAddonFog) --- */
static qa_q1ad_actor *fog_info(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *field) {
  const char *name = qa_q1ad_actor_text(entity, field);
  if (name[0] == '\0') return NULL;
  qa_q1ad_actor *found[4];
  size_t count = qa_q1ad_find(context, name, found, 4);
  if (count == 0) return NULL;
  return found[0]->classname != NULL && strcmp(found[0]->classname, "info_fog") == 0 ? found[0] : NULL;
}
static void set_fog(qa_q1ad_context *context, qa_actor_id player, double density, qa_vec3 color, double duration) {
  qa_q1ad_set_player_number(context, player, "fog_density", density);
  qa_q1ad_set_player_number(context, player, "fog_color_x", color.x);
  qa_q1ad_set_player_number(context, player, "fog_color_y", color.y);
  qa_q1ad_set_player_number(context, player, "fog_color_z", color.z);
  qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_FOG, .player = player, .has_player = true,
      .density = density, .color_vec = color, .duration = duration, .sky_factor = 0};
  qa_q1ad_emit(context, &event);
}
static void fog_activate_entity(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *player) {
  if (!qa_q1ad_is_player(context, player)) return;
  qa_actor_id previous;
  if (qa_q1ad_player_reference(context, *player, "fog.active", &previous) &&
      qa_same_actor(previous, entity->actor.id)) return;
  qa_q1ad_set_player_reference(context, *player, "fog.active", &entity->actor.id);
  qa_q1ad_actor *source = fog_info(context, entity, "fog_info_entity");
  if (source == NULL) return;
  double density = qa_q1ad_actor_number(source, "fog_density");
  qa_vec3 color = qa_q1ad_actor_vector(source, "fog_color");
  if (density != 0 || qa_length3(color) != 0 || ((int32_t)source->spawnflags & 1) != 0)
    set_fog(context, *player, density, color, entity->delay);
}
static void fog_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  fog_activate_entity(context, entity, other);
}
static void fog_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                    const qa_actor_id *other, const qa_actor_id *activator) {
  (void)activator;
  fog_activate_entity(context, entity, other);
}
static void spawn_info_fog(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_number(entity, "fog_density") == 0)
    qa_q1ad_set_number(context, entity, "fog_density", 0.05);
  else {
    qa_vec3 color = qa_q1ad_actor_vector(entity, "fog_color");
    if (color.x > 1 || color.y > 1 || color.z > 1)
      qa_q1ad_set_vector(context, entity, "fog_color", qa_scale3(color, 1.0 / 255));
  }
}
static void spawn_fog(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.coop || context->game.deathmatch != 0) { qa_q1ad_remove(context, entity); return; }
  if (entity->delay == 0) entity->delay = 0.5;
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  fog_prefix(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "activate");
  if (((int32_t)entity->spawnflags & 1) == 0) qa_q1ad_bind_prefix(entity->touch, prefix, "activate");
}
static void fog_transition(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *player, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, player)) return;
  qa_q1ad_body body;
  if (!qa_q1ad_body_by_id(context, *player, &body)) return;
  qa_q1ad_body entity_body = qa_q1ad_body_of(context, entity);
  qa_vec3 size = qa_sub3(entity_body.bounds.max, entity_body.bounds.min);
  qa_vec3 position = qa_sub3(body.origin, entity_body.bounds.min);
  double axis = qa_q1ad_actor_number(entity, "style");
  double raw = axis == 0 ? position.x / size.x : axis == 1 ? position.y / size.y : position.z / size.z;
  double tween = raw < 0 ? 0 : raw > 1 ? 1 : raw;
  qa_q1ad_actor *first = fog_info(context, entity, "fog_info_entity");
  qa_q1ad_actor *second = fog_info(context, entity, "target");
  double density = (1 - tween) * (first != NULL ? qa_q1ad_actor_number(first, "fog_density") : 0)
      + tween * (second != NULL ? qa_q1ad_actor_number(second, "fog_density") : 0);
  qa_vec3 color = qa_add3(
      qa_scale3(first != NULL ? qa_q1ad_actor_vector(first, "fog_color") : (qa_vec3){0, 0, 0}, 1 - tween),
      qa_scale3(second != NULL ? qa_q1ad_actor_vector(second, "fog_color") : (qa_vec3){0, 0, 0}, tween));
  set_fog(context, *player, density, color, 0);
}
static void spawn_fog_transition(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->game.coop || context->game.deathmatch != 0) { qa_q1ad_remove(context, entity); return; }
  double axis = qa_q1ad_actor_number(entity, "style");
  if (axis != 0 && axis != 1 && axis != 2) qa_range_error("Invalid style for trigger_fog_transition");
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  fog_prefix(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "transition");
}

void qa_q1ad_register_fog(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  fog_prefix(context, prefix);
  qa_q1ad_named_name(full, prefix, "activate");
  qa_q1ad_register_named(context, full, (qa_q1ad_handlers){.touch = fog_touch, .use = fog_use});
  qa_q1ad_named_name(full, prefix, "transition");
  qa_q1ad_register_named(context, full, (qa_q1ad_handlers){.touch = fog_transition});
  qa_q1ad_register_spawn(context, "info_fog", false, spawn_info_fog);
  qa_q1ad_register_spawn(context, "trigger_fog", false, spawn_fog);
  qa_q1ad_register_spawn(context, "trigger_fog_transition", false, spawn_fog_transition);
}

