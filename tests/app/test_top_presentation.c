/* C checks for the ported parts of src/app/bootstrap/presentation.ts and
 * presentation-scene.ts: seatViewport, the camera helpers, and
 * seatModelVisible (tests/console/q1-client-settings.test.ts). Expected
 * values come from running the TS module under bun on the same inputs.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/presentation_scene.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

static bool vec_eq(qa_vec3 v, double x, double y, double z) { return v.x == x && v.y == y && v.z == z; }
static bool camera_is(qa_scene_camera c, const double expected[12]) {
  return vec_eq(c.origin, expected[0], expected[1], expected[2]) && vec_eq(c.axis.vectors[0], expected[3], expected[4], expected[5])
      && vec_eq(c.axis.vectors[1], expected[6], expected[7], expected[8]) && vec_eq(c.axis.vectors[2], expected[9], expected[10], expected[11]);
}
static const double base_camera[12] = {1, 2, 3, 0.8067072629928589, 0.5648625493049622, -0.1736481785774231,
  -0.5837912559509277, 0.807354211807251, -0.0858316421508789, 0.09171250462532043, 0.1706153005361557, 0.981060266494751};

static void test_camera_helpers(void) {
  qa_scene_camera camera;
  memset(&camera, 0, sizeof camera);
  camera.origin = (qa_vec3){1, 2, 3};
  camera.axis = qa_angles_to_axis((qa_vec3){10, 35, -5});
  camera.viewport = (qa_rect){0, 0, 640, 480};
  CHECK(camera_is(camera, base_camera));
  static const double kicked[12] = {1, 2, 3, 0.8658457125151374, 0.45269755992479527, -0.21301643555206962,
    -0.4746836804890969, 0.8778347272238762, -0.0638880103486672, 0.15807128385189673, 0.15643259571049906, 0.9749575947703886};
  CHECK(camera_is(qa_app_camera_with_kick(camera, (qa_vec3){3, -7, 2.5}), kicked));
  CHECK(camera_is(qa_app_camera_with_kick(camera, (qa_vec3){0, 0, 0}), base_camera));
  qa_app_sim_player_view player;
  memset(&player, 0, sizeof player);
  player.origin = (qa_vec3){100, -50, 24};
  player.angles = (qa_vec3){-15, 90, 3};
  player.view_height = 22;
  player.has_client_view_offset_delta = true;
  player.client_view_offset_delta = (qa_vec3){0.5, -1, 2};
  player.foreign_character_death = true;
  static const double offset[12] = {1.5, 1, 5, 0.8067072629928589, 0.5648625493049622, -0.1736481785774231,
    -0.5837912559509277, 0.807354211807251, -0.0858316421508789, 0.09171250462532043, 0.1706153005361557, 0.981060266494751};
  CHECK(camera_is(qa_app_camera_with_client_offset(camera, &player), offset));
  static const double death[12] = {100, -50, 46, -4.222195926217864e-8, 0.9659258127212524, 0.258819043636322,
    -0.9986295104026794, -0.013545586727559566, 0.05055265501141548, 0.05233597010374069, -0.25846433639526367, 0.964601993560791};
  CHECK(camera_is(qa_app_camera_with_character_death(camera, &player), death));
  qa_scene_camera portal = camera;
  portal.clip_is_portal = true;
  CHECK(camera_is(qa_app_camera_with_client_offset(portal, &player), base_camera));
  CHECK(camera_is(qa_app_camera_with_character_death(portal, &player), base_camera));
  player.has_client_view_offset_delta = false; player.foreign_character_death = false;
  CHECK(camera_is(qa_app_camera_with_client_offset(camera, &player), base_camera));
  CHECK(camera_is(qa_app_camera_with_character_death(camera, &player), base_camera));
}

typedef struct layout { double index, count, width, height; } layout;
static void invalid_layout(void *arg) {
  layout *l = arg;
  (void)qa_app_seat_viewport(l->index, l->count, l->width, l->height);
}
static void test_seat_viewport(void) {
  static const struct { double index, count, width, height; qa_rect expected; } rows[] = {
    {0, 1, 1920, 1080, {0, 0, 1920, 1080}}, {1, 2, 1919, 1081, {0, 540, 1919, 541}}, {0, 2, 1919, 1081, {0, 0, 1919, 540}},
    {2, 3, 1001, 777, {0, 388, 500, 389}}, {3, 4, 1001, 777, {500, 388, 501, 389}}, {1, 4, 1001, 777, {500, 0, 501, 388}}};
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    qa_rect r = qa_app_seat_viewport(rows[i].index, rows[i].count, rows[i].width, rows[i].height);
    CHECK(r.x == rows[i].expected.x && r.y == rows[i].expected.y && r.width == rows[i].expected.width && r.height == rows[i].expected.height);
  }
  static const layout invalid[] = {{0, 0, 10, 10}, {4, 4, 10, 10}, {0.5, 2, 10, 10}, {-1, 2, 10, 10}, {0, 5, 10, 10}, {2, 2, 10, 10}};
  for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
    layout l = invalid[i];
    qa_caught_error caught;
    CHECK(!qa_try(invalid_layout, &l, &caught));
    CHECK(strcmp(caught.name, "RangeError") == 0 && strcmp(caught.message, "Invalid local seat layout") == 0);
  }
}

/* tests/console/q1-client-settings.test.ts: seat model visibility. */
static void test_seat_model_visible(void) {
  static int token;
  qa_session_id session = {"session:test", &token};
  qa_actor_id actor, other;
  memset(&actor, 0, sizeof actor); memset(&other, 0, sizeof other);
  actor.session = session; actor.slot = 1; actor.generation = 1;
  other.session = session; other.slot = 2; other.generation = 1;
  CHECK(qa_app_seat_model_visible(&actor, actor, true));
  CHECK(!qa_app_seat_model_visible(&actor, actor, false));
  CHECK(!qa_app_seat_model_visible(&actor, other, true));
  CHECK(qa_app_seat_model_visible(&actor, other, false));
  CHECK(!qa_app_seat_model_visible(NULL, actor, true));
  CHECK(qa_app_seat_model_visible(NULL, actor, false));
  CHECK(qa_app_seat_model_visible(&other, other, true));
}

int main(void) {
  test_camera_helpers();
  test_seat_viewport();
  test_seat_model_visible();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
