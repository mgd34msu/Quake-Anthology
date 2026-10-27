/* Port of src/content/q3/base/game/missile.ts. GPL-2.0-or-later.
 * MissileRuntime host/world wiring mirrors unported TS owners (../world.ts,
 * ../../foundation/weapon-behavior.ts); snap helpers are fully ported. */
#ifndef QA_CONTENT_Q3_BASE_GAME_MISSILE_H
#define QA_CONTENT_Q3_BASE_GAME_MISSILE_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
typedef struct qa_q3_missile_direction { double x, y, z; } qa_q3_missile_direction;
qa_vec3 qa_q3_snap_vector(qa_vec3 value);
qa_vec3 qa_q3_snap_vector_towards(qa_vec3 value, qa_vec3 toward);
/* TS MissileHost/MissileRuntime: opaque host retained; runtime steps need world/combat (see unresolved). */
typedef struct qa_q3_missile_host { void *ctx; qa_q3_game_product product; double previous_time; } qa_q3_missile_host;
typedef struct qa_q3_missile_runtime { qa_q3_missile_host host; } qa_q3_missile_runtime;
void qa_q3_missile_runtime_init(qa_q3_missile_runtime *runtime, qa_q3_missile_host host);
#endif
