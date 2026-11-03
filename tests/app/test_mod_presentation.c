/* C checks for src/app/bootstrap/selected-q3-presentation.ts and
 * mod-presentation(s).ts: tests/app/selected-q3-presentation-collection.test.ts
 * row for row, with sentinel seat contexts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "qa/app/bootstrap/mod_presentation.h"
#include "qa/app/bootstrap/mod_presentations.h"
#include "qa/app/bootstrap/selected_q3_presentation.h"
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

/* Sentinel seat contexts: every seat callback checks it received its own. */
typedef struct seat_fixture { int sentinel; qa_actor_id actor; int calls; } seat_fixture;
static seat_fixture fixtures[4];
static bool bad_ctx;
static seat_fixture *seat_of(void *ctx) {
  seat_fixture *fixture = ctx;
  if (fixture < fixtures || fixture >= fixtures + 4 || fixture->sentinel != 0x5ea7) bad_ctx = true;
  fixture->calls++;
  return fixture;
}
static qa_actor_id seat_actor(void *ctx) { return seat_of(ctx)->actor; }
static qa_seat_id seat_id(void *ctx) { qa_seat_id id; memset(&id, 0, sizeof id); id.index = seat_of(ctx) - fixtures; return id; }
static qa_client_id seat_client(void *ctx) { qa_client_id id; memset(&id, 0, sizeof id); id.slot = seat_of(ctx) - fixtures; return id; }
static qa_scene_camera seat_camera(void *ctx) { qa_scene_camera camera; memset(&camera, 0, sizeof camera); (void)seat_of(ctx); return camera; }
static qa_rect seat_viewport(void *ctx) { (void)seat_of(ctx); return (qa_rect){0, 0, 320, 240}; }
static bool seat_split(void *ctx) { (void)seat_of(ctx); return false; }
static int64_t seat_bind(void *ctx, qa_app_component_effect_frame frame, const qa_app_component_overlay *overlay) {
  (void)seat_of(ctx); (void)frame; (void)overlay; return 1;
}
static void seat_unbind(void *ctx, int64_t binding) { (void)seat_of(ctx); (void)binding; }
static void seat_center(void *ctx, const char *text, double time, double duration) { (void)seat_of(ctx); (void)text; (void)time; (void)duration; }
static qa_app_viewing_seat seats_storage[4];
static const qa_app_viewing_seat *seats(size_t count) {
  static int token;
  for (size_t i = 0; i < count; i++) {
    fixtures[i].sentinel = 0x5ea7;
    memset(&fixtures[i].actor, 0, sizeof fixtures[i].actor);
    fixtures[i].actor.session.token = &token; fixtures[i].actor.slot = (int64_t)i + 1; fixtures[i].actor.generation = 1;
    seats_storage[i] = (qa_app_viewing_seat){&fixtures[i], seat_actor, seat_id, seat_client, seat_camera, seat_viewport,
        seat_split, seat_bind, seat_unbind, seat_center};
  }
  return seats_storage;
}
/* The collection only reads the options when it creates a seat consumer, which these rows never reach. */
static int options_sentinel;
static qa_app_selected_q3_presentation_options options(void) {
  qa_app_selected_q3_presentation_options o;
  memset(&o, 0, sizeof o);
  o.base.ctx = &options_sentinel;
  return o;
}

typedef struct run { qa_app_selected_q3_presentations *collection; const qa_app_viewing_seat *seats; size_t count;
  const qa_app_selected_q3_source *source; double frame; } run;
static void prepare_run(void *ctx) {
  run *r = ctx;
  qa_app_selected_q3_presentations_prepare(r->collection, r->seats, r->count, r->source, NULL, 0, r->frame);
}
static void retain_run(void *ctx) {
  run *r = ctx;
  qa_app_selected_q3_presentations_retain(r->collection, r->seats, r->count);
}
static bool contains(const char *text, const char *needle) { return strstr(text, needle) != NULL; }

