/*
 * Port of ../quake-anthology-ts/tests/network/q3/demo-playback.test.ts over
 * qa_app_q3_demo_playback. The TS spyOn(clock, "advance") call counts have no C
 * observation point and are not reproduced; every returned frame, phase,
 * snapshot, timing and reader-offset expectation is. The TS fixture publishes the
 * snapshot object it receives to the clock (GC keeps it alive); the C fixture
 * publishes its own retained copy, since the clock borrows the pointer.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/q3_client.h"
#include "qa/app/bootstrap/network/q3_demo.h"
#include "qa/app/bootstrap/network/recorded_source.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/network/q3.h"

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
#define MAGIC 0x2d94e1c7u
#define FIX(ctx) ((demo_fixture *)sentinel((ctx)))
static void *sentinel(void *ctx) {
  CHECK(ctx != NULL && *(unsigned *)ctx == MAGIC);
  return ctx;
}

/* ---- recording builder: TS recording(operations) ---- */
typedef enum { OP_GAMESTATE, OP_SNAPSHOT, OP_NOP, OP_COMMAND } op_kind;
typedef struct op_spec { op_kind kind; int64_t time; const char *text; } op_spec;
#define END_RECORD { -1, 0, NULL }
static uint8_t *recording(const op_spec *const *records, size_t record_count, size_t *out_len) {
  qa_q3_demo_message messages[8] = {{0}};
  uint8_t *payloads[8] = {0};
  qa_q3_gamestate_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.kind = QA_Q3_GS_CONFIGSTRING; entry.index = 0; entry.value = (char *)"\\protocol\\68\\mapname\\q3dm1\\g_gametype\\0";
  for (size_t r = 0; r < record_count; r++) {
    qa_q3_server_operation ops[4];
    size_t count = 0;
    memset(ops, 0, sizeof ops);
    for (const op_spec *spec = records[r]; (int)spec->kind != -1; spec++, count++) {
      qa_q3_server_operation *op = &ops[count];
      switch (spec->kind) {
        case OP_GAMESTATE:
          op->kind = QA_Q3_OP_GAMESTATE; op->gamestate.checksum_feed = 12;
          op->gamestate.entries = &entry; op->gamestate.entry_count = 1;
          break;
        case OP_SNAPSHOT:
          op->kind = QA_Q3_OP_SNAPSHOT; op->validity.kind = QA_Q3_SNAP_VALID;
          op->snapshot.message_number = (int64_t)r + 1; op->snapshot.server_time = spec->time;
          op->snapshot.delta_number = -1; op->snapshot.area_mask_len = 32;
          qa_q3_player_state_record_init(&op->snapshot.player_state, QA_Q3_PRODUCT_BASEQ3, 0, 0, 0);
          break;
        case OP_NOP: op->kind = QA_Q3_OP_NOP; break;
        case OP_COMMAND: op->kind = QA_Q3_OP_COMMAND; op->sequence = 1; op->text = (char *)spec->text; break;
      }
    }
    qa_q3_server_message_context context;
    memset(&context, 0, sizeof context);
    context.product = QA_Q3_PRODUCT_BASEQ3; context.message_number = (int64_t)r + 1;
    size_t len = 0;
    CHECK(qa_q3_encode_server_message(0, ops, count, &context, NULL, &payloads[r], &len));
    messages[r].sequence = (int64_t)r + 1; messages[r].payload = payloads[r]; messages[r].payload_len = len;
  }
  uint8_t *bytes = NULL;
  CHECK(qa_q3_encode_demo(messages, record_count, &bytes, out_len));
  for (size_t r = 0; r < record_count; r++) free(payloads[r]);
  return bytes;
}

