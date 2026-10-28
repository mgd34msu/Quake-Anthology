/* Port of src/app/bootstrap/network/recorded-source.ts. The
 * per-family demo inputs (q1-demo/q2-demo/q3-demo) are supplied
 * prebuilt behind the playback vtable; family pairing, pause and
 * timedemo accounting, and completion reporting stay here.
 * DemoResource/DemoCompletion are sibling app/bootstrap types,
 * carried by kind/reason strings. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_RECORDED_SOURCE_H
#define QA_APP_BOOTSTRAP_NETWORK_RECORDED_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/core/cvars/index.h"
typedef struct qa_app_demo_timing { double frames, elapsed_milliseconds; } qa_app_demo_timing;
/* Malloc'd "<frames> frames, <s.sss> seconds: <fps> fps\n". */
char *qa_app_demo_timing_text(qa_app_demo_timing timing);
typedef enum qa_app_recorded_family {
  QA_APP_REC_Q1, QA_APP_REC_QW, QA_APP_REC_Q2, QA_APP_REC_Q3
} qa_app_recorded_family;
typedef struct qa_app_recorded_demo_resource {
  qa_app_recorded_family kind; const uint8_t *bytes; size_t length; const char *path;
} qa_app_recorded_demo_resource;
/* Q1/QW advance result: phase "loading"|"active"|"ended". */
typedef struct qa_app_recorded_q1_result {
  const char *phase; const char *reason; /* ended: Q1DemoEnd text */
} qa_app_recorded_q1_result;
/* Q2 advance result: kind "frame" or the terminal kind text. */
typedef struct qa_app_recorded_q2_result {
  const char *kind; double time_milliseconds;
} qa_app_recorded_q2_result;
typedef struct qa_app_recorded_q3_options {
  bool paused; double time_nudge, timescale; bool freeze_demo, timedemo;
} qa_app_recorded_q3_options;
/* Q3 advanceFrame result: kind "frame"|"end"|other; on end,
 * end_reason and timing are set. */
typedef struct qa_app_recorded_q3_result {
  const char *kind; const char *end_reason; qa_app_demo_timing timing;
} qa_app_recorded_q3_result;
typedef struct qa_app_recorded_playback {
  void *ctx;
  qa_app_recorded_family family;
  qa_app_recorded_q1_result (*advance_q1)(void *ctx, double elapsed_seconds, double frame, bool timedemo);
  qa_app_recorded_q2_result (*advance_q2)(void *ctx, double target_milliseconds);
  qa_app_recorded_q2_result (*next_frame_q2)(void *ctx);
  void (*sample_demo_q2)(void *ctx, double milliseconds);
  void (*select_player_q2)(void *ctx, double clientnum);
  qa_app_recorded_q3_result (*advance_frame_q3)(void *ctx, double real_time, qa_app_recorded_q3_options options);
  void (*sample_presentation_q3)(void *ctx, double real_time);
  const char *(*phase_q3)(void *ctx);
  void (*close)(void *ctx);
} qa_app_recorded_playback;
typedef struct qa_app_recorded_source qa_app_recorded_source;
/* complete receives the DemoCompletion text and timing (NULL when
 * TS null). Family mismatch aborts with the TS message. */
qa_app_recorded_source *qa_app_recorded_source_create(qa_app_recorded_demo_resource resource,
    qa_app_recorded_family remote_family, qa_app_recorded_playback playback,
    bool timedemo, qa_cvar_registry *cvars,
    void (*complete)(void *ctx, const char *reason, const qa_app_demo_timing *timing),
    void *complete_ctx);
void qa_app_recorded_source_destroy(qa_app_recorded_source *source);
const qa_app_recorded_demo_resource *qa_app_recorded_source_resource(const qa_app_recorded_source *source);
void qa_app_recorded_source_select_player(qa_app_recorded_source *source, double clientnum);
const char *qa_app_recorded_source_phase(const qa_app_recorded_source *source);
bool qa_app_recorded_source_timing(const qa_app_recorded_source *source, qa_app_demo_timing *out);
bool qa_app_recorded_source_is_paused(const qa_app_recorded_source *source);
void qa_app_recorded_source_set_paused(qa_app_recorded_source *source, bool paused);
void qa_app_recorded_source_advance(qa_app_recorded_source *source,
    double milliseconds, double frame, double real_time);
void qa_app_recorded_source_close(qa_app_recorded_source *source);
#endif
