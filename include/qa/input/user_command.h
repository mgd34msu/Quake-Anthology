/*
 * Port of src/input/user-command.ts: CL_AdjustAngles, CL_BaseMove
 * and CL_FinishMove from Quake I/II/III cl_input.c. The builder's
 * mouse is borrowed when supplied (the TS shares the instance)
 * and owned when NULL. The run-preference binding mirrors the TS
 * alwaysRun getter indirection through the tuning accessors.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_USER_COMMAND_H
#define QA_INPUT_USER_COMMAND_H

#include <stdbool.h>

#include "qa/contracts/common.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/input/mouse.h"
#include "qa/input/pitch_drift.h"
#include "qa/input/seat.h"

/** TS UserCommandFrame union; fields are read per kind. */
typedef struct qa_user_command_frame {
  qa_command_dialect kind;
  double acknowledged_server_time_seconds; /* q1-netquake */
  qa_vec3 delta_angles;                    /* q2-classic / q2-rerelease */
  double light_level;                      /* q2-classic */
  double server_frame;                     /* q2-rerelease */
  bool attack_allowed;                     /* q2-classic / q2-rerelease */
  double server_time_milliseconds;         /* q3 */
  double weapon;                           /* q3 */
  double sensitivity;                      /* q3 */
  bool has_pitch_drift;
  qa_pitch_drift_state pitch_drift;
} qa_user_command_frame;

/** TS ViewInputTuning. */
typedef struct qa_view_input_tuning {
  double forward_speed;
  double back_speed;
  double side_speed;
  double up_speed;
  double yaw_speed;
  double pitch_speed;
  double angle_speed_multiplier;
  double move_speed_multiplier;
  bool always_run;
} qa_view_input_tuning;

qa_view_input_tuning qa_default_view_input_tuning(qa_command_dialect dialect);

/** TS () => {speed, delay} pitch-drift settings. */
typedef void (*qa_pitch_drift_settings_fn)(void *ctx, double *out_speed,
                                           double *out_delay);
/** TS alwaysRun preference {read, write}. */
typedef bool (*qa_always_run_read_fn)(void *ctx);
typedef void (*qa_always_run_write_fn)(void *ctx, bool value);

typedef struct qa_input_command_builder qa_input_command_builder;

/* mouse NULL creates an owned default MouseInput; tuning NULL
 * selects defaultViewInputTuning(dialect). NULL only on OOM. */
qa_input_command_builder *qa_input_command_builder_create(
    qa_command_dialect dialect, qa_mouse_input *mouse,
    const qa_view_input_tuning *tuning);
void qa_input_command_builder_destroy(qa_input_command_builder *builder);
qa_command_dialect qa_input_command_builder_dialect(
    const qa_input_command_builder *builder);
qa_mouse_input *qa_input_command_builder_mouse(qa_input_command_builder *builder);
void qa_input_command_builder_bind_pitch_drift(
    qa_input_command_builder *builder, qa_pitch_drift_settings_fn settings,
    void *settings_ctx);
qa_view_input_tuning qa_input_command_builder_tuning(
    const qa_input_command_builder *builder);
void qa_input_command_builder_set_tuning(qa_input_command_builder *builder,
                                         qa_view_input_tuning value);
void qa_input_command_builder_bind_always_run(qa_input_command_builder *builder,
                                              qa_always_run_read_fn read,
                                              qa_always_run_write_fn write,
                                              void *ctx);
qa_vec3 qa_input_command_builder_view_angles(
    const qa_input_command_builder *builder);
void qa_input_command_builder_set_view_angles(qa_input_command_builder *builder,
                                              qa_vec3 angles);
/* delta_pitch mirrors the TS default of 0. */
void qa_input_command_builder_center_view(qa_input_command_builder *builder,
                                          double delta_pitch);
void qa_input_command_builder_clear(qa_input_command_builder *builder);
/* source_frame_milliseconds defaults to sample->frame_milliseconds
 * in the TS; pass it explicitly here. */
qa_user_command qa_input_command_builder_build(
    qa_input_command_builder *builder, const qa_seat_input_sample *sample,
    const qa_user_command_frame *frame, double source_frame_milliseconds);

#endif
