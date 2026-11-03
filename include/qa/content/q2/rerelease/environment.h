/* Port of src/content/q2/rerelease/environment.ts (over the base
 * Q2CharacterContext). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_ENVIRONMENT_H
#define QA_CONTENT_Q2_RERELEASE_ENVIRONMENT_H
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/rerelease/types.h"
void qa_q2_rerelease_world_effects(qa_q2_character_context *c, qa_q2rr_player_extra_state *extra);
void qa_q2_rerelease_falling_damage(qa_q2_character_context *c, qa_q2rr_player_extra_state *extra,
                                    qa_q2_rerelease_options options, double frame_seconds);
#endif
