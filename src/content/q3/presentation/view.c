/* Port of src/content/q3/presentation/view.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/view.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/client_info.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
static float f32(double v) { return (float)v; }
#define PI_F 3.1415927410125732421875 /* fround(Math.PI) */
static qa_vec3 multiply_add(qa_vec3 origin, double scale, qa_vec3 direction) {
  return qa_add3(origin, qa_scale3(direction, scale));
}
void qa_q3_pres_view_runtime_init(qa_q3_pres_view_runtime *view, qa_q3_pres_client_game_state *state,
                                  qa_q3_pres_prediction_runtime *prediction, qa_q3_pres_view_host host) {
  if (prediction->state != state) qa_range_error("View and prediction must share one client state");
  view->state = state;
  view->prediction = prediction;
  view->host = host;
  view->model_revision = 0;
}
static bool calculate_fov(qa_q3_pres_view_runtime *view, qa_q3_pres_view_settings settings);
static void offset_third_person(qa_q3_pres_view_runtime *view, qa_q3_pres_view_settings settings, double angle);
static void offset_first_person(qa_q3_pres_view_runtime *view, qa_q3_pres_view_settings settings);
bool qa_q3_pres_view_runtime_calculate_view_values(qa_q3_pres_view_runtime *view) {
  qa_q3_pres_client_game_state *state = view->state;
  if (!state->snap) qa_range_error("CG_CalcViewValues requires a current snapshot");
  qa_q3_pres_view_settings settings = view->host.settings(view->host.ctx);
  qa_q3_base_source_player_state *ps = &state->predicted_player_state;
  state->refdef = qa_q3_pres_create_refdef();
  qa_q3_pres_refdef *refdef = &state->refdef;
  double size = state->snap->player_state.pm_type == QA_Q3_PM_INTERMISSION ? 100 : settings.view_size;
  if (size < 30) { view->host.set_view_size(view->host.ctx, 30); size = 30; }
  else if (size > 100) { view->host.set_view_size(view->host.ctx, 100); size = 100; }
  refdef->width = (double)((int64_t)trunc((double)((int32_t)((int64_t)settings.video_width * (int64_t)size)) / 100) & ~1LL);
  refdef->height = (double)((int64_t)trunc((double)((int32_t)((int64_t)settings.video_height * (int64_t)size)) / 100) & ~1LL);
  refdef->x = qa_int32((settings.video_width - refdef->width) / 2);
  refdef->y = qa_int32((settings.video_height - refdef->height) / 2);
  if (refdef->width <= 0 || refdef->height <= 0) qa_range_error("Camera view requires a positive viewport");
  state->rendering_third_person = settings.third_person ||
      qa_q3_base_player_state_record_health(&state->snap->player_state) <= 0;
  refdef->view_origin = qa_q3_base_player_state_record_origin(ps);
  state->refdef_view_angles = ps->viewangles;
  if (ps->pm_type == QA_Q3_PM_INTERMISSION) {
    refdef->view_axis = qa_qvm_angles_to_axis(state->refdef_view_angles);
    return calculate_fov(view, settings);
  }
  state->bob_cycle = (double)(((int64_t)ps->bob_cycle & 128) >> 7);
  state->bob_frac_sin = fabs(f32(sin(f32(f32(((int64_t)ps->bob_cycle & 127) / 127.0) * PI_F))));
  qa_vec3 velocity = qa_q3_base_player_state_record_velocity(ps);
  state->xyspeed = f32(sqrt(f32(f32(velocity.x * velocity.x) + f32(velocity.y * velocity.y))));
  double third_person_angle = f32(settings.third_person_angle);
  if (settings.camera_orbit_integer != 0 && state->time > state->next_orbit_time) {
    state->next_orbit_time = qa_int32(state->time + settings.camera_orbit_delay);
    third_person_angle = f32(third_person_angle + f32(settings.camera_orbit_value));
    view->host.set_third_person_angle_value(view->host.ctx, third_person_angle);
  }
  if (settings.error_decay > 0) {
    double elapsed = qa_int32(state->time - state->predicted_error_time);
    double factor = f32(f32(f32(settings.error_decay) - f32(elapsed)) / f32(settings.error_decay));
    if (factor > 0 && factor < 1) refdef->view_origin = multiply_add(refdef->view_origin, factor, state->predicted_error);
    else state->predicted_error_time = 0;
  }
  if (state->rendering_third_person) offset_third_person(view, settings, third_person_angle);
  else offset_first_person(view, settings);
  refdef->view_axis = qa_qvm_angles_to_axis(state->refdef_view_angles);
  if (state->hyperspace)
    refdef->render_flags = (double)(qa_int32(refdef->render_flags) | QA_Q3_PRES_RDF_NOWORLDMODEL | QA_Q3_PRES_RDF_HYPERSPACE);
  return calculate_fov(view, settings);
}
qa_q3_pres_refdef qa_q3_pres_view_runtime_finish_refdef(qa_q3_pres_view_runtime *view) {
  if (!view->state->snap) qa_range_error("Finishing the view requires a current snapshot");
  view->state->refdef.time = view->state->time;
  memcpy(view->state->refdef.area_mask, view->state->snap->area_mask, 32);
  view->state->refdef.area_mask_length = 32;
  return qa_q3_pres_copy_refdef(&view->state->refdef);
}
void qa_q3_pres_view_runtime_zoom_down(qa_q3_pres_view_runtime *view) {
  if (view->state->zoomed) return;
  view->state->zoomed = true;
  view->state->zoom_time = view->state->time;
}
void qa_q3_pres_view_runtime_zoom_up(qa_q3_pres_view_runtime *view) {
  if (!view->state->zoomed) return;
  view->state->zoomed = false;
  view->state->zoom_time = view->state->time;
}
static void offset_third_person(qa_q3_pres_view_runtime *view, qa_q3_pres_view_settings settings, double angle) {
  qa_q3_pres_client_game_state *state = view->state;
  qa_q3_pres_refdef *refdef = &state->refdef;
  qa_q3_base_source_player_state *ps = &state->predicted_player_state;
  refdef->view_origin.z = refdef->view_origin.z + f32(ps->viewheight);
  qa_vec3 focus_angles = state->refdef_view_angles;
  if (qa_q3_base_player_state_record_health(ps) <= 0) {
    double yaw = qa_q3_base_player_state_slots_get(&ps->stats,
        qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps))->dead_yaw);
    focus_angles.y = yaw;
    state->refdef_view_angles.y = yaw;
  }
  if (focus_angles.x > 45) focus_angles.x = 45;
  qa_vec3 focus_point = multiply_add(refdef->view_origin, 512, qa_qvm_angle_vectors(focus_angles).forward);
  qa_vec3 view_point = {refdef->view_origin.x, refdef->view_origin.y, refdef->view_origin.z + 8};
  state->refdef_view_angles.x = f32(state->refdef_view_angles.x * 0.5);
  qa_angle_vectors vectors = qa_qvm_angle_vectors(state->refdef_view_angles);
  double radians = f32(f32(angle / 180) * PI_F);
  view_point = multiply_add(view_point, f32(-f32(settings.third_person_range) * f32(cos(radians))), vectors.forward);
  view_point = multiply_add(view_point, f32(-f32(settings.third_person_range) * f32(sin(radians))), vectors.right);
  if (!settings.camera_mode) {
    qa_bounds bounds = {{-4, -4, -4}, {4, 4, 4}};
    qa_q3_pres_movement_trace trace = qa_q3_pres_prediction_runtime_trace(view->prediction,
        refdef->view_origin, view_point, bounds, ps->client_num, 1);
    if (trace.result.fraction != 1) {
      view_point = (qa_vec3){trace.result.end.x, trace.result.end.y,
                             trace.result.end.z + f32(f32(1 - trace.result.fraction) * 32)};
      view_point = qa_q3_pres_prediction_runtime_trace(view->prediction, refdef->view_origin, view_point,
                                                       bounds, ps->client_num, 1).result.end;
    }
  }
  refdef->view_origin = view_point;
  focus_point = qa_sub3(focus_point, view_point);
  double distance = fmax(1, f32(sqrt(f32(f32(focus_point.x * focus_point.x) + f32(focus_point.y * focus_point.y)))));
  state->refdef_view_angles = (qa_vec3){f32(f32(-180.0 / 3.141592653589793) * f32(atan2(focus_point.z, distance))),
                                        f32(state->refdef_view_angles.y - angle), state->refdef_view_angles.z};
}
static void offset_first_person(qa_q3_pres_view_runtime *view, qa_q3_pres_view_settings settings) {
  qa_q3_pres_client_game_state *state = view->state;
  if (!state->snap) qa_range_error("First-person offset requires a snapshot");
  if (state->snap->player_state.pm_type == QA_Q3_PM_INTERMISSION) return;
  qa_q3_pres_refdef *refdef = &state->refdef;
  qa_q3_base_source_player_state *ps = &state->predicted_player_state;
  if (qa_q3_base_player_state_record_health(&state->snap->player_state) <= 0) {
    double yaw = qa_q3_base_player_state_slots_get(&state->snap->player_state.stats,
        qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(ps))->dead_yaw);
    state->refdef_view_angles = (qa_vec3){-15, yaw, 40};
    refdef->view_origin.z = refdef->view_origin.z + f32(ps->viewheight);
    return;
  }
  qa_vec3 angles = qa_add3(state->refdef_view_angles, state->kick_angles);
  if (state->damage_time != 0) {
    double ratio = f32(f32(state->time) - state->damage_time);
    if (ratio < 100) ratio = f32(ratio / 100);
    else ratio = f32(1 - f32(f32(ratio - 100) / 400));
    if (f32(f32(state->time) - state->damage_time) < 100 || ratio > 0) {
      angles.x += f32(ratio * state->damage_pitch);
      angles.z += f32(ratio * state->damage_roll);
    }
  }
  qa_vec3 velocity = qa_q3_base_player_state_record_velocity(ps);
  angles = (qa_vec3){angles.x + f32(qa_dot3(velocity, refdef->view_axis.vectors[0]) * f32(settings.run_pitch)),
                     angles.y,
                     angles.z - f32(qa_dot3(velocity, refdef->view_axis.vectors[1]) * f32(settings.run_roll))};
  double speed = fmax(state->xyspeed, 200);
  double pitch = f32(f32(state->bob_frac_sin * f32(settings.bob_pitch)) * speed);
  double roll = f32(f32(state->bob_frac_sin * f32(settings.bob_roll)) * speed);
  if ((qa_int32(ps->pm_flags) & QA_Q3_FLAG_DUCKED) != 0) {
    pitch = f32(pitch * 3);
    roll = f32(roll * 3);
  }
  if ((qa_int32(state->bob_cycle) & 1) != 0) roll = -roll;
  state->refdef_view_angles = (qa_vec3){angles.x + pitch, angles.y, angles.z + roll};
  double height = f32(refdef->view_origin.z + f32(ps->viewheight));
  double duck_delta = qa_int32(state->time - state->duck_time);
  if (duck_delta < 100) height = f32(height - f32(f32(state->duck_change * f32(qa_int32(100 - duck_delta))) / 100));
  height = f32(height + fmin(6, f32(f32(state->bob_frac_sin * state->xyspeed) * f32(settings.bob_up))));
  double land_delta = f32(qa_int32(state->time - state->land_time));
  if (land_delta < 150) height = f32(height + f32(state->land_change * f32(land_delta / 150)));
  else if (land_delta < 450) {
    land_delta = f32(land_delta - 150);
    height = f32(height + f32(state->land_change * f32(1 - f32(land_delta / 300))));
  }
  double step_delta = qa_int32(state->time - state->step_time);
  if (step_delta < 200) height = f32(height - f32(f32(state->step_change * f32(qa_int32(200 - step_delta))) / 200));
  refdef->view_origin = qa_add3((qa_vec3){refdef->view_origin.x, refdef->view_origin.y, height}, state->kick_origin);
}
static bool calculate_fov(qa_q3_pres_view_runtime *view, qa_q3_pres_view_settings settings) {
  qa_q3_pres_client_game_state *state = view->state;
  qa_q3_pres_refdef *refdef = &state->refdef;
  double fov = 90;
  if (state->predicted_player_state.pm_type != QA_Q3_PM_INTERMISSION) {
    fov = (qa_int32(settings.dm_flags) & 16) != 0 ? 90 : fmax(1, fmin(160, f32(settings.fov)));
    double zoom = fmax(1, fmin(160, f32(settings.zoom_fov)));
    double fraction = f32(f32(qa_int32(state->time - state->zoom_time)) / 150);
    if (state->zoomed) fov = fraction > 1 ? zoom : f32(fov + f32(fraction * f32(zoom - fov)));
    else if (fraction <= 1) fov = f32(zoom + f32(fraction * f32(fov - zoom)));
  }
  double radians = f32(f32(fov / 360) * PI_F);
  double tangent = f32(f32(sin(radians)) / f32(cos(radians)));
  double x = f32(f32(refdef->width) / tangent);
  double vertical = f32(f32(f32(atan2(f32(refdef->height), x)) * 360) / PI_F);
  bool in_water = (qa_int32(qa_q3_pres_prediction_runtime_point_contents(view->prediction, refdef->view_origin, -1)) & (8 | 16 | 32)) != 0;
  if (in_water) {
    double phase = f32(f32(f32(f32(f32(state->time) / 1000) * f32(0.4)) * PI_F) * 2);
    double wave = f32(sin(phase));
    fov = f32(fov + wave);
    vertical = f32(vertical - wave);
  }
  refdef->fov_x = fov;
  refdef->fov_y = vertical;
  state->zoom_sensitivity = state->zoomed ? f32(vertical / 75) : 1;
  return in_water;
}
bool qa_q3_pres_view_runtime_damage_blend_blob(qa_q3_pres_view_runtime *view,
    const qa_q3_pres_scene_shader *shader, bool rage_pro, qa_q3_pres_ref_sprite_entity *out) {
  qa_q3_pres_client_game_state *state = view->state;
  double elapsed = qa_int32(trunc(f32(f32(state->time) - state->damage_time)));
  if (state->damage_value == 0 || rage_pro || elapsed <= 0 || elapsed >= 500) return false;
  *out = qa_q3_pres_create_sprite_entity();
  out->shading.render_flags = QA_Q3_PRES_RF_FIRST_PERSON;
  out->origin = multiply_add(state->refdef.view_origin, 8, state->refdef.view_axis.vectors[0]);
  out->origin = multiply_add(out->origin, f32(state->damage_x * -8), state->refdef.view_axis.vectors[1]);
  out->origin = multiply_add(out->origin, f32(state->damage_y * 8), state->refdef.view_axis.vectors[2]);
  out->radius = f32(state->damage_value * 3);
  out->shading.custom_shader = shader;
  out->shading.shader_rgba = (qa_vec4){255, 255, 255,
      (double)((int64_t)trunc(f32(200 * f32(1 - f32(elapsed / 500)))) & 255)};
  return true;
}
bool qa_q3_pres_view_runtime_clear_test_model(qa_q3_pres_view_runtime *view) {
  view->model_revision++;
  if (!qa_q3_pres_set_string(&view->state->test_model_name, "")) return false;
  view->state->test_model_entity = qa_q3_pres_create_model_entity_default();
  view->state->test_gun = false;
  return true;
}
static bool request_test_model(qa_q3_pres_view_runtime *view, const char *name, const double *back_lerp, bool gun) {
  double revision = ++view->model_revision;
  view->state->test_model_entity = qa_q3_pres_create_model_entity_default();
  qa_q3_pres_ref_model_entity *entity = &view->state->test_model_entity;
  if (name) {
    char truncated[64];
    size_t length = strlen(name);
    if (length > 63) length = 63;
    memcpy(truncated, name, length);
    truncated[length] = '\0';
    if (!qa_q3_pres_set_string(&view->state->test_model_name, truncated)) return false;
    qa_q3_pres_scene_model model;
    if (!view->host.register_model(view->host.ctx, view->state->test_model_name, &model)) return false;
    if (revision != view->model_revision) return true;
    entity = &view->state->test_model_entity;
    entity->model = model;
    if (back_lerp) {
      entity->back_lerp = f32(*back_lerp);
      entity->frame = 1;
    }
    if (model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) view->host.print(view->host.ctx, "Can't register model\n");
    else {
      entity->origin = multiply_add(view->state->refdef.view_origin, 100, view->state->refdef.view_axis.vectors[0]);
      entity->axis = qa_qvm_angles_to_axis((qa_vec3){0, f32(180 + view->state->refdef_view_angles.y), 0});
      view->state->test_gun = false;
    }
  }
  if (revision != view->model_revision) return true;
  if (gun) {
    view->state->test_gun = true;
    view->state->test_model_entity.shading.render_flags =
        QA_Q3_PRES_RF_MINLIGHT | QA_Q3_PRES_RF_DEPTHHACK | QA_Q3_PRES_RF_FIRST_PERSON;
  }
  return true;
}
bool qa_q3_pres_view_runtime_test_model(qa_q3_pres_view_runtime *view, const char *name, const double *back_lerp) {
  return request_test_model(view, name, back_lerp, false);
}
bool qa_q3_pres_view_runtime_test_gun(qa_q3_pres_view_runtime *view, const char *name, const double *back_lerp) {
  return request_test_model(view, name, back_lerp, true);
}
static void print_number(qa_q3_pres_view_runtime *view, const char *label, double value) {
  char buffer[64];
  snprintf(buffer, sizeof buffer, "%s %lld\n", label, (long long)value);
  view->host.print(view->host.ctx, buffer);
}
void qa_q3_pres_view_runtime_next_model_frame(qa_q3_pres_view_runtime *view) {
  view->state->test_model_entity.frame = qa_int32(view->state->test_model_entity.frame + 1);
  print_number(view, "frame", view->state->test_model_entity.frame);
}
void qa_q3_pres_view_runtime_previous_model_frame(qa_q3_pres_view_runtime *view) {
  view->state->test_model_entity.frame = fmax(0, qa_int32(view->state->test_model_entity.frame - 1));
  print_number(view, "frame", view->state->test_model_entity.frame);
}
void qa_q3_pres_view_runtime_next_model_skin(qa_q3_pres_view_runtime *view) {
  view->state->test_model_entity.skin_num = qa_int32(view->state->test_model_entity.skin_num + 1);
  print_number(view, "skin", view->state->test_model_entity.skin_num);
}
void qa_q3_pres_view_runtime_previous_model_skin(qa_q3_pres_view_runtime *view) {
  view->state->test_model_entity.skin_num = fmax(0, qa_int32(view->state->test_model_entity.skin_num - 1));
  print_number(view, "skin", view->state->test_model_entity.skin_num);
}
bool qa_q3_pres_view_runtime_add_test_model(qa_q3_pres_view_runtime *view, qa_q3_pres_ref_entity *out, bool *has) {
  *has = false;
  double revision = view->model_revision;
  qa_q3_pres_ref_model_entity *entity = &view->state->test_model_entity;
  if (entity->model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) return true;
  qa_q3_pres_scene_model model;
  const char *name = view->state->test_model_name ? view->state->test_model_name : "";
  if (!view->host.register_model(view->host.ctx, name, &model)) return false;
  if (revision != view->model_revision) return true;
  entity = &view->state->test_model_entity;
  entity->model = model;
  if (model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    view->host.print(view->host.ctx, "Can't register model\n");
    return true;
  }
  if (view->state->test_gun) {
    qa_q3_pres_refdef *refdef = &view->state->refdef;
    qa_q3_pres_view_settings settings = view->host.settings(view->host.ctx);
    entity->axis = refdef->view_axis;
    entity->origin = multiply_add(refdef->view_origin, f32(settings.gun_x), refdef->view_axis.vectors[0]);
    entity->origin = multiply_add(entity->origin, f32(settings.gun_y), refdef->view_axis.vectors[1]);
    entity->origin = multiply_add(entity->origin, f32(settings.gun_z), refdef->view_axis.vectors[2]);
  }
  *out = qa_q3_pres_ref_entity_from_model(*entity);
  *has = true;
  return true;
}
