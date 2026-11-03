/* Port of src/content/q1/addons/effects.ts (quakec_mg1/misc_fx.qc, fog.qc and
 * quakec_mg3 variants). Copyright (C) 1996-2026 id Software LLC.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/effects.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

/* `${program}:fx:` / `${program}:fog:` names: static literals, since slots borrow the name. */
#define FX(context, name) \
  ((const char *const[]){"dopa:fx:" name, "mg1:fx:" name, "mg3:fx:" name, "ctf:fx:" name}[(context)->program])
#define FOG(context, name) \
  ((const char *const[]){"dopa:fog:" name, "mg1:fog:" name, "mg3:fog:" name, "ctf:fog:" name}[(context)->program])

static double fr(double value) { return (double)(float)value; }
static bool falsy(double value) { return value == 0 || isnan(value); }
static int32_t spawnflags(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }
/* TS random = () => Math.fround(game.host.random() * 2 - 1). */
static double signed_random(qa_q1_services *game) { return fr(qa_q1_host_random(game) * 2 - 1); }
static void emit_addon(qa_q1ad_context *context, qa_q1ad_event event) {
  context->services.emit(context->services.ctx, &event);
}
static void schedule(qa_q1_services *game, qa_q1_actor *entity, const char *name, double delay) {
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, name));
}
static void particles(qa_q1_services *game, qa_vec3 origin, qa_vec3 direction, double color, double count) {
  qa_q1_event event = {.kind = "particles", .has_origin = true, .origin = origin, .direction = direction,
                       .color = color, .count = count};
  qa_q1_emit(game, event);
}
static void set_velocity(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 velocity) {
  qa_q1_body_patch patch = {.has_velocity = true, .velocity = velocity};
  qa_q1_set_body(game, entity, &patch);
}
/* spawnUse(classname, name, initialize): the removedOutsideCoop gate and the use binding. */
static bool spawn_use(qa_q1ad_context *context, qa_q1_actor *entity, const char *name) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return false;
  entity->use = qa_q1_callback_registry_use(context->game->named, entity, name);
  return true;
}

static void shake_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (game->has_intermission) return;
  double end = qa_q1_actor_number(entity, "storednextthink"), start = end - entity->wait;
  size_t count = 0;
  if (game->time > end) {
    if ((spawnflags(entity) & 1) == 0) qa_q1_sound_defaults(game, entity->actor.id, qa_q1_actor_text(entity, "noise1"));
    const qa_actor_id *players = game->host.players(game->host.ctx, &count);
    for (size_t i = 0; i < count; i++)
      emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_VIEW_ROLL, .has_player = true, .player = players[i], .roll = 0});
    return;
  }
  double intensity = fr(entity->damage * (game->time < entity->delay ? (game->time - start) / (entity->wait / 3) : 1));
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_vec3 angles;
    angles.x = fr(qa_q1_host_random(game) * intensity);
    angles.y = fr(signed_random(game) * intensity);
    angles.z = fr(qa_q1_host_random(game) * intensity);
    emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_PUNCH_ANGLE, .has_player = true, .player = players[i], .angles = angles});
  }
  schedule(game, entity, FX(context, "shake"), 0.05);
}
static void shake_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  qa_q1ad_set_number(context, entity, "storednextthink", game->time + entity->wait);
  entity->delay = fr(game->time + entity->wait / 3);
  if ((spawnflags(entity) & 1) == 0) qa_q1_sound_defaults(game, entity->actor.id, qa_q1_actor_text(entity, "noise"));
  schedule(game, entity, FX(context, "shake"), 0.05);
}
static void spawn_shake(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (falsy(entity->wait)) entity->wait = 2;
  if (falsy(entity->damage)) entity->damage = 3;
  (void)qa_q1_actor_field_set(entity, "noise", "misc/quake.wav");
  (void)qa_q1_actor_field_set(entity, "noise1", "misc/quakeend.wav");
  (void)spawn_use(context, entity, FX(context, "shake"));
}
static void sound_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  if (qa_q1_actor_text(entity, "noise")[0] != '\0') qa_q1_sound_defaults(game, entity->actor.id, qa_q1_actor_text(entity, "noise"));
}
static void spawn_sound(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1_actor_text(entity, "noise")[0] == '\0') { qa_q1_remove(game, entity); return; }
  entity->use = qa_q1_callback_registry_use(game->named, entity, FX(context, "sound"));
}

