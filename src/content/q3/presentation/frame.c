/* Port of src/content/q3/presentation/frame.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/frame.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/presentation/media.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
static float f32(double v) { return (float)v; }
const char *qa_q3_pres_frame_stereo_name(qa_q3_pres_frame_stereo stereo) {
  return stereo == QA_Q3_PRES_STEREO_LEFT ? "left" : stereo == QA_Q3_PRES_STEREO_RIGHT ? "right" : "center";
}
static void check_owners(qa_q3_pres_presentation_frame_runtime *runtime,
                         qa_q3_pres_client_game_state *state, bool scene_scope) {
  qa_q3_pres_presentation_frame_host *host = &runtime->host;
  bool ok = host->snapshots && host->snapshots->state == state;
  if (!scene_scope) {
    ok = ok && host->prediction && host->prediction->state == state;
    ok = ok && host->view && host->view->state == state;
    ok = ok && host->weapons && host->weapons->selection.state == state;
    ok = ok && host->hud && host->hud->state == state;
    ok = ok && host->status && host->status->state == state;
  }
  if (!ok) qa_range_error("Q3 frame owners must share one seat's cgame state");
}
void qa_q3_pres_presentation_frame_runtime_init(qa_q3_pres_presentation_frame_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_presentation_frame_host host) {
  memset(runtime, 0, sizeof *runtime);
  runtime->state = state;
  runtime->host = host;
  runtime->scene_scope = false;
  check_owners(runtime, state, false);
}
void qa_q3_pres_presentation_frame_runtime_init_scene(qa_q3_pres_presentation_frame_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_presentation_scene_host host) {
  memset(runtime, 0, sizeof *runtime);
  runtime->state = state;
  runtime->host = host.frame;
  runtime->scene_scope = true;
  check_owners(runtime, state, true);
}
void qa_q3_pres_presentation_frame_runtime_close(qa_q3_pres_presentation_frame_runtime *runtime) {
  if (runtime->drawing && !runtime->scene_drawing) qa_range_error("Cannot retire cgame during a frame");
  runtime->closed = true;
  qa_q3_pres_scene_recorder_clear_scene(runtime->host.scene);
}
/* TS Set<number> of weapons: first insertion decides the registration order. */
typedef struct weapon_set { double *values; size_t count; } weapon_set;
static bool weapon_set_init(weapon_set *set, size_t maximum) {
  set->values = malloc(maximum * sizeof *set->values);
  set->count = 0;
  return set->values != NULL;
}
static void weapon_set_add(weapon_set *set, double weapon) {
  for (size_t i = 0; i < set->count; i++) if (set->values[i] == weapon) return;
  set->values[set->count++] = weapon;
}
static void check_clock(double value) {
  if (value != trunc(value) || value < -2147483648.0 || value > 2147483647.0)
    qa_range_error("Cgame clocks require int32");
}
static bool add_marks_and_particles(qa_q3_pres_presentation_frame_runtime *runtime) {
  qa_q3_pres_presentation_frame_host *host = &runtime->host;
  qa_q3_pres_ref_poly *polys = NULL;
  size_t poly_count = 0;
  if (!qa_q3_pres_impact_mark_system_add_marks(host->marks, &polys, &poly_count)) return false;
  for (size_t i = 0; i < poly_count; i++) qa_q3_pres_scene_recorder_add_poly(host->scene, &polys[i]);
  for (size_t i = 0; i < poly_count; i++) qa_q3_pres_ref_poly_destroy(&polys[i]);
  free(polys);
  size_t particle_count = 0;
  qa_ref_poly *particle_polys = qa_particle_system_add_particles(host->particles, NULL, &particle_count);
  for (size_t i = 0; i < particle_count; i++) {
    qa_q3_pres_ref_poly converted;
    converted.shader = (const qa_q3_pres_scene_shader *)particle_polys[i].shader;
    converted.vertices = (qa_q3_pres_ref_poly_vertex *)particle_polys[i].vertices;
    converted.vertex_count = particle_polys[i].vertex_count;
    qa_q3_pres_scene_recorder_add_poly(host->scene, &converted);
  }
  qa_ref_polys_destroy(particle_polys, particle_count);
  return true;
}
static void scene_add_entity(void *ctx, const qa_q3_pres_ref_entity *entity) {
  qa_q3_pres_admitted_ref_entity admitted;
  admitted.is_poly_record = false;
  admitted.entity = *entity;
  qa_q3_pres_scene_recorder_add_ref_entity(ctx, &admitted);
}
static void scene_add_light(void *ctx, qa_q3_pres_dynamic_light light) {
  qa_q3_pres_scene_recorder_add_light(ctx, light);
}
static void add_local_entities(qa_q3_pres_presentation_frame_runtime *runtime) {
  qa_q3_pres_local_entity_frame frame = {runtime->state->time, runtime->state->frame_time,
                                         runtime->state->refdef.view_origin};
  qa_q3_pres_local_entity_scene_sink sink = {runtime->host.scene, scene_add_entity, scene_add_light};
  qa_q3_pres_local_entity_system_add_entities(runtime->host.local_entities, frame, sink);
}
static void fade_timescale(qa_q3_pres_presentation_frame_runtime *runtime) {
  if (runtime->scene_scope) qa_range_error("Supplemental cgame cannot control primary timescale");
  qa_q3_pres_client_configuration *configuration = runtime->host.configuration;
  qa_cvar_snapshot end_snapshot, speed_snapshot, current_snapshot;
  if (!qa_q3_pres_client_configuration_read_vm_symbol(configuration, QA_Q3_PRES_VM_CG_TIMESCALE_FADE_END, &end_snapshot))
    return;
  if (!qa_q3_pres_client_configuration_read_vm_symbol(configuration, QA_Q3_PRES_VM_CG_TIMESCALE_FADE_SPEED, &speed_snapshot)) {
    qa_cvar_read_free(&end_snapshot);
    return;
  }
  if (!qa_q3_pres_client_configuration_read_vm_symbol(configuration, QA_Q3_PRES_VM_CG_TIMESCALE, &current_snapshot)) {
    qa_cvar_read_free(&end_snapshot);
    qa_cvar_read_free(&speed_snapshot);
    return;
  }
  double end = end_snapshot.numeric_value, speed = speed_snapshot.numeric_value;
  double current = current_snapshot.numeric_value;
  qa_cvar_read_free(&end_snapshot);
  qa_cvar_read_free(&speed_snapshot);
  qa_cvar_read_free(&current_snapshot);
  if (current == end) return;
  double delta = f32(f32(speed * f32(runtime->state->frame_time)) / 1000);
  double value = current < end ? qa_js_min(end, f32(current + delta)) : qa_js_max(end, f32(current - delta));
  (void)qa_q3_pres_client_configuration_set_vm_numeric_value(configuration, QA_Q3_PRES_VM_CG_TIMESCALE, value);
  if (speed != 0) runtime->host.set_timescale(runtime->host.ctx, value);
}
typedef struct { qa_q3_pres_client_weapon_runtime *weapons; qa_q3_base_source_player_state *ps; } view_weapon_source;
static void run_view_weapon(void *ctx) {
  view_weapon_source *source = ctx;
  qa_q3_pres_client_weapon_runtime_add_view_weapon(source->weapons, source->ps);
}
bool qa_q3_pres_presentation_frame_runtime_draw_scene_frame(qa_q3_pres_presentation_frame_runtime *runtime,
    qa_q3_pres_presentation_frame input, qa_scene_camera camera) {
  if (!runtime->scene_scope) qa_range_error("Primary cgame requires its full frame");
  if (runtime->closed || runtime->drawing) qa_range_error("Cgame frame is closed or already drawing");
  runtime->drawing = true;
  runtime->scene_drawing = true;
  bool ok = false;
  qa_q3_pres_presentation_frame_host *host = &runtime->host;
  qa_q3_pres_client_game_state *state = runtime->state;
  check_clock(input.server_time);
  check_clock(input.engine_frame_number);
  if (input.server_time < state->old_time) qa_range_error("Supplemental cgame source time moved backward");
  state->time = input.server_time;
  host->enter_frame(host->ctx, input);
  if (!qa_q3_pres_client_configuration_update_cvars(host->configuration)) goto done;
  if (runtime->closed) qa_range_error("Supplemental cgame retired during its frame");
  host->clear_looping_sounds(host->ctx, false);
  qa_q3_pres_scene_recorder_clear_scene(host->scene);
  if (!qa_q3_pres_snapshot_runtime_process_snapshots(host->snapshots)) goto done;
  if (runtime->closed) qa_range_error("Supplemental cgame retired during its frame");
  if (!state->snap || (state->snap->flags & 2) != 0) { ok = true; goto done; }
  if (!qa_q3_base_player_state_copy(&state->predicted_player_state, &state->snap->player_state)) goto done;
  state->physics_time = state->snap->server_time;
  state->frame_time = qa_js_max(0, qa_int32(state->time - state->old_time));
  state->old_time = state->time;
  state->client_frame = qa_int32(state->client_frame + 1);
  state->refdef.view_origin = camera.origin;
  state->refdef.view_axis = camera.axis;
  state->refdef.x = camera.viewport.x;
  state->refdef.y = camera.viewport.y;
  state->refdef.width = camera.viewport.width;
  state->refdef.height = camera.viewport.height;
  state->refdef.fov_x = atan(1.0 / camera.projection.values[0]) * 360.0 / 3.141592653589793;
  state->refdef.fov_y = atan(1.0 / camera.projection.values[5]) * 360.0 / 3.141592653589793;
  state->refdef.time = state->time;
  {
    double count = qa_q3_base_weapon_count(state->product);
    weapon_set required;
    if (!weapon_set_init(&required, state->snap->entity_count + 1)) goto done;
    weapon_set_add(&required, state->predicted_player_state.weapon);
    for (size_t i = 0; i < state->snap->entity_count; i++) {
      qa_q3_base_entity_state *entity = &state->snap->entities[i];
      if (entity->e_type == QA_Q3_BASE_ET_PLAYER || entity->e_type == QA_Q3_BASE_ET_MISSILE ||
          entity->e_type == QA_Q3_BASE_ET_GRAPPLE)
        weapon_set_add(&required, entity->weapon > count ? 0 : entity->weapon);
    }
    bool registered = true;
    for (size_t i = 0; registered && i < required.count; i++) {
      registered = qa_q3_pres_client_weapon_media_registry_register_weapon(host->weapon_registry, required.values[i]);
      if (registered && runtime->closed) qa_range_error("Supplemental cgame retired during its frame");
    }
    free(required.values);
    if (!registered) goto done;
  }
  qa_q3_pres_packet_entity_presenter_add_packet_entities(host->packet, host->packet_options(host->ctx));
  if (!add_marks_and_particles(runtime)) goto done;
  add_local_entities(runtime);
  qa_q3_pres_client_frame_audio_play_buffered_sounds(host->frame_audio);
  if (!qa_q3_pres_client_server_command_runtime_play_buffered_voice_chats(host->server_commands)) goto done;
  if (runtime->closed) qa_range_error("Supplemental cgame retired during its frame");
  ok = true;
done:
  runtime->drawing = false;
  runtime->scene_drawing = false;
  return ok;
}
bool qa_q3_pres_presentation_frame_runtime_draw_active_frame(qa_q3_pres_presentation_frame_runtime *runtime,
                                                             qa_q3_pres_presentation_frame input) {
  if (runtime->scene_scope) qa_range_error("Supplemental cgame cannot draw the primary frame");
  if (runtime->closed || runtime->drawing) qa_range_error("Cgame frame is closed or already drawing");
  runtime->drawing = true;
  bool ok = false;
  qa_q3_pres_presentation_frame_host *host = &runtime->host;
  qa_q3_pres_client_game_state *state = runtime->state;
  check_clock(input.server_time);
  check_clock(input.engine_frame_number);
  state->time = input.server_time;
  host->enter_frame(host->ctx, input);
  if (!qa_q3_pres_client_configuration_update_cvars(host->configuration)) goto done;
  if (state->info_screen_text && state->info_screen_text[0] != '\0') {
    ok = host->loading_frame(host->ctx);
    goto done;
  }
  host->clear_looping_sounds(host->ctx, false);
  qa_q3_pres_scene_recorder_clear_scene(host->scene);
  if (!qa_q3_pres_snapshot_runtime_process_snapshots(host->snapshots)) goto done;
  if (!state->snap || (state->snap->flags & 2) != 0) {
    ok = host->loading_frame(host->ctx);
    goto done;
  }
  host->set_user_command_value(host->ctx, state->weapon_select, state->zoom_sensitivity);
  state->client_frame = qa_int32(state->client_frame + 1);
  if (!qa_q3_pres_prediction_runtime_predict_player_state(host->prediction)) goto done;
  (void)qa_q3_pres_view_runtime_calculate_view_values(host->view);
  {
    const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(state->product);
    int32_t owned = qa_int32(qa_q3_base_player_state_slots_get(&state->snap->player_state.stats, schema->weapons));
    int32_t count = qa_js_to_int32(qa_q3_base_weapon_count(state->product));
    weapon_set required;
    if (!weapon_set_init(&required, state->snap->entity_count + (size_t)count + 1)) goto done;
    weapon_set_add(&required, state->predicted_player_state.weapon);
    for (int32_t weapon = 1; weapon < count; weapon++)
      if (((uint32_t)owned & (UINT32_C(1) << (weapon & 31))) != 0) weapon_set_add(&required, weapon);
    for (size_t i = 0; i < state->snap->entity_count; i++) {
      qa_q3_pres_client_entity *current_entity =
          qa_q3_pres_client_game_state_entity_at(state, state->snap->entities[i].number);
      qa_q3_base_entity_state *current = &current_entity->current_state;
      if (current->e_type == QA_Q3_BASE_ET_PLAYER)
        weapon_set_add(&required, current->weapon > count ? 0 : current->weapon);
      else if (current->e_type == QA_Q3_BASE_ET_MISSILE || current->e_type == QA_Q3_BASE_ET_GRAPPLE)
        weapon_set_add(&required, current->weapon > count ? 0 : current->weapon);
    }
    bool registered = true;
    for (size_t i = 0; registered && i < required.count; i++)
      registered = qa_q3_pres_client_weapon_media_registry_register_weapon(host->weapon_registry, required.values[i]);
    free(required.values);
    if (!registered) goto done;
  }
  if (!state->rendering_third_person) {
    qa_q3_pres_ref_sprite_entity damage;
    if (qa_q3_pres_view_runtime_damage_blend_blob(host->view, host->media->graphics.view_blood_shader,
                                                  host->ragepro, &damage)) {
      qa_q3_pres_admitted_ref_entity admitted;
      admitted.is_poly_record = false;
      admitted.entity = qa_q3_pres_ref_entity_from_sprite(damage);
      qa_q3_pres_scene_recorder_add_ref_entity(host->scene, &admitted);
    }
  }
  if (!state->hyperspace) {
    qa_q3_pres_packet_entity_presenter_add_packet_entities(host->packet, host->packet_options(host->ctx));
    if (!add_marks_and_particles(runtime)) goto done;
    add_local_entities(runtime);
  }
  {
    view_weapon_source source = {host->weapons, &state->predicted_player_state};
    host->present_view_weapon(host->ctx, &state->predicted_player_state, run_view_weapon, &source);
  }
  qa_q3_pres_client_frame_audio_play_buffered_sounds(host->frame_audio);
  if (!qa_q3_pres_client_server_command_runtime_play_buffered_voice_chats(host->server_commands)) goto done;
  if (state->test_model_entity.model.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    qa_q3_pres_ref_entity model_entity;
    bool has = false;
    if (!qa_q3_pres_view_runtime_add_test_model(host->view, &model_entity, &has)) goto done;
    if (has) {
      qa_q3_pres_admitted_ref_entity admitted;
      admitted.is_poly_record = false;
      admitted.entity = model_entity;
      qa_q3_pres_scene_recorder_add_ref_entity(host->scene, &admitted);
    }
  }
  {
    qa_q3_pres_refdef refdef = qa_q3_pres_view_runtime_finish_refdef(host->view);
    qa_q3_pres_client_frame_audio_powerup_timer_sounds(host->frame_audio);
    host->set_listener(host->ctx, state->snap->player_state.client_num, refdef.view_origin, refdef.view_axis);
    if (input.stereo != QA_Q3_PRES_STEREO_RIGHT) {
      state->frame_time = qa_js_max(0, qa_int32(state->time - state->old_time));
      state->old_time = state->time;
      qa_q3_pres_client_draw_status_add_lagometer_frame_info(host->status);
    }
    fade_timescale(runtime);
    double team = qa_q3_base_player_state_slots_get(&state->snap->player_state.persistant, QA_Q3_BASE_PERS_TEAM);
    if (team == QA_Q3_BASE_TEAM_SPECTATOR &&
        (qa_int32(state->snap->player_state.pm_flags) & QA_Q3_FLAG_SCOREBOARD) != 0) {
      (void)qa_q3_pres_client_hud_draw_tourney_scoreboard(host->hud);
      ok = true;
      goto done;
    }
    qa_cvar_snapshot stereo_snapshot;
    double stereo = 0;
    if (qa_q3_pres_client_configuration_read_vm_symbol(host->configuration, QA_Q3_PRES_VM_CG_STEREO_SEPARATION,
                                                       &stereo_snapshot)) {
      stereo = stereo_snapshot.numeric_value;
      qa_cvar_read_free(&stereo_snapshot);
    }
    double separation = input.stereo == QA_Q3_PRES_STEREO_CENTER
        ? 0 : f32(stereo * (input.stereo == QA_Q3_PRES_STEREO_LEFT ? -0.5 : 0.5));
    qa_q3_pres_client_draw_tools_tile_clear(host->tools, &refdef);
    qa_vec3 base = refdef.view_origin;
    if (separation != 0) refdef.view_origin = qa_add3(base, qa_scale3(refdef.view_axis.vectors[1], -separation));
    qa_q3_pres_scene_recorder_render_scene(host->scene, &refdef);
    refdef.view_origin = base;
    qa_q3_pres_client_hud_draw_2d(host->hud);
    qa_cvar_snapshot stats_snapshot;
    if (qa_q3_pres_client_configuration_read_vm_symbol(host->configuration, QA_Q3_PRES_VM_CG_STATS, &stats_snapshot)) {
      double stats = stats_snapshot.integer_value;
      qa_cvar_read_free(&stats_snapshot);
      if (stats != 0) {
        char text[64];
        snprintf(text, sizeof text, "cg.clientFrame:%lld\n", (long long)state->client_frame);
        host->print(host->ctx, text);
      }
    }
  }
  ok = true;
done:
  runtime->drawing = false;
  return ok;
}
