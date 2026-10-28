/* Port of src/app/bootstrap/network/q1-demo.ts. The remote
 * presentations (remote-q1/remote-qw) sit behind host vtables;
 * their adapters live in those modules. assertCurrent longjmps out
 * of receive when closed mid-advance (TS RetiredDemoOperation).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q1_DEMO_H
#define QA_APP_BOOTSTRAP_NETWORK_Q1_DEMO_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/network/q1/demos.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/quakeworld.h"
typedef struct qa_app_q1_demo_frame {
  double elapsed_seconds, frame; bool timedemo;
} qa_app_q1_demo_frame;
typedef struct qa_app_q1_demo_progress {
  double recorded_seconds, records_read;
  const char *phase;  /* loading|active|ended */
  const char *reason; /* eof|recorded-disconnect|closed when ended */
} qa_app_q1_demo_progress;
typedef void (*qa_app_demo_assert_fn)(void *ctx);
typedef struct qa_app_q1_demo_remote {
  void *ctx;
  bool (*has_player)(void *ctx);
  bool (*has_output)(void *ctx);
  double (*recorded_seconds)(void *ctx);
  void (*receive)(void *ctx, qa_netquake_message *messages, size_t count,
      double now_milliseconds, qa_app_demo_assert_fn assert_current, void *assert_ctx);
  void (*set_demo_view_angles)(void *ctx, qa_vec3 angles, bool demo);
  void (*sample_demo)(void *ctx, double seconds);
} qa_app_q1_demo_remote;
typedef struct qa_app_netquake_demo_input qa_app_netquake_demo_input;
qa_app_netquake_demo_input *qa_app_netquake_demo_input_create(
    qa_netquake_demo_reader *reader, qa_app_q1_demo_remote remote);
void qa_app_netquake_demo_input_destroy(qa_app_netquake_demo_input *input);
void qa_app_netquake_demo_input_close(qa_app_netquake_demo_input *input);
qa_app_q1_demo_progress qa_app_netquake_demo_input_advance(
    qa_app_netquake_demo_input *input, qa_app_q1_demo_frame frame);
typedef struct qa_app_qw_demo_remote {
  void *ctx;
  bool (*has_player)(void *ctx);
  bool (*has_output)(void *ctx);
  void (*server_data)(void *ctx, const qa_quakeworld_message *data);
  void (*game_state)(void *ctx, const qa_quakeworld_message *data,
      const char *const *models, size_t model_count,
      const char *const *sounds, size_t sound_count,
      qa_app_demo_assert_fn assert_current, void *assert_ctx);
  void (*receive)(void *ctx, qa_quakeworld_message *messages, size_t count,
      double now_milliseconds, qa_app_demo_assert_fn assert_current, void *assert_ctx);
  void (*set_demo_view_angles)(void *ctx, qa_vec3 angles, bool demo);
  void (*sample_demo)(void *ctx, double seconds);
  void (*sample_presentation)(void *ctx, double now_milliseconds);
  void (*prediction_sent)(void *ctx, double sequence, qa_qw_user_command command, double now_milliseconds);
  void (*prediction_acknowledged)(void *ctx, double sequence, double now_milliseconds);
} qa_app_qw_demo_remote;
typedef struct qa_app_quakeworld_demo_input qa_app_quakeworld_demo_input;
qa_app_quakeworld_demo_input *qa_app_quakeworld_demo_input_create(
    qa_quakeworld_demo_reader *reader, qa_app_qw_demo_remote remote);
void qa_app_quakeworld_demo_input_destroy(qa_app_quakeworld_demo_input *input);
void qa_app_quakeworld_demo_input_close(qa_app_quakeworld_demo_input *input);
qa_app_q1_demo_progress qa_app_quakeworld_demo_input_advance(
    qa_app_quakeworld_demo_input *input, qa_app_q1_demo_frame frame);
#endif
