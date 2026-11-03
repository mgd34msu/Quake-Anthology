/*
 * float2int regressions in materials (JS number semantics). Expected values were
 * printed by the TypeScript under bun:
 *   fogFactor(NaN, 0.5) throws RangeError "fog index NaN outside 256"; fogFactor(0.5, 0.5) is 1.
 *   prepareFogVolume(map, 0.5, ...) throws "fog index 0.5 outside 1"; index 0 gives
 *   bounds min (-1, -3, -5) max (2, 4, 6) for planes at distances 1..6.
 *   q2AnimatedTexture over frames [a, b, c]: NaN throws "Broken Q2 animation cycle",
 *   5.5 -> c, -1 -> c, 1e20 -> b, -0 -> a.
 *   q1LightStyle("mmnmmommommnonmmonqnmmo", t, 0) is 264 for t = NaN and Infinity, 308 for 1e30,
 *   286 for -0.35; q1LightStyle("abz", 1e30, 1) is 550.
 *   q2LightStyle("azm", ms).white is 0 for NaN, Infinity and -350, 3 for 1e30 and 275.
 * The C casts of NaN or a huge frame were undefined, and a fractional fog index was truncated.
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

#include "qa/materials/fog.h"
#include "qa/materials/legacy.h"
#include "qa/materials/lighting.h"

int main(void) {
  CHECK(qa_fog_factor(0.5, 0.5) == 1);
  CHECK_ABORTS((void)qa_fog_factor(NAN, 0.5));

  qa_fog_brush_map_entry fogs[1] = {{0, -1}};
  qa_fog_brush_entry brushes[1] = {{0, 6}};
  qa_fog_brush_side_entry sides[6] = {{0}, {1}, {2}, {3}, {4}, {5}};
  qa_plane planes[6];
  for (int i = 0; i < 6; i++) { planes[i].normal = (qa_vec3){1, 0, 0}; planes[i].distance = i + 1; }
  qa_fog_brush_map map = {fogs, 1, brushes, 1, sides, 6, planes, 6};
  qa_q3_material_fog parameters = {{1, 1, 1}, 100};
  qa_fog_volume volume = qa_prepare_fog_volume(map, 0, parameters, 1);
  CHECK(volume.bounds.min.x == -1 && volume.bounds.min.y == -3 && volume.bounds.min.z == -5);
  CHECK(volume.bounds.max.x == 2 && volume.bounds.max.y == 4 && volume.bounds.max.z == 6);
  CHECK_ABORTS((void)qa_prepare_fog_volume(map, 0.5, parameters, 1));

  qa_renderer_image frames[3];
  memset(frames, 0, sizeof frames);
  for (int i = 0; i < 3; i++) frames[i].ordinal = i;
  qa_surface_lighting lighting;
  memset(&lighting, 0, sizeof lighting);
  lighting.kind = "unlit";
  qa_render_material material = qa_create_q2_material("x", frames, 3, lighting, 0, "default");
  CHECK(qa_q2_animated_texture(&material, 5.5).ordinal == 2);
  CHECK(qa_q2_animated_texture(&material, -1).ordinal == 2);
  CHECK(qa_q2_animated_texture(&material, 1e20).ordinal == 1);
  CHECK(qa_q2_animated_texture(&material, -0.0).ordinal == 0);
  CHECK_ABORTS((void)qa_q2_animated_texture(&material, NAN));

  const char *style = "mmnmmommommnonmmonqnmmo";
  CHECK(qa_q1_light_style(style, NAN, 0) == 264);
  CHECK(qa_q1_light_style(style, INFINITY, 0) == 264);
  CHECK(qa_q1_light_style(style, 1e30, 0) == 308);
  CHECK(qa_q1_light_style(style, -0.35, 0) == 286);
  CHECK(qa_q1_light_style("abz", 1e30, 1) == 550);
  CHECK(qa_q2_light_style_value("azm", NAN).white == 0);
  CHECK(qa_q2_light_style_value("azm", INFINITY).white == 0);
  CHECK(qa_q2_light_style_value("azm", -350).white == 0);
  CHECK(qa_q2_light_style_value("azm", 1e30).white == 3);
  CHECK(qa_q2_light_style_value("azm", 275).white == 3);
  printf("test_js_semantics_float2int (materials): %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