/* ---- fixture ---- */
typedef struct demo_fixture {
  unsigned magic;
  qa_identity_owner identity;
  qa_q3_client_clock *clock;
  qa_q3_demo_reader *reader;
  qa_app_q3_demo_playback *playback;
  qa_q3_wire_snapshot snapshots[16]; size_t snapshot_count;
  bool retire_in_gamestate, overlap_rejected;
} demo_fixture;
static bool h_false(void *ctx) { sentinel(ctx); return false; }
static const char *h_userinfo(void *ctx) { sentinel(ctx); return ""; }
static void h_system_info(void *ctx, const char *info) { sentinel(ctx); (void)info; }
static void h_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, double ping) {
  demo_fixture *f = FIX(ctx);
  (void)ping;
  if (f->snapshot_count >= 16) { CHECK(!"too many snapshots"); return; }
  qa_q3_wire_snapshot *kept = &f->snapshots[f->snapshot_count++];
  CHECK(qa_q3_wire_snapshot_copy(kept, snapshot));
  qa_q3_client_clock_publish(f->clock, kept);
}
static void h_thunk(void *ctx) { sentinel(ctx); }
static void h_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static double h_download_size(void *ctx, double size) { sentinel(ctx); (void)size; CHECK(!"Unexpected download"); return 0; }
static void h_download(void *ctx, const qa_q3_download_block *block) { sentinel(ctx); (void)block; CHECK(!"Unexpected download"); }
static void h_clear(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx) {
  (void)assert_current; (void)assert_ctx;
  qa_q3_client_clock_clear(FIX(ctx)->clock);
}
static const qa_app_recorded_q3_options FRAME = { false, 0, 1, false, false };
typedef struct frame_scope { demo_fixture *f; double time; qa_app_recorded_q3_options options; qa_app_q3_demo_frame frame; } frame_scope;
static void frame_body(void *ctx) {
  frame_scope *scope = ctx;
  scope->frame = qa_app_q3_demo_playback_advance_frame(scope->f->playback, scope->time, scope->options);
}
static void h_gamestate(void *ctx, const qa_q3_gamestate *state, double generation, qa_app_q3_assert_fn assert_current, void *assert_ctx) {
  demo_fixture *f = FIX(ctx);
  (void)state; (void)generation; (void)assert_current; (void)assert_ctx;
  if (!f->retire_in_gamestate) return;
  /* TS: an overlapping advanceFrame rejects, then the owner closes while loading. */
  frame_scope scope = { f, 0, FRAME, { 0 } };
  qa_caught_error caught;
  f->overlap_rejected = !qa_try(frame_body, &scope, &caught) && strstr(caught.message, "already in progress") != NULL;
  qa_app_q3_demo_playback_close(f->playback);
}
static void h_attach(void *ctx, qa_q3_client_connection *connection) { sentinel(ctx); (void)connection; }
static qa_q3_wire_user_command h_command(void *ctx, const void *command) {
  sentinel(ctx); (void)command;
  CHECK(!"Demo must not sample physical input");
  qa_q3_wire_user_command none;
  memset(&none, 0, sizeof none);
  return none;
}
static void h_disconnected(void *ctx, const char *reason) { sentinel(ctx); (void)reason; CHECK(!"Demo completion is not a network disconnect"); }
static demo_fixture *fixture(const uint8_t *bytes, size_t len, bool retire_in_gamestate) {
  demo_fixture *f = calloc(1, sizeof *f);
  f->magic = MAGIC;
  f->retire_in_gamestate = retire_in_gamestate;
  CHECK(qa_identity_owner_create(&f->identity, "Q3 recorded client"));
  CHECK(qa_q3_client_clock_init(&f->clock));
  CHECK(qa_q3_demo_reader_init(&f->reader, bytes, len, NULL));
  qa_app_q3_client_host host;
  memset(&host, 0, sizeof host);
  host.ctx = f;
  host.identity.client = qa_identity_owner_client(&f->identity, 0, 0);
  host.identity.has_seat = true; host.identity.seat = qa_identity_owner_seat(&f->identity, 0);
  host.downloading = h_false; host.userinfo = h_userinfo; host.system_info = h_system_info; host.snapshot = h_snapshot;
  host.map_restart = h_thunk; host.print = h_print; host.download_size = h_download_size; host.download = h_download;
  host.clear_active = h_clear; host.gamestate = h_gamestate; host.attach = h_attach; host.command = h_command;
  host.disconnected = h_disconnected;
  qa_app_q3_demo_playback_options options = { host, f->clock, f->reader };
  f->playback = qa_app_q3_demo_playback_create(options);
  CHECK(f->playback != NULL);
  return f;
}
static void fixture_destroy(demo_fixture *f) {
  qa_app_q3_demo_playback_destroy(f->playback);
  qa_q3_demo_reader_destroy(f->reader);
  qa_q3_client_clock_destroy(f->clock);
  for (size_t i = 0; i < f->snapshot_count; i++) qa_q3_wire_snapshot_destroy(&f->snapshots[i]);
  qa_identity_owner_destroy(&f->identity);
  free(f);
}
static qa_app_q3_demo_frame advance(demo_fixture *f, double time, qa_app_recorded_q3_options options) {
  return qa_app_q3_demo_playback_advance_frame(f->playback, time, options);
}
static bool is_pending(qa_app_q3_demo_frame frame) { return strcmp(frame.kind, "pending") == 0; }
static bool is_frame(qa_app_q3_demo_frame frame, double time) { return strcmp(frame.kind, "frame") == 0 && frame.server_time == time; }
static bool phase_is(demo_fixture *f, const char *phase) { return strcmp(qa_app_q3_demo_playback_phase(f->playback), phase) == 0; }
static int64_t latest_time(demo_fixture *f) {
  qa_q3_wire_snapshot latest;
  if (!qa_q3_snapshot_history_latest(qa_q3_client_connection_history(qa_app_q3_demo_playback_connection(f->playback)), &latest)) return -1;
  int64_t time = latest.server_time;
  qa_q3_wire_snapshot_destroy(&latest);
  return time;
}

