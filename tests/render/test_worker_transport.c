/*
 * Port of tests/render/worker-transport.test.ts and
 * tests/render/fixtures/transport-worker.ts against the threaded qa_rwt_*
 * transport. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "qa/render/worker_transport.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void sleep_ms(long ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000}; nanosleep(&t, NULL); }
static qa_rwt_value text(const char *value) { qa_rwt_value v = {value, strlen(value) + 1, NULL}; return v; }
static bool equal_text(qa_rwt_value v, const char *value) { return v.size == strlen(value) + 1 && memcmp(v.bytes, value, v.size) == 0; }

static int initialize_sentinel;
/* fixtures/transport-worker.ts */
typedef struct fixture { qa_rwt_worker *worker; char *description; size_t description_size; unsigned char values[4096]; size_t values_size; int close_calls; bool close_fails_once; } fixture;
static bool fixture_dispatch(void *ctx, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_failure *failure) {
  fixture *f = (fixture *)ctx;
  if (equal_text(payload, "outer")) return qa_rwt_worker_request(f->worker, text("outer-callback"), out, failure);
  if (equal_text(payload, "inner")) return qa_rwt_worker_request(f->worker, text("inner-callback"), out, failure);
  if (equal_text(payload, "leaf")) { *out = text("leaf-result"); return true; }
  if (equal_text(payload, "failure")) { qa_rwt_failure_set(failure, "RangeError", "worker failure"); return false; }
  if (equal_text(payload, "slow")) sleep_ms(120);
  if (equal_text(payload, "values")) { out->bytes = f->values; out->size = f->values_size; return true; }
  memcpy(f->values + f->values_size, payload.bytes, payload.size); f->values_size += payload.size;
  out->bytes = payload.bytes; out->size = payload.size;
  return true;
}
static bool fixture_close(void *ctx, qa_rwt_failure *failure) {
  fixture *f = (fixture *)ctx;
  if (f->close_fails_once && f->close_calls++ == 0) { qa_rwt_failure_set(failure, "Error", "release failed"); return false; }
  free(f->description); free(f);
  return true;
}
static bool fixture_initialize(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure) {
  (void)failure;
  CHECK(ctx == &initialize_sentinel);
  fixture *f = (fixture *)calloc(1, sizeof *f);
  f->worker = worker;
  f->close_fails_once = equal_text(payload, "close-fails-once");
  f->description = (char *)malloc(payload.size ? payload.size : 1);
  if (payload.size) memcpy(f->description, payload.bytes, payload.size);
  f->description_size = payload.size;
  out->ctx = f; out->description.bytes = f->description; out->description.size = payload.size;
  out->dispatch = fixture_dispatch; out->close = fixture_close;
  return true;
}
static bool open_fixture(qa_rwt_transport **t, qa_rwt_value init, qa_rwt_host host, double timeout) {
  qa_rwt_error *error = NULL;
  return qa_rwt_open(t, fixture_initialize, &initialize_sentinel, init, host, timeout, &error);
}
static bool contains(const qa_rwt_error *error, const char *part) { return error != NULL && strstr(error->message, part) != NULL; }