static qa_q1_trace point_trace(qa_q1_services *game, qa_vec3 start, qa_vec3 end, qa_actor_id ignore, bool monsters) {
  return qa_q1_host_trace(game, (qa_q1_trace_request){.start = start, .end = end, .bounds = QA_Q1_POINT,
                                                      .has_ignore = true, .ignore = ignore, .monsters = monsters});
}
static void damage_lightning(qa_q1ad_context *context, qa_q1_actor *entity, qa_vec3 start, qa_vec3 end) {
  qa_q1_services *game = context->game;
  /* QC discards normalize's return, then assigns y from the already changed x. */
  qa_vec3 difference = qa_q1_vsub(end, start), side = {fr(-difference.y * 16), fr(-difference.y * 16), 0};
  qa_actor_id hits[3];
  size_t hit_count = 0;
  const qa_vec3 offsets[3] = {QA_Q1_ZERO, side, qa_q1_vscale(side, -1)};
  qa_actor_id self = entity->actor.id;
  for (size_t i = 0; i < 3; i++) {
    qa_q1_trace trace = point_trace(game, qa_q1_vadd(start, offsets[i]), qa_q1_vadd(end, offsets[i]), self, true);
    if (!trace.has_actor) continue;
    bool seen = false;
    for (size_t j = 0; j < hit_count; j++) seen = seen || qa_same_actor(hits[j], trace.actor);
    if (seen) continue;
    hits[hit_count++] = trace.actor;
    qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, trace.actor);
    if (!combat.has_value || !combat.value.can_take_damage) continue;
    particles(game, trace.end, (qa_vec3){0, 0, 100}, context->program == QA_Q1AD_MG3 ? 246 : 225, entity->damage * 4);
    (void)qa_q1_damage(game, trace.actor, self, &self, entity->damage, NULL, NULL, NULL, NULL);
  }
}
static void lightning_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                          const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_context *context = ctx;
  qa_q1_actor *targets[256];
  size_t count = qa_q1_find(game, entity->target, targets, 256);
  double chosen = (spawnflags(entity) & 1) != 0 ? floor((double)count * qa_q1_host_random(game)) : -1;
  for (size_t index = 0; index < count; index++) {
    if (chosen >= 0 && chosen != (double)index) continue;
    qa_q1_actor *target = targets[index];
    bool reverse = (spawnflags(entity) & 2) != 0;
    qa_vec3 start = qa_q1_get_body(game, reverse ? target : entity).origin;
    qa_q1_trace trace = point_trace(game, start, qa_q1_get_body(game, reverse ? entity : target).origin, entity->actor.id, false);
    if (context->program != QA_Q1AD_MG3 || (spawnflags(entity) & 16) == 0) {
      qa_actor_id value = target->actor.id;
      const char * value2 = qa_q1_actor_text(entity, "noise");
      double value3 = qa_q1_actor_number(entity, "volume");
      qa_q1_event sound = {.kind = "sound", .has_actor = true, .actor = value, .path = value2,
                           .sound_channel = "auto", .volume = value3, .attenuation = 1};
      qa_q1_emit(game, sound);
    }
    double style = qa_q1_actor_number(entity, "style");
    emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_LIGHTNING, .has_actor = true, .actor = target->actor.id,
                                        .start = start, .end = trace.end, .style = style == 1 ? 1 : style == 2 ? 2 : 3});
    if (entity->damage != 0) damage_lightning(context, entity, start, trace.end);
    if ((spawnflags(entity) & 4) != 0 || target->target[0] == '\0') continue;
    qa_q1_use_targets(game, target, activator);
    if ((spawnflags(entity) & 8) == 0 && qa_q1_live(game, target)) {
      double delay = target->delay;
      target->delay = fr(delay + 0.2);
      qa_q1_use_targets(game, target, activator);
      target->delay = delay;
    }
  }
}
static void spawn_lightning(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1_actor_text(entity, "noise")[0] == '\0') (void)qa_q1_actor_field_set(entity, "noise", "misc/power.wav");
  if (qa_q1_actor_number(entity, "volume") == 0) qa_q1ad_set_number(context, entity, "volume", 1);
  if (context->program == QA_Q1AD_MG3 && (spawnflags(entity) & 16) != 0) qa_q1ad_set_number(context, entity, "volume", 0);
  entity->use = qa_q1_callback_registry_use(game->named, entity, FX(context, "lightning"));
}
static void fade_targets(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1_actor *targets[256];
  size_t count = qa_q1_find(game, entity->target, targets, 256);
  int remaining = 0;
  if (!entity->activated) {
    for (size_t i = 0; i < count; i++) qa_q1ad_alpha(context, targets[i], 1);
    entity->activated = true;
  }
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *target = targets[i];
    if (qa_q1_health(game, target->actor.id) > 0) continue;
    if (qa_q1_actor_number(target, "alpha") > 0) {
      qa_q1ad_alpha(context, target, fr(qa_q1_actor_number(target, "alpha") - context->frame_time / entity->delay));
      remaining++;
    } else {
      qa_q1_remove(game, target);
    }
  }
  if (remaining > 0) schedule(game, entity, FX(context, "fade_targets"), 0);
  else qa_q1_remove(game, entity);
}
static void fade_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  qa_q1_actor *manager = qa_q1_create(game, "fade_manager", NULL, NULL);
  qa_q1ad_set_text(&manager->target, entity->target);
  manager->delay = entity->delay;
  schedule(game, manager, FX(context, "fade_targets"), 0);
}
static void spawn_fade(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (falsy(entity->delay)) entity->delay = 1;
  (void)spawn_use(context, entity, FX(context, "fade"));
}
static void freeze_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                       const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_context *context = ctx;
  qa_q1_actor *targets[256];
  size_t count = qa_q1_find(game, entity->target, targets, 256);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *target = targets[i];
    if (qa_q1_actor_number(target, "is_frozen") == 0) {
      qa_q1ad_set_number(context, target, "storednextthink", target->next_think);
      const char *stored = qa_q1_action_name(target->think);
      (void)qa_q1_actor_field_set(target, "addon.storedthink", stored != NULL ? stored : "");
      qa_q1ad_set_number(context, target, "addon.frozenDamageable", qa_q1_actor_damageable(target) ? 1 : 0);
      qa_q1_actor_set_damageable(target, false);
      qa_q1_cancel(game, target);
      target->think = qa_q1_callback_registry_action(game->named, target, "SUB_Null");
      qa_q1ad_set_number(context, target, "is_frozen", 1);
    } else {
      const char *name = qa_q1_actor_text(target, "addon.storedthink");
      double due = qa_q1_actor_number(target, "storednextthink");
      target->think = name[0] == '\0' ? (qa_q1_bound_action){0} : qa_q1_callback_registry_action(game->named, target, name);
      if (target->think.fn != NULL && due >= 0) qa_q1_schedule(game, target, due - game->time, target->think);
      qa_q1ad_set_number(context, target, "storednextthink", -1);
      qa_q1ad_set_number(context, target, "is_frozen", 0);
      qa_q1_actor_set_damageable(target, qa_q1_actor_number(target, "addon.frozenDamageable") != 0);
    }
  }
}
static void spawn_freeze(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  (void)spawn_use(context, entity, FX(context, "freeze"));
}