static const op_spec GS[] = { { OP_GAMESTATE, 0, NULL }, END_RECORD };
static const op_spec NOP[] = { { OP_NOP, 0, NULL }, END_RECORD };
#define SNAP(name, t) static const op_spec name[] = { { OP_SNAPSHOT, t, NULL }, END_RECORD }
SNAP(S1000, 1000); SNAP(S1025, 1025); SNAP(S1075, 1075); SNAP(S1100, 1100); SNAP(S1150, 1150);
SNAP(S1200, 1200); SNAP(S2100, 2100); SNAP(S2200, 2200);
static const op_spec GS_S1000[] = { { OP_GAMESTATE, 0, NULL }, { OP_SNAPSHOT, 1000, NULL }, END_RECORD };
static const op_spec GS_S2000[] = { { OP_GAMESTATE, 0, NULL }, { OP_SNAPSHOT, 2000, NULL }, END_RECORD };
static const op_spec GS_DISCONNECT[] = { { OP_GAMESTATE, 0, NULL }, { OP_COMMAND, 0, "disconnect \"recorded reason\"" }, END_RECORD };

static void test_primes_and_brackets(void) {
  const op_spec *records[] = { GS, S1000, NOP, S1025, S1075, S1150 };
  size_t len = 0;
  uint8_t *bytes = recording(records, 6, &len);
  demo_fixture *f = fixture(bytes, len, false);
  qa_app_recorded_q3_options timedemo = FRAME;
  timedemo.timedemo = true;
  CHECK(qa_app_q3_demo_playback_prime(f->playback, 5000) == NULL);
  CHECK(phase_is(f, "primed"));
  qa_q3_client_connection *connection = qa_app_q3_demo_playback_connection(f->playback);
  CHECK(qa_q3_client_connection_channel(connection) == NULL);
  CHECK(qa_q3_client_connection_get_mode(connection)->kind == QA_Q3_CLIENT_MODE_DEMO);
  CHECK(f->snapshot_count == 0);
  CHECK(is_pending(advance(f, 5000, timedemo)));
  CHECK(is_frame(advance(f, 5010, timedemo), 1050));
  CHECK(f->snapshot_count == 3 && f->snapshots[0].server_time == 1000 && f->snapshots[1].server_time == 1025
        && f->snapshots[2].server_time == 1075);
  int64_t frames = -1, elapsed = -1;
  CHECK(qa_q3_client_clock_demo_timing(f->clock, 5020, &frames, &elapsed) && frames == 1 && elapsed == 10);
  CHECK(is_frame(advance(f, 5020, timedemo), 1100));
  CHECK(f->snapshot_count >= 1 && f->snapshots[f->snapshot_count - 1].server_time == 1150);
  qa_app_q3_demo_frame end = advance(f, 5030, timedemo);
  CHECK(strcmp(end.kind, "end") == 0);
  CHECK(end.has_end && end.end.reason == QA_Q3_DEMO_TERMINATOR);
  CHECK(end.has_timing && end.timing.frames == 3 && end.timing.elapsed_milliseconds == 20);
  CHECK(phase_is(f, "ended"));
  CHECK(qa_q3_wire_command_history_current_number(qa_q3_client_connection_commands(connection)) == 0);
  qa_app_q3_demo_playback_close(f->playback);
  fixture_destroy(f);
  free(bytes);
}