/* host scaffolding: every test's closures live in one struct with a sentinel. */
typedef struct scene {
  int sentinel; qa_rwt_transport *t; int mode;
  unsigned char completed[64]; size_t completed_size; int completed_count;
  char order[64];
  qa_rwt_error *original; int notifications; qa_rwt_error *notified;
  int reentrant_ok;
} scene;
#define SENTINEL 0x51a7
static bool scene_request(void *ctx, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_error **error) {
  scene *s = (scene *)ctx; CHECK(s->sentinel == SENTINEL);
  qa_rwt_error *e = NULL;
  switch (s->mode) {
    case 1: /* nested reached callbacks */
      if (equal_text(payload, "outer-callback")) {
        strcat(s->order, "outer,");
        qa_rwt_value dropped;
        if (!qa_rwt_call(s->t, text("unrelated"), &dropped, &e) && contains(e, "Reentrant")) s->reentrant_ok++;
        e = NULL;
        if (!qa_rwt_close(s->t, &e) && contains(e, "Reentrant")) s->reentrant_ok++;
        bool ok = qa_rwt_callback_call(s->t, text("inner"), out, error);
        strcat(s->order, "outer-return,");
        return ok;
      }
      if (equal_text(payload, "inner-callback")) { strcat(s->order, "inner,"); return qa_rwt_callback_call(s->t, text("leaf"), out, error); }
      *error = qa_rwt_error_new(s->t, "Error", "Unexpected callback"); return false;
    case 2: /* host failures retain identity */
      if (equal_text(payload, "outer-callback")) return qa_rwt_callback_call(s->t, text("inner"), out, error);
      *error = s->original; return false;
    case 3: /* timed out nested subcall */
      return qa_rwt_callback_call(s->t, text("slow"), out, error);
    case 4: /* issued failure */
      *error = s->original; return false;
    default:
      *error = qa_rwt_error_new(s->t, "Error", "Unexpected callback"); return false;
  }
}
static bool scene_completed(void *ctx, qa_rwt_value payload, qa_rwt_error **error) {
  (void)error;
  scene *s = (scene *)ctx; CHECK(s->sentinel == SENTINEL);
  s->completed_count++;
  if (payload.size <= sizeof s->completed) { if (payload.size > 0) memcpy(s->completed, payload.bytes, payload.size); s->completed_size = payload.size; }
  if (s->mode == 5) {
    qa_rwt_value dropped; qa_rwt_error *e = NULL;
    if (!qa_rwt_call(s->t, text("unrelated"), &dropped, &e) && contains(e, "Reentrant")) s->reentrant_ok++;
  }
  return true;
}
static void scene_failed(void *ctx, qa_rwt_error *error) {
  scene *s = (scene *)ctx; CHECK(s->sentinel == SENTINEL);
  s->notifications++; s->notified = error;
}
static qa_rwt_host host_of(scene *s) { qa_rwt_host h = {s, scene_request, scene_completed, scene_failed}; return h; }

