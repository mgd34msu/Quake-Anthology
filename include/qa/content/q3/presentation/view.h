/* Port of src/content/q3/presentation/view.ts. GPL-2.0-or-later.
 * View calculations and model tools from id Software's cg_view.c.
 * TS async test-model registration is synchronous; the revision
 * guard still abandons a registration superseded by a reentrant
 * host callback, exactly where the TS awaits resolve. */
#ifndef QA_CONTENT_Q3_PRESENTATION_VIEW_H
#define QA_CONTENT_Q3_PRESENTATION_VIEW_H
#include <stdbool.h>
#include "qa/content/q3/presentation/prediction.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
/* TS ViewSettings. */
typedef struct qa_q3_pres_view_settings {
  double video_width, video_height, view_size;
  bool third_person;
  double third_person_range, third_person_angle;
  bool camera_mode;
  double camera_orbit_integer, camera_orbit_value, camera_orbit_delay;
  double error_decay, run_pitch, run_roll, bob_pitch, bob_roll, bob_up;
  double fov, zoom_fov, dm_flags, gun_x, gun_y, gun_z;
} qa_q3_pres_view_settings;
/* TS ViewHost. */
typedef struct qa_q3_pres_view_host {
  void *ctx;
  qa_q3_pres_view_settings (*settings)(void *ctx);
  void (*set_view_size)(void *ctx, double value);
  void (*set_third_person_angle_value)(void *ctx, double value);
  bool (*register_model)(void *ctx, const char *path, qa_q3_pres_scene_model *out);
  void (*print)(void *ctx, const char *message);
} qa_q3_pres_view_host;
typedef struct qa_q3_pres_view_runtime {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_prediction_runtime *prediction;
  qa_q3_pres_view_host host;
  double model_revision;
} qa_q3_pres_view_runtime;
/* Shared-state mismatch aborts with the TS Error text. */
void qa_q3_pres_view_runtime_init(qa_q3_pres_view_runtime *view, qa_q3_pres_client_game_state *state,
                                  qa_q3_pres_prediction_runtime *prediction, qa_q3_pres_view_host host);
/* TS calculateViewValues: returns the in-water result of the FOV step. */
bool qa_q3_pres_view_runtime_calculate_view_values(qa_q3_pres_view_runtime *view);
qa_q3_pres_refdef qa_q3_pres_view_runtime_finish_refdef(qa_q3_pres_view_runtime *view);
void qa_q3_pres_view_runtime_zoom_down(qa_q3_pres_view_runtime *view);
void qa_q3_pres_view_runtime_zoom_up(qa_q3_pres_view_runtime *view);
/* False is the TS null. */
bool qa_q3_pres_view_runtime_damage_blend_blob(qa_q3_pres_view_runtime *view,
    const qa_q3_pres_scene_shader *shader, bool rage_pro, qa_q3_pres_ref_sprite_entity *out);
/* False only on OOM (test model name). */
bool qa_q3_pres_view_runtime_clear_test_model(qa_q3_pres_view_runtime *view);
/* name NULL is TS null; back_lerp NULL is TS null. False only OOM. */
bool qa_q3_pres_view_runtime_test_model(qa_q3_pres_view_runtime *view, const char *name, const double *back_lerp);
bool qa_q3_pres_view_runtime_test_gun(qa_q3_pres_view_runtime *view, const char *name, const double *back_lerp);
void qa_q3_pres_view_runtime_next_model_frame(qa_q3_pres_view_runtime *view);
void qa_q3_pres_view_runtime_previous_model_frame(qa_q3_pres_view_runtime *view);
void qa_q3_pres_view_runtime_next_model_skin(qa_q3_pres_view_runtime *view);
void qa_q3_pres_view_runtime_previous_model_skin(qa_q3_pres_view_runtime *view);
/* TS async addTestModel, synchronous. False return value in *has is
 * the TS null; the bool return is false only on OOM. */
bool qa_q3_pres_view_runtime_add_test_model(qa_q3_pres_view_runtime *view, qa_q3_pres_ref_entity *out, bool *has);
#endif