static void test_combined_initial_gamestate(void) {
  for (int timedemo = 0; timedemo < 2; timedemo++) {
    const op_spec *records[] = { GS_S1000, S1100, S1200 };
    size_t len = 0;
    uint8_t *bytes = recording(records, 3, &len);
    demo_fixture *f = fixture(bytes, len, false);
    qa_app_recorded_q3_options options = FRAME;
    options.timedemo = timedemo != 0;
    qa_app_q3_demo_playback_prime(f->playback, 100);
    CHECK(phase_is(f, "primed"));
    CHECK(is_pending(advance(f, 100, options)));
    CHECK(is_frame(advance(f, 150, options), timedemo ? 1150 : 1100));
    CHECK(latest_time(f) == 1200);
    qa_app_q3_demo_playback_close(f->playback);
    fixture_destroy(f);
    free(bytes);
  }
}

static void test_later_gamestate_resets(void) {
  const op_spec *records[] = { GS, S1000, S1100, GS_S2000, S2100, S2200 };
  size_t len = 0;
  uint8_t *bytes = recording(records, 6, &len);
  demo_fixture *f = fixture(bytes, len, false);
  qa_app_q3_demo_playback_prime(f->playback, 100);
  advance(f, 100, FRAME);
  CHECK(is_frame(advance(f, 150, FRAME), 1000));
  CHECK(is_pending(advance(f, 250, FRAME)));
  CHECK(phase_is(f, "primed"));
  CHECK(is_frame(advance(f, 300, FRAME), 2100));
  CHECK(latest_time(f) == 2200);
  qa_app_q3_demo_playback_close(f->playback);
  fixture_destroy(f);
  free(bytes);
}

static void test_frozen_frames(void) {
  const op_spec *records[] = { GS, S1000, S1100, S1200 };
  size_t len = 0;
  uint8_t *bytes = recording(records, 4, &len);
  demo_fixture *f = fixture(bytes, len, false);
  qa_app_q3_demo_playback_prime(f->playback, 100);
  advance(f, 100, FRAME);
  CHECK(is_frame(advance(f, 150, FRAME), 1000));
  int64_t offset = qa_q3_demo_reader_offset(f->reader);
  qa_app_recorded_q3_options frozen = FRAME;
  frozen.freeze_demo = true;
  CHECK(is_frame(advance(f, 250, frozen), 1000));
  CHECK(qa_q3_demo_reader_offset(f->reader) == offset);
  CHECK(is_frame(advance(f, 250, FRAME), 1100));
  CHECK(qa_q3_demo_reader_offset(f->reader) > offset);
  qa_app_q3_demo_playback_close(f->playback);
  fixture_destroy(f);
  free(bytes);
}