static void embers_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_vec3 velocity = qa_q1_get_body(game, entity).velocity, size = qa_q1_actor_vector(entity, "size");
  qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  double up = fr(qa_q1_host_random(game) * 2 + 2);
  qa_vec3 direction;
  direction.x = fr(signed_random(game) * velocity.x);
  direction.y = fr(signed_random(game) * velocity.y);
  direction.z = fr(up * velocity.z);
  qa_vec3 at;
  at.x = origin.x + size.x * signed_random(game);
  at.y = origin.y + size.y * signed_random(game);
  at.z = origin.z;
  particles(game, at, direction, 234, 2);
  schedule(game, entity, FX(context, "embers"), entity->wait + entity->delay * qa_q1_host_random(game));
}
static void spawn_embers(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  bool tall = strcmp(entity->classname, "particle_embers_tall") == 0;
  if (qa_q1_length(qa_q1_actor_vector(entity, "size")) == 0)
    qa_q1ad_set_vector(context, entity, "size", tall ? (qa_vec3){40, 40, 0} : (qa_vec3){128, 128, 0});
  if (falsy(entity->wait)) entity->wait = 0.05;
  if (falsy(entity->delay)) entity->delay = 0.1;
  if (qa_q1_length(qa_q1_get_body(game, entity).velocity) == 0) set_velocity(game, entity, (qa_vec3){1, 1, tall ? 2 : 1});
  schedule(game, entity, FX(context, "embers"), entity->wait + entity->delay * qa_q1_host_random(game));
}
static void tele_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_vec3 raw;
  raw.x = signed_random(game) * 10;
  raw.y = signed_random(game) * 10;
  raw.z = signed_random(game) * 5;
  qa_vec3 direction = qa_q1_normalize(raw);
  double distance = context->program == QA_Q1AD_MG3 ? qa_q1_actor_number(entity, "distance") : 64;
  particles(game, qa_q1_vadd(qa_q1_get_body(game, entity).origin, qa_q1_vscale(direction, distance)),
            qa_q1_vscale(direction, distance * -0.125), 3, 3);
  schedule(game, entity, FX(context, "tele"), entity->wait + entity->delay * qa_q1_host_random(game));
}
static void spawn_tele(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (falsy(entity->delay)) entity->delay = 0.1;
  if (qa_q1_actor_number(entity, "distance") == 0) qa_q1ad_set_number(context, entity, "distance", 64);
  schedule(game, entity, FX(context, "tele"), entity->wait + entity->delay * qa_q1_host_random(game));
}
static void fountain_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_vec3 velocity = qa_q1_get_body(game, entity).velocity;
  qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  qa_vec3 direction;
  direction.x = signed_random(game) * velocity.x;
  direction.y = signed_random(game) * velocity.y;
  direction.z = velocity.z;
  particles(game, origin, direction, 13, 2);
  schedule(game, entity, FX(context, "fountain"), entity->wait + entity->delay * qa_q1_host_random(game));
}
static void fountain_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)other; (void)activator;
  schedule(game, entity, FX((qa_q1ad_context *)ctx, "fountain"), 0.1);
}
static void spawn_fountain(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (falsy(entity->wait)) entity->wait = 0.05;
  if (falsy(entity->delay)) entity->delay = 0.1;
  if (qa_q1_length(qa_q1_get_body(game, entity).velocity) == 0) set_velocity(game, entity, (qa_vec3){1, 1, 6});
  if ((spawnflags(entity) & 1) != 0) {
    entity->use = qa_q1_callback_registry_use(game->named, entity, FX(context, "fountain"));
    return;
  }
  schedule(game, entity, FX(context, "fountain"), entity->wait + entity->delay * qa_q1_host_random(game));
}

