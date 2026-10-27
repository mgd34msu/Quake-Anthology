/* Port of src/content/q2/foundation/weapons/projection.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_PROJECTION_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_PROJECTION_H
#include "qa/content/q2/foundation/weapons/types.h"
typedef struct qa_q2w_actor_view { qa_q2w_hand hand; double view_height; bool players_collide; } qa_q2w_actor_view;
typedef struct qa_q2w_projection { qa_vec3 start, direction; } qa_q2w_projection;
double qa_q2w_actor_shot_mask(qa_q2f_game *game, bool players_collide);
qa_q2w_projection qa_q2w_project_actor(qa_actor_id actor, qa_q2f_game *game, qa_q2w_actor_view view, qa_vec3 angles, qa_vec3 offset);
#endif
