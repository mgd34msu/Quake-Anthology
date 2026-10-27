/*
 * Port of the sourceActorsCheckpoint / readSourceActorsCheckpoint
 * cases in tests/persistence/save.test.ts (src/persistence/
 * save-image.ts): the "world:source-slots" provider checkpoint
 * round-trips an empty and a populated source actor list, and an
 * unsupported checkpoint identity is a SaveFormatError (abort).
 * readRecipe / the QTSAVE image codec stay blocked (see
 * include/qa/persistence/recipe.h), so no recipe cases are ported
 * yet. TS toThrow cases run in a child process and must abort.
 * GPL-2.0-or-later.
 */
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

#include "qa/persistence/save_image.h"

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

static void test_source_actors_round_trip(void) {
  const qa_source_actor_checkpoint sources[2] = {
      {"q1:game", 3, {5, 2}},
      {"q2:game", 0, {0, 7}},
  };
  qa_provider_checkpoint checkpoint;
  CHECK(qa_source_actors_checkpoint(sources, 2, &checkpoint));
  CHECK(strcmp(checkpoint.provider, "world:actors") == 0);
  CHECK(strcmp(checkpoint.schema, "world:source-slots") == 0);
  CHECK(checkpoint.version == 1);
  CHECK(checkpoint.bytes.length > 0);

  qa_source_actors_result read;
  CHECK(qa_read_source_actors_checkpoint(&checkpoint, &read));
  CHECK(read.count == 2);
  if (read.count == 2) {
    CHECK(strcmp(read.entries[0].provider, "q1:game") == 0);
    CHECK(read.entries[0].source_slot == 3);
    CHECK(read.entries[0].actor.slot == 5 && read.entries[0].actor.generation == 2);
    CHECK(strcmp(read.entries[1].provider, "q2:game") == 0);
    CHECK(read.entries[1].source_slot == 0);
    CHECK(read.entries[1].actor.slot == 0 && read.entries[1].actor.generation == 7);
  }
  qa_source_actors_result_free(&read);
  free((uint8_t *)checkpoint.bytes.bytes);
}

static void test_source_actors_empty(void) {
  qa_provider_checkpoint checkpoint;
  CHECK(qa_source_actors_checkpoint(NULL, 0, &checkpoint));
  qa_source_actors_result read;
  CHECK(qa_read_source_actors_checkpoint(&checkpoint, &read));
  CHECK(read.count == 0);
  qa_source_actors_result_free(&read);
  free((uint8_t *)checkpoint.bytes.bytes);
}

static void test_source_actors_unsupported_aborts(void) {
  qa_provider_checkpoint checkpoint;
  CHECK(qa_source_actors_checkpoint(NULL, 0, &checkpoint));
  qa_provider_checkpoint wrong_schema = checkpoint;
  wrong_schema.schema = "world:other";
  qa_provider_checkpoint wrong_version = checkpoint;
  wrong_version.version = 2;
  qa_source_actors_result read;
  CHECK_ABORTS((void)qa_read_source_actors_checkpoint(&wrong_schema, &read));
  CHECK_ABORTS((void)qa_read_source_actors_checkpoint(&wrong_version, &read));
  free((uint8_t *)checkpoint.bytes.bytes);
}

int main(void) {
  test_source_actors_round_trip();
  test_source_actors_empty();
  test_source_actors_unsupported_aborts();
  printf("test_persistence_recipe: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
