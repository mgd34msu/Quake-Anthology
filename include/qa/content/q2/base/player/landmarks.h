/* Port of src/content/q2/base/player/landmarks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_LANDMARKS_H
#define QA_CONTENT_Q2_BASE_PLAYER_LANDMARKS_H

#include <stdbool.h>

#include "qa/content/q2/foundation/host.h"

/* TS Q2LandmarkPlacement. */
typedef struct qa_q2_landmark_placement { qa_vec3 origin, velocity, angles; } qa_q2_landmark_placement;

qa_vec3 qa_q2_rotate_landmark(qa_vec3 vector, qa_vec3 angles);
/* TS fixQ2StuckPlayer; false return is TS null. */
bool qa_q2_fix_stuck_player(qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_bounds bounds, qa_vec3 *out);
/* TS placeQ2Landmark; false return is TS null. */
bool qa_q2_place_landmark(qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_landmark_carry carry,
                          qa_q2_entity *spawn, qa_bounds bounds, qa_q2_landmark_placement *out);

#endif