static void test_issue_completion(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL;
  CHECK(open_fixture(&s.t, text("test"), host_of(&s), 30000));
  CHECK(equal_text(qa_rwt_description(s.t), "test"));
  unsigned char bytes[3] = {1, 2, 3};
  qa_rwt_value v = {bytes, 3, NULL}, out; qa_rwt_error *e = NULL;
  CHECK(qa_rwt_issue(s.t, v, &e));
  bytes[0] = 99;
  CHECK(qa_rwt_call(s.t, text("values"), &out, &e));
  CHECK(out.size == 3 && memcmp(out.bytes, "\1\2\3", 3) == 0);
  qa_rwt_value_release(&out);
  CHECK(s.completed_count == 1 && s.completed_size == 3 && memcmp(s.completed, "\1\2\3", 3) == 0);
  CHECK(qa_rwt_settled(s.t));
  CHECK(qa_rwt_close(s.t, &e));
  CHECK(qa_rwt_retired(s.t));
  CHECK(qa_rwt_close(s.t, &e));
  qa_rwt_destroy(s.t);
}
static void test_nested_callbacks(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL; s.mode = 1;
  CHECK(open_fixture(&s.t, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 30000));
  qa_rwt_value out; qa_rwt_error *e = NULL;
  CHECK(qa_rwt_call(s.t, text("outer"), &out, &e));
  CHECK(equal_text(out, "leaf-result"));
  qa_rwt_value_release(&out);
  CHECK(strcmp(s.order, "outer,inner,outer-return,") == 0);
  CHECK(s.reentrant_ok == 2);
  CHECK(qa_rwt_close(s.t, &e));
  qa_rwt_destroy(s.t);
}
static void test_host_failure_identity(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL; s.mode = 2;
  CHECK(open_fixture(&s.t, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 30000));
  s.original = qa_rwt_error_new(s.t, "Error", "original host error");
  qa_rwt_value out; qa_rwt_error *e = NULL;
  CHECK(!qa_rwt_call(s.t, text("outer"), &out, &e)); CHECK(e == s.original);
  e = NULL;
  CHECK(!qa_rwt_call(s.t, text("leaf"), &out, &e)); CHECK(e == s.original);
  e = NULL;
  if (!qa_rwt_close(s.t, &e)) CHECK(e == s.original);
  CHECK(qa_rwt_retired(s.t));
  CHECK(s.notifications == 1 && s.notified == s.original);
  qa_rwt_destroy(s.t);
}
static void test_failed_close(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL;
  CHECK(open_fixture(&s.t, text("close-fails-once"), host_of(&s), 30000));
  qa_rwt_error *e = NULL;
  CHECK(!qa_rwt_close(s.t, &e)); CHECK(contains(e, "release failed")); CHECK(!qa_rwt_retired(s.t));
  (void)qa_rwt_close(s.t, &e);
  CHECK(qa_rwt_retired(s.t));
  qa_rwt_destroy(s.t);
}
static void test_timeout_keeps_ownership(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL;
  CHECK(open_fixture(&s.t, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 60));
  qa_rwt_value out; qa_rwt_error *e = NULL;
  CHECK(!qa_rwt_call(s.t, text("slow"), &out, &e)); CHECK(contains(e, "did not complete")); CHECK(!qa_rwt_retired(s.t));
  sleep_ms(150);
  (void)qa_rwt_close(s.t, &e);
  CHECK(qa_rwt_retired(s.t));
  qa_rwt_destroy(s.t);
}
static void test_nested_timeout(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL; s.mode = 3;
  CHECK(open_fixture(&s.t, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 60));
  qa_rwt_value out; qa_rwt_error *e = NULL;
  CHECK(!qa_rwt_call(s.t, text("outer"), &out, &e)); CHECK(contains(e, "did not complete")); CHECK(!qa_rwt_retired(s.t));
  sleep_ms(150);
  (void)qa_rwt_close(s.t, &e);
  CHECK(qa_rwt_retired(s.t));
  qa_rwt_destroy(s.t);
}
static void test_completion_publication(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL; s.mode = 5;
  CHECK(open_fixture(&s.t, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 30000));
  qa_rwt_value out; qa_rwt_error *e = NULL;
  CHECK(qa_rwt_issue(s.t, text("first"), &e));
  CHECK(qa_rwt_synchronize(s.t, NULL, &e));
  CHECK(s.reentrant_ok == 1);
  CHECK(qa_rwt_call(s.t, text("values"), &out, &e));
  CHECK(equal_text(out, "first"));
  qa_rwt_value_release(&out);
  CHECK(qa_rwt_close(s.t, &e));
  qa_rwt_destroy(s.t);
}
static void test_async_failure(void) {
  const char *commands[3] = {"failure", "outer", "slow"};
  for (int i = 0; i < 3; i++) {
    scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL; s.mode = 4;
    CHECK(open_fixture(&s.t, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), i == 2 ? 60 : 1000));
    s.original = qa_rwt_error_new(s.t, "Error", "asynchronous host failure");
    qa_rwt_error *e = NULL;
    CHECK(qa_rwt_issue(s.t, text(commands[i]), &e));
    for (int spin = 0; spin < 5000 && s.notifications == 0; spin++) { qa_rwt_pump(s.t); sleep_ms(1); }
    CHECK(s.notifications == 1);
    if (i == 1) CHECK(s.notified == s.original);
    else if (i == 0) { CHECK(strcmp(s.notified->name, "RangeError") == 0); CHECK(contains(s.notified, "worker failure")); }
    else { CHECK(contains(s.notified, "did not complete")); sleep_ms(150); }
    CHECK(s.notifications == 1);
    e = NULL;
    if (!qa_rwt_close(s.t, &e)) CHECK(e == s.notified);
    CHECK(qa_rwt_retired(s.t));
    sleep_ms(1); qa_rwt_pump(s.t);
    CHECK(s.notifications == 1);
    qa_rwt_destroy(s.t);
  }
}
/* Branches the TS tests do not reach directly. */
static bool failing_initialize(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure) {
  (void)ctx; (void)payload; (void)worker; (void)out;
  qa_rwt_failure_set(failure, "TypeError", "loader failed");
  return false;
}
static void test_startup_failure_and_retire_after_failure(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL;
  qa_rwt_error *e = NULL; qa_rwt_transport *t = NULL;
  CHECK(!qa_rwt_open(&t, failing_initialize, NULL, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 1000, &e));
  CHECK(t != NULL);
  CHECK(strcmp(e->name, "TypeError") == 0 && contains(e, "loader failed"));
  CHECK(qa_rwt_retired(t)); /* close of a runtime-less worker acknowledges */
  qa_rwt_destroy(t);

  memset(&s, 0, sizeof s); s.sentinel = SENTINEL;
  CHECK(open_fixture(&s.t, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 1000));
  bool retired = true;
  CHECK(qa_rwt_retire_after_failure(s.t, &retired, &e)); CHECK(!retired); /* healthy */
  qa_rwt_value out;
  CHECK(!qa_rwt_call(s.t, text("failure"), &out, &e));
  CHECK(!qa_rwt_call(s.t, text("leaf"), &out, &e)); /* commands never replay */
  CHECK(qa_rwt_retire_after_failure(s.t, &retired, &e)); CHECK(retired);
  CHECK(qa_rwt_retired(s.t));
  CHECK(!qa_rwt_call(s.t, text("leaf"), &out, &e)); CHECK(contains(e, "closed"));
  qa_rwt_destroy(s.t);
}
static bool active_dispatch(void *ctx, qa_rwt_value payload, qa_rwt_value *out, qa_rwt_failure *failure) {
  (void)ctx; (void)payload; (void)out; (void)failure; sleep_ms(40); return true;
}
static bool active_initialize(void *ctx, qa_rwt_value payload, qa_rwt_worker *worker, qa_rwt_runtime *out, qa_rwt_failure *failure) {
  (void)ctx; (void)payload; (void)worker; (void)failure;
  out->dispatch = active_dispatch;
  return true;
}
static void test_active_flag(void) {
  scene s; memset(&s, 0, sizeof s); s.sentinel = SENTINEL;
  qa_rwt_error *e = NULL; qa_rwt_transport *t = NULL;
  CHECK(qa_rwt_open(&t, active_initialize, NULL, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 1000, &e));
  CHECK(!qa_rwt_active(t));
  CHECK(qa_rwt_issue(t, text("x"), &e));
  sleep_ms(15);
  CHECK(qa_rwt_active(t));
  CHECK(!qa_rwt_settled(t));
  CHECK(qa_rwt_synchronize(t, NULL, &e));
  CHECK(!qa_rwt_active(t));
  CHECK(qa_rwt_close(t, &e));
  qa_rwt_destroy(t);
}
static void test_invalid_timeout_aborts(void) {
  pid_t child = fork();
  if (child == 0) {
    prctl(PR_SET_DUMPABLE, 0); (void)!freopen("/dev/null", "w", stderr);
    qa_rwt_transport *t = NULL; qa_rwt_error *e = NULL; scene s; memset(&s, 0, sizeof s);
    (void)qa_rwt_open(&t, fixture_initialize, NULL, (qa_rwt_value){NULL, 0, NULL}, host_of(&s), 0, &e);
    _exit(0);
  }
  int status = 0; waitpid(child, &status, 0);
  CHECK(WIFSIGNALED(status));
}

int main(void) {
  test_issue_completion();
  test_nested_callbacks();
  test_host_failure_identity();
  test_failed_close();
  test_timeout_keeps_ownership();
  test_nested_timeout();
  test_completion_publication();
  test_async_failure();
  test_startup_failure_and_retire_after_failure();
  test_active_flag();
  test_invalid_timeout_aborts();
  printf("render worker transport: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
