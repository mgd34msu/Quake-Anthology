/* Port of src/content/q3/base/game/ballistics-math.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_BALLISTICS_MATH_H
#define QA_CONTENT_Q3_BASE_GAME_BALLISTICS_MATH_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/content/q3/base/game/numeric.h"
typedef struct qa_q3_ballistic_attack { qa_vec3 muzzle, forward, right, up; } qa_q3_ballistic_attack;
/* TS Pick<GameRandom,"random"|"crandom"> as fn+ctx. */
typedef struct qa_q3_game_random_pick {
  void *ctx; double (*random)(void *ctx); double (*crandom)(void *ctx);
} qa_q3_game_random_pick;
qa_q3_game_random_pick qa_q3_game_random_pick_from(qa_game_random *random);
qa_vec3 qa_q3_bullet_endpoint(qa_q3_ballistic_attack attack, double spread, qa_q3_game_random_pick random);
enum { QA_Q3_SHOTGUN_PELLET_COUNT = 11 };
typedef struct qa_q3_shotgun_endpoints { qa_vec3 ends[QA_Q3_SHOTGUN_PELLET_COUNT]; } qa_q3_shotgun_endpoints;
qa_q3_shotgun_endpoints qa_q3_shotgun_endpoints_compute(qa_vec3 origin, qa_vec3 direction, double initial_seed);
typedef struct qa_q3_missile_params {
  double speed, duration; bool gravity; double direct, splash, radius, method, splash_method;
} qa_q3_missile_params;
/* Getter with _get_ infix: qa_q3_missile_params is the type. Unknown weapon aborts (TS Error). */
qa_q3_missile_params qa_q3_get_missile_parameters(double weapon);
qa_vec3 qa_q3_nail_velocity(qa_vec3 start, qa_vec3 forward, qa_vec3 right, qa_vec3 up, qa_q3_game_random_pick random);
qa_vec3 qa_q3_bounce_velocity(qa_vec3 velocity, qa_vec3 normal, bool half);
double qa_q3_missile_hit_time(double previous, double time, double fraction);
#endif
