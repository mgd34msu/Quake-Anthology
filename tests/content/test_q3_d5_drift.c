/*
 * C checks for the TS drift packet D5 (src/content/q3 since 53ad56d), ported from
 * tests/gameplay/q3/base/peer-input-regressions.test.ts (the four Q3 tests; the two
 * Q2 item tests belong to packet D4). Expected values are the TS assertions plus the
 * concrete values bun printed for the same calls on 2026-11-03 (TS 84aeef8d):
 *   swingAngles(angle 1e30)         -> {angle: 0, swinging: true}
 *   parsePlayerAnimationConfig(...)  -> TextParseError "<animation.cfg>:5:1: ..." and ":2:1: ..."
 *   ShaderRemapRegistry.add(80 x's)  -> newName length 63
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q3/base/game/shader_remaps.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/foundation/animation_config.h"
#include "qa/content/q3/foundation/player_pose.h"
#include "qa/content/q3/team_arena/client_policy.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/support.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                   \
  } while (0)

static void test_swing_hostile_pitch(void) {
  /* TS: a hostile pitch magnitude no longer hangs the player pose swing. */
  qa_q3_swing_angles_input input = {0, 15, 30, 0.1, 50, 1e30, false};
  qa_q3_swing_angles_result result = qa_q3_swing_angles(input);
  CHECK(isfinite(result.angle));
  CHECK(result.angle == 0 && result.swinging);
}

typedef struct parse_job { const char *text; } parse_job;
static void parse_body(void *ctx) {
  qa_q3_player_animation_config config;
  qa_q3_parse_player_animation_config(((parse_job *)ctx)->text, NULL, NULL, &config);
  qa_q3_player_animation_config_destroy(&config);
}
static void test_animation_parse_error_class(void) {
  /* TS: an animation.cfg with no frame numbers reports the parser's own error class. */
  static const char *const texts[] = {"footsteps boot\nsex m\n", "// comment only\n"};
  static const char *const messages[] = {
      "<animation.cfg>:5:1: animation prelude reached the source nonprogress cycle",
      "<animation.cfg>:2:1: animation prelude reached the source nonprogress cycle"};
  for (size_t i = 0; i < 2; i++) {
    parse_job job = {texts[i]};
    qa_caught_error caught;
    CHECK(!qa_try(parse_body, &job, &caught));
    CHECK(strcmp(caught.name, "TextParseError") == 0);
    CHECK(strcmp(caught.message, messages[i]) == 0);
  }
}

static void test_shader_remap_cut(void) {
  /* TS: shader remap names of 64 or more bytes are cut to fit the source slot. */
  qa_q3_shader_remap_registry remaps;
  qa_q3_shader_remap_registry_init(&remaps, NULL, NULL);
  char name[81];
  memset(name, 'x', 80);
  name[80] = '\0';
  qa_q3_shader_remap_registry_add(&remaps, "textures/a", name, 0);
  qa_q3_shader_remap_state state = qa_q3_shader_remap_registry_capture(&remaps);
  CHECK(state.count == 1 && strlen(state.remaps[0].new_name) == 63);
  qa_q3_shader_remap_state_destroy(&state);
  qa_q3_shader_remap_registry_destroy(&remaps);
}

static qa_q3_game_client *followed_target;
static qa_q3_game_client *client_at(void *ctx, double number) {
  CHECK(ctx == (void *)&followed_target);
  if (number != 1) qa_range_error("Missing client");
  return followed_target;
}
static void test_snapshot_view_source(void) {
  /* TS: a follower's snapshot takes position and stores from the followed client. */
  qa_q3_game_client spectator, target;
  CHECK(qa_q3_game_client_init(&spectator, QA_Q3_BASE_PRODUCT_BASEQ3));
  CHECK(qa_q3_game_client_init(&target, QA_Q3_BASE_PRODUCT_BASEQ3));
  followed_target = &target;
  spectator.ps.client_num = 1;
  CHECK(qa_q3_ta_snapshot_view_source(&spectator.ps, client_at, &followed_target) == &spectator.ps);
  spectator.ps.pm_flags = (double)((int)spectator.ps.pm_flags | QA_Q3_FLAG_FOLLOW);
  CHECK(qa_q3_ta_snapshot_view_source(&spectator.ps, client_at, &followed_target) == &target.ps);
  qa_q3_game_client_destroy(&spectator);
  qa_q3_game_client_destroy(&target);
}

int main(void) {
  test_swing_hostile_pitch();
  test_animation_parse_error_class();
  test_shader_remap_cut();
  test_snapshot_view_source();
  printf("test_q3_d5_drift: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
