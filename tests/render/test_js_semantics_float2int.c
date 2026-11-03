/*
 * float2int regressions in render/cpu (JS number semantics). worldAttributes under
 * bun: a q2-world batch with three positions throws RangeError "CPU fragment lighting
 * arrays do not cover the vertex" for index 1.5 (worldPositions[1.5] is undefined),
 * NaN and -1, and returns positions[1] for index 1. The C (long) cast truncated 1.5 to 1.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    checks++;                                                                        \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_ABORTS(...)                                                            \
  do {                                                                               \
    checks++;                                                                        \
    pid_t pid = fork();                                                              \
    if (pid == 0) {                                                                  \
      (void)prctl(PR_SET_DUMPABLE, 0);                                               \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);                       \
      __VA_ARGS__;                                                                   \
      _exit(0);                                                                      \
    }                                                                                \
    int status = 0;                                                                  \
    waitpid(pid, &status, 0);                                                        \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                     \
      failures++;                                                                    \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                                \
  } while (0)

#include "qa/render/cpu/lighting.h"

int main(void) {
  qa_vec3 positions[3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  qa_vec3 normals[3] = {{0, 0, 1}, {0, 1, 0}, {1, 0, 0}};
  qa_batch_lighting lighting;
  memset(&lighting, 0, sizeof lighting);
  lighting.kind = "q2-world";
  lighting.world_positions = positions; lighting.world_position_count = 3;
  lighting.normals = normals; lighting.normal_count = 3;
  qa_cpu_world_attributes one = qa_cpu_world_attributes_for(lighting, 1);
  CHECK(one.world_position.x == 4 && one.world_normal.y == 1);
  CHECK_ABORTS((void)qa_cpu_world_attributes_for(lighting, 1.5));
  CHECK_ABORTS((void)qa_cpu_world_attributes_for(lighting, NAN));
  CHECK_ABORTS((void)qa_cpu_world_attributes_for(lighting, -1));
  printf("test_js_semantics_float2int (render): %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
