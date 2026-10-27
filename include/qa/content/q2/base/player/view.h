/* Port of src/content/q2/base/player/view.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_VIEW_H
#define QA_CONTENT_Q2_BASE_PLAYER_VIEW_H

#include <stdbool.h>

#include "qa/content/q2/base/player/types.h"

/* TS animation frame-range returns. */
typedef struct qa_q2_base_frame_range { double first, last; } qa_q2_base_frame_range;
/* TS q2DamageFeedback result. */
typedef struct qa_q2_damage_feedback_result { double flashes, pain_index; } qa_q2_damage_feedback_result;

qa_vec4 qa_q2_add_blend(qa_vec4 blend, qa_vec3 color, double alpha);
qa_q2_base_frame_range qa_q2_pain_animation_frames(bool ducked, double index);
qa_q2_base_frame_range qa_q2_death_animation_frames(bool ducked, double index);
qa_q2_damage_feedback_result qa_q2_damage_feedback(qa_q2_character_context *context, double pain_index);
qa_q2_base_player_view qa_q2_build_view(qa_q2_character_context *context, double flashes, bool intermission);
void qa_q2_client_animation(qa_q2_character_context *context);
/* TS advanceQ2PlayerAnimation (its Pick<> state/entity widen to the
 * full structs; only the picked fields are touched). */
void qa_q2_advance_player_animation(qa_q2_base_player_state *state, qa_q2_entity *entity, bool grounded, bool duck, bool run);
void qa_q2_client_effects(qa_q2_character_context *context);

#endif