static void test_no_source(void) {
  qa_app_selected_q3_presentation_options o = options();
  qa_app_selected_q3_presentations *collection = qa_app_selected_q3_presentations_create(&o);
  qa_app_selected_q3_presentations_prepare(collection, seats(2), 2, NULL, NULL, 0, 1);
  qa_app_selected_q3_presentations_prepare(collection, NULL, 0, NULL, NULL, 0, 2);
  qa_app_selected_q3_presentations_close(collection);
  qa_app_selected_q3_presentations_destroy(collection);
  CHECK(!bad_ctx);
}
static void test_seat_not_a_client(void) {
  qa_app_selected_q3_presentation_options o = options();
  qa_app_selected_q3_presentations *collection = qa_app_selected_q3_presentations_create(&o);
  qa_app_q3_source_presentation_state state;
  memset(&state, 0, sizeof state);
  qa_app_selected_q3_source source = {"content:q3", NULL, &state};
  fixtures[0].calls = fixtures[1].calls = 0;
  qa_app_selected_q3_presentations_prepare(collection, seats(2), 2, &source, NULL, 0, 1);
  CHECK(fixtures[0].calls == 1 && fixtures[1].calls == 1); /* each seat's actor was read once */
  qa_app_selected_q3_presentations_retain(collection, NULL, 0);
  qa_app_selected_q3_presentations_close(collection);
  qa_app_selected_q3_presentations_destroy(collection);
  CHECK(!bad_ctx);
}
static void test_closed_collection(void) {
  qa_app_selected_q3_presentation_options o = options();
  qa_app_selected_q3_presentations *collection = qa_app_selected_q3_presentations_create(&o);
  qa_app_selected_q3_presentations_close(collection);
  qa_app_selected_q3_presentations_close(collection);
  run retain = {collection, NULL, 0, NULL, 0};
  qa_caught_error caught;
  CHECK(!qa_try(retain_run, &retain, &caught) && contains(caught.message, "closed"));
  run prepare = {collection, NULL, 0, NULL, 1};
  CHECK(!qa_try(prepare_run, &prepare, &caught) && contains(caught.message, "closed or already preparing"));
  qa_app_selected_q3_presentations_destroy(collection);
}
static void test_failed_preparation(void) {
  qa_app_selected_q3_presentation_options o = options();
  qa_app_selected_q3_presentations *collection = qa_app_selected_q3_presentations_create(&o);
  qa_app_selected_q3_source broken = {"content:q3", NULL, NULL};
  run failing = {collection, seats(1), 1, &broken, 1};
  qa_caught_error caught;
  CHECK(!qa_try(prepare_run, &failing, &caught));
  qa_app_selected_q3_presentations_prepare(collection, NULL, 0, NULL, NULL, 0, 2);
  qa_app_selected_q3_presentations_close(collection);
  qa_app_selected_q3_presentations_destroy(collection);
  CHECK(!bad_ctx);
}

/* mod-presentations.ts: an empty published collection without input services. */
static void mod_print(void *ctx, const char *text) { (void)ctx; (void)text; }
static void test_mod_presentations_empty(void) {
  qa_app_mod_presentations_options o;
  memset(&o, 0, sizeof o);
  o.ctx = &options_sentinel; o.print = mod_print;
  qa_app_application_mod_presentations *collection = qa_app_application_mod_presentations_create(&o);
  CHECK(!qa_app_application_mod_presentations_pending_commands(collection));
  qa_app_application_mod_presentations_prepare(collection, seats(2), 2, NULL, 0, NULL, 0, 1);
  qa_app_application_mod_presentations_publish_restored(collection);
  qa_command_context source;
  memset(&source, 0, sizeof source);
  CHECK(qa_app_application_mod_presentations_command_cvars(collection, &source) == NULL);
  qa_save_value *checkpoint = qa_app_application_mod_presentations_capture_checkpoint(collection, seats(1), 1, NULL, 0);
  CHECK(checkpoint != NULL && qa_save_value_get(checkpoint, "seats") != NULL
        && qa_save_value_get(checkpoint, "seats")->item_count == 1);
  qa_save_value_free(checkpoint);
  qa_app_application_mod_presentations_retain_presentations(collection, NULL, 0);
  qa_app_application_mod_presentations_destroy(collection);
  CHECK(!bad_ctx);
}

int main(void) {
  test_no_source();
  test_seat_not_a_client();
  test_closed_collection();
  test_failed_preparation();
  test_mod_presentations_empty();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
