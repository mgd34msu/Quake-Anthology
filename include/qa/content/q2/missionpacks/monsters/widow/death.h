/* Port of src/content/q2/missionpacks/monsters/widow/death.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_WIDOW_DEATH_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_WIDOW_DEATH_H

#include <stdbool.h>

#include "qa/content/q2/foundation/monsters/index.h"

/* origin NULL mirrors TS null (random point in bounds). */
qa_q2f_entity *qa_q2mpm_widow_gib(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double damage,
                                  bool organic, const qa_vec3 *origin, bool sized, const char *hit_sound,
                                  bool fade);
qa_q2f_entity *qa_q2mpm_widow_gib_default(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double damage,
                                          bool organic);
void qa_q2mpm_widow_effect(qa_q2f_game *game, qa_vec3 origin, const char *effect, double count);
void qa_q2mpm_widow_effect_default(qa_q2f_game *game, qa_vec3 origin);
void qa_q2mpm_spawn_widow_legs(qa_q2f_entity *self, qa_q2f_game *game);
qa_q2f_callback_definitions qa_q2mpm_widow_debris_callbacks(void);
void qa_q2mpm_widow_explosion(qa_q2m_context *context, qa_vec3 offset);
void qa_q2mpm_widow_explosion_leg(qa_q2m_context *context);
void qa_q2mpm_widow_explosion_leg_handler(void *ctx, qa_q2m_context *context);
qa_q2f_think qa_q2mpm_create_widow_explode(qa_q2m_monsters *monsters);

#endif