static void reg(qa_q1ad_context *context, const char *name, qa_q1_callback_handlers handlers) {
  handlers.ctx = context;
  qa_q1_callback_registry_register(context->game->named, name, handlers);
}

void qa_q1ad_register_effects(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  reg(context, FX(context, "shake"), (qa_q1_callback_handlers){.action = shake_action, .use = shake_use});
  qa_q1_register_spawn(game, "trigger_screenshake", spawn_shake, context);
  reg(context, FX(context, "sound"), (qa_q1_callback_handlers){.use = sound_use});
  qa_q1_register_spawn(game, "trigger_sound", spawn_sound, context);
  reg(context, FX(context, "lightning"), (qa_q1_callback_handlers){.use = lightning_use});
  qa_q1_register_spawn(game, "trigger_lightning", spawn_lightning, context);
  reg(context, FX(context, "fade_targets"), (qa_q1_callback_handlers){.action = fade_targets});
  reg(context, FX(context, "fade"), (qa_q1_callback_handlers){.use = fade_use});
  qa_q1_register_spawn(game, "trigger_fade", spawn_fade, context);
  reg(context, FX(context, "freeze"), (qa_q1_callback_handlers){.use = freeze_use});
  qa_q1_register_spawn(game, "trigger_freeze", spawn_freeze, context);
  reg(context, FX(context, "embers"), (qa_q1_callback_handlers){.action = embers_action});
  qa_q1_register_spawn(game, "particle_embers", spawn_embers, context);
  qa_q1_register_spawn(game, "particle_embers_tall", spawn_embers, context);
  reg(context, FX(context, "tele"), (qa_q1_callback_handlers){.action = tele_action});
  qa_q1_register_spawn(game, "particle_tele", spawn_tele, context);
  reg(context, FX(context, "fountain"), (qa_q1_callback_handlers){.action = fountain_action, .use = fountain_use});
  qa_q1_register_spawn(game, "particle_tele_fountain", spawn_fountain, context);
}

