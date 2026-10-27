/*
 * Port of tests/movement/q3/movement.test.ts scene-free cases (weapon
 * stage order, command history window, shared-sweep slide cases) plus
 * exact numeric cases for view/postures/animation/jump-pad/move
 * validation. The retail-BSP case skips in TS without Q3 data and is
 * not ported. TypeScript toThrow cases run in a child process and
 * must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/movement/q3.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static bool vec_eq(qa_vec3 a, double x, double y, double z) {
  return a.x == x && a.y == y && a.z == z;
}

/* ---- weapon stage (TS movement.test.ts:23) ---- */
typedef struct weapon_log {
  double ammo[16];
  char order[16][32];
  int count;
} weapon_log;
static double ammo_get(void *ctx, double weapon) { return ((weapon_log *)ctx)->ammo[(int)weapon]; }
static void ammo_set(void *ctx, double weapon, double count) {
  weapon_log *log = ctx;
  log->ammo[(int)weapon] = count;
  snprintf(log->order[log->count++], 32, "ammo:%.0f", count);
}
static void weapon_event(void *ctx, double event) {
  weapon_log *log = ctx;
  snprintf(log->order[log->count++], 32, "event:%.0f", event);
}
static void weapon_torso(void *ctx, double animation) {
  weapon_log *log = ctx;
  snprintf(log->order[log->count++], 32, "torso:%.0f", animation);
}

static void test_weapon(void) {
  weapon_log log = {0};
  log.ammo[QA_Q3_WP_MACHINEGUN] = 2;
  qa_q3_source_weapon_state state = {0};
  state.ammo_ctx = &log; state.product = "baseq3";
  state.weapon = QA_Q3_WP_MACHINEGUN; state.weapon_state = QA_Q3_WEAPON_READY;
  state.owned_weapons = 1 << QA_Q3_WP_MACHINEGUN; state.health = 100; state.max_health = 100;
  state.ammo_get = ammo_get; state.ammo_set = ammo_set;
  qa_q3_source_weapon_options options = {0};
  options.ctx = &log; options.msec = 50; options.event = weapon_event; options.start_torso = weapon_torso;
  qa_q3_move_command cmd = {0};
  cmd.buttons = QA_Q3_BUTTON_ATTACK; cmd.weapon = QA_Q3_WP_MACHINEGUN;
  for (int i = 0; i < 5; i++) qa_q3_run_weapon_step(&state, cmd, &options);
  static const char *expected[] = {"torso:7", "ammo:1", "event:23", "torso:7",
                                   "ammo:0", "event:23", "torso:7", "event:21"};
  CHECK(log.count == 8);
  for (int i = 0; i < 8 && i < log.count; i++) CHECK(strcmp(log.order[i], expected[i]) == 0);
  CHECK(state.weapon_time == 500);
  CHECK(state.weapon_state == QA_Q3_WEAPON_FIRING);
  CHECK(qa_q3_weapon_delay(150, QA_Q3_PW_SCOUT, false) == 100);
  CHECK(qa_q3_weapon_delay(130, 0, true) == 100);
  CHECK(qa_q3_weapon_delay(77, 0, false) == 77);
}

/* ---- command history (TS movement.test.ts:42) ---- */
static void test_history(void) {
  qa_q3_command_history history;
  qa_q3_command_history_init(&history);
  qa_q3_user_command out;
  CHECK(qa_q3_command_history_read(&history, -63, &out) && out.server_time_milliseconds == 0);
  CHECK(!qa_q3_command_history_read(&history, -64, &out));
  for (int number = 1; number <= 65; number++) {
    qa_q3_user_command command = {0};
    command.server_time_milliseconds = number * 8; command.weapon = 2;
    qa_q3_command_history_append(&history, command);
  }
  CHECK(!qa_q3_command_history_read(&history, 1, &out));
  CHECK(qa_q3_command_history_read(&history, 2, &out) && out.server_time_milliseconds == 16);
  CHECK(qa_q3_command_history_read(&history, 65, &out) && out.server_time_milliseconds == 520);
  CHECK_ABORTS((void)qa_q3_command_history_read(&history, 66, &out));
  CHECK_ABORTS((void)qa_q3_command_history_read(&history, 1.5, &out));
}

/* ---- slide fixture (TS movement.test.ts:138) ---- */
typedef struct slide_fixture {
  qa_identity_owner owner;
  qa_q3_motion motion;
  qa_q3_slide_move_context context;
  qa_vec3 normal;
  double fraction;
  int traces, touches;
  bool drop_plane;
} slide_fixture;

