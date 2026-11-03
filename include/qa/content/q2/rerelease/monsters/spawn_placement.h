/* Port of src/content/q2/rerelease/monsters/spawn-placement.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_SPAWN_PLACEMENT_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_SPAWN_PLACEMENT_H
#include <stdbool.h>
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/contracts/math.h"

/* False is TS null. drop defaults true in the _default form. */
bool qa_q2rm_find_rerelease_spawn_point(qa_q2_game_services *game, qa_vec3 start, qa_bounds bounds, double max_move_up,
                                        bool drop, qa_vec3 *out);
bool qa_q2rm_find_rerelease_spawn_point_default(qa_q2_game_services *game, qa_vec3 start, qa_bounds bounds,
                                                double max_move_up, qa_vec3 *out);
bool qa_q2rm_check_rerelease_ground_spawn_point(qa_q2_game_services *game, qa_vec3 origin, qa_bounds bounds,
                                                double height, double gravity);
#endif
