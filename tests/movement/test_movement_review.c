/*
 * Confirmation-gate review checks for src/movement. Each expected value comes
 * from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/movement/q3/move.h"
#include "qa/movement/q3/postures.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

typedef struct fixture {
  int point_traces;
  int ladder_probes;
  bool ladder_surface;
} fixture;

static qa_trace_result trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds, qa_actor_id pass, double mask) {
  (void)bounds;
  (void)pass;
  (void)mask;
  fixture *f = ctx;
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  bool same = start.x == end.x && start.y == end.y && start.z == end.z;
  bool horizontal = !same && start.z == end.z;
  if (same) f->point_traces++;
  if (horizontal) f->ladder_probes++;
  r.fraction = same || (horizontal && f->ladder_surface) ? 0 : 1;
  r.end = end;
  r.all_solid = same;
  r.start_solid = same;
  r.kind = "q3";
  r.surface_flags = horizontal && f->ladder_surface ? 8 : 0;
  return r;
}
static double contents(void *ctx, qa_vec3 point, qa_actor_id pass) {
  (void)ctx;
  (void)point;
  (void)pass;
  return 0;
}
static bool begin(void *ctx, qa_q3_motion *state, qa_q3_move_command *command, double msec, double substep) {
  (void)ctx;
  (void)state;
  (void)command;
  (void)msec;
  (void)substep;
  return true;
}
static void on_event(void *ctx, double event) {
  (void)ctx;
  (void)event;
}
static void on_animation(void *ctx, qa_q3_animation_request request) {
  (void)ctx;
  (void)request;
}
static bool on_weapon(void *ctx) {
  (void)ctx;
  return true;
}
static void on_torso(void *ctx) { (void)ctx; }
static bool on_firing(void *ctx) {
  (void)ctx;
  return false;
}
static void on_contact(void *ctx, qa_trace_result t) {
  (void)ctx;
  (void)t;
}

static qa_q3_motion_options options_for(fixture *f) {
  qa_q3_motion_options options;
  memset(&options, 0, sizeof options);
  options.ctx = f;
  options.trace = trace;
  options.point_contents = contents;
  options.standing_bounds = qa_q3_source_standing_bounds;
  options.has_current_bounds = true;
  options.current_bounds = qa_q3_source_postures.crouched.bounds;
  options.postures = qa_q3_source_postures;
  options.begin_step = begin;
  options.event = on_event;
  options.animation = on_animation;
  options.weapon = on_weapon;
  options.torso = on_torso;
  options.firing = on_firing;
  options.contact = on_contact;
  return options;
}
static qa_q3_motion motion(void) {
  qa_q3_motion m;
  memset(&m, 0, sizeof m);
  m.gravity = 800;
  m.speed = 320;
  m.health = 100;
  m.viewheight = 26;
  m.product = "baseq3";
  return m;
}

/* bun: a crouched hull that stands up with no client-output body hull takes the
 * standing posture unconditionally (bounds max.z 32, pmFlags 0, viewheight 26)
 * and never runs the expansion test (0 point traces). C ran movementBounds and
 * kept the crouched hull where the point trace was solid. */
static void test_check_duck_posture(void) {
  fixture f = {0, 0, false};
  qa_q3_motion_options options = options_for(&f);
  qa_q3_motion m = motion();
  qa_q3_move_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.server_time = 50;
  qa_q3_motion_result result = qa_q3_move_player(&m, cmd, &options);
  CHECK(result.bounds.max.z == 32 && result.bounds.min.z == -24);
  CHECK(m.pm_flags == 0 && m.viewheight == 26);
  CHECK(f.point_traces == 0);
  free(result.contacts);
}

/* TS `ladders === false` (pmove_ladders 0) skips the ladder probe entirely. */
static void test_ladders_disabled(void) {
  fixture f = {0, 0, true};
  qa_q3_motion_options options = options_for(&f);
  options.ladders_disabled = true;
  qa_q3_motion m = motion();
  qa_q3_move_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.server_time = 50;
  qa_q3_motion_result result = qa_q3_move_player(&m, cmd, &options);
  CHECK(f.ladder_probes == 0);
  free(result.contacts);
  fixture g = {0, 0, true};
  options = options_for(&g);
  m = motion();
  result = qa_q3_move_player(&m, cmd, &options);
  CHECK(g.ladder_probes > 0);
  free(result.contacts);
}

int main(void) {
  test_check_duck_posture();
  test_ladders_disabled();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