static qa_trace_result fixture_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                                     qa_actor_id pass_actor, double mask) {
  (void)bounds; (void)pass_actor; (void)mask;
  slide_fixture *f = ctx;
  f->traces++;
  double step = f->traces == 1 ? f->fraction : 1;
  qa_trace_result result = {0};
  result.kind = "q3"; result.fraction = step;
  result.end = (qa_vec3){(double)(float)(start.x + (double)(float)((end.x - start.x) * step)),
                         (double)(float)(start.y + (double)(float)((end.y - start.y) * step)),
                         (double)(float)(start.z + (double)(float)((end.z - start.z) * step))};
  result.contact.has_plane = !f->drop_plane;
  result.contact.plane.normal = f->normal;
  result.source_plane.plane.normal = f->normal; result.source_plane.type = 3;
  result.hit.kind = step == 1 ? QA_TRACE_HIT_NONE : QA_TRACE_HIT_WORLD;
  result.contents = 1; result.has_surface_flags = true;
  return result;
}
static void fixture_touch(void *ctx, qa_trace_result trace) { (void)trace; ((slide_fixture *)ctx)->touches++; }
static void fixture_event(void *ctx, double event) { (void)ctx; (void)event; }

static void fixture_init(slide_fixture *f, qa_vec3 velocity, qa_vec3 normal, double fraction) {
  memset(f, 0, sizeof *f);
  qa_identity_owner_create(&f->owner, "q3-slide-policy");
  f->normal = normal; f->fraction = fraction;
  f->motion.velocity = velocity; f->motion.gravity = 800; f->motion.speed = 320;
  f->motion.viewheight = 26; f->motion.health = 100; f->motion.product = "baseq3";
  f->motion.actor = qa_identity_owner_actor(&f->owner, 1, 1);
  f->context.ctx = f; f->context.state = &f->motion; f->context.frame_time = 0.1;
  f->context.bounds = qa_q3_source_standing_bounds; f->context.mask = 1;
  f->context.trace = fixture_trace; f->context.touch = fixture_touch; f->context.event = fixture_event;
}

static void test_slide(void) {
  qa_vec3 wall = {-1, 0, 0};
  for (int i = 0; i < 2; i++) {
    double timer = i == 0 ? 0 : 10;
    slide_fixture f;
    fixture_init(&f, (qa_vec3){100, 0, 100}, wall, 0.5);
    f.motion.pm_time = timer;
    CHECK(qa_q3_slide_move(&f.context, true));
    qa_vec3 clipped = qa_q3_clip_velocity((qa_vec3){100, 0, 20}, wall, 1.001);
    if (timer == 0) CHECK(vec_eq(f.motion.velocity, clipped.x, clipped.y, clipped.z));
    else CHECK(vec_eq(f.motion.velocity, 100, 0, 20));
    CHECK(f.motion.origin.z == 6);
    CHECK(f.context.impact_speed == 100);
    CHECK(f.touches == 1);
    qa_identity_owner_destroy(&f.owner);
  }
  {
    slide_fixture f;
    qa_vec3 normal = {0, 0, 1};
    fixture_init(&f, (qa_vec3){5, 0, 0}, normal, 0.5);
    f.context.has_ground_normal = true; f.context.ground_normal = normal;
    CHECK(qa_q3_slide_move(&f.context, false));
    CHECK(vec_eq(f.motion.velocity, 5, 0, 1));
    CHECK(f.traces == 2);
    qa_identity_owner_destroy(&f.owner);
  }
  {
    slide_fixture f;
    fixture_init(&f, (qa_vec3){0, -1, 0}, (qa_vec3){-0.5, (double)(float)sqrt(0.75), 0}, 0.5);
    f.context.has_ground_normal = true; f.context.ground_normal = (qa_vec3){1, 0, 0};
    f.motion.pm_time = 10;
    CHECK(qa_q3_slide_move(&f.context, false));
    CHECK(vec_eq(f.motion.velocity, 0, 0, 0));
    CHECK(f.traces == 1);
    qa_identity_owner_destroy(&f.owner);
  }
  {
    slide_fixture f;
    fixture_init(&f, (qa_vec3){1, 0, 0}, wall, 0.5);
    f.drop_plane = true;
    /* TS records the touch before throwing; the C abort kills the
     * child, so only the abort itself is checked here. */
    slide_fixture *fp = &f;
    CHECK_ABORTS((void)qa_q3_slide_move(&fp->context, false));
    qa_identity_owner_destroy(&f.owner);
  }
}

