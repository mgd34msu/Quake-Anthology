/* Port of src/content/q3/presentation/effects.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/effects.h"
#include <math.h>
#include <stdint.h>
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static float f32(double v) { return (float)v; }
static qa_axis identity_axis(void) {
  qa_axis axis = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  return axis;
}
static double seconds_of(double time) { return f32(f32(time) / 1000); }
static double life_rate(double start, double end) { return f32(1.0 / f32(qa_int32(end - start))); }
static double byte_of(double v) { return (double)(qa_qvm_float_to_int(f32(f32(v) * 255)) & 255); }
static double fx_rand(qa_q3_pres_client_effects *fx) {
  double value = fx->imports.random_integer(fx->imports.ctx);
  if (value != trunc(value) || value < 0 || value > 32767)
    qa_range_error("cgame rand() must return a 15-bit integer");
  return value;
}
static double fx_random(qa_q3_pres_client_effects *fx) { return f32((fx_rand(fx)) / 32767); }
static double fx_crandom(qa_q3_pres_client_effects *fx) { return f32(2 * f32(fx_random(fx) - 0.5)); }
static const qa_q3_pres_mission_effect_media *mission_of(qa_q3_pres_client_effects *fx) {
  if (fx->media.product != QA_Q3_BASE_PRODUCT_MISSIONPACK)
    qa_range_error("Missionpack effect requested in baseq3");
  return &fx->media.mission;
}
void qa_q3_pres_client_effects_init(qa_q3_pres_client_effects *fx, qa_q3_pres_client_game_state *state,
    qa_q3_pres_local_entity_pool *pool, qa_q3_base_product pool_product,
    qa_q3_pres_effect_media media, qa_q3_pres_effect_options options, qa_q3_pres_effect_imports imports) {
  if (state->product != media.product) qa_range_error("Effect media product differs from cgame product");
  if (state->product != pool_product) qa_range_error("Effect pool product differs from cgame product");
  fx->state = state;
  fx->pool = pool;
  fx->media = media;
  fx->options = options;
  fx->imports = imports;
  fx->smoke_seed = 0x92;
  fx->last_score_position = (qa_vec3){0, 0, 0};
}
static qa_q3_base_trajectory trajectory(qa_q3_base_trajectory_type type, double time, qa_vec3 base, qa_vec3 delta) {
  qa_q3_base_trajectory value;
  value.type = type;
  value.time = time;
  value.duration = 0;
  value.base = base;
  value.delta = delta;
  return value;
}
void qa_q3_pres_client_effects_bubble_trail(qa_q3_pres_client_effects *fx, qa_vec3 start, qa_vec3 end, double spacing) {
  if (fx->options.no_projectile_trail) return;
  spacing = f32(spacing);
  if (!isfinite(spacing) || trunc(spacing) < 1 || spacing > 2147483647)
    qa_range_error("Bubble spacing must have a positive integer divisor");
  qa_vec3 difference = qa_sub3(end, start);
  double length = qa_length3(difference);
  qa_vec3 direction = qa_normalize3(difference);
  double i = (double)((int32_t)fx_rand(fx) % (int32_t)trunc(spacing));
  qa_vec3 move = qa_add3(start, qa_scale3(direction, i));
  qa_vec3 step = qa_scale3(direction, spacing);
  for (; i < length; i = qa_qvm_float_to_int(f32(f32(i) + spacing))) {
    qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_create_sprite_entity());
    qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_MOVE_SCALE_FADE, ref);
    qa_q3_pres_ref_sprite_entity *sprite = &le->ref_entity.as.sprite;
    le->le_flags = QA_Q3_PRES_LEF_PUFF_DONT_SCALE;
    le->start_time = fx->state->time;
    le->end_time = qa_qvm_float_to_int(f32(f32(qa_int32(fx->state->time + 1000)) + f32(fx_random(fx) * 250)));
    le->life_rate = life_rate(le->start_time, le->end_time);
    sprite->shading.shader_time = seconds_of(fx->state->time);
    sprite->radius = 3;
    sprite->shading.custom_shader = fx->media.water_bubble_shader;
    sprite->shading.shader_rgba = (qa_vec4){255, 255, 255, 255};
    le->color = (qa_vec4){0, 0, 0, 1};
    le->pos = trajectory(QA_Q3_BASE_TR_LINEAR, fx->state->time, move,
                         (qa_vec3){f32(fx_crandom(fx) * 5), f32(fx_crandom(fx) * 5), f32(f32(fx_crandom(fx) * 5) + 6)});
    move = qa_add3(move, step);
  }
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_smoke_puff(qa_q3_pres_client_effects *fx,
                                                              qa_q3_pres_smoke_puff_options options) {
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_create_sprite_entity());
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_MOVE_SCALE_FADE, ref);
  qa_q3_pres_ref_sprite_entity *sprite = &le->ref_entity.as.sprite;
  le->le_flags = qa_int32(options.flags);
  le->radius = f32(options.radius);
  qa_random_step rotation = qa_q_random(fx->smoke_seed);
  fx->smoke_seed = rotation.seed;
  sprite->rotation = f32(rotation.value * 360);
  sprite->radius = le->radius;
  sprite->shading.shader_time = seconds_of(options.start_time);
  le->start_time = qa_int32(options.start_time);
  le->fade_in_time = qa_int32(options.fade_in_time);
  le->end_time = qa_qvm_float_to_int(f32(f32(le->start_time) + f32(options.duration)));
  le->life_rate = life_rate(le->fade_in_time > le->start_time ? le->fade_in_time : le->start_time, le->end_time);
  le->color = options.color;
  le->pos = trajectory(QA_Q3_BASE_TR_LINEAR, le->start_time, options.origin, options.velocity);
  sprite->origin = options.origin;
  sprite->shading.custom_shader = options.shader;
  if (fx->options.hardware == QA_Q3_PRES_EFFECT_RAGEPRO) {
    sprite->shading.custom_shader = fx->media.smoke_puff_rage_pro_shader;
    sprite->shading.shader_rgba = (qa_vec4){255, 255, 255, 255};
  } else {
    sprite->shading.shader_rgba = (qa_vec4){byte_of(le->color.x), byte_of(le->color.y), byte_of(le->color.z), 255};
  }
  return le;
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_spawn_effect(qa_q3_pres_client_effects *fx, qa_vec3 origin) {
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(
      qa_q3_pres_create_model_entity(fx->media.teleport_effect_model));
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_FADE_RGB, ref);
  qa_q3_pres_ref_model_entity *model = &le->ref_entity.as.model;
  le->start_time = fx->state->time;
  le->end_time = qa_int32(fx->state->time + 500);
  le->life_rate = life_rate(le->start_time, le->end_time);
  le->color = (qa_vec4){1, 1, 1, 1};
  model->shading.shader_time = seconds_of(fx->state->time);
  model->axis = identity_axis();
  bool base = fx->media.product == QA_Q3_BASE_PRODUCT_BASEQ3;
  model->origin = (qa_vec3){origin.x, origin.y, f32(origin.z) + (base ? -24 : 16)};
  if (base) model->shading.custom_shader = fx->media.teleport_effect_shader;
  return le;
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_make_explosion(qa_q3_pres_client_effects *fx,
                                                                  qa_q3_pres_explosion_options options) {
  int32_t duration = qa_int32(options.duration);
  if (duration <= 0) qa_range_error("CG_MakeExplosion: msec = %d", duration);
  if (options.sprite && !options.direction) qa_range_error("Sprite explosion requires a direction vector");
  double offset = (double)((int32_t)fx_rand(fx) & 63);
  qa_q3_pres_local_entity *le = NULL;
  if (options.sprite) {
    qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_create_sprite_entity());
    le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_SPRITE_EXPLOSION, ref);
    qa_q3_pres_ref_sprite_entity *sprite = &le->ref_entity.as.sprite;
    sprite->rotation = (double)((int32_t)fx_rand(fx) % 360);
    sprite->origin = qa_add3(qa_scale3(*options.direction, 16), options.origin);
  } else {
    qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(qa_q3_pres_create_model_entity_default());
    le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_EXPLOSION, ref);
    qa_q3_pres_ref_model_entity *model = &le->ref_entity.as.model;
    if (!options.direction) model->axis = identity_axis();
    else {
      double angle = (double)((int32_t)fx_rand(fx) % 360);
      qa_vec3 forward = *options.direction;
      qa_vec3 perpendicular = qa_perpendicular_vector(forward);
      qa_vec3 side = angle == 0 ? perpendicular
                                : qa_qvm_rotate_point_around_vector(forward, perpendicular, angle);
      model->axis.vectors[0] = forward;
      model->axis.vectors[1] = side;
      model->axis.vectors[2] = qa_cross3(forward, side);
    }
    model->origin = options.origin;
    model->old_origin = options.origin;
    model->model = options.model;
  }
  le->start_time = qa_int32(fx->state->time - offset);
  le->end_time = qa_int32(le->start_time + duration);
  if (options.sprite) {
    le->ref_entity.as.sprite.shading.shader_time = seconds_of(le->start_time);
    le->ref_entity.as.sprite.shading.custom_shader = options.shader;
  } else {
    le->ref_entity.as.model.shading.shader_time = seconds_of(le->start_time);
    le->ref_entity.as.model.shading.custom_shader = options.shader;
  }
  le->color = (qa_vec4){1, 1, 1, 0};
  return le;
}
void qa_q3_pres_client_effects_bleed(qa_q3_pres_client_effects *fx, qa_vec3 origin, double entity_num) {
  if (!fx->options.blood) return;
  if (!fx->state->snap) qa_range_error("CG_Bleed requires a current snapshot");
  qa_q3_pres_ref_sprite_entity *out = NULL;
  (void)qa_q3_pres_client_effects_bleed_at(fx, origin,
      entity_num == fx->state->snap->player_state.client_num, &out);
}
bool qa_q3_pres_client_effects_bleed_at(qa_q3_pres_client_effects *fx, qa_vec3 origin, bool hide,
                                        qa_q3_pres_ref_sprite_entity **out) {
  *out = NULL;
  if (!fx->options.blood) return false;
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_create_sprite_entity());
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_EXPLOSION, ref);
  qa_q3_pres_ref_sprite_entity *sprite = &le->ref_entity.as.sprite;
  le->start_time = fx->state->time;
  le->end_time = qa_int32(le->start_time + 500);
  sprite->origin = origin;
  sprite->rotation = (double)((int32_t)fx_rand(fx) % 360);
  sprite->radius = 24;
  sprite->shading.custom_shader = fx->media.blood_explosion_shader;
  if (hide) sprite->shading.render_flags = (double)(qa_int32(sprite->shading.render_flags) | QA_Q3_PRES_RF_THIRD_PERSON);
  *out = sprite;
  return true;
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_launch_gib(qa_q3_pres_client_effects *fx, qa_vec3 origin,
    qa_vec3 velocity, qa_q3_pres_scene_model model) {
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(qa_q3_pres_create_model_entity(model));
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_FRAGMENT, ref);
  le->start_time = fx->state->time;
  le->end_time = qa_qvm_float_to_int(f32(f32(qa_int32(le->start_time + 5000)) + f32(fx_random(fx) * 3000)));
  le->ref_entity.as.model.origin = origin;
  le->ref_entity.as.model.axis = identity_axis();
  le->pos = trajectory(QA_Q3_BASE_TR_GRAVITY, fx->state->time, origin, velocity);
  le->bounce_factor = f32(0.6);
  le->le_bounce_sound_type = QA_Q3_PRES_LE_SOUND_BLOOD;
  le->le_mark_type = QA_Q3_PRES_LE_MARK_BLOOD;
  return le;
}
void qa_q3_pres_client_effects_gib_player(qa_q3_pres_client_effects *fx, qa_vec3 origin) {
  if (!fx->options.blood) return;
  qa_vec3 first = {f32(fx_crandom(fx) * 250), f32(fx_crandom(fx) * 250), f32(250 + f32(fx_crandom(fx) * 250))};
  qa_q3_pres_scene_model head = ((int32_t)fx_rand(fx) & 1) != 0 ? fx->media.gib_skull : fx->media.gib_brain;
  (void)qa_q3_pres_client_effects_launch_gib(fx, origin, first, head);
  if (!fx->options.gibs) return;
  qa_q3_pres_scene_model models[9] = {fx->media.gib_abdomen, fx->media.gib_arm, fx->media.gib_chest,
      fx->media.gib_fist, fx->media.gib_foot, fx->media.gib_forearm, fx->media.gib_intestine,
      fx->media.gib_leg, fx->media.gib_leg};
  for (int i = 0; i < 9; i++) {
    qa_vec3 velocity = {f32(fx_crandom(fx) * 250), f32(fx_crandom(fx) * 250), f32(250 + f32(fx_crandom(fx) * 250))};
    (void)qa_q3_pres_client_effects_launch_gib(fx, origin, velocity, models[i]);
  }
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_launch_explode(qa_q3_pres_client_effects *fx,
    qa_vec3 origin, qa_vec3 velocity, qa_q3_pres_scene_model model) {
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(qa_q3_pres_create_model_entity(model));
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_FRAGMENT, ref);
  le->start_time = fx->state->time;
  le->end_time = qa_qvm_float_to_int(f32(f32(qa_int32(le->start_time + 10000)) + f32(fx_random(fx) * 6000)));
  le->ref_entity.as.model.origin = origin;
  le->ref_entity.as.model.axis = identity_axis();
  le->pos = trajectory(QA_Q3_BASE_TR_GRAVITY, fx->state->time, origin, velocity);
  le->bounce_factor = f32(0.1);
  le->le_bounce_sound_type = QA_Q3_PRES_LE_SOUND_BRASS;
  le->le_mark_type = QA_Q3_PRES_LE_MARK_NONE;
  return le;
}
void qa_q3_pres_client_effects_big_explode(qa_q3_pres_client_effects *fx, qa_vec3 origin) {
  if (!fx->options.blood) return;
  const double scales[5] = {1, 1, 1.5, 2, 2.5};
  for (int i = 0; i < 5; i++) {
    qa_vec3 velocity = {f32(f32(fx_crandom(fx) * 100) * scales[i]), f32(f32(fx_crandom(fx) * 100) * scales[i]),
                        f32(150 + f32(fx_crandom(fx) * 100))};
    (void)qa_q3_pres_client_effects_launch_explode(fx, origin, velocity, fx->media.smoke2);
  }
}
void qa_q3_pres_client_effects_score_plum(qa_q3_pres_client_effects *fx, double client, qa_vec3 origin, double score) {
  if (client != fx->state->predicted_player_state.client_num || !fx->options.score_plum) return;
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_create_sprite_entity());
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_SCORE_PLUM, ref);
  le->start_time = fx->state->time;
  le->end_time = qa_int32(fx->state->time + 4000);
  le->life_rate = life_rate(le->start_time, le->end_time);
  le->color = (qa_vec4){1, 1, 1, 1};
  le->radius = f32(qa_int32(score));
  double z = f32(origin.z), last_z = fx->last_score_position.z;
  le->pos.base = (qa_vec3){origin.x, origin.y, z >= f32(last_z - 20) && z <= f32(last_z + 20) ? f32(z - 20) : z};
  fx->last_score_position = origin;
  le->ref_entity.as.sprite.radius = 16;
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_lightning_bolt_beam(qa_q3_pres_client_effects *fx,
    qa_vec3 start, qa_vec3 end) {
  const qa_q3_pres_mission_effect_media *media = mission_of(fx);
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_rail(QA_Q3_PRES_REF_LIGHTNING,
                                                              qa_q3_pres_create_lightning_entity());
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_SHOW_REF_ENTITY, ref);
  le->start_time = fx->state->time;
  le->end_time = qa_int32(fx->state->time + 50);
  le->ref_entity.as.rail.origin = start;
  le->ref_entity.as.rail.old_origin = end;
  le->ref_entity.as.rail.shading.custom_shader = media->lightning_shader;
  return le;
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_kamikaze_effect(qa_q3_pres_client_effects *fx, qa_vec3 origin) {
  const qa_q3_pres_mission_effect_media *media = mission_of(fx);
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(
      qa_q3_pres_create_model_entity(media->kamikaze_effect_model));
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_KAMIKAZE, ref);
  le->start_time = fx->state->time;
  le->end_time = qa_int32(fx->state->time + 3000);
  le->life_rate = life_rate(le->start_time, le->end_time);
  le->color = (qa_vec4){1, 1, 1, 1};
  le->ref_entity.as.model.shading.shader_time = seconds_of(fx->state->time);
  le->ref_entity.as.model.origin = origin;
  return le;
}
void qa_q3_pres_client_effects_obelisk_explode(qa_q3_pres_client_effects *fx, qa_vec3 origin) {
  const qa_q3_pres_mission_effect_media *media = mission_of(fx);
  qa_vec3 raised = {origin.x, origin.y, f32(origin.z) + 64};
  qa_vec3 zero = {0, 0, 0};
  qa_q3_pres_explosion_options options = {raised, &zero, media->dish_flash_model, media->rocket_explosion_shader, 600, true};
  qa_q3_pres_local_entity *le = qa_q3_pres_client_effects_make_explosion(fx, options);
  le->light = 300;
  le->light_color = (qa_vec3){1, 0.75, 0};
}
static qa_pcm_sound *hit_sound(qa_q3_pres_client_effects *fx, qa_pcm_sound *const sounds[3]) {
  int32_t choice = (int32_t)fx_rand(fx) & 3;
  return choice < 2 ? sounds[0] : choice == 2 ? sounds[1] : sounds[2];
}
void qa_q3_pres_client_effects_obelisk_pain(qa_q3_pres_client_effects *fx, qa_vec3 origin) {
  const qa_q3_pres_mission_effect_media *media = mission_of(fx);
  fx->imports.start_sound(fx->imports.ctx, origin, 1023, 5, hit_sound(fx, media->obelisk_hit_sounds));
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_invulnerability_impact(qa_q3_pres_client_effects *fx,
    qa_vec3 origin, qa_vec3 angles) {
  const qa_q3_pres_mission_effect_media *media = mission_of(fx);
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(
      qa_q3_pres_create_model_entity(media->invulnerability_impact_model));
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_INVUL_IMPACT, ref);
  le->start_time = fx->state->time;
  le->end_time = qa_int32(fx->state->time + 1000);
  le->life_rate = life_rate(le->start_time, le->end_time);
  le->color = (qa_vec4){1, 1, 1, 1};
  le->ref_entity.as.model.shading.shader_time = seconds_of(fx->state->time);
  le->ref_entity.as.model.origin = origin;
  le->ref_entity.as.model.axis = qa_qvm_angles_to_axis(angles);
  fx->imports.start_sound(fx->imports.ctx, origin, 1023, 5, hit_sound(fx, media->invulnerability_impact_sounds));
  return le;
}
qa_q3_pres_local_entity *qa_q3_pres_client_effects_invulnerability_juiced(qa_q3_pres_client_effects *fx, qa_vec3 origin) {
  const qa_q3_pres_mission_effect_media *media = mission_of(fx);
  qa_q3_pres_ref_entity ref = qa_q3_pres_ref_entity_from_model(
      qa_q3_pres_create_model_entity(media->invulnerability_juiced_model));
  qa_q3_pres_local_entity *le = qa_q3_pres_local_entity_pool_allocate(fx->pool, QA_Q3_PRES_LE_INVUL_JUICED, ref);
  le->start_time = fx->state->time;
  le->end_time = qa_int32(fx->state->time + 10000);
  le->life_rate = life_rate(le->start_time, le->end_time);
  le->color = (qa_vec4){1, 1, 1, 1};
  le->ref_entity.as.model.shading.shader_time = seconds_of(fx->state->time);
  le->ref_entity.as.model.origin = origin;
  le->ref_entity.as.model.axis = qa_qvm_angles_to_axis((qa_vec3){0, 0, 0});
  fx->imports.start_sound(fx->imports.ctx, origin, 1023, 5, media->invulnerability_juiced_sound);
  return le;
}
static qa_q3_pres_local_entity *view_smoke(void *ctx, qa_q3_pres_smoke_puff_options o) {
  return qa_q3_pres_client_effects_smoke_puff(ctx, o);
}
static void view_gib(void *ctx, qa_vec3 o) { qa_q3_pres_client_effects_gib_player(ctx, o); }
static qa_q3_pres_local_entity *view_spawn(void *ctx, qa_vec3 o) { return qa_q3_pres_client_effects_spawn_effect(ctx, o); }
static void view_plum(void *ctx, double c, qa_vec3 o, double s) { qa_q3_pres_client_effects_score_plum(ctx, c, o, s); }
static qa_q3_pres_local_entity *view_kami(void *ctx, qa_vec3 o) { return qa_q3_pres_client_effects_kamikaze_effect(ctx, o); }
static void view_ob_ex(void *ctx, qa_vec3 o) { qa_q3_pres_client_effects_obelisk_explode(ctx, o); }
static void view_ob_pain(void *ctx, qa_vec3 o) { qa_q3_pres_client_effects_obelisk_pain(ctx, o); }
static qa_q3_pres_local_entity *view_inv_imp(void *ctx, qa_vec3 o, qa_vec3 a) {
  return qa_q3_pres_client_effects_invulnerability_impact(ctx, o, a);
}
static qa_q3_pres_local_entity *view_inv_juiced(void *ctx, qa_vec3 o) {
  return qa_q3_pres_client_effects_invulnerability_juiced(ctx, o);
}
static qa_q3_pres_local_entity *view_beam(void *ctx, qa_vec3 s, qa_vec3 e) {
  return qa_q3_pres_client_effects_lightning_bolt_beam(ctx, s, e);
}
qa_q3_pres_client_effects_view qa_q3_pres_client_effects_as_view(qa_q3_pres_client_effects *fx) {
  qa_q3_pres_client_effects_view view = {fx->pool, fx, view_smoke, view_gib};
  return view;
}
qa_q3_pres_effects_smoke_view qa_q3_pres_client_effects_as_smoke_view(qa_q3_pres_client_effects *fx) {
  qa_q3_pres_effects_smoke_view view = {fx, view_smoke};
  return view;
}
qa_q3_pres_event_effects_view qa_q3_pres_client_effects_as_event_view(qa_q3_pres_client_effects *fx) {
  qa_q3_pres_event_effects_view view = {fx, view_smoke, view_spawn, view_gib, view_plum};
  return view;
}
qa_q3_pres_mission_effects_view qa_q3_pres_client_effects_as_mission_view(qa_q3_pres_client_effects *fx) {
  qa_q3_pres_mission_effects_view view = {fx, view_kami, view_ob_ex, view_ob_pain, view_inv_imp, view_inv_juiced, view_beam};
  return view;
}
