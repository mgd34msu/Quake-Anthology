/* Port of src/content/q2/base/player/environment.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_ENVIRONMENT_H
#define QA_CONTENT_Q2_BASE_PLAYER_ENVIRONMENT_H

#include "qa/content/q2/base/player/types.h"

/* TS q2WorldEffects / q2FallingDamage. q2EnvironmentDamage's TS
 * flags parameter defaults to 0 at omitted call sites. */
void qa_q2_world_effects(qa_q2_character_context *context);
void qa_q2_environment_damage(qa_q2_character_context *context, double amount, double means, double flags);
void qa_q2_falling_damage(qa_q2_character_context *context);

#endif