/* ---- small exact cases ---- */
static void test_small(void) {
  /* constants / postures */
  CHECK(QA_Q3_EV_TAUNT_PATROL == 82 && QA_Q3_WP_CHAINGUN == 13 && QA_Q3_FLAG_INVULEXPAND == 16384);
  CHECK(qa_q3_source_standing_bounds.min.z == -24 && qa_q3_source_standing_bounds.max.z == 32);
  CHECK(qa_q3_source_postures.standing_view_height == 26);
  CHECK(qa_q3_source_postures.crouched.view_height == 12 && qa_q3_source_postures.dead.view_height == -16);
  qa_fixed_movement_pose pose = qa_q3_invulnerability_pose(true, qa_q3_source_postures);
  CHECK(pose.crouched && pose.bounds.max.x == 42 && pose.view_height == 12);
  pose = qa_q3_invulnerability_pose(false, qa_q3_source_postures);
  CHECK(pose.bounds.max.z == 16);

  /* view: pitch clamp at 16000 words -> 87.890625 degrees */
  static const double intermission[2] = {5, 6};
  qa_q3_view_angles_result view = qa_q3_view_angles((qa_vec3){20000, 16384, 0}, (qa_vec3){0, 0, 0},
                                                    (qa_vec3){1, 2, 3}, 100, 0, intermission, 2);
  CHECK(view.angles.x == 16000 * (360.0 / 65536.0));
  CHECK(view.angles.y == 90);
  CHECK(view.delta.x == -4000);
  view = qa_q3_view_angles((qa_vec3){1, 1, 1}, (qa_vec3){4, 5, 6}, (qa_vec3){7, 8, 9}, 0, 0, intermission, 2);
  CHECK(vec_eq(view.angles, 7, 8, 9) && vec_eq(view.delta, 4, 5, 6));

  /* prediction view */
  qa_q3_movement_state movement = {0};
  qa_q3_user_command command = {0};
  command.angle_words.values[1] = -16384;
  movement = qa_q3_update_prediction_view(movement, 100, command);
  CHECK(movement.view_angles.y == -90);

  /* clipVelocity exact: dot -100, backoff -100.1f staged */
  qa_vec3 clipped = qa_q3_clip_velocity((qa_vec3){100, 0, 0}, (qa_vec3){-1, 0, 0}, 1.001);
  CHECK(clipped.y == 0 && clipped.z == 0);
  CHECK(clipped.x == 100 - (double)(float)(-1 * (double)(float)(-100 * (double)(float)1.001)));

  /* animation legs flip bit */
  qa_q3_animation_context actx = {0};
  actx.animation.provider = "q3:character";
  actx.animation.state.kind = "q3"; actx.animation.state.legs = QA_Q3_ANIM_LEGS_IDLE;
  actx.product = "baseq3";
  qa_q3_animation_request legs_req = {QA_Q3_ANIM_LEGS, QA_Q3_ANIM_LEGS_RUN, false, 0};
  qa_animation_step_result anim = qa_q3_run_animation_operation(legs_req, actx);
  CHECK(anim.animation.state.legs == (128 | QA_Q3_ANIM_LEGS_RUN));
  CHECK(anim.effect_count == 1);
  free((void *)anim.effects);
  qa_q3_animation_context bad = actx;
  bad.animation.state.kind = "q1";
  CHECK_ABORTS((void)qa_q3_run_animation_operation(legs_req, bad));

  /* jump pad */
  qa_identity_owner owner;
  qa_identity_owner_create(&owner, "q3-jump-pad");
  qa_actor_id pad = qa_identity_owner_actor(&owner, 3, 1);
  qa_actor_id storage;
  qa_q3_movement_state state = {0};
  state.movement_frame = 7; state.predictable_event_sequence = 4;
  qa_q3_jump_pad_result touched = qa_q3_touch_jump_pad(state, pad, (qa_vec3){0, 0, 400}, false,
                                                       "q3:movement", &storage);
  CHECK(touched.has_event && touched.event.event == QA_Q3_EV_JUMP_PAD && touched.event.sequence == 4);
  CHECK(touched.state.predictable_event_sequence == 5);
  CHECK(touched.state.jump_pad_frame == 7 && touched.state.velocity.z == 400);
  qa_q3_movement_state finished = qa_q3_finish_jump_pad_prediction(touched.state);
  CHECK(finished.jump_pad != NULL);
  finished.movement_frame = 8;
  finished = qa_q3_finish_jump_pad_prediction(finished);
  CHECK(finished.jump_pad == NULL && finished.jump_pad_frame == 0);
  qa_identity_owner_destroy(&owner);

  /* movePlayer validation */
  qa_q3_motion motion = {0};
  motion.product = "baseq3";
  qa_q3_motion_options options = {0};
  options.standing_bounds = qa_q3_source_standing_bounds;
  options.postures = qa_q3_source_postures;
  qa_q3_move_command bad_cmd = {0};
  bad_cmd.server_time = 1.5;
  CHECK_ABORTS((void)qa_q3_move_player(&motion, bad_cmd, &options));
  bad_cmd.server_time = 10; bad_cmd.forwardmove = 128;
  CHECK_ABORTS((void)qa_q3_move_player(&motion, bad_cmd, &options));
  /* non-advancing command: no substep, current bounds reported */
  bad_cmd.forwardmove = 0; bad_cmd.server_time = 0;
  qa_q3_motion_result noop = qa_q3_move_player(&motion, bad_cmd, &options);
  CHECK(noop.bounds.min.z == -24 && noop.contact_count == 0);
  free(noop.contacts);
}

int main(void) {
  test_weapon();
  test_history();
  test_slide();
  test_small();
  if (failures != 0) {
    fprintf(stderr, "test_movement_q3: %d/%d checks failed\n", failures, checks);
    return 1;
  }
  printf("test_movement_q3: %d checks passed\n", checks);
  return 0;
}
