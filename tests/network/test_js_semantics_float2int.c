/*
 * float2int regressions in network: C conversions of doubles that were undefined
 * (NaN, out of range) or truncated where the TypeScript keeps a number. Expected
 * values printed by the TS under bun:
 *   createRereleaseContext(...).encodeAlpha/encodeScale/encodeLoopVolume/
 *   encodeLoopAttenuation(x) for x = NaN, 1e30, -1e30, 0.5: NaN NaN NaN NaN /
 *   255 255 0 255 / 1 1 0 0 / 127 8 127 32; combineBits(-1, 0) = 4294967295,
 *   combineBits(4294967301, 1) = 4294967301, combineBits(NaN, 2) = 8589934592;
 *   bitsHasHi(3 * 2^32, 1) true, (3 * 2^32, 4) false, (NaN, 1) false,
 *   (2^32 * (2^31 + 1), 1) true.
 *   packQ2Solid({min z NaN, max 16 16 32}) short 8194, r1q2 2149580816, int 1073745936;
 *   packQ2Solid({min z -24, max NaN 16 NaN}) short 96, r1q2 6144, int 1576960.
 *   new NetQuakeChannel(1.5, 1) and new NetQuakeChannel(NaN, 1) throw RangeError
 *   "Invalid NetQuake fragment limits"; new QuakeWorldChannel("client", 1, 1450.5)
 *   throws "Invalid reliable capacity"; (8000, 1024) and 1450 construct.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/network/q1/channels.h"
#include "qa/network/q2/codecs/q2repro.h"
#include "qa/network/q2/solid.h"

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

static qa_bounds box(double min_z, double max_x, double max_y, double max_z) {
  qa_bounds b;
  b.min.x = 0; b.min.y = 0; b.min.z = min_z;
  b.max.x = max_x; b.max.y = max_y; b.max.z = max_z;
  return b;
}

int main(void) {
  CHECK(isnan(qa_q2repro_encode_alpha(NAN)) && isnan(qa_q2repro_encode_scale(NAN)));
  CHECK(isnan(qa_q2repro_encode_loop_volume(NAN)) && isnan(qa_q2repro_encode_loop_attenuation(NAN)));
  CHECK(qa_q2repro_encode_alpha(1e30) == 255 && qa_q2repro_encode_scale(1e30) == 255);
  CHECK(qa_q2repro_encode_loop_volume(1e30) == 0 && qa_q2repro_encode_loop_attenuation(1e30) == 255);
  CHECK(qa_q2repro_encode_alpha(-1e30) == 1 && qa_q2repro_encode_scale(-1e30) == 1);
  CHECK(qa_q2repro_encode_loop_volume(-1e30) == 0 && qa_q2repro_encode_loop_attenuation(-1e30) == 0);
  CHECK(qa_q2repro_encode_alpha(0.5) == 127 && qa_q2repro_encode_scale(0.5) == 8);
  CHECK(qa_q2repro_encode_loop_volume(0.5) == 127 && qa_q2repro_encode_loop_attenuation(0.5) == 32);

  CHECK(qa_q2repro_combine_bits(-1, 0) == 4294967295.0);
  CHECK(qa_q2repro_combine_bits(4294967301.0, 1) == 4294967301.0);
  CHECK(qa_q2repro_combine_bits(NAN, 2) == 8589934592.0);
  CHECK(qa_q2repro_bits_has_hi(3 * 4294967296.0, 1));
  CHECK(!qa_q2repro_bits_has_hi(3 * 4294967296.0, 4));
  CHECK(!qa_q2repro_bits_has_hi(NAN, 1));
  CHECK(qa_q2repro_bits_has_hi(4294967296.0 * (2147483648.0 + 1), 1));

  CHECK(qa_q2_pack_solid(box(NAN, 16, 16, 32), QA_Q2_SOLID_SHORT) == 8194u);
  CHECK(qa_q2_pack_solid(box(NAN, 16, 16, 32), QA_Q2_SOLID_R1Q2) == 2149580816u);
  CHECK(qa_q2_pack_solid(box(NAN, 16, 16, 32), QA_Q2_SOLID_Q2PRO_V2) == 1073745936u);
  CHECK(qa_q2_pack_solid(box(-24, NAN, 16, NAN), QA_Q2_SOLID_SHORT) == 96u);
  CHECK(qa_q2_pack_solid(box(-24, NAN, 16, NAN), QA_Q2_SOLID_R1Q2) == 6144u);
  CHECK(qa_q2_pack_solid(box(-24, NAN, 16, NAN), QA_Q2_SOLID_Q2PRO_V2) == 1576960u);

  qa_netquake_channel nq;
  CHECK(qa_netquake_channel_init(&nq, 8000, 1024));
  qa_netquake_channel_destroy(&nq);
  CHECK_ABORTS(qa_netquake_channel_init(&nq, 1.5, 1));
  CHECK_ABORTS(qa_netquake_channel_init(&nq, NAN, 1));
  qa_quakeworld_channel qw;
  CHECK(qa_quakeworld_channel_init(&qw, true, 1, 1450, 2500));
  qa_quakeworld_channel_destroy(&qw);
  CHECK_ABORTS(qa_quakeworld_channel_init(&qw, true, 1, 1450.5, 2500));

  printf("test_js_semantics_float2int: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
