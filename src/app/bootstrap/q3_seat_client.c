/* ApplicationQ3Client as WorldSeatPresentation's q3Client (q3_seat_client.h). GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_seat_client.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

static qa_cvar_registry *seat_cvars(void *ctx) { return qa_app_application_q3_client_cvars(ctx); }
static qa_scene_camera seat_camera(void *ctx) { return qa_app_application_q3_client_camera(ctx); }
/* q3Client.prepare(frame, viewport, presentations, statusVisible, bodies, viewWeaponVisible, cameraControlled). */
static void seat_prepare(void *ctx, double frame, qa_rect viewport, const qa_app_simulation_presentation *presentations,
    size_t presentation_count, bool draw_status, const qa_app_component_body *bodies, size_t body_count, bool weapon_visible,
    bool native_view) {
  qa_app_application_q3_client_prepare(ctx, frame, &viewport, presentations, presentation_count, draw_status, bodies, body_count,
      weapon_visible, native_view);
}
static bool seat_shared_equipment_view_visible(void *ctx, bool *out) {
  return qa_app_application_q3_client_shared_equipment_view_visible(ctx, out);
}
static const qa_app_qvm_held_weapon *seat_shared_held_weapons(void *ctx, size_t *out_count) {
  return qa_app_application_q3_client_shared_held_weapons(ctx, out_count);
}
static const qa_app_prepared_primary_body *seat_shared_bodies(void *ctx, size_t *out_count) {
  return qa_app_application_q3_client_shared_bodies(ctx, out_count);
}
static bool seat_body_pose(void *ctx, qa_actor_id actor, qa_vec3 *origin, qa_vec3 *angles) {
  return qa_app_application_q3_client_body_pose(ctx, actor, origin, angles);
}
static qa_scene_camera seat_supplemental_weapon_camera(void *ctx, qa_scene_camera camera) {
  return qa_app_application_q3_client_supplemental_weapon_camera(ctx, camera);
}
static void seat_weapon_hud_view(void *ctx, bool *visible, bool *aggregate_warning) {
  qa_app_application_q3_client_weapon_hud_view(ctx, visible, aggregate_warning);
}
static void seat_close(void *ctx) { qa_app_application_q3_client_close(ctx); }

/* The presentation's (camera, source) => effects closure as additionalEffects: the client releases the frame it is
 * given (qa_app_application_effect_frame_destroy), so the arrays are copied. */
static void *copy_array(const void *items, size_t count, size_t size) {
  if (count == 0) return NULL;
  void *copy = malloc(count * size);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, items, count * size);
  return copy;
}
static bool frame_effects(void *ctx, qa_scene_camera camera, qa_source_scene_order *source, qa_app_application_effect_frame *out) {
  const qa_app_seat_q3_frame_hooks *hooks = ctx;
  qa_app_seat_q3_effects effects = hooks->effects(hooks->ctx, camera, source);
  memset(out, 0, sizeof *out);
  out->q3_admissions = copy_array(effects.q3_admissions, effects.q3_admission_count, sizeof *effects.q3_admissions);
  out->q3_admission_count = effects.q3_admission_count;
  out->operations.items = effects.operation_count == 0 ? NULL : malloc(effects.operation_count * sizeof *out->operations.items);
  if (effects.operation_count != 0 && out->operations.items == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < effects.operation_count; i++)
    out->operations.items[i] = (qa_app_effect_operation){false, effects.operations[i], NULL};
  out->operations.count = out->operations.capacity = effects.operation_count;
  out->lights = copy_array(effects.lights, effects.light_count, sizeof *effects.lights);
  out->light_count = effects.light_count;
  out->q3_lights = copy_array(effects.q3_lights, effects.q3_light_count, sizeof *effects.q3_lights);
  out->q3_light_count = effects.q3_light_count;
  return true;
}
static qa_scene_camera frame_camera(void *ctx, qa_scene_camera camera) {
  const qa_app_seat_q3_frame_hooks *hooks = ctx;
  return hooks->camera(hooks->ctx, camera);
}
/* q3Client.frame(effects, camera, { noWorldModel, ...q1Services.view(actor), q1Fog? }, clientViewOffsetDelta). */
static bool seat_frame(void *ctx, const qa_app_seat_q3_frame_hooks *hooks, const qa_render_command **commands, size_t *command_count) {
  qa_app_q3_frame_hooks q3_hooks = {(void *)hooks, frame_effects, frame_camera};
  qa_app_q3_frame_environment environment;
  memset(&environment, 0, sizeof environment);
  environment.has_q1_fog = hooks->has_q1_fog;
  environment.q1_fog = hooks->q1_fog;
  environment.source_sky = hooks->source_sky;
  environment.no_world_model = hooks->no_world_model;
  qa_render_frame frame = qa_app_application_q3_client_frame(ctx, &q3_hooks, &environment,
      hooks->has_client_view_offset_delta ? &hooks->client_view_offset_delta : NULL);
  *commands = frame.commands;
  *command_count = frame.command_count;
  return true;
}

qa_app_seat_q3_client qa_app_application_q3_client_seat(qa_app_application_q3_client *client) {
  const qa_app_application_q3_client_options *options = qa_app_application_q3_client_options_of(client);
  qa_app_seat_q3_client seat;
  memset(&seat, 0, sizeof seat);
  seat.ctx = client;
  seat.kind = options->kind == QA_APP_Q3_CLIENT_QVM ? "qvm" : options->kind == QA_APP_Q3_CLIENT_REMOTE ? "remote" : "local";
  seat.seat = qa_session_seat_id(options->local->player.seat);
  seat.actor = options->local->player.actor;
  seat.cvars = seat_cvars;
  seat.camera = seat_camera;
  seat.prepare = seat_prepare;
  seat.shared_equipment_view_visible = seat_shared_equipment_view_visible;
  seat.shared_held_weapons = seat_shared_held_weapons;
  seat.shared_bodies = seat_shared_bodies;
  seat.body_pose = seat_body_pose;
  seat.frame = seat_frame;
  seat.supplemental_weapon_camera = seat_supplemental_weapon_camera;
  seat.weapon_hud_view = seat_weapon_hud_view;
  seat.close = seat_close;
  return seat;
}
