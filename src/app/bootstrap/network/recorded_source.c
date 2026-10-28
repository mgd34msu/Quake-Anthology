/* Port of src/app/bootstrap/network/recorded-source.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/recorded_source.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
/* JS Number::toFixed via printf %.Nf matches for these magnitudes;
 * frames use JS number->string (integers print without fraction). */
char *qa_app_demo_timing_text(qa_app_demo_timing timing) {
  double seconds = timing.elapsed_milliseconds / 1000;
  double fps = seconds > 0 ? timing.frames / seconds : 0;
  char *text = malloc(128);
  if (!text) return NULL;
  if (timing.frames == trunc(timing.frames) && fabs(timing.frames) < 1e21)
    snprintf(text, 128, "%.0f frames, %.3f seconds: %.1f fps\n", timing.frames, seconds, fps);
  else
    snprintf(text, 128, "%g frames, %.3f seconds: %.1f fps\n", timing.frames, seconds, fps);
  return text;
}
struct qa_app_recorded_source {
  qa_app_demo_resource resource;
  qa_app_recorded_playback playback;
  bool timedemo; qa_cvar_registry *cvars;
  void (*complete)(void *, const char *, const qa_app_demo_timing *); void *complete_ctx;
  bool has_milliseconds; double milliseconds;
  const char *state; const char *terminal; bool reported, paused;
  bool has_previous_real_time; double previous_real_time, paused_milliseconds;
  bool has_benchmark_start; double benchmark_start, benchmark_frames;
  bool has_result_timing; qa_app_demo_timing result_timing;
};
qa_app_recorded_source *qa_app_recorded_source_create(qa_app_demo_resource resource,
    qa_app_recorded_family remote_family, qa_app_recorded_playback playback,
    bool timedemo, qa_cvar_registry *cvars,
    void (*complete)(void *ctx, const char *reason, const qa_app_demo_timing *timing),
    void *complete_ctx) {
  if (resource.kind != remote_family || playback.family != resource.kind)
    qa_range_error("Recording and remote presentation families differ");
  qa_app_recorded_source *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->resource = resource; s->playback = playback; s->timedemo = timedemo;
  s->cvars = cvars; s->complete = complete; s->complete_ctx = complete_ctx;
  s->state = "loading";
  return s;
}
void qa_app_recorded_source_destroy(qa_app_recorded_source *source) { free(source); }
const qa_app_demo_resource *qa_app_recorded_source_resource(const qa_app_recorded_source *source) {
  return &source->resource;
}
void qa_app_recorded_source_select_player(qa_app_recorded_source *source, double clientnum) {
  if (source->playback.family != QA_APP_REC_Q2)
    qa_range_error("View selection requires a multiview Q2 recording");
  source->playback.select_player_q2(source->playback.ctx, clientnum);
}
const char *qa_app_recorded_source_phase(const qa_app_recorded_source *source) { return source->state; }
bool qa_app_recorded_source_timing(const qa_app_recorded_source *source, qa_app_demo_timing *out) {
  if (!source->has_result_timing) return false;
  *out = source->result_timing; return true;
}
bool qa_app_recorded_source_is_paused(const qa_app_recorded_source *source) { return source->paused; }
void qa_app_recorded_source_set_paused(qa_app_recorded_source *source, bool paused) { source->paused = paused; }
void qa_app_recorded_source_advance(qa_app_recorded_source *s,
    double milliseconds, double frame, double real_time) {
  if (strcmp(s->state, "closed") == 0) return;
  if (s->paused && s->has_previous_real_time)
    s->paused_milliseconds += fmax(0, real_time - s->previous_real_time);
  s->previous_real_time = real_time; s->has_previous_real_time = true;
  if (s->paused) return;
  real_time -= s->paused_milliseconds;
  bool timedemo = s->timedemo || qa_cvar_registry_variable_value(s->cvars, "timedemo") != 0;
  qa_app_recorded_family family = s->playback.family;
  if (family == QA_APP_REC_Q1 || family == QA_APP_REC_QW) {
    qa_app_recorded_q1_result result = s->playback.advance_q1(
        s->playback.ctx, milliseconds / 1000, frame, timedemo);
    if (strcmp(result.phase, "ended") == 0)
      s->terminal = strcmp(result.reason, "recorded-disconnect") == 0 ? "disconnected" : result.reason;
    else s->state = result.phase;
  } else if (family == QA_APP_REC_Q2) {
    double target = s->has_milliseconds ? s->milliseconds + milliseconds : 0;
    qa_app_recorded_q2_result result = timedemo
        ? s->playback.next_frame_q2(s->playback.ctx)
        : s->playback.advance_q2(s->playback.ctx, target);
    if (strcmp(result.kind, "frame") == 0) {
      s->milliseconds = !s->has_milliseconds || timedemo || result.time_milliseconds < target
          ? result.time_milliseconds : target;
      s->has_milliseconds = true;
      s->playback.sample_demo_q2(s->playback.ctx, s->milliseconds);
      s->state = "active";
    } else s->terminal = result.kind;
  } else {
    qa_app_recorded_q3_options options = {false,
      qa_cvar_registry_variable_value(s->cvars, "cl_timeNudge"),
      qa_cvar_registry_variable_value(s->cvars, "timescale"),
      qa_cvar_registry_variable_value(s->cvars, "cl_freezeDemo") != 0, timedemo};
    qa_app_recorded_q3_result result = s->playback.advance_frame_q3(s->playback.ctx, real_time, options);
    if (strcmp(result.kind, "end") == 0) {
      s->terminal = strcmp(result.end_reason, "truncated-header") == 0
          || strcmp(result.end_reason, "truncated-payload") == 0 ? "truncated" : result.end_reason;
      s->result_timing = result.timing; s->has_result_timing = true;
    } else if (strcmp(result.kind, "frame") == 0) {
      s->playback.sample_presentation_q3(s->playback.ctx, real_time);
      s->state = "active";
    } else {
      s->state = strcmp(s->playback.phase_q3(s->playback.ctx), "active") == 0 ? "active" : "loading";
    }
  }
  if (timedemo && family != QA_APP_REC_Q3) {
    if (!s->terminal && strcmp(s->state, "active") == 0) {
      if (!s->has_benchmark_start) { s->benchmark_start = real_time; s->has_benchmark_start = true; }
      else s->benchmark_frames++;
    } else if (s->terminal && s->has_benchmark_start) {
      s->result_timing.frames = s->benchmark_frames;
      s->result_timing.elapsed_milliseconds = fmax(0, real_time - s->benchmark_start);
      s->has_result_timing = true;
    }
  }
  if (s->terminal && !s->reported) {
    s->reported = true; s->state = "closed";
    s->complete(s->complete_ctx, s->terminal, s->has_result_timing ? &s->result_timing : NULL);
  }
}
void qa_app_recorded_source_close(qa_app_recorded_source *source) {
  source->state = "closed";
  source->playback.close(source->playback.ctx);
}