/* --- registerAddonFog --- */
static qa_q1_actor *fog_info(qa_q1_services *game, qa_q1_actor *entity, const char *field) {
  const char *name = qa_q1_actor_text(entity, field);
  if (name[0] == '\0') return NULL;
  qa_q1_actor *found[1];
  if (qa_q1_find(game, name, found, 1) == 0) return NULL;
  return strcmp(found[0]->classname, "info_fog") == 0 ? found[0] : NULL;
}
static void set_fog(qa_q1ad_context *context, qa_actor_id player, double density, qa_vec3 color, double duration) {
  qa_q1ad_set_player_number(context, player, "fog_density", density);
  qa_q1ad_set_player_number(context, player, "fog_color_x", color.x);
  qa_q1ad_set_player_number(context, player, "fog_color_y", color.y);
  qa_q1ad_set_player_number(context, player, "fog_color_z", color.z);
  emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_FOG, .has_player = true, .player = player, .density = density,
                                      .color_vec = color, .duration = duration, .sky_factor = 0});
}
static bool push(qa_q1ad_context *context, qa_q1_actor *entity, qa_actor_id player, double duration) {
  qa_q1_actor *source = fog_info(context->game, entity, "fog_info_entity");
  if (source == NULL) return false;
  double density = qa_q1_actor_number(source, "fog_density");
  qa_vec3 color = qa_q1_actor_vector(source, "fog_color");
  if (density != 0 || qa_q1_length(color) != 0 || (spawnflags(source) & 1) != 0) set_fog(context, player, density, color, duration);
  return true;
}
/* game.sourceFogPush = (player, entity) => push(entity, player, 0). */
static bool source_fog_push(void *ctx, qa_actor_id player, qa_q1_actor *source) {
  return push(ctx, source, player, 0);
}
static void activate(qa_q1ad_context *context, qa_q1_actor *entity, const qa_actor_id *player) {
  if (player == NULL || !qa_q1_is_player(context->game, player)) return;
  qa_actor_id previous;
  if (qa_q1ad_player_reference(context, *player, "fog.active", &previous) && qa_same_actor(previous, entity->actor.id)) return;
  qa_q1ad_set_player_reference(context, *player, "fog.active", &entity->actor.id);
  (void)push(context, entity, *player, entity->delay);
}
static void fog_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                      const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface;
  activate(ctx, entity, &other);
}
static void fog_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                    const qa_actor_id *activator) {
  (void)game; (void)activator;
  activate(ctx, entity, other);
}
static void spawn_info_fog(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  if (qa_q1_actor_number(entity, "fog_density") == 0) {
    qa_q1ad_set_number(context, entity, "fog_density", 0.05);
  } else {
    qa_vec3 color = qa_q1_actor_vector(entity, "fog_color");
    if (color.x > 1 || color.y > 1 || color.z > 1) qa_q1ad_set_vector(context, entity, "fog_color", qa_q1_vscale(color, 1.0 / 255));
  }
}
static void spawn_fog(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (game->options.coop || game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  if (falsy(entity->delay)) entity->delay = 0.5;
  qa_q1ad_init_trigger(context, entity);
  entity->use = qa_q1_callback_registry_use(game->named, entity, FOG(context, "activate"));
  if ((spawnflags(entity) & 1) == 0) entity->touch = qa_q1_callback_registry_touch(game->named, entity, FOG(context, "activate"));
}
static void fog_transition(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id player,
                           const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, &player)) return;
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player);
  if (!body.has_value) return;
  qa_bounds bounds = qa_q1_get_body(game, entity).bounds;
  qa_vec3 size = qa_q1_vsub(bounds.max, bounds.min), position = qa_q1_vsub(body.value.origin, bounds.min);
  double axis = qa_q1_actor_number(entity, "style");
  double tween = qa_js_min(1, qa_js_max(0, axis == 0 ? position.x / size.x : axis == 1 ? position.y / size.y : position.z / size.z));
  qa_q1_actor *first = fog_info(game, entity, "fog_info_entity"), *second = fog_info(game, entity, "target");
  double left = (1 - tween) * (first != NULL ? qa_q1_actor_number(first, "fog_density") : 0);
  double right = tween * (second != NULL ? qa_q1_actor_number(second, "fog_density") : 0);
  double density = left +
                   right;
  qa_vec3 a = qa_q1_vscale(first != NULL ? qa_q1_actor_vector(first, "fog_color") : QA_Q1_ZERO, 1 - tween);
  qa_vec3 b = qa_q1_vscale(second != NULL ? qa_q1_actor_vector(second, "fog_color") : QA_Q1_ZERO, tween);
  qa_vec3 color = qa_q1_vadd(a, b);
  set_fog(context, player, density, color, 0);
}
static void spawn_fog_transition(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (game->options.coop || game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  double axis = qa_q1_actor_number(entity, "style");
  if (axis != 0 && axis != 1 && axis != 2) qa_range_error("Invalid style for trigger_fog_transition");
  qa_q1ad_init_trigger(context, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, FOG(context, "transition"));
}

void qa_q1ad_register_fog(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  if (context->program == QA_Q1AD_MG1 || context->program == QA_Q1AD_MG3) {
    game->source_fog_push.ctx = context;
    game->source_fog_push.fn = source_fog_push;
  }
  reg(context, FOG(context, "activate"), (qa_q1_callback_handlers){.touch = fog_touch, .use = fog_use});
  qa_q1_register_spawn(game, "info_fog", spawn_info_fog, context);
  qa_q1_register_spawn(game, "trigger_fog", spawn_fog, context);
  reg(context, FOG(context, "transition"), (qa_q1_callback_handlers){.touch = fog_transition});
  qa_q1_register_spawn(game, "trigger_fog_transition", spawn_fog_transition, context);
}