typedef struct playback_scope { demo_fixture *f; double time; } playback_scope;
static void advance_body(void *ctx) {
  playback_scope *scope = ctx;
  qa_app_q3_demo_playback_advance_frame(scope->f->playback, scope->time, FRAME);
}
static void prime_body(void *ctx) {
  playback_scope *scope = ctx;
  qa_app_q3_demo_playback_prime(scope->f->playback, scope->time);
}
static void test_eof_and_truncation(void) {
  static const uint8_t empty[1] = { 0 };
  static const uint8_t header[4] = { 7, 0, 0, 0 };
  static const uint8_t payload[9] = { 8, 0, 0, 0, 4, 0, 0, 0, 1 };
  const struct { const uint8_t *bytes; size_t len; qa_q3_demo_end_reason reason; int64_t sequence; } cases[3] = {
    { empty, 0, QA_Q3_DEMO_EOF, 0 }, { header, 4, QA_Q3_DEMO_TRUNCATED_HEADER, 7 }, { payload, 9, QA_Q3_DEMO_TRUNCATED_PAYLOAD, 8 },
  };
  for (int i = 0; i < 3; i++) {
    demo_fixture *f = fixture(cases[i].bytes, cases[i].len, false);
    const qa_q3_demo_result *end = qa_app_q3_demo_playback_prime(f->playback, 0);
    CHECK(end != NULL && end->kind == QA_Q3_DEMO_END && end->reason == cases[i].reason && end->offset == 0);
    CHECK(qa_q3_client_connection_server_message_sequence(qa_app_q3_demo_playback_connection(f->playback)) == cases[i].sequence);
    qa_app_q3_demo_playback_close(f->playback);
    playback_scope scope = { f, 0 };
    qa_caught_error caught;
    CHECK(!qa_try(advance_body, &scope, &caught) && strstr(caught.message, "retired") != NULL);
    fixture_destroy(f);
  }
}

static void test_retirement_during_gamestate(void) {
  const op_spec *records[] = { GS, S1000 };
  size_t len = 0;
  uint8_t *bytes = recording(records, 2, &len);
  demo_fixture *f = fixture(bytes, len, true);
  playback_scope scope = { f, 0 };
  qa_caught_error caught;
  CHECK(!qa_try(prime_body, &scope, &caught) && strstr(caught.message, "retired") != NULL);
  CHECK(f->overlap_rejected);
  CHECK(phase_is(f, "closed"));
  fixture_destroy(f);
  free(bytes);
}

typedef struct command_scope { qa_q3_client_connection *connection; } command_scope;
static void command_body(void *ctx) {
  char **argv = NULL; size_t argc = 0;
  if (qa_q3_client_connection_get_server_command(((command_scope *)ctx)->connection, 1, &argv, &argc)) {
    for (size_t i = 0; i < argc; i++) free(argv[i]);
    free(argv);
  }
}
static void test_recorded_disconnect(void) {
  const op_spec *records[] = { GS_DISCONNECT };
  size_t len = 0;
  uint8_t *bytes = recording(records, 1, &len);
  demo_fixture *f = fixture(bytes, len, false);
  qa_app_q3_demo_playback_prime(f->playback, 0);
  command_scope scope = { qa_app_q3_demo_playback_connection(f->playback) };
  qa_caught_error caught;
  CHECK(!qa_try(command_body, &scope, &caught) && strstr(caught.message, "recorded reason") != NULL);
  CHECK(phase_is(f, "primed"));
  qa_app_q3_demo_playback_close(f->playback);
  fixture_destroy(f);
  free(bytes);
}

int main(void) {
  test_primes_and_brackets();
  test_combined_initial_gamestate();
  test_later_gamestate_resets();
  test_frozen_frames();
  test_eof_and_truncation();
  test_retirement_during_gamestate();
  test_recorded_disconnect();
  printf("test_q3_demo: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
