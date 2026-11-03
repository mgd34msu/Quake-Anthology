/*
 * JS-semantics regressions in compat/q2. Expected values were printed by the
 * TypeScript functions under bun:
 *   rereleaseNetworkSolid(box -16,-16,-24..16,16,32, 2, 0) = 1075318800;
 *     max.x NaN -> 1075318784, max.z NaN -> 1576976 (NaN clamps become 0 under
 *     `<<`/`|`; C cast NaN to uint32_t), serverFlags 2^32 + 2 -> 0, 2^32 -> 1075318800.
 *   q2NativeArmorFlags(NaN) all false; (2^32 + 2) noArmor only; (0x184) noPowerArmor,
 *     noRegularArmor, energy; (-1) all true; (2.5) noArmor only.
 *   writeNativeUserCommand(q2: msec 300, buttons -1, angleShorts 70000/-40000/5,
 *     forward 1.5, side -32768, up 32767, impulse 257, light 1000) bytes
 *     44,255,112,17,192,99,5,0,1,0,0,128,255,127,1,232 (setUint8/setInt16 wrap;
 *     C casts of 70000 and 1000 to int16_t/uint8_t were undefined).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/compat/q2/native_damage.h"
#include "qa/compat/q2/native_mod_client_stages.h"
#include "qa/compat/q2/rerelease/spatial.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    checks++;                                                                        \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static qa_bounds box(double max_x, double max_z) {
  qa_bounds b;
  b.min = (qa_vec3){-16, -16, -24};
  b.max = (qa_vec3){max_x, 16, max_z};
  return b;
}

static int flags_are(double input, bool no_armor, bool no_power, bool no_regular, bool energy) {
  qa_armor_damage_flags f = qa_compat_q2_native_armor_flags(input);
  return f.no_armor == no_armor && f.no_power_armor == no_power && f.no_regular_armor == no_regular && f.energy == energy;
}

int main(void) {
  CHECK(qa_compat_q2_rerelease_network_solid(box(16, 32), 2, 0) == 1075318800.0);
  CHECK(qa_compat_q2_rerelease_network_solid(box(NAN, 32), 2, 0) == 1075318784.0);
  CHECK(qa_compat_q2_rerelease_network_solid(box(16, NAN), 2, 0) == 1576976.0);
  CHECK(qa_compat_q2_rerelease_network_solid(box(16, 32), 2, 4294967298.0) == 0);
  CHECK(qa_compat_q2_rerelease_network_solid(box(16, 32), 2, 4294967296.0) == 1075318800.0);

  CHECK(flags_are(NAN, false, false, false, false));
  CHECK(flags_are(4294967298.0, true, false, false, false));
  CHECK(flags_are(0x184, false, true, true, true));
  CHECK(flags_are(-1, true, true, true, true));
  CHECK(flags_are(2.5, true, false, false, false));

  qa_user_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q2;
  command.as.q2.milliseconds = 300;
  command.as.q2.buttons = -1;
  command.as.q2.angle_shorts.values[0] = 70000;
  command.as.q2.angle_shorts.values[1] = -40000;
  command.as.q2.angle_shorts.values[2] = 5;
  command.as.q2.forward_move = 1.5;
  command.as.q2.side_move = -32768;
  command.as.q2.up_move = 32767;
  command.as.q2.impulse = 257;
  command.as.q2.light_level = 1000;
  uint8_t view[16];
  memset(view, 0xee, sizeof view);
  qa_compat_q2_write_native_user_command(view, sizeof view, command, NULL, NULL);
  const uint8_t want[16] = {44, 255, 112, 17, 192, 99, 5, 0, 1, 0, 0, 128, 255, 127, 1, 232};
  CHECK(memcmp(view, want, sizeof want) == 0);

  printf("test_js_semantics_compat_q2: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
