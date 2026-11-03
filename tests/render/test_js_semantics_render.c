/*
 * JS-semantics regressions in render (docs/PORTING.md, JS number semantics).
 * Expected values come from the TypeScript under bun:
 *   shadowVisibility({x: NaN, y: 5, z: 3}, origin 0, 100, cube shadow, 4x4 depth 0.5,
 *   texel 0.25, near 1) throws RangeError "CPU shadow atlas sample escaped its image"
 *   (Math.max(low, NaN) is NaN; C fmax dropped the NaN and sampled a texel);
 *   the same call at {x: 10, y: 2, z: 3} returns 1.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/render/cpu/lighting.h"

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

int main(void) {
  float pixels[16];
  for (int i = 0; i < 16; i++) pixels[i] = 0.5f;
  qa_depth_image_level depth = {4, 4, pixels, 16};
  qa_q2_shadow_atlas atlas = {0};
  atlas.texel_size = 0.25;
  atlas.near_plane = 1;
  qa_q2_shadow_projection shadow = {0};
  shadow.kind = "cube";
  shadow.atlas_rect = (qa_vec4){0, 0, 1, 1};
  qa_vec3 origin = {0, 0, 0};
  CHECK(qa_cpu_shadow_visibility((qa_vec3){10, 2, 3}, origin, 100, shadow, atlas, depth, false) == 1);
  CHECK_ABORTS((void)qa_cpu_shadow_visibility((qa_vec3){NAN, 5, 3}, origin, 100, shadow, atlas, depth, false));
  printf("test_js_semantics_render: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
