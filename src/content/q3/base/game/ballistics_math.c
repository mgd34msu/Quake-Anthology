/* Port of src/content/q3/base/game/ballistics-math.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/ballistics_math.h"
#include <math.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static double rnd_random(void *ctx) { return qa_game_random_random((qa_game_random *)ctx); }
static double rnd_crandom(void *ctx) { return qa_game_random_crandom((qa_game_random *)ctx); }
qa_q3_game_random_pick qa_q3_game_random_pick_from(qa_game_random *random) {
  qa_q3_game_random_pick p = { random, rnd_random, rnd_crandom }; return p;
}
static float f32(double v) { return (float)v; }
qa_vec3 qa_q3_bullet_endpoint(qa_q3_ballistic_attack attack, double spread, qa_q3_game_random_pick random) {
  double angle = f32(f32(random.random(random.ctx) * f32(3.141592653589793)) * 2);
  double vertical = f32(f32(f32(f32(sin(angle)) * random.crandom(random.ctx)) * spread) * 16);
  double horizontal = f32(f32(f32(f32(cos(angle)) * random.crandom(random.ctx)) * spread) * 16);
  return qa_add3(qa_add3(qa_add3(attack.muzzle, qa_scale3(attack.forward, 131072)),
      qa_scale3(attack.right, horizontal)), qa_scale3(attack.up, vertical));
}
qa_q3_shotgun_endpoints qa_q3_shotgun_endpoints_compute(qa_vec3 origin, qa_vec3 direction, double initial_seed) {
  qa_vec3 forward = qa_normalize3_or_zero(direction);
  qa_vec3 right = qa_perpendicular_vector(forward);
  qa_vec3 up = qa_cross3(forward, right);
  double seed = initial_seed; qa_q3_shotgun_endpoints out;
  for (int count = 0; count < 11; count++) {
    qa_random_step r = qa_q_crandom(seed), u = qa_q_crandom(r.seed); seed = u.seed;
    double horizontal = f32(f32(r.value * 700) * 16), vertical = f32(f32(u.value * 700) * 16);
    out.ends[count] = qa_add3(qa_add3(qa_add3(origin, qa_scale3(forward, 131072)),
        qa_scale3(right, horizontal)), qa_scale3(up, vertical));
  }
  return out;
}
qa_q3_missile_params qa_q3_get_missile_parameters(double weapon) {
  if (weapon == 4) { qa_q3_missile_params p = {700,2500,true,100,100,150,4,5}; return p; }
  if (weapon == 5) { qa_q3_missile_params p = {900,15000,false,100,100,120,6,7}; return p; }
  if (weapon == 8) { qa_q3_missile_params p = {2000,10000,false,20,15,20,8,9}; return p; }
  if (weapon == 9) { qa_q3_missile_params p = {2000,10000,false,100,100,120,12,13}; return p; }
  qa_range_error("No Q3 missile parameters for weapon %g", weapon);
}
qa_vec3 qa_q3_nail_velocity(qa_vec3 start, qa_vec3 forward, qa_vec3 right, qa_vec3 up, qa_q3_game_random_pick random) {
  double angle = f32(f32(random.random(random.ctx) * f32(3.141592653589793)) * 2);
  double vertical = f32(f32(f32(f32(sin(angle)) * random.crandom(random.ctx)) * 500) * 16);
  double horizontal = f32(f32(f32(f32(cos(angle)) * random.crandom(random.ctx)) * 500) * 16);
  qa_vec3 end = qa_add3(qa_add3(qa_add3(start, qa_scale3(forward, 8192 * 16)),
      qa_scale3(right, horizontal)), qa_scale3(up, vertical));
  qa_vec3 direction = qa_normalize3(qa_sub3(end, start));
  double speed = f32(555 + f32(random.random(random.ctx) * 1800));
  return qa_scale3(direction, speed);
}
qa_vec3 qa_q3_bounce_velocity(qa_vec3 velocity, qa_vec3 normal, bool half) {
  qa_vec3 delta = qa_add3(velocity, qa_scale3(normal, f32(-2 * qa_dot3(velocity, normal))));
  if (half) delta = qa_scale3(delta, f32(0.65));
  return delta;
}
double qa_q3_missile_hit_time(double previous, double time, double fraction) {
  double elapsed = f32((double)qa_int32(time - previous));
  return qa_qvm_float_to_int(f32(f32(previous) + f32(elapsed * f32(fraction))));
}
